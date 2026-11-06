/*
 * mtk_mac80211.c - stub implementations of the mac80211 API used by the
 * ported mt76 sources.
 *
 * Ported for mt76wm (XNU).  Most entry points are minimal no-ops; the
 * real wiring to itl80211/net80211 happens in M4.  Every function is
 * marked TODO accordingly.
 */

#include <linux/mtk_core.h>
#include <linux/slab.h>
#include <linux/skbuff.h>

#include "../mt76core/mt76.h"

/* ================================================================== */
/* frequency helpers                                                  */
/* ================================================================== */
int ieee80211_channel_to_frequency(int chan, int band)
{
    /* TODO(M4): exact mapping incl. 6 GHz */
    if (chan <= 0)
        return 0;
    if (chan == 14)
        return 2484;
    if (chan <= 14)
        return 2407 + chan * 5;
    if (chan <= 177)
        return 5000 + chan * 5;
    return 5950 + chan * 5;
}

int ieee80211_frequency_to_channel(int freq)
{
    if (freq == 2484)
        return 14;
    if (freq >= 2412 && freq <= 2472)
        return (freq - 2407) / 5;
    if (freq >= 5000 && freq <= 5885)
        return (freq - 5000) / 5;
    if (freq >= 5955)
        return (freq - 5950) / 5;
    return 0;
}

/* ================================================================== */
/* iftype capability lookup                                           */
/* ================================================================== */
const struct ieee80211_sband_iftype_data *
ieee80211_get_he_iftype_cap(const struct ieee80211_supported_band *sband,
                            enum nl80211_iftype type)
{
    int i;

    if (!sband || !sband->iftype_data)
        return NULL;
    for (i = 0; i < (int)sband->n_iftype_data; i++) {
        if (sband->iftype_data[i].types_mask & BIT(type))
            return &sband->iftype_data[i];
    }
    return NULL;
}

const struct ieee80211_sband_iftype_data *
ieee80211_get_eht_iftype_cap(const struct ieee80211_supported_band *sband,
                             enum nl80211_iftype type)
{
    const struct ieee80211_sband_iftype_data *data;

    data = ieee80211_get_he_iftype_cap(sband, type);
    return (data && data->eht_cap.has_eht) ? data : NULL;
}

/* ================================================================== */
/* hw lifecycle                                                       */
/* ================================================================== */
struct ieee80211_hw *ieee80211_alloc_hw(size_t priv_data_len,
                                        const struct ieee80211_ops *ops)
{
    struct ieee80211_hw *hw;
    struct wiphy *wiphy;
    size_t total = sizeof(*hw) + priv_data_len;

    hw = (struct ieee80211_hw *)kzalloc(total, 0);
    if (!hw)
        return NULL;
    wiphy = (struct wiphy *)kzalloc(sizeof(*wiphy), 0);
    if (!wiphy) {
        kfree(hw);
        return NULL;
    }
    hw->wiphy = wiphy;
    wiphy->hw = hw;
    hw->priv = (u8 *)hw + sizeof(*hw);
    hw->flags = 0;
    hw->queues = 4;
    (void)ops;
    return hw;
}

struct ieee80211_hw *ieee80211_alloc_hw_nm(size_t priv_data_len,
                                            const struct ieee80211_ops *ops,
                                            const char *requested_name)
{
    (void)requested_name;
    return ieee80211_alloc_hw(priv_data_len, ops);
}

void ieee80211_free_hw(struct ieee80211_hw *hw)
{
    if (!hw)
        return;
    if (hw->wiphy)
        kfree(hw->wiphy);
    kfree(hw);
}

int ieee80211_register_hw(struct ieee80211_hw *hw)
{
    /* The net80211 registration happens through
     * mtk_net80211_attach(struct mt76_softc *) instead (M4). */
    XYLog("mt76: ieee80211_register_hw(%p) - stub (M4)\n", hw);
    return 0;
}

void ieee80211_unregister_hw(struct ieee80211_hw *hw)
{
    (void)hw;
    /* TODO(M4) */
}

void ieee80211_restart_hw(struct ieee80211_hw *hw)
{
    (void)hw;
    /* TODO(M2) */
}

