/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux compat sk_buff subset for the mt76wm XNU port.
 * Ported for mt76wm (XNU) - minimal usable implementation (M1).
 *
 * skb layout: an IOMalloc'ed sk_buff head plus a separate IOMalloc'ed
 * data buffer (head .. end).  Frags are kept in a small fixed array so
 * the RX aggregation paths compile and run with linear buffers.
 */
#ifndef MTK_LC_SKBUFF_H
#define MTK_LC_SKBUFF_H

#include <linux/mtk_core.h>
#include <linux/slab.h>
#include <IOKit/IOLib.h>
#include "../../mtk_api.h"     /* struct mtk_tx_cookie (TX completion) */

#define SKB_DATA_ALIGN(X)   ALIGN(X, 64)
#define SKB_WITH_OVERHEAD(X) ((X) - SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))
#define NET_SKB_PAD         32

#define MAX_SKB_FRAGS 16

struct skb_frag {
    void *page;
    u32 page_offset;
    u32 size;
};

struct skb_shared_info {
    u8 nr_frags;
    u8 tx_flags;
    u16 gso_size;
    u16 gso_segs;
    u16 gso_type;
    u32 tskey;
    unsigned int dataref;
    struct skb_frag frags[MAX_SKB_FRAGS];
    struct sk_buff *frag_list;
};

struct sk_buff {
    struct sk_buff *next;
    struct sk_buff *prev;
    struct list_head list;   /* for list_* based queues (mt76 free lists) */

    unsigned char *head;
    unsigned char *data;
    unsigned int tail;
    unsigned int end;
    unsigned int len;
    unsigned int data_len;
    unsigned int mac_len;
    unsigned int hdr_len;

    __be16 protocol;
    __be16 inner_protocol;
    u16 queue_mapping;
    u8 priority;
    u8 ip_summed;
    u8 pkt_type;
    u8 cloned:1, nohdr:1, peeked:1, head_frag:1;
    u32 hash;
    u32 csum;
    u32 tstamp;
    int napi_id;
    unsigned int users;

    void *dev;
    void *sk;

    /* control buffer: must be at least 48 bytes (IEEE80211_SKB_CB) */
    char cb[64];

    /* M5 TX bridge: net80211 completion cookie (glue/mtk_api.h).  Set on
     * TX hand-off; consumed exactly once by mtk_skb_release() which calls
     * mtk_net80211_tx_done(). */
    struct mtk_tx_cookie mtk_ck;
    unsigned int mtk_tx_status;     /* MTK_TX_DONE_* for the callback     */
};

struct sk_buff_head {
    struct sk_buff *next;
    struct sk_buff *prev;
    __u32 qlen;
    spinlock_t lock;
};

static inline struct skb_shared_info *skb_shinfo(const struct sk_buff *skb)
{
    return (struct skb_shared_info *)(skb->head + skb->end);
}

/* ---- allocation ---- */
/*
 * M7: TX buffers must be DMA-reachable.  The head data block of every
 * alloc_skb() is allocated from the glue's linear DMA buffer cache
 * (glue/mtk_glue.c: IOBufferMemoryDescriptor + IODMACommand blocks that
 * are registered in the VA->IOVA table), so mtk_dma_map_single() can
 * return the true bus address of skb->data for the WPDMA descriptors.
 * napi_build_skb() heads are page-pool payloads (RX) and are recycled
 * via the pool instead.  mtk_skb_release() frees each kind accordingly.
 */
void *mtk_skb_alloc_data(unsigned int size);
void mtk_skb_free_data(void *head);

static inline void skb_reserve(struct sk_buff *skb, int len)
{
    skb->data += len;
    skb->tail += len;
}

static inline struct sk_buff *alloc_skb(unsigned int size, gfp_t gfp)
{
    struct sk_buff *skb;
    unsigned int alloc = SKB_DATA_ALIGN(size) +
                         SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

    (void)gfp;
    skb = (struct sk_buff *)kzalloc(sizeof(*skb), 0);
    if (!skb)
        return NULL;
    skb->head = (unsigned char *)mtk_skb_alloc_data(alloc);
    if (!skb->head) {
        kfree(skb);
        return NULL;
    }
    skb->data = skb->head;
    skb->tail = 0;
    skb->end = SKB_DATA_ALIGN(size);
    skb->users = 1;
    skb->priority = 0;
    bzero(skb->cb, sizeof(skb->cb));
    return skb;
}

