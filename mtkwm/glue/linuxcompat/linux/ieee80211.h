/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_LINUX_IEEE80211_H
#define MTK_LC_LINUX_IEEE80211_H

/* The mac80211 stub carries the Linux-side ieee80211 frame/IE definitions. */
#include <net/mac80211.h>

/* ------------------------------------------------------------------ */
/* 802.11 information element iteration (mt7915 OBSS narrow-BW-RU      */
/* checks).  Values from the Linux include/linux/ieee80211.h.          */
/* ------------------------------------------------------------------ */
#ifndef MTK_LC_STRUCT_ELEMENT_DEFINED
#define MTK_LC_STRUCT_ELEMENT_DEFINED
struct element {
    u8 id;
    u8 datalen;
    u8 data[];
};
#endif

#ifndef WLAN_EID_EXT_CAPABILITY
#define WLAN_EID_EXT_CAPABILITY    127
#endif

/* Extended Capabilities octet 10, bit 79: OBSS Narrow Bandwidth RU
 * Tolerance Support */
#ifndef WLAN_EXT_CAPA10_OBSS_NARROW_BW_RU_TOLERANCE_SUPPORT
#define WLAN_EXT_CAPA10_OBSS_NARROW_BW_RU_TOLERANCE_SUPPORT    BIT(7)
#endif

#endif /* MTK_LC_LINUX_IEEE80211_H */
