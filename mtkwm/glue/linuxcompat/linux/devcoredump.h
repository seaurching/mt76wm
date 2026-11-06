/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_DEVCOREDUMP_H
#define MTK_LC_DEVCOREDUMP_H

#include <linux/mtk_core.h>
#include <linux/slab.h>

static inline void dev_coredumpv(struct device *dev, void *data,
                                 size_t datalen, gfp_t gfp)
{
    /* No sysfs core dump in the kext: drop the dump. */
    (void)dev; (void)gfp; (void)datalen;
    if (data)
        vfree(data);
}

static inline void dev_coredumpm(struct device *dev, void *drvdata,
                                 void *data, size_t datalen, gfp_t gfp,
                                 void *readfn, void *freefn)
{
    (void)drvdata; (void)readfn; (void)freefn;
    dev_coredumpv(dev, data, datalen, gfp);
}

#endif
