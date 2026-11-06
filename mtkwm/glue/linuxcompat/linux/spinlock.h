/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_SPINLOCK_H
#define MTK_LC_SPINLOCK_H
#include <linux/mtk_core.h>

/*
 * Static spinlock definition (mt7915 pci.c hif_list etc.).  The shim
 * keeps heap-allocated XNU lock handles which cannot be created at load
 * time, so a statically defined lock is a zero handle and its lock/unlock
 * calls are no-ops (see mtk_core.h: spin_lock() tolerates a NULL handle;
 * the port is effectively single-threaded on the XNU workloop).
 */
#define DEFINE_SPINLOCK(name) spinlock_t name = { NULL }

#endif
