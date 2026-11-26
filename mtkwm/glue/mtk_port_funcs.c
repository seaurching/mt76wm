/*
 * mtk_port_funcs.c - minimal implementations of the functions that live in
 * Linux's mt76/mac80211.c, mt76/mt792x_dma.c and mt7921/main.c (not copied
 * into the port) so the mt76wm M1 bring-up can link and initialize.
 *
 * Ported for mt76wm (XNU).  Semantics marked TODO(M2)/TODO(M4) are
 * placeholders; correctness work happens in later milestones.
 */

#include <linux/mtk_core.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>

#include "../mt76core/mt76.h"
#include "../mt76core/dma.h"
#include "../mt76core/mt792x.h"
#include "../mt76core/mt76_connac_mcu.h"
#include "../mt7921/mt7921.h"

#include "mtk_api.h"

/* ================================================================== */
/* rate tables (Linux mt76/mac80211.c)                                */
/* ================================================================== */
struct ieee80211_rate mt76_rates[12] = {
    CCK_RATE(0, 10),
    CCK_RATE(1, 20),
    CCK_RATE(2, 55),
    CCK_RATE(3, 110),
    OFDM_RATE(0, 60),
    OFDM_RATE(1, 90),
    OFDM_RATE(2, 120),
    OFDM_RATE(3, 180),
    OFDM_RATE(4, 240),
    OFDM_RATE(5, 360),
    OFDM_RATE(6, 480),
    OFDM_RATE(7, 540),
};

/* ================================================================== */
/* device allocation / registration (mt76/mac80211.c)                 */
/* ================================================================== */
struct mt76_dev *
mt76_alloc_device(struct device *pdev, unsigned int size,
                  const struct ieee80211_ops *ops,
                  const struct mt76_driver_ops *drv_ops)
{
    struct ieee80211_hw *hw;
    struct mt76_phy *phy;
    struct mt76_dev *dev;
    int i;

    hw = ieee80211_alloc_hw(size, ops);
    if (!hw)
        return NULL;

    dev = (struct mt76_dev *)hw->priv;
    dev->hw = hw;
    dev->dev = pdev;
    dev->init_wiphy = NULL;
    dev->drv = drv_ops;
    dev->dma_dev = pdev;

    phy = &dev->phy;
    phy->dev = dev;
    phy->hw = hw;
    phy->band_idx = MT_BAND0;
    dev->phys[phy->band_idx] = phy;

    spin_lock_init(&dev->rx_lock);
    spin_lock_init(&dev->lock);
    spin_lock_init(&dev->cc_lock);
    spin_lock_init(&dev->status_lock);
    spin_lock_init(&dev->wed_lock);
    mutex_init(&dev->mutex);
    init_waitqueue_head(&dev->tx_wait);

    skb_queue_head_init(&dev->mcu.res_q);
    init_waitqueue_head(&dev->mcu.wait);
    mutex_init(&dev->mcu.mutex);
    dev->tx_worker.fn = mt76_tx_worker;

    hw->wiphy->flags |= WIPHY_FLAG_IBSS_RSN;
    hw->wiphy->interface_modes =
        BIT(NL80211_IFTYPE_STATION) |
        BIT(NL80211_IFTYPE_AP) |
        BIT(NL80211_IFTYPE_P2P_CLIENT) |
        BIT(NL80211_IFTYPE_P2P_GO) |
        BIT(NL80211_IFTYPE_ADHOC);

    spin_lock_init(&dev->token_lock);
    idr_init(&dev->token);

    spin_lock_init(&dev->rx_token_lock);
    idr_init(&dev->rx_token);

    INIT_LIST_HEAD(&dev->wcid_list);
    INIT_LIST_HEAD(&dev->sta_poll_list);
    spin_lock_init(&dev->sta_poll_lock);

    INIT_LIST_HEAD(&dev->txwi_cache);
    INIT_LIST_HEAD(&dev->rxwi_cache);
    INIT_LIST_HEAD(&phy->tx_list);
    spin_lock_init(&phy->tx_lock);
    dev->token_size = dev->drv->token_size;
    spin_lock_init(&dev->scan_lock);

    for (i = 0; i < ARRAY_SIZE(dev->q_rx); i++)
        skb_queue_head_init(&dev->rx_skb[i]);

    dev->wq = alloc_ordered_workqueue("mt76", 0);

    return dev;
}

/* ================================================================== */
/* channel tables (mac80211-side; used by mt7921_mac_fill_rx and       */
/* mt7921_mcu_set_chan_info which need sband->channels)                */
/* ================================================================== */
static struct ieee80211_channel mtk_chans_2g[14];
static struct ieee80211_channel mtk_chans_5g[28];

static void mtk_init_chan_tables(void)
{
    int i, n;

    for (i = 0; i < 14; i++) {
        mtk_chans_2g[i].band = NL80211_BAND_2GHZ;
        mtk_chans_2g[i].hw_value = i + 1;
        mtk_chans_2g[i].center_freq =
            (u16)ieee80211_channel_to_frequency(i + 1, NL80211_BAND_2GHZ);
        /* 20 dBm regulatory cap: feeds SET_RATE_TX_POWER (the SKU is
         * 2 * max_reg_power in half-dBm = 0x28, the value the working
         * mwx reference hardcodes).  A zero cap transmits muted. */
        mtk_chans_2g[i].max_power = 20;
        mtk_chans_2g[i].max_reg_power = 20;
    }
    n = 0;
    for (i = 36; i <= 64 && n < (int)ARRAY_SIZE(mtk_chans_5g); i += 4) {
        mtk_chans_5g[n].band = NL80211_BAND_5GHZ;
        mtk_chans_5g[n].hw_value = i;
        mtk_chans_5g[n].center_freq =
            (u16)ieee80211_channel_to_frequency(i, NL80211_BAND_5GHZ);
        n++;
    }
    for (i = 100; i <= 144 && n < (int)ARRAY_SIZE(mtk_chans_5g); i += 4) {
        mtk_chans_5g[n].band = NL80211_BAND_5GHZ;
        mtk_chans_5g[n].hw_value = i;
        mtk_chans_5g[n].center_freq =
            (u16)ieee80211_channel_to_frequency(i, NL80211_BAND_5GHZ);
        n++;
    }
    for (i = 149; i <= 177 && n < (int)ARRAY_SIZE(mtk_chans_5g); i += 4) {
        mtk_chans_5g[n].band = NL80211_BAND_5GHZ;
        mtk_chans_5g[n].hw_value = i;
        mtk_chans_5g[n].center_freq =
            (u16)ieee80211_channel_to_frequency(i, NL80211_BAND_5GHZ);
        n++;
    }
    /* 20 dBm regulatory cap for 5 GHz too (SKU = 0x28, reference parity). */
    for (i = 0; i < n; i++) {
        mtk_chans_5g[i].max_power = 20;
        mtk_chans_5g[i].max_reg_power = 20;
    }
}

/* band init helpers (channels + rates per band) */
static void mtk_init_sband_min(struct mt76_phy *phy, struct mt76_sband *msband,
                               struct ieee80211_rate *rates, int n_rates,
                               enum nl80211_band band)
{
    msband->sband.bitrates = rates;
    msband->sband.n_bitrates = n_rates;
    msband->sband.band = band;
    /*
     * Register the band with the wiphy: mt76_connac_get_phy_mode() (the
     * bss_setup path at RUN) reads phy->hw->wiphy->bands[band] when no
     * link_sta is given, and a NULL there dereferences at &sband->ht_cap
     * (page fault NULL+0x1e -- the RUN-entry crash after assoc).
     */
    if (phy && phy->hw && phy->hw->wiphy && band < NUM_NL80211_BANDS)
        phy->hw->wiphy->bands[band] = &msband->sband;
    switch (band) {
    case NL80211_BAND_2GHZ:
        msband->sband.channels = mtk_chans_2g;
        msband->sband.n_channels = ARRAY_SIZE(mtk_chans_2g);
        break;
    case NL80211_BAND_5GHZ:
        msband->sband.channels = mtk_chans_5g;
        msband->sband.n_channels = ARRAY_SIZE(mtk_chans_5g);
        break;
    default:
        /* TODO(M6): 6 GHz channel list. */
        msband->sband.channels = NULL;
        msband->sband.n_channels = 0;
        break;
    }
}

int mt76_register_device(struct mt76_dev *dev, bool vht,
                         struct ieee80211_rate *rates, int n_rates)
{
    struct ieee80211_hw *hw = dev->hw;
    struct mt76_phy *phy = &dev->phy;

    (void)vht;
    dev_set_drvdata(dev->dev, dev);
    mt76_wcid_init(&dev->global_wcid, phy->band_idx);

    mtk_init_chan_tables();

    /* Channel/rate band setup. */
    if (phy->cap.has_2ghz)
        mtk_init_sband_min(phy, &phy->sband_2g, rates, n_rates > 4 ? 4 : n_rates,
                           NL80211_BAND_2GHZ);
    if (phy->cap.has_5ghz)
        mtk_init_sband_min(phy, &phy->sband_5g, rates + 4,
                           n_rates > 4 ? n_rates - 4 : 0, NL80211_BAND_5GHZ);
    if (phy->cap.has_6ghz)
        mtk_init_sband_min(phy, &phy->sband_6g, rates + 4,
                           n_rates > 4 ? n_rates - 4 : 0, NL80211_BAND_6GHZ);

    if (dev->init_wiphy) {
        int ret = dev->init_wiphy(dev);

        if (ret)
            return ret;
    }

    return ieee80211_register_hw(hw);
}

void mt76_unregister_device(struct mt76_dev *dev)
{
    ieee80211_unregister_hw(dev->hw);
}

void mt76_free_device(struct mt76_dev *dev)
{
    mt76_unregister_device(dev);
    ieee80211_free_hw(dev->hw);
}

void mt76_reset_device(struct mt76_dev *dev)
{
    (void)dev;
    /* TODO(M2): full device reset */
}

/* ================================================================== */
/* queue helpers                                                      */
/* ================================================================== */
struct mt76_queue *
mt76_init_queue(struct mt76_dev *dev, int qid, int idx, int n_desc,
                int ring_base, void *wed, u32 flags)
{
    struct mt76_queue *hwq;
    int err;

    (void)qid;
    hwq = (struct mt76_queue *)devm_kzalloc(dev->dev, sizeof(*hwq),
                                            GFP_KERNEL);
    if (!hwq)
        return (struct mt76_queue *)ERR_PTR(-ENOMEM);

    hwq->flags = flags;
    hwq->wed = (struct mtk_wed_device *)wed;

    err = dev->queue_ops->alloc(dev, hwq, idx, n_desc, 0, ring_base);
    if (err < 0)
        return (struct mt76_queue *)ERR_PTR(err);

    return hwq;
}

/* ================================================================== */
/* RX path (mt76/mac80211.c) -> net80211 (glue/mtk_net80211.c)         */
/* ================================================================== */
/*
 * RX delivery chain -- faithful port of Linux mt76/mac80211.c:
 *
 *   mt76_rx()                  per-frame entry (from mt7921_queue_rx_skb)
 *     -> mt76_rx_release_burst()   A-MSDU subframe aggregation
 *   mt76_rx_poll_complete()    end of each poll round
 *     -> drain dev->rx_skb[q] -> mt76_check_sta() -> mt76_rx_aggr_reorder()
 *   mt76_rx_complete()         CCMP PN check, frag_list split, deliver
 *     -> mtk_rx_deliver()      the only platform adapter (Linux ends at
 *                              ieee80211_rx_list(); we end at net80211)
 */
/* TEMP(diag, scan-debug round): delivery-chain census. */
static struct { uint64_t rx, notrun, queued, drained, delivered, locked; }
    mtk_chain_cnt;

static void mtk_chain_dump(const char *tag)
{
    static uint64_t shown[6];
    uint64_t *cur = &mtk_chain_cnt.rx;
    int i;

    for (i = 0; i < 6; i++)
        if (cur[i] != shown[i])
            break;
    if (i == 6)
        return;
    for (i = 0; i < 6; i++)
        shown[i] = cur[i];
    XYLog("mtk-dbg-chain[%s]: rx=%llu notrun=%llu queued=%llu drained=%llu "
          "delivered=%llu x=%llu\n",
          tag,
          (unsigned long long)mtk_chain_cnt.rx,
          (unsigned long long)mtk_chain_cnt.notrun,
          (unsigned long long)mtk_chain_cnt.queued,
          (unsigned long long)mtk_chain_cnt.drained,
          (unsigned long long)mtk_chain_cnt.delivered,
          (unsigned long long)mtk_chain_cnt.locked);
}

static void mtk_rx_deliver(struct mt76_dev *dev, struct sk_buff *skb)
{
    struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
    struct mt792x_dev *mdev = container_of(dev, struct mt792x_dev, mt76);
    struct mtk_rx_meta meta;
    int dbm;

    mtk_chain_cnt.delivered++;

    memset(&meta, 0, sizeof(meta));
    meta.freq = status->freq;
    meta.timestamp = status->timestamp;
    meta.rate_idx = status->rate_idx;
    meta.nss = status->nss;
    meta.encoding = status->encoding;
    meta.bw = status->bw;
    meta.enc_flags = status->enc_flags;
    meta.chains = status->chains;
    memcpy(meta.chain_signal, status->chain_signal,
           sizeof(meta.chain_signal));

    if (status->flag & RX_FLAG_DECRYPTED)
        meta.flags |= MTK_RX_FLAG_DECRYPTED;
    if (status->flag & RX_FLAG_IV_STRIPPED)
        meta.flags |= MTK_RX_FLAG_IV_STRIPPED;
    if (status->flag & RX_FLAG_MMIC_STRIPPED)
        meta.flags |= MTK_RX_FLAG_MMIC_STRIPPED;
    if (status->flag & RX_FLAG_MIC_STRIPPED)
        meta.flags |= MTK_RX_FLAG_MIC_STRIPPED;
    if (status->flag & RX_FLAG_FAILED_FCS_CRC)
        meta.flags |= MTK_RX_FLAG_FAILED_FCS;
    if (status->flag & RX_FLAG_MMIC_ERROR)
        meta.flags |= MTK_RX_FLAG_MMIC_ERROR;
    if (status->flag & RX_FLAG_8023)
        meta.flags |= MTK_RX_FLAG_8023;
    if (status->flag & RX_FLAG_ONLY_MONITOR)
        meta.flags |= MTK_RX_FLAG_ONLY_MONITOR;

    /* Average per-chain signal, dBm (works for frames without wcid,
     * e.g. beacons from not-yet-known APs). */
    dbm = mt76_rx_signal(status->chains, status->chain_signal);
    if (dbm > 0 || dbm <= -128)
        dbm = -95;              /* bogus value: assume the noise floor */
    meta.rssi_dbm = dbm;

    mtk_net80211_rx(mdev, skb->data, skb->len, &meta);
    dev_kfree_skb(skb);
}

/* Port of Linux mt76_check_ccmp_pn(): per-recipient replay detection
 * (IEEE 802.11-2020 12.5.3.4.4).  A failed replay is demoted to
 * monitor-only exactly like Linux. */