/* ================================================================== */
/* RX / TX status                                                     */
/* NOTE: the real RX hand-off is not here - mt76_rx() (mtk_port_funcs.c) */
/* forwards frames to mtk_net80211_rx() (mtk_net80211.c) which builds  */
/* the mbuf and calls ieee80211_inputm().  These mac80211 entry points */
/* remain as drop stubs for code paths that still call them.           */
/* ================================================================== */
void ieee80211_rx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
    (void)hw;
    kfree_skb(skb);
}

void ieee80211_rx_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb)
{
    ieee80211_rx(hw, skb);
}

void ieee80211_rx_napi(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
                       struct sk_buff *skb, struct napi_struct *napi)
{
    (void)sta; (void)napi;
    ieee80211_rx(hw, skb);
}

void ieee80211_tx_status(struct ieee80211_hw *hw, struct sk_buff *skb)
{
    (void)hw;
    /* Completion accounting happens in mtk_skb_release(). */
    kfree_skb(skb);
}

void ieee80211_tx_status_ext(struct ieee80211_hw *hw,
                             struct ieee80211_tx_status *status)
{
    struct ieee80211_tx_info *info;
    struct sk_buff *skb;

    (void)hw;
    if (!status || !status->skb)
        return;
    skb = status->skb;

    /*
     * M5: fold the TXS result into the skb so the net80211 completion
     * (mtk_skb_release -> mtk_net80211_tx_done) can account for it.
     * 802.11 ACK/retry is handled by the firmware; net80211 only frees
     * and counts.  status->info is set only for TXS-tracked frames
     * (IEEE80211_TX_CTL_REQ_TX_STATUS): ACK or failed there, silent
     * success otherwise.
     */
    info = status->info;
    if (info) {
        if (info->flags & IEEE80211_TX_STAT_ACK)
            skb->mtk_tx_status |= MTK_TX_DONE_ACKED;
        else
            skb->mtk_tx_status |= MTK_TX_DONE_FAILED;
    }
    kfree_skb(skb);
}

void ieee80211_free_txskb(struct ieee80211_hw *hw, struct sk_buff *skb)
{
    (void)hw;
    kfree_skb(skb);
}

void ieee80211_tx_rate_update(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                              struct ieee80211_sta *sta)
{
    (void)hw; (void)vif; (void)sta;
}

/* ================================================================== */
/* queue management                                                   */
/* ================================================================== */
void ieee80211_wake_queues(struct ieee80211_hw *hw)
{
    (void)hw;
}

void ieee80211_stop_queues(struct ieee80211_hw *hw)
{
    (void)hw;
}

void ieee80211_stop_queue(struct ieee80211_hw *hw, int queue)
{
    (void)hw; (void)queue;
}

void ieee80211_wake_queue(struct ieee80211_hw *hw, int queue)
{
    (void)hw; (void)queue;
}

struct sk_buff *ieee80211_tx_dequeue(struct ieee80211_hw *hw,
                                     struct ieee80211_txq *txq)
{
    /* TODO(M4): pull from net80211 TXQ */
    (void)hw; (void)txq;
    return NULL;
}

void ieee80211_schedule_txq(struct ieee80211_hw *hw, struct ieee80211_txq *txq)
{
    (void)hw; (void)txq;
}

void ieee80211_return_txq(struct ieee80211_hw *hw, struct ieee80211_txq *txq,
                          bool force)
{
    (void)hw; (void)txq; (void)force;
}

struct ieee80211_txq *ieee80211_next_txq(struct ieee80211_hw *hw, int ac)
{
    (void)hw; (void)ac;
    return NULL;
}

void ieee80211_txq_schedule_start(struct ieee80211_hw *hw, u8 ac)
{
    (void)hw; (void)ac;
}

void ieee80211_txq_schedule_end(struct ieee80211_hw *hw, u8 ac)
{
    (void)hw; (void)ac;
}

u32 ieee80211_txq_aql_pending(struct ieee80211_hw *hw, struct ieee80211_txq *txq)
{
    (void)hw; (void)txq;
    return 0;
}

/* ================================================================== */
/* iteration / lookup                                                 */
/* ================================================================== */
void ieee80211_iterate_active_interfaces(struct ieee80211_hw *hw,
                                         u32 iter_flags,
                                         void (*iter)(void *data, u8 *mac,
                                                      struct ieee80211_vif *vif),
                                         void *data)
{
    (void)hw; (void)iter_flags; (void)iter; (void)data;
    /* TODO(M4): iterate net80211 vifs */
}

