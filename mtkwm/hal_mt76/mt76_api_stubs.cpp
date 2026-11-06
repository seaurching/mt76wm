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
 * Weak placeholder implementations of the HAL ↔ mt76-port interface
 * (see hal_mt76/mt76_softc.h). The ported code in mtkwm/mt7921/pci.c
 * provides strong definitions that override these at link time.
 */

#include "mt76_softc.h"
#include <compat.h>
#include <linux/kernel.h>

__attribute__((weak)) int mtk_pci_init(struct mt76_softc *sc)
{
    XYLog("%s: weak stub - mt7921 port not wired yet\n", __FUNCTION__);
    return -1;
}

#define MTK_FAMILY_STUB(fn, name) \
    __attribute__((weak)) int fn(struct mt76_softc *sc) \
    { \
        XYLog("%s: %s family not ported yet\n", __FUNCTION__, name); \
        return -1; \
    }

MTK_FAMILY_STUB(mtk_mt7925_probe, "MT7925")
MTK_FAMILY_STUB(mtk_mt7915_probe, "MT7915")
MTK_FAMILY_STUB(mtk_mt7615_probe, "MT7615")
MTK_FAMILY_STUB(mtk_mt76x2_probe, "MT76x2")
MTK_FAMILY_STUB(mtk_mt76x0_probe, "MT76x0")
MTK_FAMILY_STUB(mtk_mt7603_probe, "MT7603")
MTK_FAMILY_STUB(mtk_mt7996_probe, "MT7996")

__attribute__((weak)) void mtk_pci_deinit(struct mt76_softc *sc)
{
}

__attribute__((weak)) bool mtk_intr_filter(struct mt76_softc *sc)
{
    return false;
}

__attribute__((weak)) void mtk_intr_action(struct mt76_softc *sc)
{
}

__attribute__((weak)) int mtk_net80211_attach(struct mt76_softc *sc)
{
    return -1;
}

__attribute__((weak)) int mtk_net80211_enable(struct mt76_softc *sc)
{
    return -1;
}

__attribute__((weak)) void mtk_net80211_disable(struct mt76_softc *sc)
{
}