static inline struct sk_buff *__dev_alloc_skb(unsigned int size, gfp_t gfp)
{
    struct sk_buff *skb = alloc_skb(size + NET_SKB_PAD, gfp);

    if (skb)
        skb_reserve(skb, NET_SKB_PAD);
    return skb;
}

#define dev_alloc_skb(len) __dev_alloc_skb((len), GFP_ATOMIC)

static inline struct sk_buff *napi_build_skb(void *data, unsigned int frag_size)
{
    struct sk_buff *skb;
    struct skb_shared_info *sh;

    if (!data || frag_size <= SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))
        return NULL;
    skb = (struct sk_buff *)kzalloc(sizeof(*skb), 0);
    if (!skb)
        return NULL;
    skb->head = (unsigned char *)data;
    skb->data = (unsigned char *)data;
    skb->tail = 0;
    /* Linux layout: the shared info sits at the end of the fragment so
     * RX continuation fragments can be attached via skb_add_rx_frag().
     * The buffer is a recycled page-pool block: clear the shinfo. */
    skb->end = SKB_WITH_OVERHEAD(frag_size);
    skb->users = 1;
    skb->head_frag = 1;
    sh = skb_shinfo(skb);
    bzero(sh, sizeof(*sh));
    return skb;
}

static inline struct sk_buff *build_skb(void *data, unsigned int frag_size)
{
    return napi_build_skb(data, frag_size);
}

static inline struct sk_buff *netdev_alloc_skb(struct net_device *dev,
                                               unsigned int len)
{
    (void)dev;
    return __dev_alloc_skb(len, GFP_ATOMIC);
}

/* ---- free ---- */
void mtk_skb_release(struct sk_buff *skb);

static inline void kfree_skb(struct sk_buff *skb)
{
    if (skb)
        mtk_skb_release(skb);
}

#define dev_kfree_skb(skb)      kfree_skb(skb)
#define dev_kfree_skb_any(skb)  kfree_skb(skb)
#define dev_kfree_skb_irq(skb)  kfree_skb(skb)
#define consume_skb(skb)        kfree_skb(skb)
#define __kfree_skb(skb)        kfree_skb(skb)

static inline void napi_consume_skb(struct sk_buff *skb, int budget)
{
    (void)budget;
    kfree_skb(skb);
}

static inline struct sk_buff *skb_get(struct sk_buff *skb)
{
    skb->users++;
    return skb;
}

static inline bool skb_shared(const struct sk_buff *skb)
{
    return skb->users > 1;
}

static inline int skb_cloned(const struct sk_buff *skb)
{
    return skb->cloned;
}

/* ---- pointer helpers ---- */
static inline unsigned char *skb_put(struct sk_buff *skb, unsigned int len)
{
    /* tail is an offset from head (Linux layout), so the write pointer
     * is head + tail - NOT data + tail (which double-counts headroom
     * and misplaces every payload built after skb_reserve(), e.g. the
     * MCU command buffers in __mt76_mcu_msg_alloc()). */
    unsigned char *tail = skb->head + skb->tail;

    skb->tail += len;
    skb->len += len;
    return tail;
}

static inline unsigned char *__skb_put(struct sk_buff *skb, unsigned int len)
{
    return skb_put(skb, len);
}

static inline unsigned char *skb_push(struct sk_buff *skb, unsigned int len)
{
    skb->data -= len;
    skb->len += len;
    return skb->data;
}

static inline unsigned char *__skb_push(struct sk_buff *skb, unsigned int len)
{
    return skb_push(skb, len);
}

static inline unsigned char *skb_pull(struct sk_buff *skb, unsigned int len)
{
    unsigned char *p = skb->data;

    skb->data += len;
    skb->len -= len;
    return p;
}

static inline unsigned char *skb_pull_inline(struct sk_buff *skb,
                                             unsigned int len)
{
    return skb_pull(skb, len);
}

static inline unsigned char *__skb_pull(struct sk_buff *skb, unsigned int len)
{
    return skb_pull(skb, len);
}

static inline void skb_trim(struct sk_buff *skb, unsigned int len)
{
    skb->len = len;
    skb->tail = len;
}

/* Linux pskb_trim() returns -ENOMEM on failure; the stub never fails. */
static inline int pskb_trim(struct sk_buff *skb, unsigned int len)
{
    if (len < skb->len)
        skb_trim(skb, len);
    return 0;
}

static inline unsigned int skb_headroom(const struct sk_buff *skb)
{
    return skb->data - skb->head;
}

