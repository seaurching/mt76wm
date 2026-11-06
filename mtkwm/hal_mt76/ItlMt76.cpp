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

#include "ItlMt76.hpp"
#include <compat.h>
#include <linux/kernel.h>

#define super ItlHalService
OSDefineMetaClassAndStructors(ItlMt76, ItlHalService)

/*
 * MediaTek/ITTIM PCI device IDs across all mt76 families (Linux
 * mt76/<family>/pci.c + pci_ids.h).
 * NOTE: the PCIe vendor is 0x14c3 (PCI_VENDOR_ID_MEDIATEK); 0x0e8d is the
 * MediaTek *USB* vendor id and must not be used for PCIe matching.
 */
struct mt76_pci_id {
    UInt16 vendor;
    UInt16 device;
};

static const struct mt76_pci_id mt76_pci_ids[] = {
    /* MT7921 family (Wi-Fi 6/6E client) */
    { 0x14c3, 0x7961 }, { 0x14c3, 0x7922 }, { 0x14c3, 0x0608 },
    { 0x14c3, 0x0616 }, { 0x14c3, 0x7920 }, { 0x14c3, 0x7902 },
    { 0x0b48, 0x7922 },                        /* ITTIM MT7922 */
    /* MT7925 family (Wi-Fi 7 client) */
    { 0x14c3, 0x7925 }, { 0x14c3, 0x7927 }, { 0x14c3, 0x7928 },
    { 0x14c3, 0x7935 }, { 0x14c3, 0x0717 }, { 0x14c3, 0x0738 },
    { 0x14c3, 0x6639 },
    /* MT7915 family (Wi-Fi 6/6E) */
    { 0x14c3, 0x7915 }, { 0x14c3, 0x7906 }, { 0x14c3, 0x7916 },
    { 0x14c3, 0x790a },
    /* MT7615 family (Wi-Fi 5) */
    { 0x14c3, 0x7615 }, { 0x14c3, 0x7663 },
    /* MT76x2 family (Wi-Fi 5) */
    { 0x14c3, 0x7612 }, { 0x14c3, 0x7662 }, { 0x14c3, 0x7611 },
    { 0x14c3, 0x7602 },
    /* MT76x0 family (Wi-Fi 5) */
    { 0x14c3, 0x7610 }, { 0x14c3, 0x7630 }, { 0x14c3, 0x7650 },
    /* MT7603 family (Wi-Fi 4) */
    { 0x14c3, 0x7603 },
    /* MT7996 family (Wi-Fi 7 AP) - main endpoints (dual-HIF secondaries
     * 0x7991/0x799a/0x799b intentionally unmatched in single-HIF mode) */
    { 0x14c3, 0x7990 }, { 0x14c3, 0x7992 }, { 0x14c3, 0x7993 },
};

bool ItlMt76::mt76_match(IOPCIDevice *device)
{
    UInt16 vendor = device->configRead16(kIOPCIConfigVendorID);
    UInt16 devId = device->configRead16(kIOPCIConfigDeviceID);

    for (size_t i = 0; i < sizeof(mt76_pci_ids) / sizeof(mt76_pci_ids[0]); i++) {
        if (mt76_pci_ids[i].vendor == vendor && mt76_pci_ids[i].device == devId) {
            XYLog("%s matched MediaTek device 0x%04x:0x%04x\n", __FUNCTION__, vendor, devId);
            return true;
        }
    }
    return false;
}

