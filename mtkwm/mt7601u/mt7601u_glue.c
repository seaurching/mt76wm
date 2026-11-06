// SPDX-License-Identifier: GPL-2.0-only
/*
 * mt7601u_glue.c - net80211 bridge for the MT7601U USB driver (mt76wm).
 *
 * The glue layer (glue/mtk_net80211.c) talks to the hardware through the
 * family hook table in glue/mtk_api.h (struct mtk_dev_ops).  This file
 * translates those hooks into the ported Linux mac80211 entry points
 * (main.c) using a small set of synthetic mac80211-shim objects (one
 * station-mode vif + one ieee80211_sta per net80211 node), the same way
 * glue/mtk_port_funcs.c does for the mt792x families.
 */

#include "mt7601u.h"
#include "usb.h"
#include "mac.h"
#include "eeprom.h"
#include "mcu.h"

#include "../glue/mtk_api.h"

#define MTK7601_MAX_STA     4

struct mt7601u_sta_ctx {
    void                *ni;
    bool                 used;
    struct ieee80211_sta *sta;      /* drv_priv = struct mt76_sta      */
};

static struct {
    bool                     valid;
    struct ieee80211_vif    *vif;   /* drv_priv = struct mt76_vif      */
    struct mt7601u_sta_ctx   sta[MTK7601_MAX_STA];
} g;

void mt7601u_init_debugfs(struct mt7601u_dev *dev)
{
    /* debugfs is not ported (see docs/porting-notes.md). */
    (void)dev;
}

static struct mt7601u_sta_ctx *mt7601u_sta_find(void *ni)
{
    int i;

    for (i = 0; i < MTK7601_MAX_STA; i++)
        if (g.sta[i].used && g.sta[i].ni == ni)
            return &g.sta[i];
    return NULL;
}

static int mt7601u_ensure_vif(struct mt7601u_dev *dev)
{
    struct ieee80211_vif *vif;
    struct mt76_vif *mvif;
    int ret;

    if (g.valid)
        return 0;

    vif = (struct ieee80211_vif *)kzalloc(sizeof(*vif) +
                                          sizeof(struct mt76_vif), 0);
    if (!vif)
        return -ENOMEM;
    vif->type = NL80211_IFTYPE_STATION;
    memcpy(vif->addr, dev->macaddr, ETH_ALEN);
    vif->bss_conf.vif = vif;
    vif->bss_conf.beacon_int = 100;
    vif->bss_conf.dtim_period = 1;
    vif->deflink_conf = &vif->bss_conf;
    vif->link_conf[0] = &vif->bss_conf;
    vif->cfg.assoc = false;

    mvif = (struct mt76_vif *)vif->drv_priv;
    mvif->idx = 0;

    ret = mt7601u_add_interface(dev->hw, vif);
    if (ret) {
        kfree(vif);
        return ret;
    }

    g.vif = vif;
    g.valid = true;
    return 0;
}

/* ---- hw start / stop ---- */

int mt7601u_glue_start(struct mt7601u_dev *dev)
{
    int ret = mt7601u_ensure_vif(dev);

    if (ret)
        return ret;
    return mt7601u_start(dev->hw);
}

void mt7601u_glue_stop(struct mt7601u_dev *dev)
{
    mt7601u_stop(dev->hw, false);
}

/* ---- station (wcid) lifecycle ---- */

