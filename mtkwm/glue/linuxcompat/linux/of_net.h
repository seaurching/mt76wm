/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_OF_NET_H
#define MTK_LC_OF_NET_H

#include <linux/of.h>

static inline int of_get_mac_address(struct device_node *np, u8 *addr)
{
    (void)np; (void)addr;
    return -ENODATA;
}

#endif
