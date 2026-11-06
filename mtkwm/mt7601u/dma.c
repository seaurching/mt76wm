// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 *
 * dma.c - MT7601U USB bulk datapath (port for mt76wm/XNU).  The Linux
 * mac80211 RX delivery (ieee80211_rx_list/netif_receive_skb_list) is
 * replaced by the family-independent glue entry mtk_net80211_rx() /
 * mtk_net80211_rx_flush() (glue/mtk_api.h).  TX keeps the Linux URB
 * queue model on top of the URB emulation (usb.c).
 */

#include "mt7601u.h"
#include "dma.h"
#include "usb.h"
#include "trace.h"
#include "../glue/mtk_api.h"

static int mt7601u_submit_rx_buf(struct mt7601u_dev *dev,
                 struct mt7601u_dma_buf_rx *e, gfp_t gfp);

static unsigned int ieee80211_get_hdrlen_from_buf(const u8 *data, unsigned len)
{
    const struct ieee80211_hdr *hdr = (const struct ieee80211_hdr *)data;
    unsigned int hdrlen;

    if (unlikely(len < 10))
        return 0;
    hdrlen = ieee80211_hdrlen(hdr->frame_control);
    if (unlikely(hdrlen > len))
        return 0;
    return hdrlen;
}

/* Rate decode for the RX metadata (Linux mt76_mac_process_rate). */
static void mt7601u_fill_rx_meta_rate(struct mtk_rx_meta *meta, u16 rate)
{
    u8 idx = FIELD_GET(MT_RXWI_RATE_MCS, rate);

    meta->enc_flags = 0;
    meta->bw = 0;
    meta->nss = 1;
    meta->rate_idx = idx;

    switch (FIELD_GET(MT_RXWI_RATE_PHY, rate)) {
    case MT_PHY_TYPE_OFDM:
        if (idx >= 8)
            idx = 0;
        meta->rate_idx = idx + 4;
        return;
    case MT_PHY_TYPE_CCK:
        if (idx >= 8) {
            idx -= 8;
            meta->enc_flags |= RX_ENC_FLAG_SHORTPRE;
        }
        if (idx >= 4)
            idx = 0;
        meta->rate_idx = idx;
        return;
    case MT_PHY_TYPE_HT_GF:
        meta->enc_flags |= RX_ENC_FLAG_HT_GF;
        fallthrough;
    case MT_PHY_TYPE_HT:
        meta->encoding = RX_ENC_HT;
        meta->rate_idx = idx;
        meta->nss = 1 + (idx >> 3);
        break;
    default:
        return;
    }

    if (rate & MT_RXWI_RATE_SGI)
        meta->enc_flags |= RX_ENC_FLAG_SHORT_GI;

    if (rate & MT_RXWI_RATE_STBC)
        meta->enc_flags |= 1 << RX_ENC_FLAG_STBC_SHIFT;

    if (rate & MT_RXWI_RATE_BW)
        meta->bw = 1;   /* RATE_INFO_BW_40 */
}

static void
mt7601u_rx_monitor_beacon(struct mt7601u_dev *dev, struct mt7601u_rxwi *rxwi,
              u16 rate, int rssi)
{
    dev->bcn_freq_off = rxwi->freq_off;
    dev->bcn_phy_mode = FIELD_GET(MT_RXWI_RATE_PHY, rate);
    ewma_rssi_add(&dev->avg_rssi, -rssi);
}

static int
mt7601u_rx_is_our_beacon(struct mt7601u_dev *dev, u8 *data)
{
    struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)data;

    return ieee80211_is_beacon(hdr->frame_control) &&
        ether_addr_equal(hdr->addr2, dev->ap_bssid);
}

/*
 * Parse one aggregated RX segment and hand the 802.11 MPDU to the
 * net80211 glue (mtk_net80211_rx is family-independent: it copies the
 * frame into an mbuf and queues it for if_input).
 */
