/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat kfifo for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_KFIFO_H
#define MTK_LC_KFIFO_H

#include <linux/mtk_core.h>
#include <linux/string.h>

/*
 * Minimal single-lock-less power-of-two byte ring, sufficient for the
 * mt76x02 TX-status FIFO (fixed-size records, kfifo_init on a
 * preallocated buffer).  Callers synchronize with their own spinlock,
 * matching the Linux usage in mt76x02_mac.c / mt76x02_mmio.c.
 */
struct mtk_kfifo {
    unsigned char *buf;
    unsigned int size;      /* power of two */
    unsigned int in;        /* write index  */
    unsigned int out;       /* read index   */
};

#define DECLARE_KFIFO_PTR(name, type)   struct mtk_kfifo name

static inline void
mtk_kfifo_init(struct mtk_kfifo *f, void *buffer, unsigned int size)
{
    f->buf = (unsigned char *)buffer;
    f->size = size;
    f->in = 0;
    f->out = 0;
}

#define kfifo_init(fifo, buffer, size) \
    mtk_kfifo_init((fifo), (buffer), (unsigned int)(size))

static inline int
mtk_kfifo_in(struct mtk_kfifo *f, const void *src, unsigned int len)
{
    unsigned int mask = f->size - 1;
    unsigned int idx;

    if (f->in - f->out + len > f->size)
        return 0;
    idx = f->in & mask;
    if (idx + len <= f->size) {
        memcpy(f->buf + idx, src, len);
    } else {
        memcpy(f->buf + idx, src, f->size - idx);
        memcpy(f->buf, (const unsigned char *)src + (f->size - idx),
               len - (f->size - idx));
    }
    f->in += len;
    return len;
}

static inline int
mtk_kfifo_out(struct mtk_kfifo *f, void *dst, unsigned int len)
{
    unsigned int mask = f->size - 1;
    unsigned int idx;

    if (f->in - f->out < len)
        return 0;
    idx = f->out & mask;
    if (idx + len <= f->size) {
        memcpy(dst, f->buf + idx, len);
    } else {
        memcpy(dst, f->buf + idx, f->size - idx);
        memcpy((unsigned char *)dst + (f->size - idx), f->buf,
               len - (f->size - idx));
    }
    f->out += len;
    return len;
}

#define kfifo_put(fifo, val)    mtk_kfifo_in((fifo), &(val), sizeof(val))
#define kfifo_get(fifo, val)    mtk_kfifo_out((fifo), (val), sizeof(*(val)))

static inline int mtk_kfifo_is_full(const struct mtk_kfifo *f)
{
    return f->in - f->out >= f->size;
}

static inline int mtk_kfifo_is_empty(const struct mtk_kfifo *f)
{
    return f->in == f->out;
}

static inline unsigned int mtk_kfifo_len(const struct mtk_kfifo *f)
{
    return f->in - f->out;
}

#define kfifo_is_full(fifo)     mtk_kfifo_is_full(fifo)
#define kfifo_is_empty(fifo)    mtk_kfifo_is_empty(fifo)
#define kfifo_len(fifo)         mtk_kfifo_len(fifo)

#endif /* MTK_LC_KFIFO_H */
