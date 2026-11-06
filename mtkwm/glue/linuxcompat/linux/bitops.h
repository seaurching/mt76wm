/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_BITOPS_H
#define MTK_LC_BITOPS_H

#include <linux/bitfield.h>     /* itl80211: BIT/GENMASK/find_*_bit/set_bit */
#include <linux/mtk_core.h>

#define hweight_long(x) hweight64(x)

/* find last (most significant) set bit, 0-based index */
static inline int __fls(unsigned long x)
{
    return (int)(sizeof(unsigned long) * 8 - 1 - __builtin_clzl(x));
}

static inline void assign_bit(int nr, volatile unsigned long *addr, int val)
{
    if (val)
        set_bit(nr, addr);
    else
        clear_bit(nr, addr);
}

#define BITMAP_FIRST_WORD_MASK(start) (~0UL << ((start) & (BITS_PER_LONG - 1)))

static inline void bitmap_zero(unsigned long *dst, unsigned int nbits)
{
    memset(dst, 0, BITS_TO_LONGS(nbits) * sizeof(unsigned long));
}

static inline int bitmap_empty(const unsigned long *src, unsigned int nbits)
{
    unsigned int i;

    for (i = 0; i < BITS_TO_LONGS(nbits); i++)
        if (src[i])
            return 0;
    return 1;
}

#endif