static void
mt76_check_ccmp_pn(struct sk_buff *skb)
{
    struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
    struct mt76_wcid *wcid = status->wcid;
    struct ieee80211_hdr *hdr;
    int security_idx;
    int ret;

    if (!(status->flag & RX_FLAG_DECRYPTED))
        return;

    if (status->flag & RX_FLAG_ONLY_MONITOR)
        return;

    if (!wcid || !wcid->rx_check_pn)
        return;

    security_idx = status->qos_ctl & IEEE80211_QOS_CTL_TID_MASK;
    if (status->flag & RX_FLAG_8023)
        goto skip_hdr_check;

    hdr = (struct ieee80211_hdr *)mt76_skb_get_hdr(skb);
    if (!(status->flag & RX_FLAG_IV_STRIPPED)) {
        if (ieee80211_is_frag(hdr) &&
            !ieee80211_is_first_frag(hdr->seq_ctrl))
            return;
    }

    /* IEEE 802.11-2020, 12.5.3.4.4 c): a single replay counter for
     * individually addressed robust Management frames (To DS == 0). */
    if (ieee80211_is_mgmt(hdr->frame_control) &&
        !ieee80211_has_tods(hdr->frame_control))
        security_idx = IEEE80211_NUM_TIDS;

skip_hdr_check:
    BUILD_BUG_ON(sizeof(status->iv) != sizeof(wcid->rx_key_pn[0]));
    ret = memcmp(status->iv, wcid->rx_key_pn[security_idx],
                 sizeof(status->iv));
    if (ret <= 0) {
        status->flag |= RX_FLAG_ONLY_MONITOR;
        return;
    }

    memcpy(wcid->rx_key_pn[security_idx], status->iv, sizeof(status->iv));

    if (status->flag & RX_FLAG_IV_STRIPPED)
        status->flag |= RX_FLAG_PN_VALIDATED;
}

/* Port of Linux mt76_check_sta(): pspoll source resolution, RSSI/poll
 * bookkeeping and the STA power-save state machine.  mt76_airtime_check()
 * is omitted deliberately: it is a no-op on MT7921 (drv_flags carries no
 * MT_DRV_SW_RX_AIRTIME -- airtime is reported from WTBL polling in
 * mt7921_mac_sta_poll instead). */
static void
mt76_check_sta(struct mt76_dev *dev, struct sk_buff *skb)
{
    struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
    struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)mt76_skb_get_hdr(skb);
    struct ieee80211_sta *sta;
    struct mt76_wcid *wcid = status->wcid;
    u8 tidno = status->qos_ctl & IEEE80211_QOS_CTL_TID_MASK;
    int signal;
    bool ps;

    if (ieee80211_is_pspoll(hdr->frame_control) && !wcid &&
        !(status->flag & RX_FLAG_8023)) {
        sta = ieee80211_find_sta_by_ifaddr(dev->hw, hdr->addr2, NULL);
        if (sta)
            wcid = status->wcid = (struct mt76_wcid *)sta->drv_priv;
    }

    if (!wcid || !wcid->sta)
        return;

    sta = wcid_to_sta(wcid);

    signal = mt76_rx_signal(status->chains, status->chain_signal);
    if (signal <= 0 && signal > -128)
        ewma_signal_add(&wcid->rssi, -signal);

    wcid->inactive_count = 0;

    if (status->flag & RX_FLAG_8023)
        return;

    if (!test_bit(MT_WCID_FLAG_CHECK_PS, &wcid->flags))
        return;

    if (ieee80211_is_pspoll(hdr->frame_control)) {
        ieee80211_sta_pspoll(sta);
        return;
    }

    if (ieee80211_has_morefrags(hdr->frame_control) ||
        !(ieee80211_is_mgmt(hdr->frame_control) ||
          ieee80211_is_data(hdr->frame_control)))
        return;

    ps = ieee80211_has_pm(hdr->frame_control);

    if (ps && (ieee80211_is_data_qos(hdr->frame_control) ||
               ieee80211_is_qos_nullfunc(hdr->frame_control)))
        ieee80211_sta_uapsd_trigger(sta, tidno);

    if (!!test_bit(MT_WCID_FLAG_PS, &wcid->flags) == ps)
        return;

    if (ps)
        set_bit(MT_WCID_FLAG_PS, &wcid->flags);

    if (dev->drv->sta_ps)
        dev->drv->sta_ps(dev, sta, ps);

    if (!ps)
        clear_bit(MT_WCID_FLAG_PS, &wcid->flags);

    ieee80211_sta_ps_transition(sta, ps);
}

static void
mt76_rx_release_amsdu(struct mt76_phy *phy, enum mt76_rxq_id q)
{
    struct sk_buff *skb = phy->rx_amsdu[q].head;
    struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
    struct mt76_dev *dev = phy->dev;

    phy->rx_amsdu[q].head = NULL;
    phy->rx_amsdu[q].tail = NULL;

    /*
     * Validate if the amsdu has a proper first subframe (Linux): a
     * single MSDU can be parsed as A-MSDU when the unauthenticated
     * A-MSDU flag of the QoS header gets flipped; in such cases the
     * first subframe has a LLC/SNAP header where the destination
     * address should be.
     */
    if (skb_shinfo(skb)->frag_list) {
        int offset = 0;

        if (!(status->flag & RX_FLAG_8023)) {
            offset = ieee80211_get_hdrlen_from_skb(skb);

            if ((status->flag &
                 (RX_FLAG_DECRYPTED | RX_FLAG_IV_STRIPPED)) ==
                RX_FLAG_DECRYPTED)
                offset += 8;
        }

        if (ether_addr_equal(skb->data + offset, rfc1042_header)) {
            dev_kfree_skb(skb);
            return;
        }
    }

    __skb_queue_tail(&dev->rx_skb[q], skb);
    mtk_chain_cnt.queued++;
}

static void
mt76_rx_release_burst(struct mt76_phy *phy, enum mt76_rxq_id q,
                      struct sk_buff *skb)
{
    struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;

    if (phy->rx_amsdu[q].head &&
        (!status->amsdu || status->first_amsdu ||
         status->seqno != phy->rx_amsdu[q].seqno))
        mt76_rx_release_amsdu(phy, q);

    if (!phy->rx_amsdu[q].head) {
        phy->rx_amsdu[q].tail = &skb_shinfo(skb)->frag_list;
        phy->rx_amsdu[q].seqno = status->seqno;
        phy->rx_amsdu[q].head = skb;
    } else {
        *phy->rx_amsdu[q].tail = skb;
        phy->rx_amsdu[q].tail = &skb->next;
    }

    if (!status->amsdu || status->last_amsdu)
        mt76_rx_release_amsdu(phy, q);
}

void mt76_rx(struct mt76_dev *dev, enum mt76_rxq_id q, struct sk_buff *skb)
{
    struct mt76_phy *phy = &dev->phy;

    mtk_chain_cnt.rx++;
    if (!test_bit(MT76_STATE_RUNNING, &phy->state)) {
        mtk_chain_cnt.notrun++;
        dev_kfree_skb(skb);
        return;
    }

    mt76_rx_release_burst(phy, q, skb);
}

void mt76_rx_complete(struct mt76_dev *dev, struct sk_buff_head *frames,
                      struct napi_struct *napi)
{
    struct sk_buff *skb;

    (void)napi;
    /*
     * XNU: deliver OUTSIDE any spinlock.  Linux holds rx_lock across
     * delivery because its RX allocations are GFP_ATOMIC; on XNU the
     * net80211 path allocates nodes/mbufs and may block, and blocking
     * with preemption disabled panics ("thread_invoke:
     * preemption_level 1").  Producer (mt76_rx) and consumer both run
     * serialized on the tasklet thread, so no lock is needed here.
     */
    while ((skb = __skb_dequeue(frames)) != NULL) {
        struct sk_buff *nskb = skb_shinfo(skb)->frag_list;

        mt76_check_ccmp_pn(skb);
        skb_shinfo(skb)->frag_list = NULL;

        mtk_rx_deliver(dev, skb);

        /* subsequent amsdu subframes: Linux delivers each separately */
        while (nskb) {
            skb = nskb;
            nskb = skb->next;
            skb->next = NULL;
            mtk_rx_deliver(dev, skb);
        }
    }
    mtk_net80211_rx_flush(container_of(dev, struct mt792x_dev, mt76));
}

void mt76_rx_poll_complete(struct mt76_dev *dev, enum mt76_rxq_id q,
                           struct napi_struct *napi)
{
    struct sk_buff_head frames;
    struct sk_buff *skb;

    (void)napi;
    __skb_queue_head_init(&frames);
    while ((skb = __skb_dequeue(&dev->rx_skb[q])) != NULL) {
        mtk_chain_cnt.drained++;
        mt76_check_sta(dev, skb);
        mt76_rx_aggr_reorder(skb, &frames);
    }
    mtk_chain_dump("poll");
    mt76_rx_complete(dev, &frames, napi);
    if (dev->drv->rx_poll_complete)
        dev->drv->rx_poll_complete(dev, q);
}

int mt76_rx_signal(u8 chain_mask, s8 *chain_signal)
{
    int i, count = 0, sum = 0;

    for (i = 0; i < IEEE80211_MAX_CHAINS; i++) {
        if (!(chain_mask & BIT(i)))
            continue;
        sum += chain_signal[i];
        count++;
    }
    return count > 0 ? sum / count : 0;
}

/* ================================================================== */
/* channel / survey helpers                                           */
/* ================================================================== */
void mt76_update_survey(struct mt76_phy *phy)
{
    (void)phy;    /* TODO(M2): survey accounting */
}

void mt76_phy_chandef_set(struct mt76_phy *phy,
                          struct cfg80211_chan_def *chandef)
{
    phy->chandef = *chandef;
    phy->main_chandef = *chandef;
}

bool mt76_has_tx_pending(struct mt76_phy *phy)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(phy->q_tx); i++) {
        if (phy->q_tx[i] && phy->q_tx[i]->queued)
            return true;
    }
    return false;
}

int mt76_get_rate(struct mt76_dev *dev, struct ieee80211_supported_band *sband,
                  int idx, bool cck)
{
    struct ieee80211_rate *rate;
    int offset = 0;

    (void)dev;
    if (cck)
        offset = 0;
    else if (sband && sband->band != NL80211_BAND_2GHZ)
        offset = 4;
    rate = &mt76_rates[offset + idx];
    return rate->hw_value;
}

s8 mt76_get_power_bound(struct mt76_phy *phy, s8 txpower)
{
    return txpower - phy->txpower_cur;
}

int mt76_get_txpower(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                     unsigned int link_id, int *dbm)
{
    struct mt76_phy *phy = (struct mt76_phy *)hw->priv;

    (void)vif; (void)link_id;
    *dbm = phy->txpower_cur;
    return 0;
}

int mt76_get_sar_power(struct mt76_phy *phy, struct ieee80211_channel *chan,
                       int power)
{
    (void)phy; (void)chan;
    return power;
}

void mt76_insert_ccmp_hdr(struct sk_buff *skb, u8 key_id)
{
    u8 hdr[8] = { 0 };

    (void)key_id;
    /*
     * TODO(M6): real CCMP header insertion (RX re-insert for decrypted
     * fragments).  RX pool buffers have no headroom below skb->data and
     * fragments only occur with A-MPDU (out of scope), so skip instead
     * of pushing below the buffer head.
     */
    if (skb_headroom(skb) < 8)
        return;
    memcpy(skb_push(skb, 8), hdr, 8);
}

void mt76_wcid_remove_poll(struct mt76_dev *dev, struct mt76_wcid *wcid);

void mt76_wcid_cleanup(struct mt76_dev *dev, struct mt76_wcid *wcid)
{
    struct sk_buff_head list;
    struct sk_buff *skb;
    int i;

    /*
     * mt76wm: flush the TXS-pending skbs and unlink wcid->list from
     * dev->wcid_list under the status lock FIRST.  The watchdog's
     * mt76_tx_status_check() walks that list; the join path rebuilt the
     * peer record (remove + re-add) while a walk was in flight, the
     * re-init memset zeroed a still-linked list node, and __list_del
     * dereferenced NULL+8 (page fault).  mt76_tx_status_skb_get(-1)
     * flushes every pending entry and drops the wcid from the list.
     */
    mt76_tx_status_lock(dev, &list);
    mt76_tx_status_skb_get(dev, wcid, -1, &list);
    mt76_tx_status_unlock(dev, &list);

    mt76_wcid_remove_poll(dev, wcid);
    while ((skb = skb_dequeue(&wcid->tx_pending)) != NULL)
        dev_kfree_skb(skb);

    for (i = 0; i < ARRAY_SIZE(wcid->aggr); i++)
        mt76_rx_aggr_stop(dev, wcid, i);

    idr_destroy(&wcid->pktid);
}

void mt76_wcid_add_poll(struct mt76_dev *dev, struct mt76_wcid *wcid)
{
    spin_lock_bh(&dev->sta_poll_lock);
    if (list_empty(&wcid->poll_list))
        list_add_tail(&wcid->poll_list, &dev->sta_poll_list);
    spin_unlock_bh(&dev->sta_poll_lock);
}

void mt76_wcid_remove_poll(struct mt76_dev *dev, struct mt76_wcid *wcid)
{
    spin_lock_bh(&dev->sta_poll_lock);
    if (!list_empty(&wcid->poll_list))
        list_del_init(&wcid->poll_list);
    spin_unlock_bh(&dev->sta_poll_lock);
}

/* ================================================================== */
/* ethtool / page pool                                                */
/* ================================================================== */
void mt76_ethtool_worker(struct mt76_ethtool_worker_info *wi,
                         struct mt76_sta_stats *stats, bool eht)
{
    (void)eht;
    if (wi && wi->data && stats) {
        /* TODO(M2): export stats */
    }
}

void mt76_ethtool_page_pool_stats(struct mt76_dev *dev, u64 *data, int *index)
{
    (void)dev; (void)data;
    if (index)
        *index = 0;
}

int mt76_create_page_pool(struct mt76_dev *dev, struct mt76_queue *q)
{
    struct page_pool_params pp_params = {
        .order = 0,
        .flags = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV,
        .pool_size = (unsigned int)q->ndesc,
        .dev = dev->dma_dev,
        .dma_dir = DMA_FROM_DEVICE,
        .offset = q->buf_offset,
        .max_len = (unsigned int)q->buf_size,
    };

    /*
     * M6: real implementation - a pool of linear DMA-capable buffers
     * (glue/mtk_glue.c, IOBufferMemoryDescriptor backed).  This runs for
     * every queue (TX rings included, where buf_size == 0 and the pool is
     * never drawn from), so ring allocation no longer fails here.
     */
    q->page_pool = page_pool_create(&pp_params);
    if (!q->page_pool)
        return -ENOMEM;
    return 0;
}