void ieee80211_iterate_active_interfaces_atomic(struct ieee80211_hw *hw,
                                                u32 iter_flags,
                                                void (*iter)(void *data, u8 *mac,
                                                             struct ieee80211_vif *vif),
                                                void *data)
{
    ieee80211_iterate_active_interfaces(hw, iter_flags, iter, data);
}

void ieee80211_iterate_stations_atomic(struct ieee80211_hw *hw,
                                       void (*iter)(void *data,
                                                    struct ieee80211_sta *sta),
                                       void *data)
{
    (void)hw; (void)iter; (void)data;
    /* TODO(M4) */
}

void ieee80211_iter_keys(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                         void (*iter)(struct ieee80211_hw *hw,
                                      struct ieee80211_vif *vif,
                                      struct ieee80211_sta *sta,
                                      struct ieee80211_key_conf *key,
                                      void *data),
                         void *data)
{
    (void)hw; (void)vif; (void)iter; (void)data;
    /* TODO(M4) */
}

void ieee80211_iter_keys_rcu(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                             void (*iter)(struct ieee80211_hw *hw,
                                          struct ieee80211_vif *vif,
                                          struct ieee80211_sta *sta,
                                          struct ieee80211_key_conf *key,
                                          void *data),
                             void *data)
{
    ieee80211_iter_keys(hw, vif, iter, data);
}

u32 ieee80211_calc_tx_airtime(struct ieee80211_hw *hw,
                              struct ieee80211_tx_info *info, int len)
{
    /* TODO(M2): real per-rate airtime accounting.  A byte-count based
     * placeholder keeps the mt76x02 ED/airtime accounting running. */
    (void)hw;
    (void)info;
    return (u32)(len * 8 / 6);
}

/*
 * Ported for mt76wm (XNU): faithful port of Linux mt76/mac80211.c
 * __mt76_sta_remove() (mac80211.c is not ported as a whole); used by the
 * mt76x02 key-sync path.
 */
void __mt76_sta_remove(struct mt76_phy *phy, struct ieee80211_vif *vif,
                       struct ieee80211_sta *sta)
{
    struct mt76_dev *dev = phy->dev;
    struct mt76_wcid *wcid = (struct mt76_wcid *)sta->drv_priv;
    int i, idx = wcid->idx;

    for (i = 0; i < ARRAY_SIZE(wcid->aggr); i++)
        mt76_rx_aggr_stop(dev, wcid, i);

    if (dev->drv->sta_remove)
        dev->drv->sta_remove(dev, vif, sta);

    mt76_wcid_cleanup(dev, wcid);

    mt76_wcid_mask_clear(dev->wcid_mask, idx);
    phy->num_sta--;
}

struct ieee80211_sta *ieee80211_find_sta(struct ieee80211_vif *vif,
                                         const u8 *addr)
{
    (void)vif; (void)addr;
    /* TODO(M4) */
    return NULL;
}

void ieee80211_get_tx_rates(struct ieee80211_vif *vif, struct ieee80211_sta *sta,
                            struct sk_buff *skb,
                            struct ieee80211_tx_rate *dest, int count)
{
    (void)vif; (void)sta; (void)skb;
    if (count > 0) {
        dest[0].idx = 0;
        dest[0].count = 1;
        dest[0].flags = 0;
    }
}

/* ================================================================== */
/* MLME notifications (no-ops until net80211 wiring)                  */
/* ================================================================== */
void ieee80211_scan_completed(struct ieee80211_hw *hw,
                              struct cfg80211_scan_info *info)
{
    (void)hw; (void)info;
}

void ieee80211_ready_on_channel(struct ieee80211_hw *hw) { (void)hw; }
void ieee80211_ready_on_channel_expired(struct ieee80211_hw *hw) { (void)hw; }
void ieee80211_sta_eosp(struct ieee80211_sta *sta) { (void)sta; }

void ieee80211_sta_register_airtime(struct ieee80211_sta *sta, u8 tid,
                                    u32 tx_airtime, u32 rx_airtime)
{
    (void)sta; (void)tid; (void)tx_airtime; (void)rx_airtime;
}

void ieee80211_send_bar(struct ieee80211_vif *vif, u8 *ra, u16 tid, u16 ssn)
{
    (void)vif; (void)ra; (void)tid; (void)ssn;
}

int ieee80211_start_tx_ba_session(struct ieee80211_sta *sta, u16 tid,
                                  u16 timeout)
{
    (void)sta; (void)tid; (void)timeout;
    return 0;
}