static inline unsigned int skb_tailroom(const struct sk_buff *skb)
{
    return skb->end - skb->tail;
}

static inline unsigned int skb_headlen(const struct sk_buff *skb)
{
    return skb->len - skb->data_len;
}

static inline int skb_is_nonlinear(const struct sk_buff *skb)
{
    return skb->data_len != 0;
}

static inline void skb_copy_to_linear_data(struct sk_buff *skb,
                                           const void *from, unsigned int len)
{
    memcpy(skb->data, from, len);
}

static inline void skb_copy_to_linear_data_offset(struct sk_buff *skb,
                                                  int offset, const void *from,
                                                  unsigned int len)
{
    memcpy(skb->data + offset, from, len);
}

static inline void skb_copy_from_linear_data(const struct sk_buff *skb,
                                             void *to, unsigned int len)
{
    memcpy(to, skb->data, len);
}

static inline void *skb_put_data(struct sk_buff *skb, const void *data,
                                 unsigned int len)
{
    void *p = skb_put(skb, len);

    memcpy(p, data, len);
    return p;
}

static inline void *skb_put_zero(struct sk_buff *skb, unsigned int len)
{
    void *p = skb_put(skb, len);

    bzero(p, len);
    return p;
}

static inline int skb_cow_head(struct sk_buff *skb, unsigned int headroom)
{
    (void)headroom;
    return 0;   /* buffers are private; nothing to do */
}

static inline int skb_cow(struct sk_buff *skb, unsigned int headroom)
{
    (void)headroom;
    return 0;
}

static inline int skb_put_padto(struct sk_buff *skb, unsigned int len)
{
    unsigned int size = skb->len;

    if (unlikely(size > len))
        return -EINVAL;
    if (likely(size < len))
        skb_put_zero(skb, len - size);
    return 0;
}

static inline int skb_linearize(struct sk_buff *skb)
{
    (void)skb;
    return 0;
}

static inline int pskb_expand_head(struct sk_buff *skb, int nhead, int ntail,
                                   gfp_t gfp)
{
    (void)skb; (void)nhead; (void)ntail; (void)gfp;
    return 0;
}

static inline int __skb_pad(struct sk_buff *skb, int pad, bool free_on_error)
{
    (void)skb; (void)pad; (void)free_on_error;
    return 0;
}

static inline int skb_pad(struct sk_buff *skb, int pad)
{
    (void)skb; (void)pad;
    return 0;
}

static inline void skb_add_rx_frag(struct sk_buff *skb, int i, void *page,
                                   int off, int size, unsigned int truesize)
{
    struct skb_shared_info *sh = skb_shinfo(skb);

    (void)truesize;
    if (i >= MAX_SKB_FRAGS)
        return;
    sh->frags[i].page = page;
    sh->frags[i].page_offset = off;
    sh->frags[i].size = size;
    if (i >= sh->nr_frags)
        sh->nr_frags = i + 1;
    skb->data_len += size;
    skb->len += size;
}

#define skb_walk_frags(skb, iter) \
    for ((iter) = skb_shinfo(skb)->frag_list; (iter); (iter) = (iter)->next)

static inline void skb_mark_for_recycle(struct sk_buff *skb) { (void)skb; }

static inline void skb_list_del_init(struct sk_buff *skb)
{
    if (skb->prev)
        skb->prev->next = skb->next;
    if (skb->next)
        skb->next->prev = skb->prev;
    skb->next = skb;
    skb->prev = skb;
}

static inline u16 skb_get_queue_mapping(const struct sk_buff *skb)
{
    return skb->queue_mapping;
}

static inline void skb_set_queue_mapping(struct sk_buff *skb, u16 qm)
{
    skb->queue_mapping = qm;
}

static inline void skb_orphan(struct sk_buff *skb)
{
    skb->sk = NULL;
}

/* ---- queues ---- */
static inline void __skb_queue_head_init(struct sk_buff_head *list)
{
    list->prev = (struct sk_buff *)list;
    list->next = (struct sk_buff *)list;
    list->qlen = 0;
}

static inline void skb_queue_head_init(struct sk_buff_head *list)
{
    spin_lock_init(&list->lock);
    __skb_queue_head_init(list);
}

static inline void __skb_insert(struct sk_buff *n, struct sk_buff *prev,
                                struct sk_buff *next,
                                struct sk_buff_head *list)
{
    n->next = next;
    n->prev = prev;
    next->prev = n;
    prev->next = n;
    list->qlen++;
}

