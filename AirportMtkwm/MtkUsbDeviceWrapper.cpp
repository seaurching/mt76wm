/*
 * MtkUsbDeviceWrapper.cpp — stage-1 provider for USB MediaTek devices.
 */
#include "MtkUsbDeviceWrapper.hpp"
#include "MtkMt76Hal.hpp"
#include "MtkLog.hpp"
#include "Apple80211.h"

#define super IOService
OSDefineMetaClassAndStructors(MtkUsbDeviceWrapper, IOService);

static IOPMPowerState powerStateArrayUSB[2] =
{
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMDeviceUsable, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
};

extern IO80211WorkQueue *_fWorkloop;

IOWorkLoop *MtkUsbDeviceWrapper::getWorkLoop() const
{
    return _fWorkloop;
}

IOService *MtkUsbDeviceWrapper::
probe(IOService *provider, SInt32 *score)
{
    /* Same safe-boot escape as the PCI wrapper. */
    int off = 0;
    if (PE_parse_boot_argn("-nvoff", &off, sizeof(off)) || PE_parse_boot_argn("-mtkwlanoff", &off, sizeof(off)))
        return NULL;
    mtk_dbg_set_nub(provider);
    XYLog("%s\n", __PRETTY_FUNCTION__);
    super::probe(provider, score);
    IOUSBHostDevice *device = OSDynamicCast(IOUSBHostDevice, provider);
    if (!device)
        return NULL;
    fHalService = new MtkUsbMt76Hal;
    if (!fHalService) {
        XYLog("%s HAL alloc fail\n", __FUNCTION__);
        return NULL;
    }
    this->usbNub = device;
    XYLog("%s Found\n", __FUNCTION__);
    return this;
}

bool MtkUsbDeviceWrapper::
start(IOService *provider)
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    if (_fWorkloop == NULL)
        _fWorkloop = IO80211WorkQueue::workQueue();
    if (!super::start(provider))
        return false;
    if (!usbNub->open(this)) {
        XYLog("%s: can't open the USB device\n", __FUNCTION__);
        super::stop(provider);
        return false;
    }
    /* Find the interface carrying the bulk endpoints (the first
     * IOUSBHostInterface child, as documented in IOUSBHostDevice.h). */
    OSIterator *it = usbNub->getChildIterator(gIOServicePlane);
    if (it) {
        IOService *candidate;
        while ((candidate = (IOService *)it->getNextObject())) {
            IOUSBHostInterface *intf =
                OSDynamicCast(IOUSBHostInterface, candidate);
            if (intf && intf->open(this)) {
                usbInterface = intf;
                break;
            }
        }
        it->release();
    }
    if (!usbInterface) {
        XYLog("%s: can't find/open the USB interface\n", __FUNCTION__);
        usbNub->close(this);
        super::stop(provider);
        return false;
    }
    /* The HAL drives USB transfers with `this` as the open-session owner. */
    ((MtkUsbMt76Hal *)fHalService)->setUSB(usbNub, usbInterface, this);
    UInt8 builtIn = 0;
    setProperty("built-in", OSData::withBytes(&builtIn, sizeof(builtIn)));
    PMinit();
    registerPowerDriver(this, powerStateArrayUSB, 2);
    provider->joinPMtree(this);
    logTimer = IOTimerEventSource::timerEventSource(this, &MtkUsbDeviceWrapper::logTimerFired);
    if (logTimer && _fWorkloop->addEventSource(logTimer) == kIOReturnSuccess)
        logTimer->setTimeoutMS(500);
    else
        OSSafeReleaseNULL(logTimer);
    registerService();
    return true;
}

void MtkUsbDeviceWrapper::
publishLog()
{
    mtk_dbg_publish();
}

void MtkUsbDeviceWrapper::
logTimerFired(OSObject *owner, IOTimerEventSource *sender)
{
    MtkUsbDeviceWrapper *self = OSDynamicCast(MtkUsbDeviceWrapper, owner);
    if (!self)
        return;
    self->publishLog();
    sender->setTimeoutMS(2000);
}

void MtkUsbDeviceWrapper::
stop(IOService *provider)
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    if (logTimer) {
        logTimer->cancelTimeout();
        _fWorkloop->removeEventSource(logTimer);
        OSSafeReleaseNULL(logTimer);
    }
    if (usbInterface) {
        usbInterface->close(this);
        usbInterface = NULL;
    }
    if (usbNub) {
        usbNub->close(this);
    }
    PMstop();
    super::stop(provider);
}

IOReturn MtkUsbDeviceWrapper::
setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice)
{
    return IOPMAckImplied;
}
