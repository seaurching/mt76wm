/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat DMA API for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_DMA_MAPPING_H
#define MTK_LC_DMA_MAPPING_H

#include <linux/mtk_core.h>
#include <linux/slab.h>

/*
 * DMA allocations use IOBufferMemoryDescriptor (physically contiguous).
 * dma_addr_t (u64) holds the physical address.  Implementations live in
 * glue/mtk_glue.c.  M1: real allocation, no IOMMU.
 */
typedef u64 dma64_addr_t;

enum dma_data_direction {
    DMA_BIDIRECTIONAL = 0,
    DMA_TO_DEVICE = 1,
    DMA_FROM_DEVICE = 2,
    DMA_NONE = 3,
};

mtk_any_ptr mtk_dma_alloc_coherent(size_t size, dma_addr_t *dma_handle, gfp_t gfp);
void mtk_dma_free_coherent(void *vaddr, size_t size, dma_addr_t dma_handle);
dma_addr_t mtk_dma_map_single(void *vaddr, size_t size,
                              enum dma_data_direction dir);
void mtk_dma_unmap_single(dma_addr_t dma_addr, size_t size,
                          enum dma_data_direction dir);

#define dma_alloc_coherent(dev, size, handle, gfp) \
    mtk_dma_alloc_coherent((size), (handle), (gfp))
#define dma_free_coherent(dev, size, vaddr, handle) \
    mtk_dma_free_coherent((vaddr), (size), (handle))
#define dmam_alloc_coherent(dev, size, handle, gfp) \
    mtk_dma_alloc_coherent((size), (handle), (gfp))
#define dmam_free_coherent(dev, size, vaddr, handle) \
    mtk_dma_free_coherent((vaddr), (size), (handle))

#define dma_map_single(dev, addr, size, dir) \
    mtk_dma_map_single((addr), (size), (dir))
#define dma_unmap_single(dev, addr, size, dir) \
    mtk_dma_unmap_single((addr), (size), (dir))
#define dma_map_page(dev, page, off, size, dir) ((dma_addr_t)0)
#define dma_unmap_page(dev, addr, size, dir) do { } while (0)
#define dma_map_sg(dev, sg, nents, dir) (nents)
#define dma_unmap_sg(dev, sg, nents, dir) do { } while (0)
#define dma_sync_single_for_cpu(dev, addr, size, dir) do { } while (0)
#define dma_sync_single_for_device(dev, addr, size, dir) do { } while (0)
#define dma_sync_single_range_for_cpu(dev, addr, off, size, dir) do { } while (0)
#define dma_sync_single_range_for_device(dev, addr, off, size, dir) do { } while (0)

#define dma_mapping_error(dev, addr) ((addr) == 0)

static inline int dma_set_mask(struct device *dev, u64 mask)
{
    (void)dev; (void)mask;
    return 0;
}
static inline int dma_set_coherent_mask(struct device *dev, u64 mask)
{
    (void)dev; (void)mask;
    return 0;
}
static inline int dma_set_mask_and_coherent(struct device *dev, u64 mask)
{
    (void)dev; (void)mask;
    return 0;
}

#define DMA_ATTR_NON_CONSISTENT 0x1
#define DMA_ATTR_WEAK_ORDERING  0x2
#define DMA_ATTR_SKIP_CPU_SYNC  0x4

#endif /* MTK_LC_DMA_MAPPING_H */
