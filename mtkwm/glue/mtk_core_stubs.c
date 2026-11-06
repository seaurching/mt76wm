/*
 * mt76wm - MediaTek Wi-Fi driver for macOS
 * Copyright (C) 2026 mt76wm contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * mtk_core_stubs.c - weak fill-ins for functions referenced by the family
 * ports whose Linux implementations lived in files we dropped (mt76/mac80211.c,
 * mt76/scan.c, wed, beacon monitoring). Without these definitions the kext
 * fails to bind at load time ("could not find a kext which exports this
 * symbol"). All are no-ops or trivial: their semantics land with the M4/M5
 * milestones per family. Weak so a real implementation can override.
 */

#include "../mt76core/mt76.h"

/* --- Linux bitops --- */
/* NOTE: the KPI header declares fls(unsigned int) but modern kernels no
 * longer export it to kexts, so we provide our own definition. */

__attribute__((weak)) int fls(unsigned int x)
{
    return x ? 32 - __builtin_clz(x) : 0;
}

/* --- mt76 core (dropped mac80211.c / scan.c / wed helpers) --- */

__attribute__((weak)) void mt76_wed_dma_reset(struct mt76_dev *dev)
{
}

__attribute__((weak)) struct mt76_phy *
mt76_alloc_radio_phy(struct mt76_dev *dev, unsigned int size, u8 band_idx)
{
    return NULL;
}

__attribute__((weak)) void mt76_rx_beacon(struct mt76_phy *phy, struct sk_buff *skb)
{
}

__attribute__((weak)) void mt76_beacon_mon_check(struct mt76_phy *phy)
{
}

__attribute__((weak)) void mt76_abort_scan(struct mt76_dev *dev)
{
}

__attribute__((weak)) void mt76_abort_roc(struct mt76_phy *phy)
{
}

/* --- mac80211 shim gaps (declared extern "C" in net/mac80211.h) --- */

__attribute__((weak)) void
ieee80211_sta_set_buffered(struct ieee80211_sta *sta, u8 tid, bool buffered)
{
}
