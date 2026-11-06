/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_MTD_MTD_H
#define MTK_LC_MTD_MTD_H

#include <linux/mtk_core.h>

struct mtd_info {
    const char *name;
    unsigned long size;
};

static inline struct mtd_info *get_mtd_device_nm(const char *name)
{
    (void)name;
    return (struct mtd_info *)ERR_PTR(-ENODEV);
}

static inline void put_mtd_device(struct mtd_info *mtd)
{
    (void)mtd;
}

static inline int mtd_read(struct mtd_info *mtd, long from, size_t len,
                           size_t *retlen, u_char *buf)
{
    (void)mtd; (void)from; (void)len; (void)retlen; (void)buf;
    return -EIO;
}

static inline int mtd_is_bitflip(int err) { (void)err; return 0; }
static inline int mtd_is_eccerr(int err) { (void)err; return 0; }
static inline int mtd_is_uncorrectable(int err) { (void)err; return 0; }

#endif