/* ================================================================== */
/* mt792x DMA (Linux mt792x_dma.c) - minimal ring management          */
/* ================================================================== */
int mt792x_dma_alloc_queues(struct mt792x_dev *dev,
                            const struct mt792x_dma_layout *layout)
{
    struct mt76_dev *mdev = &dev->mt76;
    int ret;

    /* Mirrors Linux mt792x_dma_alloc_queues(): tx_data0 + tx_mcu +
     * tx_fwdl + (optional) tx_done + rx_mcu + rx_data. */
    mt76_dma_attach(&dev->mt76);

    ret = mt792x_dma_disable(dev, true);
    if (ret)
        return ret;

    /* init tx queue */
    ret = mt76_connac_init_tx_queues(dev->phy.mt76, layout->tx_data0.qid,
                                     layout->tx_data0.n_desc,
                                     layout->tx_data0.ring_base,
                                     NULL, 0);
    if (ret)
        return ret;

    mt76_wr(dev, MT_WFDMA0_TX_RING0_EXT_CTRL, 0x4);

    /* command to WM */
    ret = mt76_init_mcu_queue(mdev, MT_MCUQ_WM, layout->tx_mcu.qid,
                              layout->tx_mcu.n_desc, layout->tx_mcu.ring_base);
    if (ret)
        return ret;

    /* firmware download */
    ret = mt76_init_mcu_queue(mdev, MT_MCUQ_FWDL, layout->tx_fwdl.qid,
                              layout->tx_fwdl.n_desc,
                              layout->tx_fwdl.ring_base);
    if (ret)
        return ret;

    /* tx done (MT7928 WM2 ring) */
    if (layout->tx_done.ring_base) {
        ret = mt76_queue_alloc(dev, &mdev->q_rx[MT_RXQ_MCU_WA],
                               layout->tx_done.qid,
                               layout->tx_done.n_desc,
                               MT_RX_BUF_SIZE,
                               layout->tx_done.ring_base);
        if (ret)
            return ret;
    }

    /* rx event */
    ret = mt76_queue_alloc(dev, &mdev->q_rx[MT_RXQ_MCU],
                           layout->rx_mcu.qid, layout->rx_mcu.n_desc,
                           MT_RX_BUF_SIZE, layout->rx_mcu.ring_base);
    if (ret)
        return ret;

    return mt76_queue_alloc(dev, &mdev->q_rx[MT_RXQ_MAIN],
                            layout->rx_data.qid, layout->rx_data.n_desc,
                            MT_RX_BUF_SIZE, layout->rx_data.ring_base);
}

void mtk_set_poll_dev(struct mt792x_dev *dev);

int mt792x_dma_enable(struct mt792x_dev *dev)
{
    u32 irq_mask;

    if (!dev)
        return -EINVAL;

    /* Ported from Linux mt792x_dma.c:mt792x_dma_enable() -- the MT7961/
     * MT7922 (non-7925/7928) branch.  Without these register writes the
     * WFDMA engine never runs and the device raises no interrupts, so MCU
     * commands go unanswered and no frames reach the air. */

    /* reset dma idx */
    /* per-ring prefetch settings (mwx_dma_enable); the MCU WM and FWDL
     * rings need their windows programmed like every other ring. */
#define MTK_PREFETCH(base, depth)   (((base) & 0xffff) << 16 | (depth))
    mt76_wr(dev, MT_WFDMA0_RX_RING0_EXT_CTRL, MTK_PREFETCH(0x0, 0x4));
    mt76_wr(dev, MT_WFDMA0_RX_RING2_EXT_CTRL, MTK_PREFETCH(0x40, 0x4));
    mt76_wr(dev, MT_WFDMA0_RX_RING3_EXT_CTRL, MTK_PREFETCH(0x80, 0x4));
    mt76_wr(dev, MT_WFDMA0_RX_RING4_EXT_CTRL, MTK_PREFETCH(0xc0, 0x4));
    mt76_wr(dev, MT_WFDMA0_RX_RING5_EXT_CTRL, MTK_PREFETCH(0x100, 0x4));
    mt76_wr(dev, MT_WFDMA0_TX_RING0_EXT_CTRL, MTK_PREFETCH(0x140, 0x4));
    mt76_wr(dev, MT_WFDMA0_TX_RING1_EXT_CTRL, MTK_PREFETCH(0x180, 0x4));
    mt76_wr(dev, MT_WFDMA0_TX_RING2_EXT_CTRL, MTK_PREFETCH(0x1c0, 0x4));
    mt76_wr(dev, MT_WFDMA0_TX_RING3_EXT_CTRL, MTK_PREFETCH(0x200, 0x4));
    mt76_wr(dev, MT_WFDMA0_TX_RING4_EXT_CTRL, MTK_PREFETCH(0x240, 0x4));
    mt76_wr(dev, MT_WFDMA0_TX_RING5_EXT_CTRL, MTK_PREFETCH(0x280, 0x4));
    mt76_wr(dev, MT_WFDMA0_TX_RING6_EXT_CTRL, MTK_PREFETCH(0x2c0, 0x4));
    mt76_wr(dev, MT_WFDMA0_TX_RING16_EXT_CTRL, MTK_PREFETCH(0x340, 0x4));
    mt76_wr(dev, MT_WFDMA0_TX_RING17_EXT_CTRL, MTK_PREFETCH(0x380, 0x4));
#undef MTK_PREFETCH

    mt76_wr(dev, MT_WFDMA0_RST_DTX_PTR, ~0);
    /* configure delay interrupt */
    mt76_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG0, 0);

    mt76_set(dev, MT_WFDMA0_GLO_CFG,
             MT_WFDMA0_GLO_CFG_TX_WB_DDONE |
             MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN |
             MT_WFDMA0_GLO_CFG_CLK_GAT_DIS |
             MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
             FIELD_PREP(MT_WFDMA0_GLO_CFG_DMA_SIZE, 3) |
             MT_WFDMA0_GLO_CFG_FIFO_DIS_CHECK |
             MT_WFDMA0_GLO_CFG_RX_WB_DDONE |
             MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN |
             MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);

    /* fire up the TX/RX DMA engines */
    /* mwx_dma_enable barrier: make every ring/queue write above visible
     * to the WPDMA before the engines start consuming descriptors. */
    __sync_synchronize();
    mt76_set(dev, MT_WFDMA0_GLO_CFG,
             MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);

    mt76_set(dev, MT_WFDMA_DUMMY_CR, MT_WFDMA_NEED_REINIT);

    /* enable interrupts for TX/RX rings + MCU command responses */
    /* Bring-up: enable every host interrupt source; the tasklet masks
     * and acks what it handles.  Bit20 fires on MCU-ring activity but is
     * not covered by the Linux-era masks. */
    irq_mask = 0xffffffff;
    /*
     * NOTE: mt76_set_irq_mask(dev, 0, 0, mask) only updates the software
     * shadow (the write is gated on addr != 0).  MT7921 enables host
     * interrupts by writing MT_WFDMA0_HOST_INT_ENA directly (same as
     * mt7921_irq_enable() in mt7921/pci_mac.c).
     */
    mtk_set_poll_dev(dev);
    dev->mt76.mmio.irqmask = irq_mask;
    if (dev->irq_map)
        mt76_wr(dev, dev->irq_map->host_irq_enable, irq_mask);
    else
        mt76_wr(dev, MT_WFDMA0_HOST_INT_ENA, irq_mask);
    mt76_set(dev, MT_MCU2HOST_SW_INT_ENA, MT_MCU_CMD_WAKE_RX_PCIE);
    /* Forward WPDMA interrupts through the PCIe MAC to the host.  The
     * Linux mt7921 driver relies on the boot value here, but the
     * AirportMtkWlan (mwx) port found it must be programmed explicitly
     * on macOS (dma_enable + attach both write 0xff). */
    mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);

    /* TODO: mt792x_dma_prefetch() tuning for MT7925/7927/7928 families */
    return 0;
}

int mt792x_dma_disable(struct mt792x_dev *dev, bool force)
{
    /* mwx_dma_disable semantics: quiesce the engine, bypass the DMA
     * scheduler and pulse the WFDMA logic reset before reprogramming rings
     * (reprogramming under a live scheduler wedges it). */
    int rv;

    mt76_clear(dev, MT_WFDMA0_GLO_CFG,
               MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN |
               MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN |
               MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
               MT_WFDMA0_GLO_CFG_OMIT_RX_INFO |
               MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);

    rv = mt76_poll_msec(dev, MT_WFDMA0_GLO_CFG,
                        MT_WFDMA0_GLO_CFG_TX_DMA_BUSY |
                        MT_WFDMA0_GLO_CFG_RX_DMA_BUSY, 0, 100);
    if (!force && rv != 0)
        return rv;

    /* disable DMASHDL and set bypass */
    mt76_clear(dev, MT_WFDMA0_GLO_CFG_EXT0,
               MT_WFDMA0_GLO_CFG_EXT0_CSR_TX_DMASHDL_EN);
    mt76_set(dev, MT_DMASHDL_SW_CONTROL, MT_DMASHDL_DMASHDL_BYPASS);

    if (force) {
        mt76_clear(dev, MT_WFDMA0_RST,
                   MT_WFDMA0_RST_DMASHDL_ALL_RST | MT_WFDMA0_RST_LOGIC_RST);
        mt76_set(dev, MT_WFDMA0_RST,
                 MT_WFDMA0_RST_DMASHDL_ALL_RST | MT_WFDMA0_RST_LOGIC_RST);
        mdelay(20);
    }

    return 0;
}

void mt792x_dma_cleanup(struct mt792x_dev *dev)
{
    (void)dev;  /* TODO(M2) */
}

int mt792x_wpdma_reset(struct mt792x_dev *dev, bool force)
{
    (void)dev; (void)force;
    return 0;   /* TODO(M2) */
}

int mt792x_wpdma_reinit_cond(struct mt792x_dev *dev)
{
    (void)dev;
    return 0;   /* TODO(M2) */
}

int mt792x_wfsys_reset(struct mt792x_dev *dev)
{
    /* Ported from Linux mt792x_dma.c:mt792x_wfsys_reset_default().
     * The WFSYS reset pulse + INIT_DONE poll is what brings the MCU ROM
     * out of fw_own deep sleep; without it the LPCTL driver-own handshake
     * never completes (OWN_SYNC stays 1) on macOS where power management
     * parks the idle WiFi slot. */
    u32 addr = is_connac2(&dev->mt76) ? 0x18000140 : 0x7c000140;

    mt76_clear(dev, addr, WFSYS_SW_RST_B);
    IOSleep(50);
    mt76_set(dev, addr, WFSYS_SW_RST_B);

    if (!mt76_poll_msec(dev, addr, WFSYS_SW_INIT_DONE,
                        WFSYS_SW_INIT_DONE, 500)) {
        XYLog("mtk-dbg: wfsys_reset: SW_INIT_DONE timeout (reg 0x%x now 0x%x)\n",
              addr, mt76_rr(dev, addr));
        return -ETIMEDOUT;
    }
    XYLog("mtk-dbg: wfsys_reset done (reg 0x%x = 0x%x)\n",
          addr, mt76_rr(dev, addr));

    /* The reset can clear the interrupt enable chain (and the MSI
     * capability's enable bit).  Re-arm everything the DMA-enable path
     * programs and ack stale status (upstream mwx_pci_irq_state: restoring
     * the MSI bit alone is not enough -- INT_STA must be acked and
     * HOST_INT_ENA / PCIE_MAC_INT_ENABLE rewritten).  Harmless before
     * dma_enable runs: dma_enable programs the same registers again. */
    mt76_wr(dev, MT_WFDMA0_HOST_INT_STA, 0xffffffff);
    if (dev->irq_map)
        mt76_wr(dev, dev->irq_map->host_irq_enable, dev->mt76.mmio.irqmask);
    else
        mt76_wr(dev, MT_WFDMA0_HOST_INT_ENA, dev->mt76.mmio.irqmask);
    mt76_set(dev, MT_MCU2HOST_SW_INT_ENA, MT_MCU_CMD_WAKE_RX_PCIE);
    mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);
    mtk_xnu_msi_rearm();
    return 0;
}

/* ================================================================== */
/* MCU / firmware readiness gate (mt76wm)                              */
/*                                                                      */
/* mt7921_register_device() queues init_work asynchronously, so the     */
/* firmware load (patch + RAM + N9 start) races the net80211 enable     */
/* path.  Linux cannot hit this because ieee80211_register_hw() only    */
/* runs at the end of mt7921_init_work(); in the XNU glue the net80211  */
/* stack is attached at probe time instead.  Commands issued before the */
/* MCU is up get no response and time out (the 0x8ed per-channel storm), */
/* so the start path must wait for the init result before scanning.     */
/* ================================================================== */
static volatile int g_mcu_init_state;   /* 0 = pending, 1 = ok, 2 = no-fw,
                                         * <0 = failed */

extern "C" void mtk_mcu_init_done(void *mdev, int ret)
{
    (void)mdev;
    if (ret == MTK_MCU_INIT_NOMCU)
        g_mcu_init_state = MTK_MCU_INIT_NOMCU;
    else
        g_mcu_init_state = (ret == 0) ? 1 : (ret ? ret : -1);
    XYLog("mtk-dbg: firmware init complete: ret=%d\n", ret);
}

/* Bounded wait for mt7921_init_work() to finish.  Returns 0 when the
 * firmware is up, 2 when the device runs without firmware (probe
 * fallback), negative when init failed or never completed. */
extern "C" int mtk_mcu_wait_ready(unsigned int timeout_ms)
{
    unsigned int i;

    for (i = 0; i < timeout_ms && g_mcu_init_state == 0; i++)
        IOSleep(1);

    if (g_mcu_init_state == 1 || g_mcu_init_state == MTK_MCU_INIT_NOMCU)
        return g_mcu_init_state == 1 ? 0 : MTK_MCU_INIT_NOMCU;
    if (g_mcu_init_state == 0) {
        XYLog("mtk-dbg: firmware init still pending after %u ms\n",
              timeout_ms);
        return -ETIMEDOUT;
    }
    XYLog("mtk-dbg: firmware init failed (%d)\n", g_mcu_init_state);
    return -EIO;
}

extern "C" bool mtk_mcu_is_ready(void)
{
    return g_mcu_init_state == 1;
}

/*
 * Wake and abort every in-flight MCU waiter: mt76_mcu_get_response()
 * polls on (res_q non-empty || MT76_MCU_RESET), so flagging
 * MT76_MCU_RESET + purging the queue makes blocked senders return
 * immediately instead of holding the stop path for up to 3 s each.
 * With @final, also mark the device removed so mt792x_reset() cannot
 * queue a chip-reset storm during teardown.
 */
extern "C" void mtk_mcu_abort(void *mdev, bool final)
{
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;

    if (!dev)
        return;
    if (final)
        set_bit(MT76_REMOVED, &dev->mt76.phy.state);
    set_bit(MT76_MCU_RESET, &dev->mphy.state);
    wake_up(&dev->mt76.mcu.wait);
    skb_queue_purge(&dev->mt76.mcu.res_q);
}