int mt7601u_glue_sta_add(struct mt7601u_dev *dev, void *ni,
             const unsigned char *mac, unsigned int aid)
{
    struct mt7601u_sta_ctx *ctx;
    struct ieee80211_sta *sta;
    struct mt76_sta *msta;
    unsigned long flags;
    int ret, i;

    if (!ni || !mac)
        return -EINVAL;
    ret = mt7601u_ensure_vif(dev);
    if (ret)
        return ret;

    ctx = mt7601u_sta_find(ni);
    if (ctx)
        return 0;               /* re-association: state already set up */

    for (i = 0; i < MTK7601_MAX_STA; i++)
        if (!g.sta[i].used)
            break;
    if (i == MTK7601_MAX_STA)
        return -ENOSPC;
    ctx = &g.sta[i];

    sta = (struct ieee80211_sta *)kzalloc(sizeof(*sta) +
                                          sizeof(struct mt76_sta), 0);
    if (!sta)
        return -ENOMEM;
    sta->aid = (u16)aid;
    sta->wme = 1;
    sta->rx_nss = 1;
    sta->deflink.sta = sta;
    memcpy(sta->addr, mac, ETH_ALEN);
    sta->deflink.rx_nss = 1;
    sta->deflink.ht_cap.ht_supported = true;
    sta->deflink.ht_cap.ampdu_factor = 3;
    sta->deflink.ht_cap.ampdu_density = IEEE80211_HT_MPDU_DENSITY_2;

    msta = (struct mt76_sta *)sta->drv_priv;

    ret = mt7601u_sta_add(dev->hw, g.vif, sta);
    if (ret) {
        kfree(sta);
        return ret;
    }

    /* Default TX rate until a rate-control policy exists: HT MCS7,
     * 20 MHz, long GI.  TODO: real rate control (minstrel-style). */
    spin_lock_irqsave(&dev->lock, flags);
    msta->wcid.tx_rate = FIELD_PREP(MT_TXWI_RATE_MCS, 7) |
                 FIELD_PREP(MT_TXWI_RATE_PHY_MODE, MT_PHY_TYPE_HT);
    msta->wcid.tx_rate_set = true;
    msta->wcid.tx_rate_nss = 1;
    spin_unlock_irqrestore(&dev->lock, flags);

    ctx->ni = ni;
    ctx->sta = sta;
    ctx->used = true;

    XYLog("%s: wcid %d bound to %02x:%02x:%02x:%02x:%02x:%02x aid %u\n",
          __FUNCTION__, msta->wcid.idx, mac[0], mac[1], mac[2], mac[3],
          mac[4], mac[5], aid);
    return 0;
}

void mt7601u_glue_sta_remove(struct mt7601u_dev *dev, void *ni)
{
    struct mt7601u_sta_ctx *ctx = mt7601u_sta_find(ni);

    if (!ctx)
        return;

    mt7601u_sta_remove(dev->hw, g.vif, ctx->sta);
    ctx->used = false;
    ctx->ni = NULL;
    kfree(ctx->sta);
    ctx->sta = NULL;
}

/* ---- BSS (RUN state) ---- */

int mt7601u_glue_bss(struct mt7601u_dev *dev, const unsigned char *bssid,
             unsigned int aid, unsigned int bintval, unsigned int dtim,
             unsigned int qos, unsigned int basic_rates, bool assoc)
{
    struct ieee80211_bss_conf *info;
    u64 changed;
    int ret;

    ret = mt7601u_ensure_vif(dev);
    if (ret)
        return ret;
    info = &g.vif->bss_conf;

    if (assoc) {
        memcpy(info->bssid, bssid, ETH_ALEN);
        info->beacon_int = bintval ? bintval : 100;
        info->dtim_period = dtim ? dtim : 1;
        info->basic_rates = basic_rates ? basic_rates : 0x15f;
        info->use_short_slot = true;
        info->use_short_preamble = true;
        info->use_cts_prot = false;
        info->ht_operation_mode = 0;
        g.vif->cfg.assoc = true;
        g.vif->cfg.aid = (u16)aid;
        changed = BSS_CHANGED_ASSOC | BSS_CHANGED_BSSID |
              BSS_CHANGED_BASIC_RATES | BSS_CHANGED_BEACON_INT |
              BSS_CHANGED_ERP_CTS_PROT | BSS_CHANGED_ERP_PREAMBLE |
              BSS_CHANGED_ERP_SLOT | BSS_CHANGED_HT;
    } else {
        memset(info->bssid, 0, ETH_ALEN);
        g.vif->cfg.assoc = false;
        changed = BSS_CHANGED_ASSOC | BSS_CHANGED_BSSID;
    }

    mutex_lock(&dev->mutex);
    mt7601u_bss_info_changed(dev->hw, g.vif, info, changed);
    mutex_unlock(&dev->mutex);
    return 0;
}

/* ---- keys ---- */

