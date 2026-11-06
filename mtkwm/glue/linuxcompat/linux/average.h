/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_AVERAGE_H
#define MTK_LC_AVERAGE_H

#include <linux/mtk_core.h>

/*
 * Exponential weighted moving average (approximation of
 * include/linux/average.h).  Good enough for RSSI smoothing.
 */
#define DECLARE_EWMA(name, _factor, _weight)                            \
    struct ewma_##name {                                                \
        unsigned long internal;                                         \
    };                                                                  \
    static inline void ewma_##name##_init(struct ewma_##name *e)        \
    {                                                                   \
        e->internal = 0;                                                \
    }                                                                   \
    static inline void ewma_##name##_add(struct ewma_##name *e,         \
                                         unsigned long val)             \
    {                                                                   \
        unsigned long old = e->internal;                                \
        e->internal = old + ((val << (_factor)) - old) / (1ul << (_weight)); \
    }                                                                   \
    static inline unsigned long ewma_##name##_read(struct ewma_##name *e) \
    {                                                                   \
        return e->internal >> (_factor);                                \
    }

#endif
