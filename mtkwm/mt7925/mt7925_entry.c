/*
 * mt7925_entry.c - XNU HAL entry point for the MT7925 family
 * (MT7925/MT7927/MT7928/MT7935).
 *
 * Ported for mt76wm (XNU).  This file is the IOKit-side twin of
 * mtkwm/glue/mtk_api.c (mtk_pci_init for MT7921): it sees both worlds -
 * struct mt76_softc (hal_mt76/mt76_softc.h) and the bridge into the ported
 * driver (mtkwm/mt7925/pci.c).  The strong mtk_mt7925_probe() here
 * overrides the weak stub in hal_mt76/mt76_api_stubs.cpp.
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

#include "../hal_mt76/mt76_softc.h"
#include "../glue/mtk_api.h"

#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>

/*
 * Bridge into the ported driver (implemented at the bottom of
 * mtkwm/mt7925/pci.c).  The softc pointer is passed opaquely so pci.c
 * stays free of the IOKit headers.
 */
extern "C" int mt7925_xnu_probe(struct mt76_softc *sc, void *bar0_va,
                                unsigned int vendor, unsigned int device);
extern "C" void mt7925_xnu_remove(struct mt76_softc *sc);
extern "C" bool mt7925_xnu_intr_filter(struct mt76_softc *sc);
extern "C" void mt7925_xnu_intr_action(struct mt76_softc *sc);
extern "C" void *mt7925_xnu_get_mdev(struct mt76_softc *sc);

static const struct mtk_dev_ops mt7925_dev_ops = {
    mt7925_xnu_remove,
    mt7925_xnu_intr_filter,
    mt7925_xnu_intr_action,
};

extern "C" int mtk_mt7925_probe(struct mt76_softc *sc)
{
    unsigned int vendor, device;
    void *bar0;
    int ret;

    if (!sc || !sc->sc_dev || !sc->sc_bar0) {
        XYLog("%s: invalid arguments\n", __FUNCTION__);
        return -1;
    }

    vendor = sc->sc_dev->configRead16(kIOPCIConfigVendorID);
    device = sc->sc_dev->configRead16(kIOPCIConfigDeviceID);
    bar0 = (void *)sc->sc_bar0->getVirtualAddress();

    XYLog("%s: BAR0 %p, %04x:%04x\n", __FUNCTION__, bar0, vendor, device);

    ret = mt7925_xnu_probe(sc, bar0, vendor, device);
    if (ret) {
        XYLog("%s: probe failed (%d)\n", __FUNCTION__, ret);
        return ret;
    }

    sc->sc_mdev = mt7925_xnu_get_mdev(sc);
    mtk_dev_register(sc, sc->sc_mdev, &mt7925_dev_ops);
    return 0;
}