static u32 mt7601u_key_suite(unsigned int cipher)
{
    switch (cipher) {
    case MTK_KEY_CIPHER_WEP40:   return WLAN_CIPHER_SUITE_WEP40;
    case MTK_KEY_CIPHER_WEP104:  return WLAN_CIPHER_SUITE_WEP104;
    case MTK_KEY_CIPHER_TKIP:    return WLAN_CIPHER_SUITE_TKIP;
    case MTK_KEY_CIPHER_CCMP:    return WLAN_CIPHER_SUITE_CCMP;
    default:                     return 0;
    }
}

int mt7601u_glue_set_key(struct mt7601u_dev *dev, void *ni,
             const struct mtk_key_desc *k, bool add)
{
    struct mt7601u_sta_ctx *ctx = NULL;
    struct ieee80211_key_conf conf;
    u32 suite;
    int ret;

    if (!k)
        return -EINVAL;
    ret = mt7601u_ensure_vif(dev);
    if (ret)
        return ret;

    suite = mt7601u_key_suite(k->cipher);
    if (!suite)
        return -EOPNOTSUPP;         /* caller falls back to sw crypto */
    if (k->key_len > 32)
        return -EINVAL;

    memset(&conf, 0, sizeof(conf));
    conf.cipher = suite;
    conf.keyidx = (s8)(k->key_id & 0x7);
    conf.key_id = (u8)k->key_id;
    conf.keylen = (u8)k->key_len;
    conf.key_len = (u8)k->key_len;
    memcpy(conf.key, k->key, k->key_len);

    if (ni != NULL && !(k->flags & (MTK_KEY_FLAG_GROUP | MTK_KEY_FLAG_IGTK))) {
        ctx = mt7601u_sta_find(ni);
        if (!ctx)
            return -ENOENT;
        conf.flags |= IEEE80211_KEY_FLAG_PAIRWISE;
    }

    return mt7601u_set_key(dev->hw, add ? SET_KEY : DELETE_KEY, g.vif,
                   ctx ? ctx->sta : NULL, &conf);
}

/* ---- EDCA / slot timing ---- */

int mt7601u_glue_set_edca(struct mt7601u_dev *dev,
              const struct mtk_edca_ac *ac4)
{
    struct ieee80211_tx_queue_params params;
    static const u16 m2q[4] = { IEEE80211_AC_VO, IEEE80211_AC_VI,
                    IEEE80211_AC_BE, IEEE80211_AC_BK };
    int ret, i;

    if (!ac4)
        return -EINVAL;
    ret = mt7601u_ensure_vif(dev);
    if (ret)
        return ret;

    for (i = 0; i < 4; i++) {
        memset(&params, 0, sizeof(params));
        params.cw_min = (u16)ac4[i].cw_min;
        params.cw_max = (u16)ac4[i].cw_max;
        params.aifs = (u8)ac4[i].aifs;
        params.txop = (u16)ac4[i].txop;
        params.acm = ac4[i].acm != 0;
        mt7601u_conf_tx(dev->hw, g.vif, 0, m2q[i], &params);
    }
    return 0;
}

int mt7601u_glue_set_slot(struct mt7601u_dev *dev, int short_slot)
{
    int slottime = short_slot ? 9 : 20;

    mt76_rmw_field(dev, MT_BKOFF_SLOT_CFG,
               MT_BKOFF_SLOT_CFG_SLOTTIME, slottime);
    return 0;
}

/* ---- channel ---- */

int mt7601u_glue_chan_switch(struct mt7601u_dev *dev, unsigned int freq,
                 unsigned int offchannel)
{
    struct cfg80211_chan_def chandef = {};
    struct ieee80211_channel *chan = NULL;
    int i, ret;

    if (!freq || !dev->sband_2g)
        return -EINVAL;

    for (i = 0; i < dev->sband_2g->n_channels; i++) {
        if (dev->sband_2g->channels[i].center_freq == freq) {
            chan = &dev->sband_2g->channels[i];
            break;
        }
    }
    if (!chan) {
        XYLog("%s: no channel for %u MHz\n", __FUNCTION__, freq);
        return -EINVAL;
    }

    chandef.chan = chan;
    chandef.width = NL80211_CHAN_WIDTH_20_NOHT;
    chandef.center_freq1 = freq;

    /* Scan hops come through as offchannel tunes; fold them into the
     * Linux sw_scan_start/complete hooks (AGC save + SCANNING state). */
    if (offchannel && !test_bit(MT7601U_STATE_SCANNING, &dev->state))
        mt7601u_sw_scan(dev->hw, g.vif, dev->macaddr);
    else if (!offchannel && test_bit(MT7601U_STATE_SCANNING, &dev->state))
        mt7601u_sw_scan_complete(dev->hw, g.vif);

    ret = mt7601u_phy_set_channel(dev, &chandef);
    return ret;
}