static inline void __skb_queue_tail(struct sk_buff_head *list,
                                    struct sk_buff *n)
{
    __skb_insert(n, list->prev, (struct sk_buff *)list, list);
}

static inline void skb_queue_tail(struct sk_buff_head *list,
                                  struct sk_buff *n)
{
    spin_lock(&list->lock);
    __skb_queue_tail(list, n);
    spin_unlock(&list->lock);
}

static inline void __skb_queue_head(struct sk_buff_head *list,
                                    struct sk_buff *n)
{
    __skb_insert(n, (struct sk_buff *)list, list->next, list);
}

static inline void __skb_unlink(struct sk_buff *skb, struct sk_buff_head *list)
{
    struct sk_buff *next = skb->next;
    struct sk_buff *prev = skb->prev;

    if (prev)
        prev->next = next;
    if (next)
        next->prev = prev;
    skb->next = NULL;
    skb->prev = NULL;
    list->qlen--;
}

static inline struct sk_buff *__skb_dequeue(struct sk_buff_head *list)
{
    struct sk_buff *skb = list->next;

    if (skb == (struct sk_buff *)list)
        skb = NULL;
    else
        __skb_unlink(skb, list);
    return skb;
}

static inline struct sk_buff *skb_dequeue(struct sk_buff_head *list)
{
    struct sk_buff *skb;

    spin_lock(&list->lock);
    skb = __skb_dequeue(list);
    spin_unlock(&list->lock);
    return skb;
}

static inline struct sk_buff *skb_peek(const struct sk_buff_head *list)
{
    struct sk_buff *skb = list->next;

    return (skb == (struct sk_buff *)list) ? NULL : skb;
}

static inline struct sk_buff *skb_peek_tail(const struct sk_buff_head *list)
{
    struct sk_buff *skb = list->prev;

    return (skb == (struct sk_buff *)list) ? NULL : skb;
}

static inline int skb_queue_empty(const struct sk_buff_head *list)
{
    return list->next == (struct sk_buff *)list;
}

static inline unsigned int skb_queue_len(const struct sk_buff_head *list)
{
    return list->qlen;
}

static inline void skb_queue_purge(struct sk_buff_head *list)
{
    struct sk_buff *skb;

    while ((skb = skb_dequeue(list)) != NULL)
        kfree_skb(skb);
}

static inline void __skb_queue_purge(struct sk_buff_head *list)
{
    struct sk_buff *skb;

    while ((skb = __skb_dequeue(list)) != NULL)
        kfree_skb(skb);
}

static inline void skb_queue_splice_tail_init(struct sk_buff_head *from,
                                              struct sk_buff_head *to)
{
    struct sk_buff *skb;

    while ((skb = __skb_dequeue(from)) != NULL)
        __skb_queue_tail(to, skb);
}

#define skb_queue_walk_safe(list, skb, tmp)                    \
    for ((skb) = (list)->next, (tmp) = (skb) ? (skb)->next : NULL;    \
         (skb) != (struct sk_buff *)(list);                \
         (skb) = (tmp), (tmp) = (skb) ? (skb)->next : NULL)

#define skb_queue_walk(list, skb)                        \
    for ((skb) = (list)->next;                        \
         (skb) != (struct sk_buff *)(list);                \
         (skb) = (skb)->next)

/* ---- checksum stubs ---- */
#define CHECKSUM_NONE       0
#define CHECKSUM_UNNECESSARY 1
#define CHECKSUM_COMPLETE   2
#define CHECKSUM_PARTIAL    3

#define skb_checksum_none_assert(skb) do { } while (0)

/* ---- misc ---- */
static inline int skb_network_offset(const struct sk_buff *skb)
{
    return 0;
}
static inline void skb_reset_mac_header(struct sk_buff *skb) { (void)skb; }
static inline void skb_set_mac_header(struct sk_buff *skb, const int offset)
{
    (void)skb; (void)offset;
}
static inline void skb_reset_network_header(struct sk_buff *skb) { (void)skb; }
static inline void skb_reset_transport_header(struct sk_buff *skb) { (void)skb; }
static inline unsigned char *skb_mac_header(const struct sk_buff *skb)
{
    return skb->data;
}
static inline int skb_mac_header_was_set(const struct sk_buff *skb)
{
    return 1;
}

#define PKT_HOST        0
#define PKT_BROADCAST   1
#define PKT_MULTICAST   2
#define PKT_OTHERHOST   3

#endif /* MTK_LC_SKBUFF_H */