/* ================================================================== */
/* mt7921/main.c functions (not ported; ieee80211_ops comes later)    */
/* ================================================================== */
/* forward decls: vif helpers are defined further down with the M6 bridge */
static struct mt792x_vif *mtk_mvif(void);
static int mtk_ensure_vif(struct mt792x_dev *dev);
static int mtk_vif_register(struct mt792x_dev *dev);

int __mt7921_start(struct mt792x_phy *phy)
{
    struct mt792x_dev *dev = phy->dev;
    struct mt76_phy *mphy = phy->mt76;
    int err;

    /*
     * Idempotent: the WCL gates' self-heal (mtk_datapath_recover) can
     * race the enable path at boot, and a second full MCU start sequence
     * re-registers the vif/OMAC -- the duplicate DEV_INFO wedges the
     * firmware (no beacons -> empty Wi-Fi list).
     */
    if (test_bit(MT76_STATE_RUNNING, &mphy->state))
        return 0;

    /*
     * mt76wm: wait for the async firmware bring-up before touching the
     * MCU.  If the load failed, fail the interface start cleanly so the
     * scan state machine never fires commands into a dead firmware.
     */
    err = mtk_mcu_wait_ready(10000);
    if (err == MTK_MCU_INIT_NOMCU) {
        /* probe fallback: no firmware, no MCU sequence - just mark
         * running so the TX-only bring-up keeps working. */
        XYLog("%s: no-firmware mode, skipping MCU start sequence\n",
              __FUNCTION__);
        mt792x_mutex_acquire(dev);
        set_bit(MT76_STATE_RUNNING, &mphy->state);
        mt792x_mutex_release(dev);
        return 0;
    }
    if (err) {
        XYLog("%s: firmware not ready (%d), refusing start\n",
              __FUNCTION__, err);
        return err;
    }

    /* clear a stale abort from a previous stop */
    clear_bit(MT76_MCU_RESET, &dev->mphy.state);

    /* Sband tables must exist before SET_CHAN_DOMAIN walks them (the
     * glue builds them lazily in mtk_chan_switch; do it here too). */
    if (!mphy->sband_2g.sband.channels) {
        mtk_init_chan_tables();
        mtk_init_sband_min(mphy, &mphy->sband_2g, mt76_rates, 4,
                           NL80211_BAND_2GHZ);
        mtk_init_sband_min(mphy, &mphy->sband_5g, mt76_rates + 4, 8,
                           NL80211_BAND_5GHZ);
    }

    /* mt7921_mcu_set_chan_info() derefs chandef.chan; the glue has no
     * mac80211 to pre-set one, so stage a default 2 GHz channel first
     * (mtk_chan_switch() will replace it on the first hop). */
    if (!mphy->chandef.chan) {
        struct cfg80211_chan_def chandef;

        cfg80211_chandef_create(&chandef, &mphy->sband_2g.sband.channels[0],
                                NL80211_CHAN_WIDTH_20_NOHT);
        mt76_phy_chandef_set(mphy, &chandef);
    }

    /* Linux __mt7921_start(): MAC enable, channel domain, RX path,
     * SAR defaults, then mark running. */
    mt792x_mutex_acquire(dev);

    err = mt76_connac_mcu_set_mac_enable(&dev->mt76, 0, true, false);
    if (err) {
        XYLog("%s: MAC enable failed (%d)\n", __FUNCTION__, err);
        goto out;
    }

    err = mt76_connac_mcu_set_channel_domain(mphy);
    if (err) {
        XYLog("%s: channel domain failed (%d)\n", __FUNCTION__, err);
        goto out;
    }

    /*
     * TX power SKU (reference init: mt7921_mcu_set_rate_txpower, "init
     * rate txpower ok").  Without the per-rate power table the firmware
     * transmits with no power -- the AP hears nothing and every TXS is
     * ACK_TIMEOUT while RX works fine.
     */
    if (!mphy->cap.has_2ghz && !mphy->cap.has_5ghz) {
        /* capability query left hw_path empty: assume the MT7921 dual-band */
        mphy->cap.has_2ghz = true;
        mphy->cap.has_5ghz = true;
    }
    err = mt76_connac_mcu_set_rate_txpower(mphy);
    if (err) {
        XYLog("%s: rate txpower failed (%d)\n", __FUNCTION__, err);
        goto out;
    }
    XYLog("%s: rate txpower ok\n", __FUNCTION__);

    /*
     * Register the vif/OMAC at init (reference: mt7921_mcu_uni_add_dev
     * in the init sequence).  The firmware's own-address filter is built
     * from this record: without it, the AP's unicast auth response and
     * its ACK (addressed to our MAC) are dropped as "other UC" and the
     * join dies at ACK timeout.
     */
    err = mtk_ensure_vif(dev);
    if (err) {
        XYLog("%s: vif alloc failed (%d)\n", __FUNCTION__, err);
        goto out;
    }
    err = mtk_vif_register(dev);
    if (err) {
        XYLog("%s: vif/OMAC register failed (%d)\n", __FUNCTION__, err);
        goto out;
    }

    err = mt7921_mcu_set_chan_info(phy, MCU_EXT_CMD(SET_RX_PATH));
    if (err) {
        XYLog("%s: SET_RX_PATH failed (%d)\n", __FUNCTION__, err);
        goto out;
    }

    err = mt7921_set_tx_sar_pwr(mphy->hw, NULL);
    if (err)
        goto out;

    mt792x_mac_reset_counters(phy);
    /* Standing RX filter (open): fixed here, once, at MCU-ready. */
    mtk_rxfilter_set(dev);
    set_bit(MT76_STATE_RUNNING, &mphy->state);
    XYLog("mtk-dbg: MCU start sequence done, running\n");

out:
    mt792x_mutex_release(dev);
    return err;
}

int mt7921_start(struct ieee80211_hw *hw)
{
    struct mt792x_dev *dev = mt792x_hw_dev(hw);

    return __mt7921_start(&dev->phy);
}

void mt7921_set_stream_he_caps(struct mt792x_phy *phy)
{
    (void)phy;  /* TODO(M2): build HE/EHT capa per band */
}

int mt7921_set_tx_sar_pwr(struct ieee80211_hw *hw,
                          const struct cfg80211_sar_specs *sar)
{
    (void)hw; (void)sar;
    return 0;   /* TODO(M2) */
}

/* ================================================================== */
/* MCU hardware scan                                                   */
/* ================================================================== */
/*
 * mtkwm scans via the firmware scan engine -- the D-series reference
 * recipe (AirportMtkWlan MtkMwx.cpp mt7921_mcu_hw_scan / mwx_scan):
 * the firmware hops the channels itself and forwards beacons / probe
 * responses as ordinary frames on the RX data ring; only a scan-done
 * event marks the end.  The soft-scan path (per-hop
 * MCU_EXT_CMD_CHANNEL_SWITCH + dwell timer) never hears other-BSS
 * frames on this firmware: 1200+ probe_reqs went out with healthy TXS
 * while zero beacons reached the RX bridge.
 */
#define MTK_HW_SCAN_DWELL_MS   120
#define MTK_HW_SCAN_MAX_CHAN   64
/* itl80211 constants (not visible in this translation unit). */
#define MTK_NWID_LEN           32    /* IEEE80211_NWID_LEN */
#define MTK_CHAN_MAX           255   /* IEEE80211_CHAN_MAX */

static u8 mtk_scan_seq;

int mtk_hw_scan_start(void *mdev, const struct mtk_hw_scan_params *p)
{
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;
    struct mt76_connac_hw_scan_req *req;
    struct sk_buff *skb;
    int n_ssids = 0, nchan = 0, i;

    if (!dev || !p)
        return -EINVAL;
    if (test_bit(MT76_HW_SCANNING, &dev->mt76.phy.state))
        return -EBUSY;

    skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, sizeof(*req));
    if (!skb)
        return -ENOMEM;
    req = (struct mt76_connac_hw_scan_req *)skb_put_zero(skb, sizeof(*req));

    mtk_scan_seq = (mtk_scan_seq + 1) & 0x7f;
    req->seq_num = mtk_scan_seq;
    req->bss_idx = 0;
    req->version = 1;

    /* Active only when macOS asked for probes; driver-initiated sweeps
     * stay passive over every channel (reference behavior). */
    if (p->active) {
        req->scan_type = 1;
        req->probe_req_num = 2;
        if (p->ssid_len > 0 && p->ssid_len <= MTK_NWID_LEN) {
            req->ssids[0].ssid_len = cpu_to_le32(p->ssid_len);
            memcpy(req->ssids[0].ssid, p->ssid, p->ssid_len);
            n_ssids = 1;
        }
    }
    req->ssid_type = n_ssids ? 0x4 : 0x1;
    req->ssid_type_ext = n_ssids ? 1 : 0;
    req->ssids_num = n_ssids;

    for (i = 1; i <= MTK_CHAN_MAX && nchan < MTK_HW_SCAN_MAX_CHAN; i++) {
        struct mt76_connac_mcu_scan_channel *chan;

        if (p->restricted &&
            !(p->channels[i / 8] & (1u << (i % 8))))
            continue;
        if (nchan < 32)
            chan = &req->channels[nchan];
        else
            chan = &req->ext_channels[nchan - 32];
        chan->channel_num = (u8)i;
        /* 1: 2.4GHz, 2: 5GHz (6GHz would be 3, not scanned yet). */
        chan->band = (i < 15) ? 1 : 2;
        nchan++;
    }

    if (nchan <= 32) {
        req->channels_num = nchan;
    } else {
        req->channels_num = 32;
        req->ext_channels_num = nchan - 32;
    }
    req->channel_type = nchan ? 4 : 0;
    req->timeout_value = cpu_to_le16(nchan * MTK_HW_SCAN_DWELL_MS);
    req->channel_min_dwell_time = cpu_to_le16(MTK_HW_SCAN_DWELL_MS);
    req->channel_dwell_time = cpu_to_le16(MTK_HW_SCAN_DWELL_MS);
    req->scan_func |= SCAN_FUNC_SPLIT_SCAN;
    /* wildcard BSSID */
    memset(req->bssid, 0xff, ETH_ALEN);

    set_bit(MT76_HW_SCANNING, &dev->mt76.phy.state);
    if (mt76_mcu_skb_send_msg(&dev->mt76, skb,
                              MCU_CE_CMD(START_HW_SCAN), false) != 0) {
        clear_bit(MT76_HW_SCANNING, &dev->mt76.phy.state);
        return -EIO;
    }
    XYLog("mtk-dbg: hw_scan start seq=%u channels=%u active=%d "
          "ssid_len=%u dwell=%dms\n",
          mtk_scan_seq, nchan, p->active, p->ssid_len, MTK_HW_SCAN_DWELL_MS);
    return 0;
}

int mtk_offchannel(void *mdev)
{
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;

    return dev ? dev->mt76.phy.offchannel : 0;
}

void mtk_rfcr_open(void *mdev)
{
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;

    if (!dev)
        return;
    /*
     * TEMP(join-experiment): fully open the RX filter during the join
     * window.  Even the AP's ACK (RA = our MAC) may be dropped by
     * DROP_OTHER_UC when the own-address table is empty -- which would
     * show up as ACK_TIMEOUT on every auth attempt while beacons
     * (broadcast) keep flowing.  If the auth response appears with
     * RFCR=0, the own-address programming is the missing piece.
     */
    /*
     * Reference value (MtkMwx.cpp init: sc_rxfilter = DROP_OTHER_UC,
     * mwx_write(RFCR, sc_rxfilter)).  Writing 0 (all drop-bits clear)
     * kept the firmware's BSS-mode beacon filter misbehaving -- beacons
     * only flowed in scan mode.  With the OMAC/own-address registered,
     * DROP_OTHER_UC no longer eats our unicast (auth responses).
     */
    mt76_wr(dev, MT_WF_RFCR(0), MT_WF_RFCR_DROP_OTHER_UC);
}

unsigned int mtk_rfcr_read(void *mdev)
{
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;

    return dev ? mt76_rr(dev, MT_WF_RFCR(0)) : 0;
}

/*
 * Synchronous scan abort for the join path.  Only sent when a firmware
 * scan is actually in flight: an active scan's CANCEL is answered by the
 * firmware (no timeout hazard), and waiting for it means the following
 * CHANNEL_SWITCH cannot be swallowed by a still-aborting scan engine --
 * the auth frame then leaves on the right channel instead of dying at
 * ACK timeout.  With no scan in flight nothing is sent at all.
 */
void mtk_hw_scan_cancel_sync(void *mdev)
{
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;

    if (!dev || !test_bit(MT76_HW_SCANNING, &dev->mt76.phy.state))
        return;
    clear_bit(MT76_HW_SCANNING, &dev->mt76.phy.state);
    {
        struct {
            u8 seq_num;
            u8 is_ext_channel;
            u8 rsv[2];
        } __packed req = { .seq_num = mtk_scan_seq };

        mt76_mcu_send_msg(&dev->mt76, MCU_CE_CMD(CANCEL_HW_SCAN),
                          &req, sizeof(req), true);
    }
    XYLog("mtk-dbg: fw-scan aborted synchronously for join\n");
}

void mtk_deep_sleep_off(void *mdev)
{
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;

    if (dev)
        mt76_connac_mcu_set_deep_sleep(&dev->mt76, false);
}

int mtk_hw_scan_active(void *mdev)
{
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;

    return dev && test_bit(MT76_HW_SCANNING, &dev->mt76.phy.state);
}

int mtk_hw_scan_cancel(void *mdev)
{
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;
    struct {
        u8 seq_num;
        u8 is_ext_channel;
        u8 rsv[2];
    } __packed req = { .seq_num = mtk_scan_seq };

    if (!dev)
        return -EINVAL;
    clear_bit(MT76_HW_SCANNING, &dev->mt76.phy.state);
    /*
     * Fire-and-forget, exactly like the reference
     * (mt7921_mcu_hw_scan_cancel sends with NULL response): the
     * firmware does not answer CANCEL for an already-completed scan,
     * and a wait=true round trip here burned a 3 s MCU timeout that
     * wedged the queue and cascaded into a DriverAvailable reset
     * flap (IFF_RUNNING lost, every SCAN_REQ returned NotReady).
     */
    return mt76_mcu_send_msg(&dev->mt76, MCU_CE_CMD(CANCEL_HW_SCAN),
                             &req, sizeof(req), false);
}

