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

/* ===================== PCIe ASPM / L1SS control =====================
 *
 * Ported from AirportMtkWlan mwx_pci_disable_aspm (commits e4a304a /
 * eee2ac7).  On Ryzen + MT7921/MT7922 systems leaving L1SS/L1 active caps
 * throughput near 40 Mbps and stalls DMA completions.  Rules learned
 * upstream the hard way:
 *  - PCIe Base Spec 5.4.1.3: disable ASPM on the Downstream Port (parent)
 *    BEFORE the Upstream Port (endpoint).
 *  - AMD parent bridges (vendor 0x1022) are NEVER touched unless
 *    -mtkforceparentaspm: macOS does not own native PCIe PM via ACPI _OSC
 *    there, and writing root-port registers can hard-freeze the link once
 *    DMA starts.
 *  - Parent bridges never get their L1SS registers modified (matches
 *    Linux mt76_pci_disable_aspm).
 *  - -mtkallowaspm keeps ASPM on entirely; -mtknoparentaspm skips the
 *    parent only.
 */
static IOPCIDevice *s_aspm_dev = NULL;

extern "C" int PE_parse_boot_argn(const char *, void *, int);

static void mtk_clear_aspm_dev(IOPCIDevice *d, const char *role, bool clear_l1ss)
{
    if (d == NULL)
        return;
    if (clear_l1ss) {
        IOByteCount l1ss = 0;
        if (d->extendedFindPCICapability(kIOPCIExpressL1PMSubstatesCapability,
                                         &l1ss) != 0 && l1ss >= 0x100) {
            UInt32 ctl1 = d->extendedConfigRead32(l1ss + 0x08);
            if (ctl1 & 0x0000000fU) {
                d->extendedConfigWrite32(l1ss + 0x08, ctl1 & ~0x0000000fU);
                XYLog("aspm(%s): L1SS CTL1 0x%08x -> 0x%08x\n", role, ctl1,
                      (UInt32)d->extendedConfigRead32(l1ss + 0x08));
            }
        }
    }
    UInt8 pcie = 0;
    if (d->findPCICapability(kIOPCICapabilityIDPCIExpress, &pcie) != 0 && pcie != 0) {
        UInt16 lnkctl = d->configRead16(pcie + 0x10);
        if (lnkctl & 0x0003U) {
            d->configWrite16(pcie + 0x10, (UInt16)(lnkctl & ~0x0003U));
            XYLog("aspm(%s): LNKCTL 0x%04x -> 0x%04x\n", role, lnkctl,
                  d->configRead16(pcie + 0x10));
        }
    }
}

extern "C" void mtk_xnu_disable_aspm(void)
{
    IOPCIDevice *dev = s_aspm_dev;
    int allow = 0, no_parent = 0, force_parent = 0;

    if (dev == NULL || PE_parse_boot_argn("-mtkallowaspm", &allow, sizeof(allow)))
        return;
    PE_parse_boot_argn("-mtknoparentaspm", &no_parent, sizeof(no_parent));
    PE_parse_boot_argn("-mtkforceparentaspm", &force_parent, sizeof(force_parent));

    IOPCIDevice *parent = NULL;
    for (IORegistryEntry *e = dev->getParentEntry(gIOServicePlane);
         e != NULL; e = e->getParentEntry(gIOServicePlane)) {
        parent = OSDynamicCast(IOPCIDevice, e);
        if (parent != NULL)
            break;
    }
    if (parent != NULL && !no_parent) {
        UInt16 parent_vendor = parent->configRead16(kIOPCIConfigVendorID);
        if (parent_vendor == 0x1022 && !force_parent) {
            XYLog("aspm: skipping parent bridge for AMD vendor 0x1022\n");
        } else {
            mtk_clear_aspm_dev(parent, "parent", false);
        }
    }
    mtk_clear_aspm_dev(dev, "ep", true);
}

extern "C" bool mtk_xnu_aspm_supported(void)
{
    /* ASPM is disabled outright and never re-enabled, so mt76's dynamic
     * aspm_supported power-save toggling stays off. */
    return false;
}

