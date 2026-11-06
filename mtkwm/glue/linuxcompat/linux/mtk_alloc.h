/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_ALLOC_H
#define MTK_LC_ALLOC_H

#include <IOKit/IOLib.h>
#include <libkern/libkern.h>

/*
 * Canonical size-tracking heap pair used by ALL kmalloc/kzalloc/kfree-style
 * shims (itl80211/linux/kernel.h and linux/slab.h).
 *
 * Modern XNU's IOFree/kfree validates the size against the original
 * allocation; passing a wrong size panics ("not found in any zone"), which
 * bites hardest on large allocations (firmware buffers etc.).  We therefore
 * prefix every block with a 16-byte header storing the requested size (16
 * bytes keeps the payload 16-byte aligned) and free with the true size.
 */

#define MTK_HEAP_HDR 16u

static inline void *mtk_heap_alloc(size_t size)
{
    char *raw = (char *)IOMalloc(size + MTK_HEAP_HDR);

    if (!raw)
        return NULL;
    *(size_t *)(void *)raw = size;
    return raw + MTK_HEAP_HDR;
}

static inline void mtk_heap_free(const void *p)
{
    char *raw;

    if (!p)
        return;
    raw = (char *)p - MTK_HEAP_HDR;
    IOFree(raw, *(size_t *)(void *)raw + MTK_HEAP_HDR);
}

static inline size_t mtk_heap_size(const void *p)
{
    if (!p)
        return 0;
    return *(const size_t *)(const void *)((const char *)p - MTK_HEAP_HDR);
}

#endif /* MTK_LC_ALLOC_H */
