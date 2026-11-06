/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_LINUX_STRING_H
#define MTK_LC_LINUX_STRING_H

#include <linux/mtk_core.h>

#ifndef strscpy
#define strscpy(dst, src, size) \
    ((size) ? (strlcpy((dst), (src), (size)), 0) : 0)
#endif

#endif /* MTK_LC_LINUX_STRING_H */