static void mt7601u_rx_process_seg(struct mt7601u_dev *dev, u8 *data,
                       u32 seg_len, struct list_head *list)
{
    /* The RX path is serialized by the single rx tasklet; the scratch
     * buffer is only needed for L2PAD frames (2 pad bytes after the
     * 802.11 header have to be stripped). */
    static u8 scratch[4096];
    struct mt7601u_rxwi *rxwi;
    struct mtk_rx_meta meta;
    u32 fce_info, hdr_len = 0;
    u16 rate;
    int rssi;
    u8 *frame;
    u32 len;

    (void)list;

    /* DMA_INFO field at the beginning of the segment contains only some of
     * the information, we need to read the FCE descriptor from the end.
     */
    fce_info = get_unaligned_le32(data + seg_len - MT_FCE_INFO_LEN);
    seg_len -= MT_FCE_INFO_LEN;

    data += MT_DMA_HDR_LEN;
    seg_len -= MT_DMA_HDR_LEN;

    rxwi = (struct mt7601u_rxwi *) data;
    data += sizeof(struct mt7601u_rxwi);
    seg_len -= sizeof(struct mt7601u_rxwi);

    if (unlikely(rxwi->zero[0] || rxwi->zero[1] || rxwi->zero[2]))
        dev_err_once(dev->dev, "Error: RXWI zero fields are set\n");
    if (unlikely(FIELD_GET(MT_RXD_INFO_TYPE, fce_info)))
        dev_err_once(dev->dev, "Error: RX path seen a non-pkt urb\n");

    trace_mt_rx(dev, rxwi, fce_info);

    len = FIELD_GET(MT_RXWI_CTL_MPDU_LEN, rxwi->ctl);
    if (len < 10 || len > seg_len)
        goto bad_frame;

    memset(&meta, 0, sizeof(meta));
    rate = le16_to_cpu(rxwi->rate);

    if (rxwi->rxinfo & cpu_to_le32(MT_RXINFO_DECRYPT)) {
        meta.flags |= MTK_RX_FLAG_DECRYPTED |
                  MTK_RX_FLAG_MMIC_STRIPPED |
                  MTK_RX_FLAG_MIC_STRIPPED;
        meta.flags |= MTK_RX_FLAG_IV_STRIPPED;
    }
    /* let mac80211/net80211 take care of PN validation since apparently
     * the hardware does not support it
     */
    if (rxwi->rxinfo & cpu_to_le32(MT_RXINFO_PN_LEN))
        meta.flags &= ~MTK_RX_FLAG_IV_STRIPPED;

    meta.chains = 1;
    meta.chain_signal[0] = meta.rssi_dbm =
        mt7601u_phy_get_rssi(dev, rxwi, rate);
    meta.freq = dev->chandef.chan->center_freq;
    mt7601u_fill_rx_meta_rate(&meta, rate);

    frame = data;
    if (rxwi->rxinfo & cpu_to_le32(MT_RXINFO_L2PAD)) {
        hdr_len = ieee80211_get_hdrlen_from_buf(data, len);
        if (!hdr_len || hdr_len + 2 > len)
            goto bad_frame;
        memcpy(scratch, data, hdr_len);
        memcpy(scratch + hdr_len, data + hdr_len + 2, len - hdr_len - 2);
        frame = scratch;
        len -= 2;
    }

    rssi = meta.rssi_dbm;
    spin_lock_bh(&dev->con_mon_lock);
    if (mt7601u_rx_is_our_beacon(dev, frame))
        mt7601u_rx_monitor_beacon(dev, rxwi, rate, rssi);
    else if (rxwi->rxinfo & cpu_to_le32(MT_RXINFO_U2M))
        ewma_rssi_add(&dev->avg_rssi, -rssi);
    spin_unlock_bh(&dev->con_mon_lock);

    mtk_net80211_rx(dev, frame, len, &meta);
    return;

bad_frame:
    dev_err_ratelimited(dev->dev, "Error: incorrect frame len:%u hdr:%u\n",
                len, hdr_len);
}

static u16 mt7601u_rx_next_seg_len(u8 *data, u32 data_len)
{
    u32 min_seg_len = MT_DMA_HDR_LEN + MT_RX_INFO_LEN +
        sizeof(struct mt7601u_rxwi) + MT_FCE_INFO_LEN;
    u16 dma_len = get_unaligned_le16(data);

    if (data_len < min_seg_len ||
        WARN_ON_ONCE(!dma_len) ||
        WARN_ON_ONCE(dma_len + MT_DMA_HDRS > data_len) ||
        WARN_ON_ONCE(dma_len & 0x3) ||
        WARN_ON_ONCE(dma_len < min_seg_len))
        return 0;

    return MT_DMA_HDRS + dma_len;
}

