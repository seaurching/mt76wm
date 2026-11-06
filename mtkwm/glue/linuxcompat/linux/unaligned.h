/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_UNALIGNED_H
#define MTK_LC_UNALIGNED_H

#include <linux/mtk_core.h>
#include <libkern/OSByteOrder.h>

/*
 * get_unaligned_le16/le32, get_unaligned_be16 and put_unaligned_le16/le32
 * are already provided as static inline helpers by itl80211/linux/types.h
 * (pulled in via linux/mtk_core.h).  Re-declaring them here collides with
 * those definitions in every TU that sees both headers, so only the
 * helpers missing from types.h are defined below.
 */
static inline void put_unaligned_le64(u64 val, void *p)
{
    *(__le64 *)p = cpu_to_le64(val);
}
static inline u32 get_unaligned_be32(const void *p)
{
    return __be32_to_cpup((const __be32 *)p);
}
static inline u64 get_unaligned_le64(const void *p)
{
    u64 v;

    memcpy(&v, p, sizeof(v));
    return le64_to_cpu(v);
}

#endif
