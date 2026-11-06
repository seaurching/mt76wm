/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat page_pool for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_PAGE_POOL_HELPERS_H
#define MTK_LC_PAGE_POOL_HELPERS_H

#include <linux/mtk_core.h>
#include <linux/dma-mapping.h>

/*
 * M6: page_pool is implemented as a linear DMA-capable buffer allocator.
 *
 * Each RX buffer is one physically contiguous, DMA-reachable block
 * (IOBufferMemoryDescriptor + IODMACommand, same pattern as
 * hal_mt76/MtkDma.cpp).  The block layout is
 *
 *     [ struct page (64-byte aligned header) | payload (size bytes) ]
 *
 * so virt_to_head_page() recovers the struct page from the payload
 * pointer by subtracting MTK_PP_HDR_SIZE - no lookup table needed.  The
 * struct page carries the DMA address of the payload and the owning
 * pool; freed buffers recycle onto a per-pool freelist.
 *
 * The implementations live in glue/mtk_glue.c.
 */

struct page_pool;   /* opaque (defined in glue/mtk_glue.c) */

struct page {
    void *virt;             /* payload VA (block base + MTK_PP_HDR_SIZE)   */
    dma_addr_t dma_addr;    /* DMA (IOVM) address of the payload          */
    struct page_pool *pp;   /* owning pool (for recycle)                  */
    /* allocator bookkeeping (kept here so the block is self-describing) */
    void *bmd;              /* IOBufferMemoryDescriptor *                 */
    void *cmd;              /* IODMACommand *                             */
    dma_addr_t alloc_dma;   /* DMA address of the block base              */
    unsigned long alloc_size;
    struct page *next;      /* pool freelist link                         */
};

/* Payload offset inside a pool block: keep it 64-byte aligned. */
#define MTK_PP_HDR_SIZE 64

/* struct page lives at the head of every pool block: it must fit. */
typedef char mtk_pp_hdr_size_check[(sizeof(struct page) <= MTK_PP_HDR_SIZE)
                                   ? 1 : -1];

struct page_pool_params {
    unsigned int order;
    unsigned int flags;
    int nid;
    struct device *dev;
    enum dma_data_direction dma_dir;
    unsigned int max_len;
    unsigned int offset;
    unsigned int pool_size;
    void *netdev;
    void *napi;
};

#define PP_FLAG_DMA_MAP     0x1
#define PP_FLAG_DMA_SYNC_DEV 0x2

/* Out-of-line allocator (glue/mtk_glue.c). */
struct page_pool *mtk_page_pool_create(unsigned int pool_size,
                                       unsigned int max_len);
void mtk_page_pool_destroy(struct page_pool *pp);
struct page *mtk_page_pool_alloc(struct page_pool *pp, unsigned int size);
void mtk_page_pool_recycle_page(struct page *page);
void mtk_page_pool_recycle_buf(void *buf);

static inline struct page_pool *
page_pool_create(const struct page_pool_params *params)
{
    if (!params)
        return NULL;
    return mtk_page_pool_create(params->pool_size, params->max_len);
}

static inline void page_pool_destroy(struct page_pool *pool)
{
    mtk_page_pool_destroy(pool);
}

static inline void page_pool_put_full_page(struct page_pool *pool,
                                           struct page *page, bool allow_direct)
{
    (void)pool; (void)allow_direct;   /* pool is carried by the page */
    if (page)
        mtk_page_pool_recycle_page(page);
}

static inline struct page *page_pool_alloc_frag(struct page_pool *pool,
                                                unsigned int *offset,
                                                unsigned int size, gfp_t gfp)
{
    struct page *page;

    (void)gfp;
    if (offset)
        *offset = 0;    /* buffers are standalone blocks: always aligned */
    page = mtk_page_pool_alloc(pool, size);
    return page;
}

static inline dma_addr_t page_pool_get_dma_addr(const struct page *page)
{
    return page ? page->dma_addr : 0;
}

static inline enum dma_data_direction
page_pool_get_dma_dir(const struct page_pool *pool)
{
    (void)pool;
    return DMA_FROM_DEVICE;
}

/* netmem descriptor shim: pp_page_to_nmdesc(page)->pp == page->pp */
struct nmdesc {
    struct page_pool *pp;
};

static inline struct nmdesc *pp_page_to_nmdesc(struct page *page)
{
    return (struct nmdesc *)&page->pp;
}

static inline struct page *virt_to_head_page(const void *addr)
{
    return (struct page *)((u8 *)addr - MTK_PP_HDR_SIZE);
}

static inline char *page_address(const struct page *page)
{
    return page ? (char *)page->virt : NULL;
}

/*
 * Simple non-DMA page allocator for driver-side scratch buffers
 * (Linux dev_alloc_pages()/put_page() subset, order-sized).
 */
static inline struct page *dev_alloc_pages(unsigned int order)
{
    unsigned long size = (unsigned long)PAGE_SIZE << order;
    struct page *page;

    page = (struct page *)kzalloc(sizeof(*page), 0);
    if (!page)
        return NULL;
    page->virt = kzalloc(size, 0);
    if (!page->virt) {
        kfree(page);
        return NULL;
    }
    page->alloc_size = size;
    return page;
}

static inline void put_page(struct page *page)
{
    if (!page)
        return;
    if (page->bmd || page->pp) {
        /* pool-owned page: recycle it */
        mtk_page_pool_recycle_page(page);
        return;
    }
    kfree(page->virt);
    kfree(page);
}

static inline int page_pool_ethtool_stats_get_count(void)
{
    return 0;
}

static inline void page_pool_ethtool_stats_get_strings(u8 *data)
{
    (void)data;
}

static inline void page_pool_ethtool_stats_get(struct page_pool *pool,
                                               u64 *data)
{
    (void)pool; (void)data;
}

#endif