static void
mt7601u_rx_process_entry(struct mt7601u_dev *dev, struct mt7601u_dma_buf_rx *e)
{
    u32 seg_len, data_len = e->urb->actual_length;
    u8 *data = (u8 *)page_address(e->p);
    int cnt = 0;

    if (!test_bit(MT7601U_STATE_INITIALIZED, &dev->state))
        return;

    while ((seg_len = mt7601u_rx_next_seg_len(data, data_len))) {
        mt7601u_rx_process_seg(dev, data, seg_len, NULL);

        data_len -= seg_len;
        data += seg_len;
        cnt++;
    }

    if (cnt > 1)
        trace_mt_rx_dma_aggr(dev, cnt, 0);

    /* flush the per-batch mbuf list into the stack */
    mtk_net80211_rx_flush(dev);
}

static struct mt7601u_dma_buf_rx *
mt7601u_rx_get_pending_entry(struct mt7601u_dev *dev)
{
    struct mt7601u_rx_queue *q = &dev->rx_q;
    struct mt7601u_dma_buf_rx *buf = NULL;
    unsigned long flags;

    spin_lock_irqsave(&dev->rx_lock, flags);

    if (!q->pending)
        goto out;

    buf = &q->e[q->start];
    q->pending--;
    q->start = (q->start + 1) % q->entries;
out:
    spin_unlock_irqrestore(&dev->rx_lock, flags);

    return buf;
}

static void mt7601u_complete_rx(struct urb *urb)
{
    struct mt7601u_dev *dev = (struct mt7601u_dev *)urb->context;
    struct mt7601u_rx_queue *q = &dev->rx_q;
    unsigned long flags;

    /* do no schedule rx tasklet if urb has been unlinked
     * or the device has been removed
     */
    switch (urb->status) {
    case -ECONNRESET:
    case -ESHUTDOWN:
    case -ENOENT:
    case -EPROTO:
        return;
    default:
        dev_err_ratelimited(dev->dev, "rx urb failed: %d\n",
                    urb->status);
        fallthrough;
    case 0:
        break;
    }

    spin_lock_irqsave(&dev->rx_lock, flags);
    if (WARN_ONCE(q->e[q->end].urb != urb, "RX urb mismatch"))
        goto out;

    q->end = (q->end + 1) % q->entries;
    q->pending++;
    tasklet_schedule(&dev->rx_tasklet);
out:
    spin_unlock_irqrestore(&dev->rx_lock, flags);
}

static void mt7601u_rx_tasklet(struct tasklet_struct *t)
{
    struct mt7601u_dev *dev = (struct mt7601u_dev *)from_tasklet(dev, t, rx_tasklet);
    struct mt7601u_dma_buf_rx *e;

    while ((e = mt7601u_rx_get_pending_entry(dev))) {
        if (e->urb->status)
            continue;

        mt7601u_rx_process_entry(dev, e);
        mt7601u_submit_rx_buf(dev, e, GFP_ATOMIC);
    }
}

static void mt7601u_complete_tx(struct urb *urb)
{
    struct mt7601u_tx_queue *q = (struct mt7601u_tx_queue *)urb->context;
    struct mt7601u_dev *dev = q->dev;
    struct sk_buff *skb;
    unsigned long flags;

    switch (urb->status) {
    case -ECONNRESET:
    case -ESHUTDOWN:
    case -ENOENT:
    case -EPROTO:
        return;
    default:
        dev_err_ratelimited(dev->dev, "tx urb failed: %d\n",
                    urb->status);
        fallthrough;
    case 0:
        break;
    }

    spin_lock_irqsave(&dev->tx_lock, flags);
    if (WARN_ONCE(q->e[q->start].urb != urb, "TX urb mismatch"))
        goto out;

    skb = q->e[q->start].skb;
    q->e[q->start].skb = NULL;
    trace_mt_tx_dma_done(dev, skb);

    __skb_queue_tail(&dev->tx_skb_done, skb);
    tasklet_schedule(&dev->tx_tasklet);

    if (q->used == q->entries - q->entries / 8)
        ieee80211_wake_queue(dev->hw, skb_get_queue_mapping(skb));

    q->start = (q->start + 1) % q->entries;
    q->used--;
out:
    spin_unlock_irqrestore(&dev->tx_lock, flags);
}

