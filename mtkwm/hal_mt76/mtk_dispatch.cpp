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
 * mtk_dispatch.cpp - multi-family chip dispatch.
 *
 * Routes a matched PCI device to its per-family probe (ported Linux
 * mt76/<family>/pci.c entry). Families that are not ported yet resolve to
 * weak -ENOSYS stubs (hal_mt76/mt76_api_stubs.cpp) and attach fails
 * gracefully with a log line.
 */

#include "mt76_softc.h"
#include <compat.h>
#include <linux/kernel.h>

enum mtk_family {
    MTK_FAMILY_NONE = 0,
    MTK_FAMILY_MT7603,
    MTK_FAMILY_MT76X0,
    MTK_FAMILY_MT76X2,
    MTK_FAMILY_MT7615,
    MTK_FAMILY_MT7915,
    MTK_FAMILY_MT7921,   /* MT7921/7922/7920/7902 */
    MTK_FAMILY_MT7925,
    MTK_FAMILY_MT7996,
};

static enum mtk_family classify(UInt16 vendor, UInt16 device)
{
    if (vendor == 0x0b48 && device == 0x7922)
        return MTK_FAMILY_MT7921;               /* ITTIM MT7922 */

    if (vendor != 0x14c3)                        /* PCI_VENDOR_ID_MEDIATEK */
        return MTK_FAMILY_NONE;

    switch (device) {
    case 0x7961: case 0x7922: case 0x7920: case 0x7902:
    case 0x0608: case 0x0616:
        return MTK_FAMILY_MT7921;
    case 0x7925: case 0x7927: case 0x7928: case 0x7935:
    case 0x0717: case 0x0738: case 0x6639:
        return MTK_FAMILY_MT7925;
    case 0x7915: case 0x7906: case 0x7916: case 0x790a:
        return MTK_FAMILY_MT7915;
    case 0x7615: case 0x7663:
        return MTK_FAMILY_MT7615;
    case 0x7612: case 0x7662: case 0x7611: case 0x7602:
        return MTK_FAMILY_MT76X2;
    case 0x7610: case 0x7630: case 0x7650:
        return MTK_FAMILY_MT76X0;
    case 0x7603:
        return MTK_FAMILY_MT7603;
    /* MT7996 family: main endpoints only for now. The dual-HIF second
     * endpoints (0x7991/0x799a/0x799b) are intentionally left unmatched
     * in single-HIF mode (TODO M2: claim as companions). */
    case 0x7990: case 0x7992: case 0x7993:
        return MTK_FAMILY_MT7996;
    default:
        return MTK_FAMILY_NONE;
    }
}

extern "C" int mtk_attach(struct mt76_softc *sc, UInt16 vendor, UInt16 device)
{
    enum mtk_family fam = classify(vendor, device);

    switch (fam) {
    case MTK_FAMILY_MT7921:
        return mtk_pci_init(sc);                /* existing MT7921 port */
    case MTK_FAMILY_MT7925:
        XYLog("%s: MT7925 family\n", __FUNCTION__);
        return mtk_mt7925_probe(sc);
    case MTK_FAMILY_MT7915:
        XYLog("%s: MT7915 family\n", __FUNCTION__);
        return mtk_mt7915_probe(sc);
    case MTK_FAMILY_MT7615:
        XYLog("%s: MT7615 family\n", __FUNCTION__);
        return mtk_mt7615_probe(sc);
    case MTK_FAMILY_MT76X2:
        XYLog("%s: MT76x2 family\n", __FUNCTION__);
        return mtk_mt76x2_probe(sc);
    case MTK_FAMILY_MT76X0:
        XYLog("%s: MT76x0 family\n", __FUNCTION__);
        return mtk_mt76x0_probe(sc);
    case MTK_FAMILY_MT7603:
        XYLog("%s: MT7603 family\n", __FUNCTION__);
        return mtk_mt7603_probe(sc);
    case MTK_FAMILY_MT7996:
        XYLog("%s: MT7996 family\n", __FUNCTION__);
        return mtk_mt7996_probe(sc);
    default:
        XYLog("%s: unknown MediaTek device 0x%04x:0x%04x\n",
              __FUNCTION__, vendor, device);
        return -1;
    }
}
