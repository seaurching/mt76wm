/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_MTD_PARTITIONS_H
#define MTK_LC_MTD_PARTITIONS_H

#include <linux/mtd/mtd.h>

struct mtd_partition {
    const char *name;
    uint64_t size;
    uint64_t offset;
};

#endif
