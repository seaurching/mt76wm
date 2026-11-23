//
//  MtkPciDeviceWrapper.cpp
//  AirportMtkwm-Sonoma
//
//  Created by qcwap on 2023/6/27.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#include "MtkPciDeviceWrapper.hpp"
#include "MtkLog.hpp"
#include "Apple80211.h"

#include "MtkMt76Hal.hpp"

#define super IOService
OSDefineMetaClassAndStructors(MtkPciDeviceWrapper, IOService);

#define  PCI_MSI_FLAGS        2    /* Message Control */
#define  PCI_CAP_ID_MSI        0x05    /* Message Signalled Interrupts */
#define  PCI_MSIX_FLAGS        2    /* Message Control */
#define  PCI_CAP_ID_MSIX    0x11    /* MSI-X */
#define  PCI_MSIX_FLAGS_ENABLE    0x8000    /* MSI-X enable */
#define  PCI_MSI_FLAGS_ENABLE    0x0001    /* MSI feature enabled */

static IOPMPowerState powerStateArray[2] =
{
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMDeviceUsable, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
};

static void pciMsiXClearAndSet(IOPCIDevice *device, UInt8 msixCap, UInt16 clear, UInt16 set)
{
    UInt16 ctrl;
    
    ctrl = device->configRead16(msixCap + PCI_MSIX_FLAGS);
    ctrl &= ~clear;
    ctrl |= set;
    device->configWrite16(msixCap + PCI_MSIX_FLAGS, ctrl);
}

extern IOWorkLoop *_fWorkloop;

IOWorkLoop *MtkPciDeviceWrapper::getWorkLoop() const
{
    return _fWorkloop;
}

IOService* MtkPciDeviceWrapper::
probe(IOService *provider, SInt32 *score)
{
    /* Safe boot: -mtkwlanoff leaves the card alone. Do NOT key off -nvoff:
     * that is OpenCore's common "start once without NVIDIA driver" flag, and
     * coupling to it silently kills Wi-Fi (upstream 3907c5c). */
    int off = 0;
    if (PE_parse_boot_argn("-mtkwlanoff", &off, sizeof(off)))
        return NULL;
    mtk_dbg_set_nub(provider);
    XYLog("%s\n", __PRETTY_FUNCTION__);
    bool isMatch = false;
    super::probe(provider, score);
    UInt8 msiCap = 0;
    UInt8 msixCap = 0;
    IOPCIDevice* device = OSDynamicCast(IOPCIDevice, provider);
    if (!device)
        return NULL;
    if (ItlMt76::mt76_match(device)) {
        isMatch = true;
        if (fHalService) {   /* probe may run repeatedly; don't leak or double-own */
            fHalService->release();
            fHalService = NULL;
        }
        fHalService = new MtkMt76Hal;
    }
    if (isMatch) {
        XYLog("%s Found\n", __FUNCTION__);
        /* Guard the capability offsets: findPCICapability can fail without
         * writing the out param, and an uninitialized offset fed to
         * pciMsiXClearAndSet writes random config registers (upstream 3526eb7). */
        bool hasMsix = device->findPCICapability(PCI_CAP_ID_MSIX, &msixCap) && msixCap != 0;
        if (hasMsix)
            pciMsiXClearAndSet(device, msixCap, PCI_MSIX_FLAGS_ENABLE, 0);
        bool hasMsi = device->findPCICapability(PCI_CAP_ID_MSI, &msiCap) && msiCap != 0;
        /*
         * Do not set the MSI enable bit by hand: nothing has programmed the
         * MSI address/data yet, and with MSI enabled the device stops raising
         * INTx. IOPCIFamily enables MSI when the PCIMessaged vector is
         * registered (see pci_intr_establish).
         */
        if (!hasMsi && !hasMsix) {
            XYLog("%s No MSI cap\n", __FUNCTION__);
            fHalService->release();
            fHalService = NULL;
            return NULL;
        }
        this->pciNub = device;
        return this;
    }
    return NULL;
}

bool MtkPciDeviceWrapper::
start(IOService *provider)
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    _fWorkloop = IO80211WorkQueue::workQueue();
    if (!super::start(provider)) {
        return false;
    }
    IOLog("%s::super start succeed\n", getName());
    UInt8 builtIn = 0;
    setProperty("built-in", OSData::withBytes(&builtIn, sizeof(builtIn)));
    PMinit();
    registerPowerDriver(this, powerStateArray, 2);
    provider->joinPMtree(this);
    logTimer = IOTimerEventSource::timerEventSource(this, &MtkPciDeviceWrapper::logTimerFired);
    if (logTimer && _fWorkloop->addEventSource(logTimer) == kIOReturnSuccess)
        logTimer->setTimeoutMS(500);
    else
        OSSafeReleaseNULL(logTimer);
    registerService();
    return true;
}

void MtkPciDeviceWrapper::
publishLog()
{
    mtk_dbg_publish();
}

void MtkPciDeviceWrapper::
logTimerFired(OSObject *owner, IOTimerEventSource *sender)
{
    MtkPciDeviceWrapper *self = OSDynamicCast(MtkPciDeviceWrapper, owner);
    if (!self)
        return;
    self->publishLog();
    sender->setTimeoutMS(2000);
}

void MtkPciDeviceWrapper::
stop(IOService *provider)
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    if (logTimer) {
        logTimer->cancelTimeout();
        _fWorkloop->removeEventSource(logTimer);
        OSSafeReleaseNULL(logTimer);
    }
    PMstop();
    super::stop(provider);
}

IOReturn MtkPciDeviceWrapper::
setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice)
{
    return IOPMAckImplied;
}