/* ===================== MSI re-arm after WFSYS reset =====================
 *
 * WFSYS reset can clear the MSI capability's enable bit (upstream
 * mwx_pci_irq_state).  IOPCIFamily programmed the address/data when the
 * PCIMessaged vector was registered and does not re-program them, so we
 * snapshot them while enabled and restore them if a reset wiped the
 * enable.  Never write address/data blind -- that is the D1 trap (MSI
 * messages to an unprogrammed address stop INTx and can stall WPDMA).
 */
#define kMtkPCICapIDMSI 0x05

extern "C" void mtk_xnu_msi_rearm(void)
{
    static bool saved = false;
    static UInt32 s_addr_lo = 0, s_addr_hi = 0;
    static UInt16 s_ctrl = 0, s_data = 0;
    static bool s_64 = false;

    IOPCIDevice *dev = s_aspm_dev;
    UInt8 cap = 0;

    if (dev == NULL)
        return;
    if (!dev->findPCICapability(kMtkPCICapIDMSI, &cap) || cap == 0)
        return;

    UInt16 ctrl = dev->configRead16(cap + 0x02);
    if (ctrl & 0x0001) {                 /* enabled: keep a snapshot */
        s_64 = (ctrl & 0x0080) != 0;
        s_ctrl = ctrl;
        s_addr_lo = dev->configRead32(cap + 0x04);
        if (s_64) {
            s_addr_hi = dev->configRead32(cap + 0x08);
            s_data = dev->configRead16(cap + 0x0c);
        } else {
            s_addr_hi = 0;
            s_data = dev->configRead16(cap + 0x08);
        }
        saved = true;
        return;
    }
    if (!saved)
        return;                          /* never saw it programmed; IOPCIFamily's job */

    /* Enable got cleared (WFSYS reset): restore what IOPCIFamily set up. */
    dev->configWrite32(cap + 0x04, s_addr_lo);
    if (s_64) {
        dev->configWrite32(cap + 0x08, s_addr_hi);
        dev->configWrite16(cap + 0x0c, s_data);
    } else {
        dev->configWrite16(cap + 0x08, s_data);
    }
    dev->configWrite16(cap + 0x02, (UInt16)(s_ctrl | 0x0001));
    XYLog("msi: restored after reset (cap 0x%x ctrl 0x%04x addr 0x%08x)\n",
          cap, s_ctrl, s_addr_lo);
}

bool ItlMt76::attach(IOPCIDevice *device)
{
    com.sc_dev = device;
    s_aspm_dev = device;
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

    /* Snapshot the MSI address/data now that IOPCIFamily has programmed
     * them (mtk_xnu_msi_rearm restores them if a WFSYS reset wipes them). */
    mtk_xnu_msi_rearm();

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
    s_aspm_dev = NULL;
    mtk_pci_deinit(&com);
    /* Release the net80211 timers (MtkCTimeout instances) or kmutil
     * unload fails with "class MtkCTimeout has instances". */
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
    /* Match the mwx template values: the networking family and the WCL
     * layer key behaviour off some of these. */
    return "MT7921";
}

int16_t ItlMt76::getBSSNoise()
{
    return 0;
}

bool ItlMt76::is5GBandSupport()
{
    return true;
}

int ItlMt76::getTxNSS()
{
    return 1;
}

const char *ItlMt76::getFirmwareName()
{
    return "WIFI_RAM_CODE_MT7961_1.bin";
}

UInt32 ItlMt76::supportedFeatures()
{
    /* Must match MtkMwx: 0 reads as "no capabilities" and leaves the
     * IO80211 stack reporting the driver unavailable. */
    return kIONetworkFeatureMultiPages;
}

const char *ItlMt76::getFirmwareCountryCode()
{
    return "IN";
}

uint32_t ItlMt76::getTxQueueSize()
{
    return 256;
}

void ItlMt76::clearScanningFlags()
{
}

IOReturn ItlMt76::setMulticastList(IOEthernetAddress *addr, int count)
{
    return kIOReturnSuccess;
}
