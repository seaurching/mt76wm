/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_LINUX_IF_ETHER_H
#define MTK_LC_LINUX_IF_ETHER_H

#include <linux/mtk_core.h>

#ifndef ETH_ALEN
#define ETH_ALEN 6
#endif
#ifndef ETH_HLEN
#define ETH_HLEN 14
#endif
#ifndef ETH_FRAME_LEN
#define ETH_FRAME_LEN 1514
#endif

#ifndef ETH_P_IP
#define ETH_P_IP    0x0800
#endif
#ifndef ETH_P_IPV6
#define ETH_P_IPV6  0x86DD
#endif
#ifndef ETH_P_ARP
#define ETH_P_ARP   0x0806
#endif
#ifndef ETH_P_8021Q
#define ETH_P_8021Q 0x8100
#endif
#ifndef ETH_P_802_3
#define ETH_P_802_3 0x0001
#endif
#ifndef ETH_P_AARP
#define ETH_P_AARP  0x80F3
#endif
#ifndef ETH_P_IPX
#define ETH_P_IPX   0x8137
#endif
#ifndef ETH_P_802_2
#define ETH_P_802_2 0x0004
#endif
#ifndef ETH_P_PREAUTH
#define ETH_P_PREAUTH   0x88C7
#endif
#ifndef ETH_P_WAI
#define ETH_P_WAI   0x88B4
#endif

#ifndef ETH_P_TDLS
#define ETH_P_TDLS  0x890D
#endif
#ifndef ETH_P_WAPI
#define ETH_P_WAPI  0x88B4
#endif

#endif /* MTK_LC_LINUX_IF_ETHER_H */
