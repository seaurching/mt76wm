/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_TIMER_H
#define MTK_LC_TIMER_H
#include <linux/mtk_core.h>
#include <linux/ktime.h>

/* Complete type for by-value embedding (mt76x02.h); hrtimers are not
 * armed by the ported code yet, so the members are placeholders. */
struct hrtimer {
    ktime_t expires;
    void *function;         /* enum hrtimer_restart (*function)(...) */
    int state;
};

#endif