bool ItlMt76::attach(IOPCIDevice *device)
{
    com.sc_dev = device;
    com.sc_ether = getController();
    fNRegWins = 0;
    memset(fRegWinMaps, 0, sizeof(fRegWinMaps));
    memset(fRegWinMds, 0, sizeof(fRegWinMds));

    /* Wait for the PCIe link to actually come up before touching the BAR.
     *
     * At boot time (kext loaded from the startup set) the M.2 slot may
     * still be unpowered / the link untrained: the config space reads back
     * 0xffff and the first MMIO access through a freshly built mapping
     * page-faults ("Kernel trap ... type=14 page fault" in readl).
     * Manually loading the kext later in the session never hits this
     * because the link is long up by then. */
    for (int i = 0; ; i++) {
        uint16_t vend = device->configRead16(kIOPCIConfigVendorID);
        uint16_t devid = device->configRead16(kIOPCIConfigDeviceID);
        if (vend == 0x14c3 && devid != 0 && devid != 0xffff)
            break;
        if (i >= 100) {
            XYLog("%s: PCIe link not ready (vendor=%04x device=%04x)\n",
                  __FUNCTION__, vend, devid);
            return false;
        }
        IOSleep(100);
    }

    /* Map BAR0 (register space).
     *
     * Also verify the mapping covers the whole register window: on some
     * early-boot probes mapDeviceMemoryWithRegister() hands back a
     * truncated/partially-built mapping whose pages are not present yet,
     * and the first register read past page 0 faults. */
    com.sc_bar0 = NULL;
    for (int attempt = 0; attempt < 10; attempt++) {
        /* NOTE: no map options -- plain mapDeviceMemoryWithRegister() is
         * the exact call that was verified working on manual loads
         * (build 9/11). */
        IOMemoryMap *map = device->mapDeviceMemoryWithRegister(
                kIOPCIConfigBaseAddress0);
        if (map != NULL) {
            if (map->getVirtualAddress() != 0 &&
                map->getLength() >= 0x10000) {
                com.sc_bar0 = map;
                break;
            }
            XYLog("%s: BAR0 map attempt %d: va=%p len=0x%llx (incomplete, retrying)\n",
                  __FUNCTION__, attempt,
                  (void *)map->getVirtualAddress(),
                  (unsigned long long)map->getLength());
            map->release();
        } else {
            XYLog("%s: BAR0 map attempt %d: no map yet\n", __FUNCTION__, attempt);
        }
        IOSleep(100);
    }
    if (com.sc_bar0 == NULL) {
        XYLog("%s: can't map BAR0\n", __FUNCTION__);
        return false;
    }
    XYLog("%s: BAR0 va=%p len=0x%llx phys=0x%llx cfg-bar0=0x%x\n", __FUNCTION__,
          (void *)com.sc_bar0->getVirtualAddress(),
          (unsigned long long)com.sc_bar0->getLength(),
          (unsigned long long)com.sc_bar0->getPhysicalAddress(),
          device->configRead32(kIOPCIConfigBaseAddress0));
    /* Settle: let the pmap entries for the mapping really land before the
     * first register access (no-op cost when loading manually). */
    IOSleep(300);

    /*
     * Register access beyond the 1 MB BAR window goes through the HIF L1
     * remap window (see mtkwm/mt76core/mmio.c) -- no per-segment windows
     * are needed any more.  Keep a one-shot BAR size probe for diagnosis.
     */
    {
        uint32_t bar_lo = device->configRead32(kIOPCIConfigBaseAddress0);
        uint32_t bar_hi = device->configRead32(kIOPCIConfigBaseAddress0 + 4);
        uint64_t mask, size;

        device->configWrite32(kIOPCIConfigBaseAddress0, 0xffffffff);
        device->configWrite32(kIOPCIConfigBaseAddress0 + 4, 0xffffffff);
        mask = ((uint64_t)device->configRead32(kIOPCIConfigBaseAddress0 + 4) << 32) |
               (device->configRead32(kIOPCIConfigBaseAddress0) & ~0xfULL);
        device->configWrite32(kIOPCIConfigBaseAddress0, bar_lo);
        device->configWrite32(kIOPCIConfigBaseAddress0 + 4, bar_hi);
        size = ~mask + 1;
        XYLog("%s: BAR0 size=0x%llx (phys 0x%llx)\n", __FUNCTION__,
              (unsigned long long)size,
              (unsigned long long)com.sc_bar0->getPhysicalAddress());
    }
    mtk_mmio_register_windows(NULL, 0,
                              (unsigned int)com.sc_bar0->getLength());

    /* Find the MSI interrupt index (same pattern as itlwm's ItlIwx). */
    int msiIntrIndex = -1;
    for (int index = 0; ; index++) {
        int interruptType;
        int ret = device->getInterruptType(index, &interruptType);
        if (ret != kIOReturnSuccess)
            break;
        if (interruptType & kIOInterruptTypePCIMessaged) {
            msiIntrIndex = index;
            break;
        }
    }
    if (msiIntrIndex == -1) {
        XYLog("%s: can't find MSI interrupt controller\n", __FUNCTION__);
        com.sc_bar0->release();
        com.sc_bar0 = NULL;
        return false;
    }

    com.sc_intr = IOFilterInterruptEventSource::filterInterruptEventSource(this,
                    (IOInterruptEventSource::Action)&ItlMt76::intrAction,
                    &ItlMt76::intrFilter, device, msiIntrIndex);
    if (com.sc_intr == NULL ||
        getMainWorkLoop()->addEventSource(com.sc_intr) != kIOReturnSuccess) {
        XYLog("%s: can't establish interrupt\n", __FUNCTION__);
        OSSafeReleaseNULL(com.sc_intr);
        com.sc_bar0->release();
        com.sc_bar0 = NULL;
        return false;
    }
    com.sc_intr->enable();

    /* Hardware bring-up: family dispatch -> DMA rings, chip id,
     * MCU/firmware (ported mt76 code). */
    if (mtk_attach(&com, device->configRead16(kIOPCIConfigVendorID),
                   device->configRead16(kIOPCIConfigDeviceID)) != 0) {
        XYLog("%s: mtk_attach failed\n", __FUNCTION__);
        detach(device);
        return false;
    }
    /* Register with the itl80211 (net80211) stack. */
    if (mtk_net80211_attach(&com) != 0) {
        XYLog("%s: mtk_net80211_attach failed\n", __FUNCTION__);
        detach(device);
        return false;
    }
    XYLog("%s: attached MediaTek device\n", __FUNCTION__);
    return true;
}

