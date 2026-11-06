// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 *
 * mac.c - MT7601U MAC helpers (port for mt76wm/XNU).  The mac80211 RX
 * status plumbing moved to dma.c (mtk_net80211_rx bridge); the TXS
 * reporting path is reduced to register polling (net80211 completion
 * rides the TX URB path, see tx.c).
 */

#include "mt7601u.h"
#include "trace.h"
#include <linux/etherdevice.h>

void mt7601u_set_macaddr(struct mt7601u_dev *dev, const u8 *addr)
{
    ether_addr_copy(dev->macaddr, addr);

    if (!is_valid_ether_addr(dev->macaddr)) {
        eth_random_addr(dev->macaddr);
        dev_info(dev->dev,
             "Invalid MAC address, using random address %pM\n",
             dev->macaddr);
    }

    mt76_wr(dev, MT_MAC_ADDR_DW0, get_unaligned_le32(dev->macaddr));
    mt76_wr(dev, MT_MAC_ADDR_DW1, get_unaligned_le16(dev->macaddr + 4) |
        FIELD_PREP(MT_MAC_ADDR_DW1_U2ME_MASK, 0xff));
}

u16 mt76_mac_tx_rate_val(struct mt7601u_dev *dev,
             const struct ieee80211_tx_rate *rate, u8 *nss_val)
{
    u16 rateval;
    u8 phy, rate_idx;
    u8 nss = 1;
    u8 bw = 0;

    if (rate->flags & IEEE80211_TX_RC_MCS) {
        rate_idx = rate->idx;
        nss = 1 + (rate->idx >> 3);
        phy = MT_PHY_TYPE_HT;
        if (rate->flags & IEEE80211_TX_RC_GREEN_FIELD)
            phy = MT_PHY_TYPE_HT_GF;
        if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
            bw = 1;
    } else {
        const struct ieee80211_rate *r;
        u16 val;

        /* XNU: the single 2.4 GHz band lives on dev->sband_2g. */
        r = &dev->sband_2g->bitrates[rate->idx];
        if (rate->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
            val = r->hw_value_short;
        else
            val = r->hw_value;

        phy = val >> 8;
        rate_idx = val & 0xff;
        bw = 0;
    }

    rateval = FIELD_PREP(MT_RXWI_RATE_MCS, rate_idx);
    rateval |= FIELD_PREP(MT_RXWI_RATE_PHY, phy);
    rateval |= FIELD_PREP(MT_RXWI_RATE_BW, bw);
    if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
        rateval |= MT_RXWI_RATE_SGI;

    *nss_val = nss;
    return rateval;
}

void mt76_mac_wcid_set_rate(struct mt7601u_dev *dev, struct mt76_wcid *wcid,
                const struct ieee80211_tx_rate *rate)
{
    unsigned long flags;

    spin_lock_irqsave(&dev->lock, flags);
    wcid->tx_rate = mt76_mac_tx_rate_val(dev, rate, &wcid->tx_rate_nss);
    wcid->tx_rate_set = true;
    spin_unlock_irqrestore(&dev->lock, flags);
}

struct mt76_tx_status mt7601u_mac_fetch_tx_status(struct mt7601u_dev *dev)
{
    struct mt76_tx_status stat = {};
    u32 val;

    val = mt7601u_rr(dev, MT_TX_STAT_FIFO);
    stat.valid = !!(val & MT_TX_STAT_FIFO_VALID);
    stat.success = !!(val & MT_TX_STAT_FIFO_SUCCESS);
    stat.aggr = !!(val & MT_TX_STAT_FIFO_AGGR);
    stat.ack_req = !!(val & MT_TX_STAT_FIFO_ACKREQ);
    stat.pktid = FIELD_GET(MT_TX_STAT_FIFO_PID_TYPE, val);
    stat.wcid = FIELD_GET(MT_TX_STAT_FIFO_WCID, val);
    stat.rate = FIELD_GET(MT_TX_STAT_FIFO_RATE, val);

    return stat;
}

void mt76_send_tx_status(struct mt7601u_dev *dev, struct mt76_tx_status *stat)
{
    /*
     * XNU: the per-frame TX status goes back to net80211 through the
     * URB completion path (mt7601u_tx_status() in tx.c).  The TX_STAT_FIFO
     * reports are only drained to keep the FIFO from filling up.
     * TODO: feed rate/counters back into the statistics.
     */
    (void)dev;
    (void)stat;
}

void mt7601u_mac_set_protection(struct mt7601u_dev *dev, bool legacy_prot,
                int ht_mode)
{
    int mode = ht_mode & IEEE80211_HT_OP_MODE_PROTECTION;
    bool non_gf = !!(ht_mode & IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT);
    u32 prot[6];
    bool ht_rts[4] = {};
    int i;

    prot[0] = MT_PROT_NAV_SHORT |
          MT_PROT_TXOP_ALLOW_ALL |
          MT_PROT_RTS_THR_EN;
    prot[1] = prot[0];
    if (legacy_prot)
        prot[1] |= MT_PROT_CTRL_CTS2SELF;

    prot[2] = prot[4] = MT_PROT_NAV_SHORT | MT_PROT_TXOP_ALLOW_BW20;
    prot[3] = prot[5] = MT_PROT_NAV_SHORT | MT_PROT_TXOP_ALLOW_ALL;

    if (legacy_prot) {
        prot[2] |= MT_PROT_RATE_CCK_11;
        prot[3] |= MT_PROT_RATE_CCK_11;
        prot[4] |= MT_PROT_RATE_CCK_11;
        prot[5] |= MT_PROT_RATE_CCK_11;
    } else {
        prot[2] |= MT_PROT_RATE_OFDM_24;
        prot[3] |= MT_PROT_RATE_DUP_OFDM_24;
        prot[4] |= MT_PROT_RATE_OFDM_24;
        prot[5] |= MT_PROT_RATE_DUP_OFDM_24;
    }

    switch (mode) {
    case IEEE80211_HT_OP_MODE_PROTECTION_NONE:
        break;

    case IEEE80211_HT_OP_MODE_PROTECTION_NONMEMBER:
        ht_rts[0] = ht_rts[1] = ht_rts[2] = ht_rts[3] = true;
        break;

    case IEEE80211_HT_OP_MODE_PROTECTION_20MHZ:
        ht_rts[1] = ht_rts[3] = true;
        break;

    case IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED:
        ht_rts[0] = ht_rts[1] = ht_rts[2] = ht_rts[3] = true;
        break;
    }

    if (non_gf)
        ht_rts[2] = ht_rts[3] = true;

    for (i = 0; i < 4; i++)
        if (ht_rts[i])
            prot[i + 2] |= MT_PROT_CTRL_RTS_CTS;

    for (i = 0; i < 6; i++)
        mt7601u_wr(dev, MT_CCK_PROT_CFG + i * 4, prot[i]);
}

void mt7601u_mac_set_short_preamble(struct mt7601u_dev *dev, bool short_preamb)
{
    if (short_preamb)
        mt76_set(dev, MT_AUTO_RSP_CFG, MT_AUTO_RSP_PREAMB_SHORT);
    else
        mt76_clear(dev, MT_AUTO_RSP_CFG, MT_AUTO_RSP_PREAMB_SHORT);
}

void mt7601u_mac_config_tsf(struct mt7601u_dev *dev, bool enable, int interval)
{
    u32 val = mt7601u_rr(dev, MT_BEACON_TIME_CFG);

    val &= ~(MT_BEACON_TIME_CFG_TIMER_EN |
         MT_BEACON_TIME_CFG_SYNC_MODE |
         MT_BEACON_TIME_CFG_TBTT_EN);

    if (!enable) {
        mt7601u_wr(dev, MT_BEACON_TIME_CFG, val);
        return;
    }

    val &= ~MT_BEACON_TIME_CFG_INTVAL;
    val |= FIELD_PREP(MT_BEACON_TIME_CFG_INTVAL, interval << 4) |
        MT_BEACON_TIME_CFG_TIMER_EN |
        MT_BEACON_TIME_CFG_SYNC_MODE |
        MT_BEACON_TIME_CFG_TBTT_EN;
}

static void mt7601u_check_mac_err(struct mt7601u_dev *dev)
{
    u32 val = mt7601u_rr(dev, 0x10f4);

    if (!(val & BIT(29)) || !(val & (BIT(7) | BIT(5))))
        return;

    dev_err(dev->dev, "Error: MAC specific condition occurred\n");

    mt76_set(dev, MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_RESET_CSR);
    udelay(10);
    mt76_clear(dev, MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_RESET_CSR);
}

void mt7601u_mac_work(struct work_struct *work)
{
    struct mt7601u_dev *dev = (struct mt7601u_dev *)container_of(work,
                               struct mt7601u_dev,
                               mac_work.work);
    struct {
        u32 addr_base;
        u32 span;
        u64 *stat_base;
    } spans[] = {
        { MT_RX_STA_CNT0,	3,	dev->stats.rx_stat },
        { MT_TX_STA_CNT0,	3,	dev->stats.tx_stat },
        { MT_TX_AGG_STAT,	1,	dev->stats.aggr_stat },
        { MT_MPDU_DENSITY_CNT,	1,	dev->stats.zero_len_del },
        { MT_TX_AGG_CNT_BASE0,	8,	&dev->stats.aggr_n[0] },
        { MT_TX_AGG_CNT_BASE1,	8,	&dev->stats.aggr_n[16] },
    };
    u32 sum, n;
    int i, j, k;

    /* Note: using MCU_RANDOM_READ is actually slower then reading all the
     *	 registers by hand.  MCU takes ca. 20ms to complete read of 24
     *	 registers while reading them one by one will takes roughly
     *	 24*200us =~ 5ms.
     */

    k = 0;
    n = 0;
    sum = 0;
    for (i = 0; i < ARRAY_SIZE(spans); i++)
        for (j = 0; j < spans[i].span; j++) {
            u32 val = mt7601u_rr(dev, spans[i].addr_base + j * 4);

            spans[i].stat_base[j * 2] += val & 0xffff;
            spans[i].stat_base[j * 2 + 1] += val >> 16;

            /* Calculate average AMPDU length */
            if (spans[i].addr_base != MT_TX_AGG_CNT_BASE0 &&
                spans[i].addr_base != MT_TX_AGG_CNT_BASE1)
                continue;

            n += (val >> 16) + (val & 0xffff);
            sum += (val & 0xffff) * (1 + k * 2) +
                (val >> 16) * (2 + k * 2);
            k++;
        }

    atomic_set(&dev->avg_ampdu_len, n ? DIV_ROUND_CLOSEST(sum, n) : 1);

    mt7601u_check_mac_err(dev);

    ieee80211_queue_delayed_work(dev->hw, &dev->mac_work, 10 * HZ);
}

void
mt7601u_mac_wcid_setup(struct mt7601u_dev *dev, u8 idx, u8 vif_idx, u8 *mac)
{
    u8 zmac[ETH_ALEN] = {};
    u32 attr;

    attr = FIELD_PREP(MT_WCID_ATTR_BSS_IDX, vif_idx & 7) |
           FIELD_PREP(MT_WCID_ATTR_BSS_IDX_EXT, !!(vif_idx & 8));

    mt76_wr(dev, MT_WCID_ATTR(idx), attr);

    if (mac)
        memcpy(zmac, mac, sizeof(zmac));

    mt7601u_addr_wr(dev, MT_WCID_ADDR(idx), zmac);
}

void mt7601u_mac_set_ampdu_factor(struct mt7601u_dev *dev)
{
    struct ieee80211_sta *sta;
    struct mt76_wcid *wcid;
    void *msta;
    u8 min_factor = 3;
    int i;

    rcu_read_lock();
    for (i = 0; i < ARRAY_SIZE(dev->wcid); i++) {
        wcid = rcu_dereference(dev->wcid[i]);
        if (!wcid)
            continue;

        msta = container_of(wcid, struct mt76_sta, wcid);
        sta = container_of(msta, struct ieee80211_sta, drv_priv);

        min_factor = min(min_factor, sta->deflink.ht_cap.ampdu_factor);
    }
    rcu_read_unlock();

    mt7601u_wr(dev, MT_MAX_LEN_CFG, 0xa0fff |
           FIELD_PREP(MT_MAX_LEN_CFG_AMPDU, min_factor));
}

int mt76_mac_wcid_set_key(struct mt7601u_dev *dev, u8 idx,
              struct ieee80211_key_conf *key)
{
    enum mt76_cipher_type cipher;
    u8 key_data[32];
    u8 iv_data[8];
    u32 val;

    cipher = mt76_mac_get_key_info(key, key_data);
    if (cipher == MT_CIPHER_NONE && key)
        return -EINVAL;

    trace_set_key(dev, idx);

    mt7601u_wr_copy(dev, MT_WCID_KEY(idx), key_data, sizeof(key_data));

    memset(iv_data, 0, sizeof(iv_data));
    if (key) {
        iv_data[3] = key->keyidx << 6;
        if (cipher >= MT_CIPHER_TKIP) {
            /* Note: start with 1 to comply with spec,
             *	 (see comment on common/cmm_wpa.c:4291).
             */
            iv_data[0] |= 1;
            iv_data[3] |= 0x20;
        }
    }
    mt7601u_wr_copy(dev, MT_WCID_IV(idx), iv_data, sizeof(iv_data));

    val = mt7601u_rr(dev, MT_WCID_ATTR(idx));
    val &= ~MT_WCID_ATTR_PKEY_MODE & ~MT_WCID_ATTR_PKEY_MODE_EXT;
    val |= FIELD_PREP(MT_WCID_ATTR_PKEY_MODE, cipher & 7) |
           FIELD_PREP(MT_WCID_ATTR_PKEY_MODE_EXT, cipher >> 3);
    val &= ~MT_WCID_ATTR_PAIRWISE;
    val |= MT_WCID_ATTR_PAIRWISE *
        !!(key && key->flags & IEEE80211_KEY_FLAG_PAIRWISE);
    mt7601u_wr(dev, MT_WCID_ATTR(idx), val);

    return 0;
}

int mt76_mac_shared_key_setup(struct mt7601u_dev *dev, u8 vif_idx, u8 key_idx,
              struct ieee80211_key_conf *key)
{
    enum mt76_cipher_type cipher;
    u8 key_data[32];
    u32 val;

    cipher = mt76_mac_get_key_info(key, key_data);
    if (cipher == MT_CIPHER_NONE && key)
        return -EINVAL;

    trace_set_shared_key(dev, vif_idx, key_idx);

    mt7601u_wr_copy(dev, MT_SKEY(vif_idx, key_idx),
            key_data, sizeof(key_data));

    val = mt76_rr(dev, MT_SKEY_MODE(vif_idx));
    val &= ~(MT_SKEY_MODE_MASK << MT_SKEY_MODE_SHIFT(vif_idx, key_idx));
    val |= cipher << MT_SKEY_MODE_SHIFT(vif_idx, key_idx);
    mt76_wr(dev, MT_SKEY_MODE(vif_idx), val);

    return 0;
}

/* ---- key material extraction (Linux mt76_mac_get_key_info) ---- */

enum mt76_cipher_type
mt76_mac_get_key_info(struct ieee80211_key_conf *key, u8 *key_data)
{
    memset(key_data, 0, 32);
    if (!key)
        return MT_CIPHER_NONE;

    if (key->keylen > 32)
        return MT_CIPHER_NONE;

    memcpy(key_data, key->key, key->keylen);

    switch (key->cipher) {
    case WLAN_CIPHER_SUITE_WEP40:
        return MT_CIPHER_WEP40;
    case WLAN_CIPHER_SUITE_WEP104:
        return MT_CIPHER_WEP104;
    case WLAN_CIPHER_SUITE_TKIP:
        return MT_CIPHER_TKIP;
    case WLAN_CIPHER_SUITE_CCMP:
        return MT_CIPHER_AES_CCMP;
    default:
        return MT_CIPHER_NONE;
    }
}
