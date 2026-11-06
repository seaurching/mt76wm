/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_WORKQUEUE_H
#define MTK_LC_WORKQUEUE_H
#include <linux/mtk_core.h>

/* No pending-bit tracking in the port (mt7615 mac.c rate work). */
#ifndef work_pending
#define work_pending(work) (0)
#endif
#endif