void mt7921_scan_work(struct work_struct *work)
{
    struct mt792x_phy *phy =
        container_of(work, struct mt792x_phy, scan_work.work);
    struct mt792x_dev *dev = phy->dev;
    struct sk_buff *skb;
    bool got = false;

    while (true) {
        spin_lock_bh(&dev->mt76.lock);
        skb = __skb_dequeue(&phy->scan_event_list);
        spin_unlock_bh(&dev->mt76.lock);
        if (!skb)
            break;

        if (skb->len >= sizeof(struct mt76_connac_hw_scan_done)) {
            struct mt76_connac_hw_scan_done *event =
                (struct mt76_connac_hw_scan_done *)skb->data;
            unsigned int probe_sum = 0, mdrdy_sum = 0;
            int i;

            for (i = 0;
                 i < MT76_CONNAC_SCAN_DONE_EVENT_MAX_CHANNEL_NUM; i++) {
                probe_sum += event->beacon_probe_num[i];
                mdrdy_sum += event->mdrdy_count[i];
            }
            XYLog("mtk-dbg: hw_scan done seq=%u complete_ch=%u state=%u "
                  "beacon_scan=%u probe_sum=%u mdrdy_sum=%u "
                  "beacon_2g=%u beacon_5g=%u\n",
                  event->seq_num, event->complete_channel_num,
                  event->current_state,
                  le32_to_cpu(event->beacon_scan_num),
                  probe_sum, mdrdy_sum,
                  le32_to_cpu(event->beacon_2g_num),
                  le32_to_cpu(event->beacon_5g_num));
        } else {
            XYLog("mtk-dbg: hw_scan done short event len=%u\n", skb->len);
        }
        dev_kfree_skb(skb);
        got = true;
    }
    if (!got)
        return;

    /* ACK the firmware scan state, then hand completion to net80211
     * (reference: mt7921_mcu_scan_event -> mwx_end_scan_task). */
    mtk_hw_scan_cancel(dev);
    mtk_scan_done_notify();
}

void mt7921_roc_work(struct work_struct *work)
{
    (void)work; /* TODO(M2): remain-on-channel */
}

int mt7921_init_debugfs(struct mt792x_dev *dev)
{
    (void)dev;  /* debugfs not available in the kext */
    return 0;
}

void mt7921_roc_abort_sync(struct mt792x_dev *dev)
{
    (void)dev;  /* TODO(M2) */
}

void mt7921_set_runtime_pm(struct mt792x_dev *dev)
{
    (void)dev;  /* TODO(M2) */
}

int mt7921_set_channel(struct mt76_phy *mphy)
{
    struct mt792x_phy *phy = (struct mt792x_phy *)mphy->priv;
    struct mt792x_dev *dev = phy->dev;
    int ret;

    /*
     * mt76wm: never fire a channel-switch into a firmware that is not
     * answering (still loading, failed, or device stopping).  Without
     * this gate every scan hop burns the full 3 s MCU timeout.
     */
    if (!mtk_mcu_is_ready() ||
        test_bit(MT76_MCU_RESET, &dev->mphy.state) ||
        test_bit(MT76_REMOVED, &dev->mphy.state)) {
        static int warned;

        if (warned++ < 8)
            XYLog("%s: MCU not ready (state %d), skipping tune\n",
                  __FUNCTION__, g_mcu_init_state);
        return -EIO;
    }

    /* Channel switch (Linux mt7921/main.c mt7921_set_channel): program
     * the MCU with the chandef that was staged on mphy. */
    mt792x_mutex_acquire(dev);
    ret = mt7921_mcu_set_chan_info(phy, MCU_EXT_CMD(CHANNEL_SWITCH));
    mt792x_mutex_release(dev);
    if (ret == 0)
        /* Reference parity (MtkMwx.cpp mt7921_set_channel): program the
         * LMAC timing right after the tune -- MT_TMAC_CDTR/ODTR are the
         * CCK/OFDM TX data-timeout (ACK wait) values and ICR0 carries
         * SIFS/slot.  Unprogrammed they can make the LMAC declare
         * ACK_TIMEOUT on frames the AP did acknowledge. */
        mt792x_mac_set_timeing(&dev->phy);
    return ret;
}

void mt7921_csa_work(struct work_struct *work)
{
    (void)work; /* TODO(M2) */
}

void mt7921_stats_work(struct work_struct *work)
{
    (void)work;
}


/* ================================================================== */
/* IRQ / NAPI poll (Linux mt792x_dma.c)                               */
/* ================================================================== */
bool mtk_txq_drained(void *mdev)
{
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;
    struct mt76_queue *tq;

    if (!dev)
        return true;
    /* phy.q_tx[] is the alias set the TX scheduler uses. */
    tq = dev->mt76.phy.q_tx[MT_TXQ_BE];
    return tq == NULL || tq->queued == 0;
}

void mtk_rxfilter_set(void *mdev)
{
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;

    if (!dev)
        return;
    /*
     * Standing RX filter: hardware register only, exactly like the
     * working mwx reference (MtkMwx.cpp:1153-1162): STA mode drops
     * other-station unicast, everything else -- beacons and probe
     * responses included -- passes.  Do NOT send the MCU SET_RX_FILTER
     * command: Linux pairs it with the firmware hw-scan result engine,
     * and its fif bitmap (FILTER_OTHER_BSS = BIT(6)) collides with
     * MT_WF_RFCR_DROP_BCAST (BIT(6)) -- a beacon is a broadcast frame.
     */
    mtk_rfcr_open(dev);
    XYLog("mtk-dbg: rxfilter RFCR(0)=0x%08x (standing, register-only)\n",
          mt76_rr(dev, MT_WF_RFCR(0)));
}

void mtk_dbg_mcu_regs(void *mdev, const char *tag)
{
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;
    u32 glc, hie, sta;

    if (!dev)
        return;
    glc = mt76_rr(dev, MT_WFDMA0_GLO_CFG);
    hie = mt76_rr(dev, MT_WFDMA0_HOST_INT_ENA);
    sta = mt76_rr(dev, MT_WFDMA0_HOST_INT_STA);
    {
        struct mt76_queue *rxq = &dev->mt76.q_rx[MT_RXQ_MCU];
        struct mt76_queue *txq = dev->mt76.q_mcu[MT_MCUQ_WM];
        struct mt76_queue *fdq = dev->mt76.q_mcu[MT_MCUQ_FWDL];
        /* NOTE: ring registers must be read directly through q->regs
         * (readl), NOT via mt76_rr() which remaps the value as a register
         * offset and yields garbage. */
        u32 rc = rxq->regs ? readl(&rxq->regs->cpu_idx) : 0;
        u32 rd = rxq->regs ? readl(&rxq->regs->dma_idx) : 0;
        u32 rb = rxq->regs ? readl(&rxq->regs->desc_base) : 0;
        u32 tc = txq && txq->regs ? readl(&txq->regs->cpu_idx) : 0;
        u32 td = txq && txq->regs ? readl(&txq->regs->dma_idx) : 0;
        u32 fc = fdq && fdq->regs ? readl(&fdq->regs->cpu_idx) : 0;
        u32 fd_ = fdq && fdq->regs ? readl(&fdq->regs->dma_idx) : 0;
        XYLog("mtk-dbg-regs[%s]: GLO=%08x HOST_INT_ENA=%08x HOST_INT_STA=%08x "
              "mcu_rxq cpu=%u dma=%u queued=%d desc=%08x/%08x "
              "tx_mcuq cpu=%u dma=%u  fwdlq cpu=%u dma=%u\n",
              tag, glc, hie, sta, rc, rd, rxq->queued,
              rb, (u32)rxq->desc_dma,
              tc, td, fc, fd_);
    }
}

irqreturn_t mt792x_irq_handler(int irq, void *dev_instance)
{
    struct mt792x_dev *dev = (struct mt792x_dev *)dev_instance;

    (void)irq;
    if (test_bit(MT76_REMOVED, &dev->mt76.phy.state))
        return IRQ_NONE;

    /* mask further interrupts; the tasklet re-arms them */
    if (!dev->irq_map)
        return IRQ_NONE;
    mt76_wr(dev, dev->irq_map->host_irq_enable, 0);

    if (!test_bit(MT76_STATE_INITIALIZED, &dev->mphy.state))
        return IRQ_NONE;

    tasklet_schedule(&dev->mt76.irq_tasklet);
    return IRQ_HANDLED;
}

/* --- MCU wait polling (interrupt-free bring-up) ------------------- */
static struct mt792x_dev *mtk_poll_dev;
static int mtk_poll_busy;

void mtk_set_poll_dev(struct mt792x_dev *dev)
{
    mtk_poll_dev = dev;
}

void mtk_mcu_wait_poll(void)
{
    if (!mtk_poll_dev || mtk_poll_busy)
        return;
    mtk_poll_busy = 1;
    mt792x_irq_tasklet((unsigned long)mtk_poll_dev);
    /*
     * Force-poll the MCU RX/WA rings: this platform never delivers the
     * device interrupt to the IOFilterInterruptEventSource, so the
     * HOST_INT_STA RX_DONE bit that gates the poll inside
     * mt792x_irq_tasklet never latches -- the MCU responses sit in the
     * ring forever (dma_idx advances, nobody collects).  The napi_shim
     * runs one polling round synchronously.
     */
    napi_schedule(&mtk_poll_dev->mt76.napi[MT_RXQ_MCU]);
    napi_schedule(&mtk_poll_dev->mt76.napi[MT_RXQ_MCU_WA]);
    mtk_poll_busy = 0;
}

void mt792x_irq_tasklet(unsigned long data)
{
    struct mt792x_dev *dev = (struct mt792x_dev *)data;
    const struct mt792x_irq_map *irq_map = dev->irq_map;
    int pass;

    /* MSI is edge-triggered: events that arrive while host_irq_enable is
     * cleared raise no new message, so a single pass can strand work until
     * the next (forced) interrupt.  Drain up to 8 passes per invocation
     * (upstream 3176c02 mwx_intr drain loop). */
    for (pass = 0; pass < 8; pass++) {
        u32 intr, mask = 0;

        mt76_wr(dev, irq_map->host_irq_enable, 0);

        intr = mt76_rr(dev, MT_WFDMA0_HOST_INT_STA);
        intr &= dev->mt76.mmio.irqmask;
        mt76_wr(dev, MT_WFDMA0_HOST_INT_STA, intr);

        mask |= intr & irq_map->rx.all_complete_mask;
        if (intr & dev->irq_map->tx.mcu_complete_mask)
            mask |= dev->irq_map->tx.mcu_complete_mask;

        if (intr & MT_INT_MCU_CMD) {
            u32 intr_sw = mt76_rr(dev, MT_MCU_CMD);

            mt76_wr(dev, MT_MCU_CMD, intr_sw);
            if (intr_sw & MT_MCU_CMD_WAKE_RX_PCIE) {
                mask |= irq_map->rx.data_complete_mask;
                intr |= irq_map->rx.data_complete_mask;
            }
        }

        mt76_set_irq_mask(&dev->mt76, irq_map->host_irq_enable, mask, 0);

        if (intr & dev->irq_map->tx.all_complete_mask)
            napi_schedule(&dev->mt76.tx_napi);

        if (intr & irq_map->rx.wm_complete_mask)
            napi_schedule(&dev->mt76.napi[MT_RXQ_MCU]);

        if (intr & irq_map->rx.wm2_complete_mask)
            napi_schedule(&dev->mt76.napi[MT_RXQ_MCU_WA]);

        if (intr & irq_map->rx.data_complete_mask)
            napi_schedule(&dev->mt76.napi[MT_RXQ_MAIN]);

        if (!intr)
            break;
    }
}

void mt792x_rx_poll_complete(struct mt76_dev *mdev, enum mt76_rxq_id q)
{
    struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);
    const struct mt792x_irq_map *irq_map = dev->irq_map;

    if (q == MT_RXQ_MAIN)
        mt76_connac_irq_enable(mdev, irq_map->rx.data_complete_mask);
    else if (q == MT_RXQ_MCU_WA)
        mt76_connac_irq_enable(mdev, irq_map->rx.wm2_complete_mask);
    else
        mt76_connac_irq_enable(mdev, irq_map->rx.wm_complete_mask);
}

int mt792x_poll_rx(struct napi_struct *napi, int budget)
{
    struct mt792x_dev *dev = (struct mt792x_dev *)mt76_priv(napi->dev);

    if (!mt76_connac_pm_ref(&dev->mphy, &dev->pm)) {
        napi_complete(napi);
        queue_work(dev->mt76.wq, &dev->pm.wake_work);
        return 0;
    }
    int done = mt76_dma_rx_poll(napi, budget);
    mt76_connac_pm_unref(&dev->mphy, &dev->pm);
    return done;
}

int mt792x_poll_tx(struct napi_struct *napi, int budget)
{
    struct mt792x_dev *dev = container_of(napi, struct mt792x_dev, mt76.tx_napi);

    (void)budget;
    if (!mt76_connac_pm_ref(&dev->mphy, &dev->pm)) {
        napi_complete(napi);
        queue_work(dev->mt76.wq, &dev->pm.wake_work);
        return 0;
    }
    mt76_connac_tx_cleanup(&dev->mt76);
    if (napi_complete(napi))
        mt76_connac_irq_enable(&dev->mt76, dev->irq_map->tx.all_complete_mask);
    mt76_connac_pm_unref(&dev->mphy, &dev->pm);
    return 0;
}

/* ================================================================== */
/* further missing symbols surfaced by the full link                   */
/* ================================================================== */
/* TODO(M4): replace with the real ieee80211_ops (mt7921/main.c). */
const struct ieee80211_ops mt7921_ops = { };

void mt76_wcid_init(struct mt76_wcid *wcid, u8 band_idx)
{
    memset(wcid, 0, sizeof(*wcid));
    wcid->phy_idx = band_idx;
    ewma_signal_init(&wcid->rssi);
    INIT_LIST_HEAD(&wcid->list);
    INIT_LIST_HEAD(&wcid->poll_list);
    INIT_LIST_HEAD(&wcid->tx_list);
    skb_queue_head_init(&wcid->tx_pending);
    skb_queue_head_init(&wcid->tx_offchannel);
    idr_init(&wcid->pktid);
    wcid->cipher = MT_CIPHER_NONE;
}

void mt76_set_stream_caps(struct mt76_phy *phy, bool vht)
{
    (void)phy; (void)vht;    /* TODO(M2): stream capabilities */
}

void mt76_pci_disable_aspm(struct pci_dev *pdev)
{
    (void)pdev;
    /* Real work lives in the XNU HAL: ItlMt76.cpp keeps the IOPCIDevice
     * (config space access) and the AMD-parent/boot-arg policy. */
    mtk_xnu_disable_aspm();
}

bool mt76_pci_aspm_supported(struct pci_dev *pdev)
{
    (void)pdev;
    return mtk_xnu_aspm_supported();
}

int mt7921_mac_sta_add(struct mt76_dev *mdev, struct ieee80211_vif *vif,
                       struct ieee80211_sta *sta)
{
    (void)mdev; (void)vif; (void)sta;
    return 0;                /* TODO(M2): WTBL setup */
}

int mt7921_mac_sta_event(struct mt76_dev *mdev, struct ieee80211_vif *vif,
                         struct ieee80211_sta *sta, enum mt76_sta_event ev)
{
    (void)mdev; (void)vif; (void)sta; (void)ev;
    return 0;
}

void mt7921_mac_sta_remove(struct mt76_dev *mdev, struct ieee80211_vif *vif,
                           struct ieee80211_sta *sta)
{
    (void)mdev; (void)vif; (void)sta;
}

/* ================================================================== */
/* M6: STA / BSS / key / EDCA bridge (net80211 side calls these via    */
/* glue/mtk_api.h).  The linux side owns the mac80211-shim objects     */
/* (ieee80211_vif / ieee80211_sta + mt792x_vif / mt792x_sta in         */
/* drv_priv) and the WTBL/wcid bookkeeping, mirroring the upstream     */
/* mt7921 add_interface / mac_sta_add / sta_event(ASSOC) / set_key     */
/* flow (mt7921/main.c in the Linux tree).                             */
/* ================================================================== */
#include "../mt76core/util.h"

