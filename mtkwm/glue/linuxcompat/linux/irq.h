/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat irq for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_IRQ_H
#define MTK_LC_IRQ_H

/* irqreturn_t / IRQF_SHARED / request_irq() live in linux/mtk_core.h and
 * linux/slab.h; this header exists so `#include <linux/irq.h>` resolves. */
#include <linux/mtk_core.h>

#endif /* MTK_LC_IRQ_H */