void ItlMt76::detach(IOPCIDevice *device)
{
    XYLog("%s\n", __FUNCTION__);
    mtk_pci_deinit(&com);
    /* Release the net80211 timers (CTimeout instances) or kmutil
     * unload fails with "class CTimeout has instances". */
    mtk_net80211_ifdetach();
    mtk_mmio_register_windows(NULL, 0, 0);
    if (com.sc_intr) {
        com.sc_intr->disable();
        getMainWorkLoop()->removeEventSource(com.sc_intr);
        OSSafeReleaseNULL(com.sc_intr);
    }
    if (com.sc_bar0) {
        OSSafeReleaseNULL(com.sc_bar0);
    }
    com.sc_mdev = NULL;
}

bool ItlMt76::intrFilter(OSObject *object, IOFilterInterruptEventSource *src)
{
    ItlMt76 *that = (ItlMt76 *)object;

    /*
     * Follow itlwm/iwx semantics: accept EVERY interrupt unconditionally
     * and let the handler branch in the action context.  Returning false
     * here makes IOFilterInterruptEventSource treat the MSI as spurious,
     * which can wedge the interrupt source for edge-triggered MSI.
     */
    mtk_intr_filter(&that->com);
    return true;
}

void ItlMt76::intrAction(OSObject *object, IOFilterInterruptEventSource *src, int count)
{
    ItlMt76 *that = (ItlMt76 *)object;
    mtk_intr_action(&that->com);
}

IOReturn ItlMt76::enable(IONetworkInterface *interface)
{
    return mtk_net80211_enable(&com) == 0 ? kIOReturnSuccess : kIOReturnError;
}

IOReturn ItlMt76::disable(IONetworkInterface *interface)
{
    mtk_net80211_disable(&com);
    return kIOReturnSuccess;
}

struct ieee80211com *ItlMt76::get80211Controller()
{
    return &com.sc_ic;
}

ItlDriverInfo *ItlMt76::getDriverInfo()
{
    return this;
}

ItlDriverController *ItlMt76::getDriverController()
{
    return this;
}

const char *ItlMt76::getFirmwareVersion()
{
    return "unloaded";
}

int16_t ItlMt76::getBSSNoise()
{
    return -95;
}

bool ItlMt76::is5GBandSupport()
{
    return true;
}

int ItlMt76::getTxNSS()
{
    return 2;
}

const char *ItlMt76::getFirmwareName()
{
    return "WIFI_RAM_CODE_MT7961_1.bin";
}

UInt32 ItlMt76::supportedFeatures()
{
    return 0;
}

const char *ItlMt76::getFirmwareCountryCode()
{
    return "XX";
}

uint32_t ItlMt76::getTxQueueSize()
{
    return 512;
}

void ItlMt76::clearScanningFlags()
{
}

IOReturn ItlMt76::setMulticastList(IOEthernetAddress *addr, int count)
{
    return kIOReturnSuccess;
}