#define MTK_MAX_STA         4       /* MT792x_WTBL_STA is 16-ish        */
#define MTK_KEY_SLOT_PTK(n) ((n) & 3)               /* 0..3             */
#define MTK_KEY_SLOT_GTK(n) (4 + ((n) & 3))         /* 4..7             */
#define MTK_KEY_SLOT_IGTK   8
#define MTK_KEY_SLOTS       9

struct mtk_sta_ctx {
    void                    *ni;    /* net80211 node pointer (lookup)   */
    bool                     used;
    bool                     assoced; /* STA_REC_UPDATE(ASSOC) sent      */
    struct ieee80211_sta    *sta;   /* drv_priv = mt792x_sta            */
};

static struct {
    bool                     valid;
    struct ieee80211_vif    *vif;   /* drv_priv = mt792x_vif            */
    struct ieee80211_key_conf kconf[MTK_KEY_SLOTS];
    u8                       kvalid[MTK_KEY_SLOTS];
    bool                     bss_up;
    bool                     sta_assoced;  /* BSS sta record -> ASSOC   */
    struct mtk_sta_ctx       sta[MTK_MAX_STA];
} g_m6;

static struct mt792x_vif *mtk_mvif(void)
{
    return g_m6.valid ? (struct mt792x_vif *)g_m6.vif->drv_priv : NULL;
}

static struct mtk_sta_ctx *mtk_sta_find(void *ni)
{
    int i;

    for (i = 0; i < MTK_MAX_STA; i++)
        if (g_m6.sta[i].used && g_m6.sta[i].ni == ni)
            return &g_m6.sta[i];
    return NULL;
}

/*
 * Build the mac80211-shim rate bitmaps (bit i = i-th band rate) from an
 * 802.11 rate list in 500kbps units: 2 GHz = CCK 1/2/5.5/11 + OFDM
 * 6..54, 5 GHz = OFDM 6..54.  mt76_connac_mcu_sta_tlv() feeds these to
 * the firmware RA record.
 */
static void mtk_build_rate_bitmaps(const u8 *rates, unsigned int nrates,
                                   u32 *r2g, u32 *r5g)
{
    static const u8 map2g[] = { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 };
    static const u8 map5g[] = { 12, 18, 24, 36, 48, 72, 96, 108 };
    unsigned int i, j;

    *r2g = 0;
    *r5g = 0;
    for (i = 0; i < nrates; i++) {
        u8 r = rates[i] & 0x7f;     /* strip IEEE80211_RATE_BASIC */

        for (j = 0; j < ARRAY_SIZE(map2g); j++)
            if (map2g[j] == r)
                *r2g |= BIT(j);
        for (j = 0; j < ARRAY_SIZE(map5g); j++)
            if (map5g[j] == r)
                *r5g |= BIT(j);
    }
    if (!*r2g)
        *r2g = 0xfff;               /* unknown: claim the full set     */
    if (!*r5g)
        *r5g = 0xff;
}

static int mtk_ensure_vif(struct mt792x_dev *dev)
{
    struct ieee80211_vif *vif;
    struct mt792x_vif *mvif;
    int idx;

    if (g_m6.valid)
        return 0;

    vif = (struct ieee80211_vif *)kzalloc(sizeof(*vif) +
                                          sizeof(struct mt792x_vif), 0);
    if (!vif)
        return -ENOMEM;
    vif->type = NL80211_IFTYPE_STATION;
    mtk_get_mac(dev, vif->addr);
    memcpy(vif->bss_conf.addr, vif->addr, ETH_ALEN);
    vif->bss_conf.vif = vif;
    vif->bss_conf.beacon_int = 100;
    vif->bss_conf.dtim_period = 1;
    vif->bss_conf.qos = 1;          /* WMM: the firmware records exist  */
    vif->bss_conf.he_support = 0;
    /* Reference parity (MtkMwx.cpp sta_rec_phy): the STA record's
     * basic_rate is pinned to 6/12/24M OFDM (0x0150).  A zero bitmap
     * gives the firmware no basic rate for its control responses.
     * Bit0 (CCK 1M) is added so mt76_connac2_mac_tx_rate_val()'s
     * ffs(basic_rates) picks CCK 1M for the fixed-rate mgmt TX on
     * 2.4 GHz (reference mt7921_mac_tx_rate_val sends auth at 1M). */
    vif->bss_conf.basic_rates = 0x0151;
    vif->deflink_conf = &vif->bss_conf;
    vif->link_conf[0] = &vif->bss_conf;
    vif->cfg.assoc = false;

    mvif = (struct mt792x_vif *)vif->drv_priv;
    mvif->bss_conf.mt76.idx = 0;
    mvif->bss_conf.mt76.link_idx = 0;
    mvif->bss_conf.mt76.omac_idx = 0;
    mvif->bss_conf.mt76.band_idx = 0;
    mvif->bss_conf.mt76.wmm_idx = 0;
    mvif->bss_conf.vif = mvif;
    mvif->phy = &dev->phy;
    ewma_rssi_init(&mvif->bss_conf.rssi);
    mvif->sta.vif = mvif;
    mvif->sta.deflink.sta = &mvif->sta;

    /*
     * The BSS/broadcast wcid: upstream mt7921_add_interface() puts it
     * at WTBL index MT792x_WTBL_RESERVED - vif_idx (19 for the first
     * interface) and registers it in dev->wcid[].
     */
    idx = MT792x_WTBL_RESERVED - mvif->bss_conf.mt76.idx;
    mt76_wcid_init(&mvif->sta.deflink.wcid, mvif->bss_conf.mt76.band_idx);
    mvif->sta.deflink.wcid.idx = idx;
    mvif->sta.deflink.wcid.hw_key_idx = (u8)-1;
    mvif->sta.deflink.wcid.hw_key_idx2 = (u8)-1;
    mvif->sta.deflink.wcid.tx_info |= MT_WCID_TX_INFO_SET;
    rcu_assign_pointer(dev->mt76.wcid[idx], &mvif->sta.deflink.wcid);

    g_m6.vif = vif;
    g_m6.valid = true;
    return 0;
}

/*
 * Register the vif/OMAC with the firmware (reference: mt7921_mcu_uni_add_dev
 * in the init sequence, kept for the driver lifetime).  The record carries
 * our MAC: the firmware's own-address filter is built from it, and without
 * it the AP's unicast auth response is dropped as "other UC".
 * Caller holds the device mutex.
 */
static int mtk_vif_register(struct mt792x_dev *dev)
{
    struct mt792x_vif *mvif = mtk_mvif();
    static int registered;
    int ret;

    /* Once per driver lifetime (reference parity): a duplicate
     * DEV_INFO_UPDATE wedges this firmware. */
    if (registered)
        return 0;

    ret = mt76_connac_mcu_uni_add_dev(&dev->mphy, &g_m6.vif->bss_conf,
                                      &mvif->bss_conf.mt76,
                                      &mvif->sta.deflink.wcid, true);
    if (ret == 0) {
        registered = 1;
        XYLog("%s: vif/OMAC registered mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
              __FUNCTION__, g_m6.vif->bss_conf.addr[0], g_m6.vif->bss_conf.addr[1],
              g_m6.vif->bss_conf.addr[2], g_m6.vif->bss_conf.addr[3],
              g_m6.vif->bss_conf.addr[4], g_m6.vif->bss_conf.addr[5]);
        /*
         * Reference init parity (MtkMwx.cpp STA branch): explicitly
         * disable sniffer mode.  With the SNIFFER state left undefined
         * the firmware drops BSS-mode beacons (EVT_BEACON went silent
         * right after the tune to the BSS channel) and the WCL join can
         * never complete.
         */
        ret = mt7921_mcu_set_sniffer(dev, g_m6.vif, false);
        XYLog("mtk-dbg: set_sniffer(0) -> %d\n", ret);
        ret = 0;
    }
    return ret;
}

/* ---- station (WTBL) lifecycle ---- */

extern "C" int
mtk_sta_add(void *mdev, void *ni, const unsigned char *mac,
            unsigned int aid, const unsigned char *rates,
            unsigned int nrates)
{
    const struct mtk_dev_ops *ops = mtk_ops_for_mdev(mdev);
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;
    struct mt792x_vif *mvif;
    struct mtk_sta_ctx *ctx;
    struct ieee80211_sta *sta;
    struct mt792x_sta *msta;
    int ret, idx, i;

    if (ops && ops->sta_add)
        return ops->sta_add(mdev, ni, mac, aid, rates, nrates);
    if (!dev || !ni || !mac)
        return -EINVAL;
    ret = mtk_ensure_vif(dev);
    if (ret)
        return ret;
    mvif = mtk_mvif();

    ctx = mtk_sta_find(ni);
    if (ctx) {
        /* re-association: refresh the STA record in place */
        sta = ctx->sta;
        sta->aid = aid;
        msta = (struct mt792x_sta *)sta->drv_priv;
        mt792x_mutex_acquire(dev);
        ret = mt7921_mcu_sta_update(dev, sta, g_m6.vif, true,
                                    MT76_STA_INFO_STATE_ASSOC);
        mt792x_mutex_release(dev);
        return ret;
    }
    for (i = 0; i < MTK_MAX_STA; i++)
        if (!g_m6.sta[i].used)
            break;
    if (i == MTK_MAX_STA)
        return -ENOSPC;
    ctx = &g_m6.sta[i];

    sta = (struct ieee80211_sta *)kzalloc(sizeof(*sta) +
                                          sizeof(struct mt792x_sta), 0);
    if (!sta)
        return -ENOMEM;

    sta->aid = (u16)aid;
    sta->wme = 1;
    sta->mfp = 0;
    sta->rx_nss = 1;
    sta->deflink.sta = sta;
    memcpy(sta->addr, mac, ETH_ALEN);
    memcpy(sta->deflink.addr, mac, ETH_ALEN);
    sta->deflink.rx_nss = 1;
    sta->deflink.smps_mode = IEEE80211_SMPS_OFF;
    mtk_build_rate_bitmaps(rates, nrates,
                           &sta->deflink.supp_rates[NL80211_BAND_2GHZ],
                           &sta->deflink.supp_rates[NL80211_BAND_5GHZ]);
    sta->deflink.wcid = NULL;

    msta = (struct mt792x_sta *)sta->drv_priv;
    msta->vif = mvif;
    msta->deflink.sta = msta;
    msta->deflink.last_txs = jiffies;
    ewma_avg_signal_init(&msta->deflink.avg_ack_signal);

    /* WTBL allocation (upstream mt7921_mac_sta_add). */
    idx = mt76_wcid_alloc(dev->mt76.wcid_mask, MT792x_WTBL_STA - 1);
    if (idx < 0) {
        kfree(sta);
        return -ENOSPC;
    }
    mt76_wcid_init(&msta->deflink.wcid, mvif->bss_conf.mt76.band_idx);
    msta->deflink.wcid.sta = 1;
    msta->deflink.wcid.idx = (u16)idx;
    msta->deflink.wcid.phy_idx = mvif->bss_conf.mt76.band_idx;
    msta->deflink.wcid.hw_key_idx = (u8)-1;
    msta->deflink.wcid.hw_key_idx2 = (u8)-1;
    msta->deflink.wcid.tx_info |= MT_WCID_TX_INFO_SET;
    rcu_assign_pointer(dev->mt76.wcid[idx], &msta->deflink.wcid);

    ctx->ni = ni;
    ctx->sta = sta;
    ctx->used = true;
    ctx->assoced = false;

    mt792x_mutex_acquire(dev);
    mt7921_mac_wtbl_update(dev, idx, MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
    /* create the STA record, then mark it associated */
    ret = mt7921_mcu_sta_update(dev, sta, g_m6.vif, true,
                                MT76_STA_INFO_STATE_NONE);
    if (!ret) {
        ret = mt7921_mcu_sta_update(dev, sta, g_m6.vif, true,
                                    MT76_STA_INFO_STATE_ASSOC);
        if (!ret)
            ctx->assoced = true;   /* done here; bss_setup must not repeat */
    }
    mt792x_mutex_release(dev);

    if (ret) {
        ctx->used = false;
        ctx->ni = NULL;
        ctx->sta = NULL;
        rcu_assign_pointer(dev->mt76.wcid[idx], NULL);
        mt76_wcid_mask_clear(dev->mt76.wcid_mask, idx);
        kfree(sta);
        return ret;
    }
    XYLog("%s: WTBL %d bound to %02x:%02x:%02x:%02x:%02x:%02x aid %u\n",
          __FUNCTION__, idx, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
          aid);
    return 0;
}

extern "C" void
mtk_sta_remove(void *mdev, void *ni)
{
    const struct mtk_dev_ops *ops = mtk_ops_for_mdev(mdev);
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;
    struct mtk_sta_ctx *ctx;
    struct mt792x_sta *msta;
    struct mt76_wcid *wcid;
    int idx;

    if (ops && ops->sta_remove) {
        ops->sta_remove(mdev, ni);
        return;
    }
    if (!dev || !ni)
        return;
    ctx = mtk_sta_find(ni);
    if (!ctx)
        return;

    msta = (struct mt792x_sta *)ctx->sta->drv_priv;
    wcid = &msta->deflink.wcid;
    idx = wcid->idx;

    mt792x_mutex_acquire(dev);
    mt7921_mcu_sta_update(dev, ctx->sta, g_m6.vif, false,
                          MT76_STA_INFO_STATE_NONE);
    mt792x_mutex_release(dev);

    rcu_assign_pointer(dev->mt76.wcid[idx], NULL);
    mt76_wcid_mask_clear(dev->mt76.wcid_mask, idx);
    /* parked offchannel frames are not covered by mt76_wcid_cleanup() */
    skb_queue_purge(&wcid->tx_offchannel);
    mt76_wcid_cleanup(&dev->mt76, wcid);

    ctx->used = false;
    ctx->assoced = false;
    ctx->ni = NULL;
    kfree(ctx->sta);
    ctx->sta = NULL;
}

/* ---- BSS (RUN state) ---- */

extern "C" int
mtk_bss_prog_bssid(void *mdev, const unsigned char *bssid,
                   unsigned int bintval, unsigned int dtim)
{
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;
    struct mt792x_vif *mvif;
    int ret;

    if (!dev || !bssid)
        return -EINVAL;
    ret = mtk_ensure_vif(dev);
    if (ret)
        return ret;
    mvif = mtk_mvif();
    if (!mvif || !g_m6.vif)
        return -EINVAL;
    memcpy(g_m6.vif->bss_conf.bssid, bssid, ETH_ALEN);
    /*
     * The BSS_INFO basic TLV carries beacon_int/dtim -- the firmware's
     * beacon RX timing keys off them.  They must be valid HERE (the AUTH
     * entry), not only later in mtk_bss_setup: with beacon_int=0 the
     * firmware silently drops every beacon and the WCL join can never
     * complete (firstBeaconNotified never latches).
     */
    g_m6.vif->bss_conf.beacon_int = (u16)(bintval ? bintval : 100);
    g_m6.vif->bss_conf.dtim_period = (u8)(dtim ? dtim : 1);
    mt792x_mutex_acquire(dev);
    /*
     * BSS_INFO_UPDATE only (the basic TLV carries the BSSID) -- never
     * DEV_INFO: a duplicate uni_add_dev wedges this firmware.  Programmed
     * at the AUTH entry, well before the RUN transition where an extra
     * BSS_INFO registration previously timed out the MCU.  Without this
     * the firmware RX filter matches beacons against a zero BSSID, drops
     * every beacon from the AP the moment we leave scan mode, and the WCL
     * join can never complete (firstBeaconNotified never latches).
     */
    ret = mt76_connac_mcu_uni_add_bss(&dev->mphy, g_m6.vif,
                                      &mvif->sta.deflink.wcid, true, NULL);
    mt792x_mutex_release(dev);
    XYLog("mtk-dbg: bss prog bssid %02x:%02x:%02x:%02x:%02x:%02x rv=%d\n",
          bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], ret);
    return ret;
}

