/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_SLAB_H
#define MTK_LC_SLAB_H

#include <linux/mtk_core.h>
#include <linux/mtk_alloc.h>
#include <IOKit/IOLib.h>

/*
 * Plain heap allocation -> IOMalloc/IOFree.  gfp flags are ignored.
 * Size tracking lives in linux/mtk_alloc.h (shared with kernel.h).
 */

#define MTK_KMALLOC_HDR MTK_HEAP_HDR

static inline mtk_any_ptr kmalloc(size_t size, gfp_t gfp)
{
    (void)gfp;
    return mtk_heap_alloc(size);
}

static inline size_t ksize(const void *p)
{
    return mtk_heap_size(p);
}

static inline mtk_any_ptr kmalloc_noprof(size_t size, gfp_t gfp)
{
    return kmalloc(size, gfp);
}

static inline mtk_any_ptr kzalloc_noprof(size_t size, gfp_t gfp)
{
    mtk_any_ptr p = kmalloc(size, gfp);
    if (p)
        bzero(p, size);
    return p;
}

static inline void kfree(const void *p)
{
    mtk_heap_free(p);
}

static inline mtk_any_ptr krealloc(const void *p, size_t size, gfp_t gfp)
{
    mtk_any_ptr n;

    if (!size) {
        kfree(p);
        return NULL;
    }
    n = kmalloc(size, gfp);
    if (n && p) {
        size_t old = ksize(p);
        memcpy(n, p, old < size ? old : size);
    }
    if (p)
        kfree(p);
    return n;
}

static inline mtk_any_ptr kvmalloc(size_t size, gfp_t gfp)
{
    return kmalloc(size, gfp);
}

static inline mtk_any_ptr kvzalloc(size_t size, gfp_t gfp)
{
    mtk_any_ptr p = kmalloc(size, gfp);
    if (p)
        bzero(p, size);
    return p;
}

static inline mtk_any_ptr vzalloc(unsigned long size)
{
    mtk_any_ptr p = kmalloc(size, 0);
    if (p)
        bzero(p, size);
    return p;
}

static inline mtk_any_ptr vcalloc(size_t n, size_t size)
{
    return kvzalloc(n * size, 0);
}

static inline void vfree(const void *p)
{
    kfree(p);
}

static inline mtk_any_ptr kmemdup_noprof(const void *src, size_t len, gfp_t gfp)
{
    mtk_any_ptr p = kmalloc(len, gfp);

    if (p && src)
        memcpy(p, src, len);
    return p;
}

static inline char *kstrdup(const char *s, gfp_t gfp)
{
    size_t len;
    char *p;

    if (!s)
        return NULL;
    len = strlen(s) + 1;
    p = (char *)kmalloc(len, gfp);
    if (p)
        memcpy(p, s, len);
    return p;
}

static inline char *kstrndup(const char *s, size_t len, gfp_t gfp)
{
    char *p;
    size_t n = 0;

    while (n < len && s[n])
        n++;
    p = (char *)kmalloc(n + 1, gfp);
    if (p) {
        memcpy(p, s, n);
        p[n] = '\0';
    }
    return p;
}

static inline mtk_any_ptr kmalloc_array(size_t n, size_t size, gfp_t gfp)
{
    return kmalloc(n * size, gfp);
}

static inline mtk_any_ptr kcalloc_array(size_t n, size_t size, gfp_t gfp)
{
    mtk_any_ptr p = kmalloc(n * size, gfp);
    if (p)
        bzero(p, n * size);
    return p;
}

/* struct_size-style flex allocation, 3 or 4 arguments (gfp optional). */
#define kzalloc_flex(obj_expr, member, count, ...) \
    kzalloc(sizeof(obj_expr) + (size_t)(count) * sizeof((obj_expr).member[0]))
/* Linux kzalloc_obj() typed-allocation alias (mt7615 mac.c). */
#define kzalloc_obj(P, ...) kzalloc(sizeof(P), ##__VA_ARGS__)

/* ---- device-managed allocations (M1: plain alloc, freed manually) ---- */
static inline mtk_any_ptr devm_kmalloc(struct device *dev, size_t size, gfp_t gfp)
{
    (void)dev;
    return kmalloc(size, gfp);
}

static inline mtk_any_ptr devm_kzalloc(struct device *dev, size_t size, gfp_t gfp)
{
    mtk_any_ptr p = devm_kmalloc(dev, size, gfp);

    if (p)
        bzero(p, size);
    return p;
}

static inline void *devm_kcalloc(struct device *dev, size_t n, size_t size,
                                 gfp_t gfp)
{
    mtk_any_ptr p = devm_kmalloc(dev, n * size, gfp);

    if (p)
        bzero(p, n * size);
    return p;
}

static inline void *devm_kmemdup(struct device *dev, const void *src,
                                 size_t len, gfp_t gfp)
{
    (void)dev;
    return kmemdup_noprof(src, len, gfp);
}

static inline char *devm_kstrdup(struct device *dev, const char *s, gfp_t gfp)
{
    (void)dev;
    return kstrdup(s, gfp);
}

static inline char *devm_kasprintf(struct device *dev, gfp_t gfp,
                                   const char *fmt, ...)
{
    (void)dev; (void)gfp; (void)fmt;
    /* TODO(M2): real formatted alloc */
    return kstrdup("mtk", gfp);
}

static inline void devm_kfree(struct device *dev, const void *p)
{
    (void)dev;
    kfree(p);
}

static inline int devm_request_irq(struct device *dev, unsigned int irq,
                                   irqreturn_t (*handler)(int, void *),
                                   unsigned long flags, const char *name,
                                   void *data)
{
    /* The XNU side owns the interrupt source (IOFilterInterruptEventSource);
     * the handler is invoked from the glue bottom half. */
    (void)dev; (void)irq; (void)handler; (void)flags; (void)name; (void)data;
    return 0;
}

static inline void devm_free_irq(struct device *dev, unsigned int irq,
                                 void *data)
{
    (void)dev; (void)irq; (void)data;
}

static inline int request_irq(unsigned int irq,
                              irqreturn_t (*handler)(int, void *),
                              unsigned long flags, const char *name, void *data)
{
    (void)irq; (void)handler; (void)flags; (void)name; (void)data;
    return 0;
}
static inline void free_irq(unsigned int irq, void *data)
{
    (void)irq; (void)data;
}
static inline void synchronize_irq(unsigned int irq) { (void)irq; }

#endif /* MTK_LC_SLAB_H */