static void mt7601u_tx_tasklet(struct tasklet_struct *t)
{
    struct mt7601u_dev *dev = (struct mt7601u_dev *)from_tasklet(dev, t, tx_tasklet);
    struct sk_buff_head skbs;
    unsigned long flags;

    __skb_queue_head_init(&skbs);

    spin_lock_irqsave(&dev->tx_lock, flags);

    set_bit(MT7601U_STATE_MORE_STATS, &dev->state);
    if (!test_and_set_bit(MT7601U_STATE_READING_STATS, &dev->state))
        queue_delayed_work(dev->stat_wq, &dev->stat_work,
                       msecs_to_jiffies(10));

    skb_queue_splice_tail_init(&dev->tx_skb_done, &skbs);

    spin_unlock_irqrestore(&dev->tx_lock, flags);

    while (!skb_queue_empty(&skbs)) {
        struct sk_buff *skb = __skb_dequeue(&skbs);

        mt7601u_tx_status(dev, skb);
    }
}

static int mt7601u_dma_submit_tx(struct mt7601u_dev *dev,
                 struct sk_buff *skb, u8 ep)
{
    struct usb_device *usb_dev = mt7601u_to_usb_dev(dev);
    unsigned snd_pipe = usb_sndbulkpipe(usb_dev, dev->out_eps[ep]);
    struct mt7601u_dma_buf_tx *e;
    struct mt7601u_tx_queue *q = &dev->tx_q[ep];
    unsigned long flags;
    int ret;

    spin_lock_irqsave(&dev->tx_lock, flags);

    if (WARN_ON(q->entries <= q->used)) {
        ret = -ENOSPC;
        goto out;
    }

    e = &q->e[q->end];
    usb_fill_bulk_urb(e->urb, usb_dev, snd_pipe, skb->data, skb->len,
              mt7601u_complete_tx, q);
    ret = usb_submit_urb(e->urb, GFP_ATOMIC);
    if (ret) {
        /* Special-handle ENODEV from TX urb submission because it will
         * often be the first ENODEV we see after device is removed.
         */
        if (ret == -ENODEV)
            set_bit(MT7601U_STATE_REMOVED, &dev->state);
        else
            dev_err(dev->dev, "Error: TX urb submit failed:%d\n",
                ret);
        goto out;
    }

    q->end = (q->end + 1) % q->entries;
    q->used++;
    e->skb = skb;

    if (q->used >= q->entries)
        ieee80211_stop_queue(dev->hw, skb_get_queue_mapping(skb));
out:
    spin_unlock_irqrestore(&dev->tx_lock, flags);

    return ret;
}

/* Map hardware Q to USB endpoint number */
static u8 q2ep(u8 qid)
{
    /* TODO: take management packets to queue 5 */
    return qid + 1;
}

/* Map USB endpoint number to Q id in the DMA engine */
static enum mt76_qsel ep2dmaq(u8 ep)
{
    if (ep == 5)
        return MT_QSEL_MGMT;
    return MT_QSEL_EDCA;
}

int mt7601u_dma_enqueue_tx(struct mt7601u_dev *dev, struct sk_buff *skb,
               struct mt76_wcid *wcid, int hw_q)
{
    u8 ep = q2ep(hw_q);
    u32 dma_flags;
    int ret;

    dma_flags = MT_TXD_PKT_INFO_80211;
    if (wcid->hw_key_idx == 0xff)
        dma_flags |= MT_TXD_PKT_INFO_WIV;

    ret = mt7601u_dma_skb_wrap_pkt(skb, ep2dmaq(ep), dma_flags);
    if (ret)
        return ret;

    ret = mt7601u_dma_submit_tx(dev, skb, ep);
    if (ret) {
        ieee80211_free_txskb(dev->hw, skb);
        return ret;
    }

    return 0;
}

static void mt7601u_kill_rx(struct mt7601u_dev *dev)
{
    int i;

    for (i = 0; i < dev->rx_q.entries; i++)
        usb_poison_urb(dev->rx_q.e[i].urb);
}

static int mt7601u_submit_rx_buf(struct mt7601u_dev *dev,
                 struct mt7601u_dma_buf_rx *e, gfp_t gfp)
{
    struct usb_device *usb_dev = mt7601u_to_usb_dev(dev);
    u8 *buf = (u8 *)page_address(e->p);
    unsigned pipe;
    int ret;

    pipe = usb_rcvbulkpipe(usb_dev, dev->in_eps[MT_EP_IN_PKT_RX]);

