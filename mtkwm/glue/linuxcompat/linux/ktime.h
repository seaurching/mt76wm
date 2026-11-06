/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_KTIME_H
#define MTK_LC_KTIME_H
#include <linux/mtk_core.h>

/* Minimal kernel time types used by the mt7915 coredump bookkeeping
 * (struct mt7915_crash_data). */
struct timespec64 {
    s64 tv_sec;
    long tv_nsec;
};

#ifndef MTK_LC_GUID_T_DEFINED
#define MTK_LC_GUID_T_DEFINED
typedef struct { u8 b[16]; } guid_t;
#endif

#endif