extern "C" int
mtk_bss_setup(void *mdev, const unsigned char *bssid, unsigned int aid,
              unsigned int bintval, unsigned int dtim,
              unsigned int qos, unsigned int basic_rates)
{
    const struct mtk_dev_ops *ops = mtk_ops_for_mdev(mdev);
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;
    struct mt792x_vif *mvif;
    int ret, i;

    if (ops && ops->bss_setup)
        return ops->bss_setup(mdev, bssid, aid, bintval, dtim, qos,
                              basic_rates);
    if (!dev || !bssid)
        return -EINVAL;
    ret = mtk_ensure_vif(dev);
    if (ret)
        return ret;
    mvif = mtk_mvif();

    memcpy(g_m6.vif->bss_conf.bssid, bssid, ETH_ALEN);
    g_m6.vif->cfg.aid = (u16)aid;
    g_m6.vif->cfg.assoc = true;
    g_m6.vif->bss_conf.aid = (u16)aid;
    g_m6.vif->bss_conf.assoc = true;
    g_m6.vif->bss_conf.beacon_int = (u16)(bintval ? bintval : 100);
    g_m6.vif->bss_conf.dtim_period = (u8)(dtim ? dtim : 1);
    g_m6.vif->bss_conf.qos = qos ? 1 : 0;
    g_m6.vif->bss_conf.basic_rates = basic_rates ? basic_rates : 0x15f;

    mt792x_mutex_acquire(dev);
    /*
     * Reference RUN parity (MtkMwx.cpp: set_associated_bss + peer only).
     * The BSS context comes from the init-time uni_add_dev; do NOT send
     * uni_add_bss here -- the extra UNI BSS_INFO registration wedges this
     * firmware at the RUN transition (MCU timeout -> suppressed reset ->
     * the AP's EAPOL M1 never reaches us).
     */
    /* BSS power-save / connected state (mt7921_mcu_set_bss_pm ==
     * reference mt7921_mcu_set_associated_bss: SET_BSS_ABORT +
     * SET_BSS_CONNECTED). */
    ret = mt7921_mcu_set_bss_pm(dev, g_m6.vif, true);
    if (ret)
        goto out;
    /*
     * BSS station record: reference parity (MtkMwx.cpp RUN case =
     * set_associated_bss + set_associated_peer ONLY).  There is no
     * sta_update(NULL)-style BSS-record update in the reference and
     * ours carried EXTRA_INFO_NEW every time (newly is hardwired true
     * for the sta==NULL path), which this firmware answers with a
     * 3 s MCU timeout that stalls every command behind it (key
     * installs first).  Skip it.
     */
    /* per-node records reach the ASSOC state (mt7921_mac_sta_event) */
    for (i = 0; i < MTK_MAX_STA; i++) {
        if (!g_m6.sta[i].used)
            continue;
        ret = mt7921_mcu_sta_update(dev, g_m6.sta[i].sta, g_m6.vif, true,
                                    MT76_STA_INFO_STATE_ASSOC);
        if (ret)
            goto out;
    }
    g_m6.bss_up = true;
out:
    mt792x_mutex_release(dev);
    return ret;
}

extern "C" void
mtk_bss_teardown(void *mdev)
{
    const struct mtk_dev_ops *ops = mtk_ops_for_mdev(mdev);
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;
    struct mt792x_vif *mvif;

    if (ops && ops->bss_teardown) {
        ops->bss_teardown(mdev);
        return;
    }
    if (!dev || !g_m6.valid || !g_m6.bss_up)
        return;
    mvif = mtk_mvif();

    mt792x_mutex_acquire(dev);
    mt7921_mcu_set_bss_pm(dev, g_m6.vif, false);
    mt7921_mcu_sta_update(dev, NULL, g_m6.vif, false,
                          MT76_STA_INFO_STATE_NONE);
    /* The vif/OMAC record is registered once at init and kept for the
     * driver lifetime (reference parity): removing it here left the
     * firmware with no own-address filter, so the next join's unicast
     * auth response was dropped as "other UC". */
    mt792x_mutex_release(dev);

    g_m6.vif->cfg.assoc = false;
    g_m6.vif->bss_conf.assoc = false;
    g_m6.bss_up = false;
    g_m6.sta_assoced = false;
}

/* ---- keys ---- */

static int mtk_key_suite(unsigned int cipher)
{
    switch (cipher) {
    case MTK_KEY_CIPHER_WEP40:   return WLAN_CIPHER_SUITE_WEP40;
    case MTK_KEY_CIPHER_TKIP:    return WLAN_CIPHER_SUITE_TKIP;
    case MTK_KEY_CIPHER_CCMP:    return WLAN_CIPHER_SUITE_CCMP;
    case MTK_KEY_CIPHER_WEP104:  return WLAN_CIPHER_SUITE_WEP104;
    case MTK_KEY_CIPHER_BIP:     return WLAN_CIPHER_SUITE_BIP_CMAC_128;
    case MTK_KEY_CIPHER_CCMP256: return WLAN_CIPHER_SUITE_CCMP_256;
    case MTK_KEY_CIPHER_GCMP:    return WLAN_CIPHER_SUITE_GCMP;
    default:                     return -1;
    }
}

static int mtk_key_slot(const struct mtk_key_desc *k)
{
    if (k->flags & MTK_KEY_FLAG_IGTK)
        return MTK_KEY_SLOT_IGTK;
    if (k->flags & MTK_KEY_FLAG_GROUP)
        return MTK_KEY_SLOT_GTK(k->key_id);
    return MTK_KEY_SLOT_PTK(k->key_id);
}

extern "C" int
mtk_key_add(void *mdev, void *ni, const struct mtk_key_desc *k)
{
    const struct mtk_dev_ops *ops = mtk_ops_for_mdev(mdev);
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;
    struct mt792x_vif *mvif;
    struct mtk_sta_ctx *ctx = NULL;
    struct mt792x_sta *msta;
    struct ieee80211_key_conf *conf;
    struct mt76_wcid *wcid;
    u8 *wcid_keyidx;
    int suite, slot, ret;

    if (ops && ops->key_add)
        return ops->key_add(mdev, ni, k);
    if (!dev || !k)
        return -EINVAL;
    if (!g_m6.valid)
        return -ENXIO;
    suite = mtk_key_suite(k->cipher);
    if (suite < 0)
        return -EOPNOTSUPP;         /* caller falls back to sw crypto */
    if (k->key_len > 32)
        return -EINVAL;
    mvif = mtk_mvif();

    /* PTKs ride the peer wcid, GTK/IGTK the BSS wcid (upstream set_key) */
    if (ni != NULL && !(k->flags & (MTK_KEY_FLAG_GROUP | MTK_KEY_FLAG_IGTK))) {
        ctx = mtk_sta_find(ni);
        if (!ctx)
            return -ENOENT;
    }
    msta = ctx ? (struct mt792x_sta *)ctx->sta->drv_priv : &mvif->sta;
    wcid = &msta->deflink.wcid;

    /* upstream mt7921_set_key cipher whitelist */
    switch (k->cipher) {
    case MTK_KEY_CIPHER_WEP40:
    case MTK_KEY_CIPHER_WEP104:
        if (!mvif->wep_sta)
            return -EOPNOTSUPP;
        break;
    case MTK_KEY_CIPHER_TKIP:
    case MTK_KEY_CIPHER_CCMP:
    case MTK_KEY_CIPHER_CCMP256:
    case MTK_KEY_CIPHER_GCMP:
    case MTK_KEY_CIPHER_BIP:
        break;
    default:
        return -EOPNOTSUPP;
    }

    slot = mtk_key_slot(k);
    conf = &g_m6.kconf[slot];
    memset(conf, 0, sizeof(*conf));
    conf->cipher = (u32)suite;
    conf->keyidx = (s8)(k->key_id & 0x7);
    conf->key_id = (u8)k->key_id;
    conf->keylen = (u8)k->key_len;
    conf->key_len = (u8)k->key_len;
    memcpy(conf->key, k->key, k->key_len);
    g_m6.kvalid[slot] = 1;

    if (suite == WLAN_CIPHER_SUITE_BIP_CMAC_128) {
        conf->flags |= 1 /* IEEE80211_KEY_FLAG_GENERATE_MMIE */;
        wcid_keyidx = &wcid->hw_key_idx2;
    } else {
        wcid_keyidx = &wcid->hw_key_idx;
    }
    *wcid_keyidx = (u8)conf->keyidx;

    mt792x_mutex_acquire(dev);
    mt76_wcid_key_setup(&dev->mt76, wcid, conf);
    ret = mt76_connac_mcu_add_key(&dev->mt76, g_m6.vif, &msta->deflink.bip,
                                  conf, MCU_UNI_CMD(STA_REC_UPDATE),
                                  wcid, SET_KEY);
    mt792x_mutex_release(dev);
    return ret;
}

extern "C" int
mtk_key_delete(void *mdev, void *ni, const struct mtk_key_desc *k)
{
    const struct mtk_dev_ops *ops = mtk_ops_for_mdev(mdev);
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;
    struct mt792x_vif *mvif;
    struct mtk_sta_ctx *ctx = NULL;
    struct mt792x_sta *msta;
    struct ieee80211_key_conf *conf;
    struct mt76_wcid *wcid;
    u8 *wcid_keyidx;
    int suite, slot, ret = 0;

    if (ops && ops->key_delete)
        return ops->key_delete(mdev, ni, k);
    if (!dev || !k || !g_m6.valid)
        return -EINVAL;
    suite = mtk_key_suite(k->cipher);
    if (suite < 0)
        return 0;
    mvif = mtk_mvif();

    if (ni != NULL && !(k->flags & (MTK_KEY_FLAG_GROUP | MTK_KEY_FLAG_IGTK))) {
        ctx = mtk_sta_find(ni);
        if (!ctx)
            return 0;
    }
    msta = ctx ? (struct mt792x_sta *)ctx->sta->drv_priv : &mvif->sta;
    wcid = &msta->deflink.wcid;
    wcid_keyidx = (suite == WLAN_CIPHER_SUITE_BIP_CMAC_128) ?
                  &wcid->hw_key_idx2 : &wcid->hw_key_idx;
    if (*wcid_keyidx == (u8)(k->key_id & 0x7))
        *wcid_keyidx = (u8)-1;

    slot = mtk_key_slot(k);
    if (!g_m6.kvalid[slot])
        return 0;
    conf = &g_m6.kconf[slot];

    mt792x_mutex_acquire(dev);
    ret = mt76_connac_mcu_add_key(&dev->mt76, g_m6.vif, &msta->deflink.bip,
                                  conf, MCU_UNI_CMD(STA_REC_UPDATE),
                                  wcid, DELETE_KEY);
    mt792x_mutex_release(dev);

    memset(conf, 0, sizeof(*conf));
    g_m6.kvalid[slot] = 0;
    return ret;
}

/* ---- EDCA / slot timing ---- */

extern "C" int
mtk_set_edca(void *mdev, const struct mtk_edca_ac *ac4)
{
    const struct mtk_dev_ops *ops = mtk_ops_for_mdev(mdev);
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;
    struct mt792x_vif *mvif;
    static const int m2l[4] = { IEEE80211_AC_VO, IEEE80211_AC_VI,
                                IEEE80211_AC_BE, IEEE80211_AC_BK };
    int ret, i;

    if (ops && ops->set_edca)
        return ops->set_edca(mdev, ac4);
    if (!dev || !ac4)
        return -EINVAL;
    if (!g_m6.valid)
        return -ENXIO;
    mvif = mtk_mvif();

    for (i = 0; i < 4; i++) {
        struct ieee80211_tx_queue_params *q =
            &mvif->bss_conf.queue_params[m2l[i]];

        q->cw_min = (u16)ac4[i].cw_min;
        q->cw_max = (u16)ac4[i].cw_max;
        q->aifs = (u8)ac4[i].aifs;
        q->txop = (u16)ac4[i].txop;
        q->acm = ac4[i].acm != 0;
    }

    mt792x_mutex_acquire(dev);
    ret = mt7921_mcu_set_tx(dev, g_m6.vif);
    mt792x_mutex_release(dev);
    return ret;
}

extern "C" int
mtk_set_slot(void *mdev, int short_slot)
{
    const struct mtk_dev_ops *ops = mtk_ops_for_mdev(mdev);
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;
    int slottime = short_slot ? 9 : 20;

    if (ops && ops->set_slot)
        return ops->set_slot(mdev, short_slot);
    if (!dev)
        return -EINVAL;
    if (dev->phy.slottime == slottime)
        return 0;
    dev->phy.slottime = slottime;
    mt792x_mac_set_timeing(&dev->phy);
    return 0;
}

/*
 * LMAC timing (ACK wait / SIFS / slot) for the join path: the reference
 * runs mt7921_mac_set_timing at the RUN entry (record 62f7dc5) as well as
 * after every channel switch.  Do NOT call it from updateslot at runtime:
 * the ARB TX/RX-disable toggle around the register writes drops in-flight
 * frames ("removing runtime arbiter toggle", same record).
 */
extern "C" void mtk_mac_set_timing(void *mdev)
{
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;

    if (!dev)
        return;
    mt792x_mac_set_timeing(&dev->phy);
}

/*
 * mt76_wcid_key_setup() (Linux mt76/mac80211.c): seed the wcid RX PN
 * replay state from the installed key.  The net80211 side keeps its own
 * replay counters (ieee80211_input_hwdecrypt), so only the CCMP marker
 * matters here.
 */
void mt76_wcid_key_setup(struct mt76_dev *dev, struct mt76_wcid *wcid,
                         struct ieee80211_key_conf *key)
{
    (void)dev;
    wcid->rx_check_pn = 0;
    if (!key)
        return;
    if (key->cipher != WLAN_CIPHER_SUITE_CCMP)
        return;
    wcid->rx_check_pn = 1;
    memset(wcid->rx_key_pn, 0, sizeof(wcid->rx_key_pn));
}

