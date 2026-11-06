/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_REGMAP_H
#define MTK_LC_REGMAP_H

#include <linux/mtk_core.h>

struct regmap;

static inline int regmap_read(struct regmap *map, unsigned int reg,
                              unsigned int *val)
{
    (void)map; (void)reg;
    if (val)
        *val = 0;
    return -ENODEV;
}
static inline int regmap_write(struct regmap *map, unsigned int reg,
                               unsigned int val)
{
    (void)map; (void)reg; (void)val;
    return -ENODEV;
}
static inline int regmap_update_bits(struct regmap *map, unsigned int reg,
                                     unsigned int mask, unsigned int val)
{
    (void)map; (void)reg; (void)mask; (void)val;
    return -ENODEV;
}

#endif
