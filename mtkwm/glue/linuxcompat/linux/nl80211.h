/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat nl80211 for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_NL80211_H
#define MTK_LC_NL80211_H

/* The nl80211 enums used by the ported drivers (nl80211_dfs_regions,
 * nl80211_chan_width, nl80211_iftype, nl80211_band) live in the mac80211
 * compat shim; include it so `#include <linux/nl80211.h>` resolves. */
#include <net/mac80211.h>

#endif /* MTK_LC_NL80211_H */
