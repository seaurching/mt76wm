/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_JIFFIES_H
#define MTK_LC_JIFFIES_H
#include <linux/mtk_core.h>

/* No timer coalescing in the port (mt7615 mcu.c delayed work). */
#ifndef round_jiffies_up
#define round_jiffies_up(j) (j)
#endif
#endif
