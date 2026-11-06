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

#ifndef mt76_softc_h
#define mt76_softc_h

#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/network/IOEthernetController.h>
#include <net80211/ieee80211_var.h>

/*
 * XNU softc for the mt76-based MediaTek devices.
 *
 * Holds the IOKit-side resources (BAR mapping, interrupt source, controller
 * back-pointer) and embeds the net80211 state. The Linux-side device core
 * (struct mt792x_dev) is allocated and owned by the ported code and hooked
 * in via sc_mdev (kept as void* here so this header stays IOKit-only).
 */
struct mt76_softc {
    IOPCIDevice                     *sc_dev;
    IOMemoryMap                     *sc_bar0;       /* BAR0 mapping           */
    IOFilterInterruptEventSource    *sc_intr;       /* MSI interrupt source   */
    IOEthernetController            *sc_ether;      /* back-pointer to mtkwm  */
    struct ieee80211com              sc_ic;         /* net80211 state         */
    void                            *sc_mdev;       /* struct mt792x_dev *    */
};

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Interface contract between the IOKit HAL (hal_mt76/) and the ported mt76
 * code (mtkwm/mt7921/, mtkwm/mt76core/, mtkwm/glue/).
 *
 * Implemented by the port (mt7921/pci.c); weak placeholder stubs live in
 * hal_mt76/mt76_api_stubs.cpp and are overridden by the strong definitions.
 */

/* Full hardware init after BAR0 is mapped and MSI is enabled:
 * allocate/attach struct mt792x_dev, set up bus ops + register base,
 * DMA rings, read chip id, run MCU/firmware bring-up. 0 on success. */
int  mtk_pci_init(struct mt76_softc *sc);

/* Multi-family dispatch entry (hal_mt76/mtk_dispatch.cpp): routes to the
 * per-family probe below based on the PCI vendor/device id. */
int  mtk_attach(struct mt76_softc *sc, UInt16 vendor, UInt16 device);

/* Per-family probes (implemented by each family port; weak -ENOSYS stubs
 * until then). Same contract as mtk_pci_init. */
int  mtk_mt7925_probe(struct mt76_softc *sc);
int  mtk_mt7915_probe(struct mt76_softc *sc);
int  mtk_mt7615_probe(struct mt76_softc *sc);
int  mtk_mt76x2_probe(struct mt76_softc *sc);
int  mtk_mt76x0_probe(struct mt76_softc *sc);
int  mtk_mt7603_probe(struct mt76_softc *sc);
int  mtk_mt7996_probe(struct mt76_softc *sc);

/* Tear down what mtk_pci_init set up. */
void mtk_pci_deinit(struct mt76_softc *sc);

/* Interrupt filter (runs in primary interrupt context): read/ack the
 * interrupt status register; return true if the workloop action should run. */
bool mtk_intr_filter(struct mt76_softc *sc);

/* Interrupt action (runs serialized on the workloop): rx/tx polling etc. */
void mtk_intr_action(struct mt76_softc *sc);

/* net80211 registration: fill sc_ic (ic_caps, media, callbacks), register
 * the interface with itl80211 (ieee80211_ifattach). Called by the HAL after
 * mtk_pci_init succeeded. 0 on success. */
int  mtk_net80211_attach(struct mt76_softc *sc);

/* Interface bring-up/tear-down (net80211 INIT/SCAN on enable, INIT on
 * disable; hw start/stop). Called from ItlMt76::enable/disable. */
int  mtk_net80211_enable(struct mt76_softc *sc);
void mtk_net80211_disable(struct mt76_softc *sc);

/* Full net80211 teardown (ieee80211_ifdetach + timer frees).  Must run on
 * the unload path or the leaked CTimeout instances pin the kext. */
void mtk_net80211_ifdetach(void);

#ifdef __cplusplus
}

/* C++ linkage: the definition lives in mt76core/mmio.c (compiled as
 * C++, so its symbol is name-mangled -- do not wrap in extern "C").
 * `wins` is an array of { u32 base; u32 len; void *va; } entries. */
void mtk_mmio_register_windows(const void *wins, int n, unsigned int main_len);
#endif

#endif /* mt76_softc_h */