/* ---- TX ---- */

/* TID -> mac80211 AC index (VO=0 VI=1 BE=2 BK=3), as in mtk_port_funcs.c */
static const u8 mt7601u_tid_to_ac[8] = {
    2, 3, 3, 2, 1, 1, 0, 0
};

int mt7601u_glue_tx_frame(struct mt7601u_dev *dev,
              const struct mtk_tx_cookie *ck, const void *frame,
              unsigned int len, unsigned int flags,
              unsigned int prio, const struct mtk_key_desc *hwkey)
{
    struct mt7601u_sta_ctx *ctx = NULL;
    struct ieee80211_tx_control control = {};
    struct ieee80211_tx_info *info;
    struct sk_buff *skb;

    (void)hwkey;    /* keys are installed on the wcid; the MAC encrypts */

    if (!ck || !frame || len < 16 || len > 4096)
        return -EINVAL;
    if (test_bit(MT7601U_STATE_REMOVED, &dev->state))
        return -ENODEV;
    if (mt7601u_ensure_vif(dev))
        return -ENOMEM;

    skb = alloc_skb(len + 128, GFP_ATOMIC);
    if (!skb)
        return -ENOMEM;
    skb_reserve(skb, 128);
    memcpy(skb_put(skb, len), frame, len);

    skb->mtk_ck = *ck;

    if (flags & MTK_TX_FLAG_MGMT)
        skb_set_queue_mapping(skb, IEEE80211_AC_BE);
    else
        skb_set_queue_mapping(skb, mt7601u_tid_to_ac[prio & 0x7]);

    info = IEEE80211_SKB_CB(skb);
    info->control.vif = g.vif;
    if (flags & MTK_TX_FLAG_NO_ACK)
        info->flags |= IEEE80211_TX_CTL_NO_ACK;

    /* Per-frame rate: legacy 6M OFDM for management, HT MCS7 for data.
     * TODO: real rate control + probe/rate-fallback policy. */
    if (flags & MTK_TX_FLAG_MGMT) {
        info->control.rates[0].idx = 4;     /* OFDM 6M in the 2g table */
        info->control.rates[0].flags = 0;
    } else {
        info->control.rates[0].idx = 7;     /* HT MCS7 */
        info->control.rates[0].flags = IEEE80211_TX_RC_MCS;
    }
    info->control.rates[0].count = 1;

    ctx = mt7601u_sta_find((void *)ck->ni);
    control.sta = ctx ? ctx->sta : NULL;

    /* mt7601u_tx() either transmits (async completion frees the skb and
     * runs mtk_net80211_tx_done()) or drops it via ieee80211_free_txskb()
     * (which completes the cookie the same way).  Both paths hand the
     * cookie back exactly once, so the frame is always "accepted". */
    mt7601u_tx(dev->hw, &control, skb);
    return 0;
}

void mt7601u_glue_tx_flush(struct mt7601u_dev *dev)
{
    struct mt7601u_tx_queue *q;
    struct sk_buff *skb;
    int i, j;

    if (!dev->tx_q)
        return;

    for (i = 0; i < __MT_EP_OUT_MAX; i++) {
        q = &dev->tx_q[i];
        for (j = 0; j < q->entries; j++)
            usb_kill_urb(q->e[j].urb);
        for (j = 0; j < q->entries; j++) {
            skb = q->e[j].skb;
            if (!skb)
                continue;
            q->e[j].skb = NULL;
            mt7601u_tx_status(dev, skb);
        }
        q->start = 0;
        q->end = 0;
        q->used = 0;
    }
}