int ieee80211_stop_tx_ba_session(struct ieee80211_sta *sta, u16 tid)
{
    (void)sta; (void)tid;
    return 0;
}

void ieee80211_refresh_tx_agg_session_timer(struct ieee80211_sta *sta, u16 tid)
{
    (void)sta; (void)tid;
}

void ieee80211_beacon_loss(struct ieee80211_vif *vif) { (void)vif; }
void ieee80211_connection_loss(struct ieee80211_vif *vif) { (void)vif; }
void ieee80211_disconnect(struct ieee80211_vif *vif, bool reconnect)
{
    (void)vif; (void)reconnect;
}

void ieee80211_cqm_rssi_notify(struct ieee80211_vif *vif,
                               enum nl80211_cqm_rssi_threshold_event rssi_event,
                               s32 rssi_level, gfp_t gfp)
{
    (void)vif; (void)rssi_event; (void)rssi_level; (void)gfp;
}

struct sk_buff *ieee80211_beacon_get_template(struct ieee80211_hw *hw,
                                              struct ieee80211_vif *vif,
                                              struct ieee80211_mutable_offsets *offs,
                                              int link_id)
{
    (void)hw; (void)vif; (void)link_id;
    if (offs)
        bzero(offs, sizeof(*offs));
    /* TODO(M4): build the beacon from net80211 */
    return NULL;
}

/* ================================================================== */
/* chanctx emulation (used by mt792x_get_mac80211_ops)                */
/* ================================================================== */
int ieee80211_emulate_add_chanctx(struct ieee80211_hw *hw,
                                  struct ieee80211_chanctx_conf *conf)
{
    (void)hw; (void)conf;
    return 0;
}

void ieee80211_emulate_remove_chanctx(struct ieee80211_hw *hw,
                                      struct ieee80211_chanctx_conf *conf)
{
    (void)hw; (void)conf;
}

void ieee80211_emulate_change_chanctx(struct ieee80211_hw *hw,
                                      struct ieee80211_chanctx_conf *conf,
                                      u32 changed)
{
    (void)hw; (void)conf; (void)changed;
}

int ieee80211_emulate_switch_vif_chanctx(struct ieee80211_hw *hw,
                                         struct ieee80211_vif_chanctx_switch *vifs,
                                         int n_vifs,
                                         enum ieee80211_chanctx_switch_mode mode)
{
    (void)hw; (void)vifs; (void)n_vifs; (void)mode;
    return 0;
}

/* ================================================================== */
/* work queue helpers                                                 */
/* ================================================================== */
void ieee80211_queue_work(struct ieee80211_hw *hw, struct work_struct *work)
{
    (void)hw;
    mtk_queue_work(work);
}

void ieee80211_queue_delayed_work(struct ieee80211_hw *hw,
                                  struct delayed_work *dwork,
                                  unsigned long delay)
{
    (void)hw;
    mtk_queue_delayed_work(dwork, delay);
}

/* ================================================================== */
/* regulatory / cfg80211 helpers                                      */
/* ================================================================== */
int regulatory_hint(struct wiphy *wiphy, const char *alpha2)
{
    /* TODO(M4): regulatory handling */
    (void)wiphy; (void)alpha2;
    return 0;
}

int regulatory_set_wiphy_regd(struct wiphy *wiphy,
                              struct ieee80211_regdomain *rd)
{
    (void)wiphy; (void)rd;
    return 0;
}

int regulatory_set_wiphy_regd_sync_rtnl(struct wiphy *wiphy,
                                        struct ieee80211_regdomain *rd)
{
    return regulatory_set_wiphy_regd(wiphy, rd);
}

void cfg80211_chandef_create(struct cfg80211_chan_def *chandef,
                             struct ieee80211_channel *chan,
                             enum nl80211_chan_width width)
{
    if (!chandef)
        return;
    chandef->chan = chan;
    chandef->width = width;
    chandef->center_freq1 = chan ? chan->center_freq : 0;
    chandef->center_freq2 = 0;
}

bool cfg80211_chandef_valid(const struct cfg80211_chan_def *chandef)
{
    return chandef && chandef->chan;
}

bool cfg80211_reg_can_beacon(struct wiphy *wiphy,
                             struct cfg80211_chan_def *chandef,
                             enum nl80211_iftype iftype)
{
    (void)wiphy; (void)chandef; (void)iftype;
    return true;
}