/* ================================================================== */
/* net80211 -> mt76 bridge (see glue/mtk_api.h)                       */
/* ================================================================== */
unsigned int mtk_bands(void *mdev)
{
    const struct mtk_dev_ops *ops = mtk_ops_for_mdev(mdev);
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;
    struct mt76_hw_cap *cap;
    unsigned int bands = 0;

    if (ops && ops->bands)
        return ops->bands(mdev);
    if (!dev)
        return 0;
    cap = &dev->mt76.phy.cap;

    /*
     * The capability bits are filled by mt7921_mcu_get_nic_capability()
     * during probe.  If the MCU bring-up did not complete, assume the
     * MT7921's dual-band 2.4/5 GHz set so scanning still has channels
     * to walk.
     */
    if (cap->has_2ghz)
        bands |= MTK_BAND_2GHZ;
    if (cap->has_5ghz)
        bands |= MTK_BAND_5GHZ;
    if (cap->has_6ghz)
        bands |= MTK_BAND_6GHZ;
    if (!bands)
        bands = MTK_BAND_2GHZ | MTK_BAND_5GHZ;
    return bands;
}

extern "C" int mtk_chan_switch(void *mdev, unsigned int freq,
                               unsigned int offchannel)
{
    const struct mtk_dev_ops *ops = mtk_ops_for_mdev(mdev);
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;
    struct mt76_phy *mphy;
    struct ieee80211_channel *chan = NULL, *c;
    struct mt76_sband *sband;
    struct cfg80211_chan_def chandef;
    int i, n;

    if (ops && ops->chan_switch)
        return ops->chan_switch(mdev, freq, offchannel);
    if (!dev || freq == 0)
        return -EINVAL;

    mphy = &dev->mt76.phy;
    /*
     * Lazy sband init: the probe fallback never reaches
     * mt76_register_device() (where Linux fills the sband channel
     * tables), so build them on first use -- 2g gets the first 4 rates
     * (11b), 5g the OFDM ones, same split as mt76_register_device().
     */
    if (!mphy->sband_2g.sband.channels) {
        mtk_init_chan_tables();
        mtk_init_sband_min(mphy, &mphy->sband_2g, mt76_rates, 4,
                           NL80211_BAND_2GHZ);
        mtk_init_sband_min(mphy, &mphy->sband_5g, mt76_rates + 4, 8,
                           NL80211_BAND_5GHZ);
    }
    if (freq < 2500)
        sband = &mphy->sband_2g;
    else if (freq < 5925)
        sband = &mphy->sband_5g;
    else
        sband = &mphy->sband_6g;

    c = sband->sband.channels;
    n = sband->sband.n_channels;
    for (i = 0; c != NULL && i < n; i++) {
        if (c[i].center_freq == freq) {
            chan = &c[i];
            break;
        }
    }
    if (chan == NULL) {
        XYLog("%s: no channel for %u MHz\n", __FUNCTION__, freq);
        return -EINVAL;
    }

    cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_WIDTH_20_NOHT);
    mt76_phy_chandef_set(mphy, &chandef);
    /* Scan hops are offchannel (TX is held back / sent as offchannel TX);
     * a BSS channel switch must clear the flag or data TX stalls. */
    mphy->offchannel = offchannel != 0;
    return mt7921_set_channel(mphy);
}

extern "C" int mtk_hw_start(void *mdev)
{
    const struct mtk_dev_ops *ops = mtk_ops_for_mdev(mdev);
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;

    if (ops && ops->hw_start)
        return ops->hw_start(mdev);
    if (!dev)
        return -1;
    return mt7921_start(dev->mt76.hw);
}

extern "C" void mtk_hw_stop(void *mdev)
{
    const struct mtk_dev_ops *ops = mtk_ops_for_mdev(mdev);
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;

    if (ops && ops->hw_stop) {
        ops->hw_stop(mdev);
        return;
    }
    if (!dev)
        return;
    mt792x_mutex_acquire(dev);
    clear_bit(MT76_STATE_RUNNING, &dev->mphy.state);
    mt792x_mutex_release(dev);
}

/* True when the datapath hardware is actually up (MT76_STATE_RUNNING).
 * fill_rx drops every frame when this is clear -- the net80211 mirror
 * alone is not trustworthy after a suppressed reset. */
extern "C" int mtk_hw_running(void *mdev)
{
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;

    return dev && test_bit(MT76_STATE_RUNNING, &dev->mt76.phy.state);
}

extern "C" int mtk_get_mac(void *mdev, unsigned char *out)
{
    const struct mtk_dev_ops *ops = mtk_ops_for_mdev(mdev);
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;

    if (ops && ops->get_mac)
        return ops->get_mac(mdev, out);
    if (!dev || !out)
        return -1;
    memcpy(out, dev->mt76.phy.macaddr, ETH_ALEN);
    return 0;
}

/*
 * Override the station MAC.  The net80211 fallback address (and later
 * macOS's private address per network) must live in mt76_phy.macaddr too:
 * the vif/OMAC record registered with the firmware is built from it, and
 * the firmware's own-address filter drops unicast frames addressed to
 * anything else ("other UC") -- the AP's auth response would never reach
 * net80211.
 */
extern "C" void mtk_set_mac(void *mdev, const unsigned char *mac)
{
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;

    if (!dev || !mac)
        return;
    memcpy(dev->mt76.phy.macaddr, mac, ETH_ALEN);
}

/* ================================================================== */
/* TX bridge (net80211 -> mt76): see glue/mtk_api.h                    */
/*                                                                      */
/* mtk_start() (glue/mtk_net80211.c) dequeues 802.11 frames from        */
/* ic_mgtq/if_snd and hands them here.  The frame is copied into an     */
/* skb carrying the net80211 completion cookie and pushed through the   */
/* standard mt76 TX pipeline:                                          */
/*                                                                      */
/*   mt76_tx() -> wcid->tx_pending/tx_offchannel                       */
/*     -> mt76_txq_schedule_pending() -> __mt76_tx_queue_skb()         */
/*     -> mt76_dma_tx_queue_skb() -> mt7921e_tx_prepare_skb()          */
/*     -> mt76_connac2_mac_write_txwi() + hw TXP -> WPDMA kick         */
/*                                                                      */
/* Completion (mt7921_mac_tx_free token release and/or the TXS event)   */
/* runs __mt76_tx_complete_skb() -> ieee80211_tx_status_ext()          */
/* (glue/mtk_mac80211.c) which folds the ACK result into the skb;      */
/* mtk_skb_release() (glue/mtk_glue.c) then calls                      */
/* mtk_net80211_tx_done() exactly once per cookie.                     */
/* ================================================================== */

/* TID -> mac80211 AC index (MT_TXQ_* numbering: VO=0 VI=1 BE=2 BK=3). */
static const u8 mtk_tid_to_ac[8] = {
    2, 3, 3, 2, 1, 1, 0, 0
};

extern "C" int
mtk_tx_frame(void *mdev, const struct mtk_tx_cookie *ck,
             const void *frame, unsigned int len, unsigned int flags,
             unsigned int prio, const struct mtk_key_desc *hwkey)
{
    const struct mtk_dev_ops *ops = mtk_ops_for_mdev(mdev);
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;
    struct ieee80211_tx_info *info;
    struct mt76_wcid *wcid;
    struct ieee80211_sta *sta = NULL;
    struct mtk_sta_ctx *ctx;
    struct sk_buff *skb;

    if (ops && ops->tx_frame)
        return ops->tx_frame(mdev, ck, frame, len, flags, prio, hwkey);
    if (!dev || !ck || frame == NULL || len < 16 || len > 4096)
        return -EINVAL;
    if (dev->mt76.queue_ops == NULL)
        return -ENXIO;      /* M1 fallback: no TX rings */
    if (!test_bit(MT76_STATE_RUNNING, &dev->mphy.state))
        return -ENETDOWN;
    if (test_bit(MT76_RESET, &dev->mphy.state) ||
        test_bit(MT76_MCU_RESET, &dev->mphy.state))
        return -ENETDOWN;

    skb = alloc_skb(len + 2, GFP_ATOMIC);
    if (!skb)
        return -ENOMEM;
    memcpy(skb_put(skb, len), frame, len);

    skb->mtk_ck = *ck;
    skb->priority = prio & 0x7;

    /*
     * M6: pick the per-node wcid when the frame's node owns a WTBL
     * entry (mtk_sta_add), else the global wcid (mgmt before assoc,
     * broadcast).  The wcid drives the TXD WLAN_IDX so the firmware
     * finds the right station record / key.
     */
    wcid = &dev->mt76.global_wcid;
    ctx = mtk_sta_find((void *)ck->ni);
    if (ctx) {
        struct mt792x_sta *msta =
            (struct mt792x_sta *)ctx->sta->drv_priv;

        wcid = &msta->deflink.wcid;
        sta = ctx->sta;
    }

    /*
     * Defensive: the probe fallback path never reaches
     * mt76_register_device(), so global_wcid (and any wcid that missed
     * mt76_wcid_init) has a zeroed tx_pending head -- the first
     * __skb_queue_tail would write through NULL (panic).  Lazy-init here;
     * the workloop serialises TX so this is race-free.
     */
    if (!wcid->tx_pending.next) {
        u8 band = wcid->phy_idx;
        mt76_wcid_init(wcid, band);
    }
    /* Same fallback hazard for the per-phy TX list (Linux inits it in
     * mt76_register_device, which the probe fallback skips). */
    {
        struct mt76_phy *tphy = &dev->mt76.phy;
        if (!tphy->tx_list.next) {
            INIT_LIST_HEAD(&tphy->tx_list);
            spin_lock_init(&tphy->tx_lock);
        }
    }

    info = IEEE80211_SKB_CB(skb);
    info->control.vif = g_m6.valid ? g_m6.vif : NULL;
    if (flags & MTK_TX_FLAG_NO_ACK)
        info->flags |= IEEE80211_TX_CTL_NO_ACK;
    if (flags & MTK_TX_FLAG_STATUS)
        info->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS;
    if (flags & MTK_TX_FLAG_OFFCHAN)
        info->flags |= IEEE80211_TX_CTL_TX_OFFCHAN;

    /*
     * M6 hardware crypto: the frame stays a plaintext MPDU; the key
     * installed via mtk_key_add() is referenced from the TX info so
     * mt76_connac2_mac_write_txwi() sets MT_TXD3_PROTECT_FRAME and the
     * firmware encrypts with the WTBL key.
     */
    if (hwkey && g_m6.valid) {
        int slot = mtk_key_slot(hwkey);

        if (g_m6.kvalid[slot])
            info->control.hw_key = &g_m6.kconf[slot];
    }

    /*
     * Queue mapping -> mt76 qid.  On MT7921 all AC queues alias the
     * single MT7921_TXQ_BAND0 ring (mt76_connac_init_tx_queues()), but
     * the AC still feeds the LMAC WMM index in the TXD
     * (mt76_connac_lmac_mapping()).  Management frames are moved to
     * MT_TXQ_PSD by the mt76 TX scheduler (MT_DRV_HW_MGMT_TXQ); they
     * are marked OFFCHAN so they ride wcid->tx_offchannel, which is
     * drained even while phy->offchannel is set (scan dwell).
     */
    if (flags & MTK_TX_FLAG_MGMT) {
        /* WMM VO: reference parity (mwx_tx: "management and control
         * frames stay on VO, like mac80211").  With MT_DRV_HW_MGMT_TXQ
         * removed, mgmt rides this AC queue instead of PSD/ALTX0 --
         * the ALTX0 route needs a vif/own_mac context that pre-assoc
         * auth frames lack, and the LMAC emitted ACK_TIMEOUT TXS (the
         * frame reached the air broken). */
        skb_set_queue_mapping(skb, MT_TXQ_VO);
    } else {
        skb_set_queue_mapping(skb, mtk_tid_to_ac[prio & 0x7]);
        if (dev->mt76.phy.offchannel)
            info->flags |= IEEE80211_TX_CTL_TX_OFFCHAN;
    }

    mt76_tx(&dev->mt76.phy, sta, wcid, skb);

    /*
     * The tx_worker kthread is not started in the port and
     * mt76_worker_schedule() runs its handler inline (mt76core/util.h),
     * so this is normally redundant - but it also covers the case where
     * the inline round was skipped (re-entrancy guard / PM state).
     */
    mt76_txq_schedule_all(&dev->mt76.phy);
    return 0;
}

extern "C" void
mtk_tx_status_check(void *mdev)
{
    const struct mtk_dev_ops *ops = mtk_ops_for_mdev(mdev);
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;

    if (ops && ops->tx_status_check) {
        mtk_advance_jiffies();
        ops->tx_status_check(mdev);
        return;
    }
    if (!dev)
        return;
    /* Keep the jiffies clock moving even on an idle link so the TXS
     * stale timeout below is meaningful. */
    mtk_advance_jiffies();
    /* Reap TX-status entries that saw DMA completion but no TXS within
     * MT_TX_STATUS_SKB_TIMEOUT (lost firmware TXS events), then give the
     * TX scheduler another round in case a full ring stalled the wcid
     * pending lists. */
    mt76_tx_status_check(&dev->mt76, false);
    if (dev->mt76.queue_ops)
        mt76_txq_schedule_all(&dev->mt76.phy);
}

extern "C" void
mtk_tx_flush(void *mdev)
{
    const struct mtk_dev_ops *ops = mtk_ops_for_mdev(mdev);
    struct mt792x_dev *dev = (struct mt792x_dev *)mdev;

    if (ops && ops->tx_flush) {
        ops->tx_flush(mdev);
        return;
    }
    if (!dev)
        return;

    /*
     * Order matters: mt76_connac2_tx_token_put() drops the TX tokens and
     * marks the TXS-tracked skbs DMA-done (but may defer the completion
     * while the TXS is still pending); mt76_tx_status_check(flush=true)
     * then force-completes everything left in the wcid pktid idrs.  Each
     * completed skb runs mtk_net80211_tx_done() from mtk_skb_release().
     */
    if (dev->mt76.queue_ops) {
        mt76_connac2_tx_token_put(&dev->mt76);
        mt76_tx_status_check(&dev->mt76, true);
    }

    /*
     * Frames parked on the wcid TX lists but not DMA-queued yet: the
     * global wcid and every per-node wcid bound by mtk_sta_add() (M6).
     */
    skb_queue_purge(&dev->mt76.global_wcid.tx_pending);
    skb_queue_purge(&dev->mt76.global_wcid.tx_offchannel);
    {
        int i;

        for (i = 0; i < MTK_MAX_STA; i++) {
            struct mt792x_sta *msta;

            if (!g_m6.sta[i].used)
                continue;
            msta = (struct mt792x_sta *)g_m6.sta[i].sta->drv_priv;
            skb_queue_purge(&msta->deflink.wcid.tx_pending);
            skb_queue_purge(&msta->deflink.wcid.tx_offchannel);
        }
    }

    /* Unblock a token-exhausted queue for the next interface up. */
    if (dev->mt76.queue_ops)
        mt76_set_tx_blocked(&dev->mt76, false);
}