    usb_fill_bulk_urb(e->urb, usb_dev, pipe, buf, MT_RX_URB_SIZE,
              mt7601u_complete_rx, dev);

    trace_mt_submit_urb(dev, e->urb);
    ret = usb_submit_urb(e->urb, gfp);
    if (ret)
        dev_err(dev->dev, "Error: submit RX URB failed:%d\n", ret);

    return ret;
}

static int mt7601u_submit_rx(struct mt7601u_dev *dev)
{
    int i, ret;

    for (i = 0; i < dev->rx_q.entries; i++) {
        ret = mt7601u_submit_rx_buf(dev, &dev->rx_q.e[i], GFP_KERNEL);
        if (ret)
            return ret;
    }

    return 0;
}

static void mt7601u_free_rx(struct mt7601u_dev *dev)
{
    int i;

    for (i = 0; i < dev->rx_q.entries; i++) {
        __free_pages(dev->rx_q.e[i].p, MT_RX_ORDER);
        usb_free_urb(dev->rx_q.e[i].urb);
    }
}

static int mt7601u_alloc_rx(struct mt7601u_dev *dev)
{
    int i;

    memset(&dev->rx_q, 0, sizeof(dev->rx_q));
    dev->rx_q.dev = dev;
    dev->rx_q.entries = N_RX_ENTRIES;

    for (i = 0; i < N_RX_ENTRIES; i++) {
        dev->rx_q.e[i].urb = usb_alloc_urb(0, GFP_KERNEL);
        dev->rx_q.e[i].p = dev_alloc_pages(MT_RX_ORDER);

        if (!dev->rx_q.e[i].urb || !dev->rx_q.e[i].p)
            return -ENOMEM;
    }

    return 0;
}

static void mt7601u_free_tx_queue(struct mt7601u_tx_queue *q)
{
    int i;

    for (i = 0; i < q->entries; i++)  {
        usb_poison_urb(q->e[i].urb);
        if (q->e[i].skb)
            mt7601u_tx_status(q->dev, q->e[i].skb);
        usb_free_urb(q->e[i].urb);
    }
}

static void mt7601u_free_tx(struct mt7601u_dev *dev)
{
    int i;

    if (!dev->tx_q)
        return;

    for (i = 0; i < __MT_EP_OUT_MAX; i++)
        mt7601u_free_tx_queue(&dev->tx_q[i]);
}

static int mt7601u_alloc_tx_queue(struct mt7601u_dev *dev,
                  struct mt7601u_tx_queue *q)
{
    int i;

    q->dev = dev;
    q->entries = N_TX_ENTRIES;

    for (i = 0; i < N_TX_ENTRIES; i++) {
        q->e[i].urb = usb_alloc_urb(0, GFP_KERNEL);
        if (!q->e[i].urb)
            return -ENOMEM;
    }

    return 0;
}

static int mt7601u_alloc_tx(struct mt7601u_dev *dev)
{
    int i;

    dev->tx_q = (struct mt7601u_tx_queue *)devm_kcalloc(dev->dev,
                         __MT_EP_OUT_MAX,
                         sizeof(*dev->tx_q), GFP_KERNEL);
    if (!dev->tx_q)
        return -ENOMEM;

    for (i = 0; i < __MT_EP_OUT_MAX; i++)
        if (mt7601u_alloc_tx_queue(dev, &dev->tx_q[i]))
            return -ENOMEM;

    return 0;
}

int mt7601u_dma_init(struct mt7601u_dev *dev)
{
    int ret;

    tasklet_setup(&dev->tx_tasklet, mt7601u_tx_tasklet);
    tasklet_setup(&dev->rx_tasklet, mt7601u_rx_tasklet);

    ret = mt7601u_alloc_tx(dev);
    if (ret)
        goto err;
    ret = mt7601u_alloc_rx(dev);
    if (ret)
        goto err;

    ret = mt7601u_submit_rx(dev);
    if (ret)
        goto err;

    return 0;
err:
    mt7601u_dma_cleanup(dev);
    return ret;
}

void mt7601u_dma_cleanup(struct mt7601u_dev *dev)
{
    mt7601u_kill_rx(dev);

    tasklet_kill(&dev->rx_tasklet);

    mt7601u_free_rx(dev);
    mt7601u_free_tx(dev);

    tasklet_kill(&dev->tx_tasklet);
}
