/*
 * MtkUsbDeviceWrapper.hpp — stage-1 provider for USB MediaTek devices in the
 * native AirPort form.  Twin of MtkPciDeviceWrapper: matches IOUSBHostDevice
 * personalities, builds the MtkUsbMt76Hal (ItlMt7601u inner) and publishes
 * itself so AirportMtkwm can bind on top.
 */
#ifndef MtkUsbDeviceWrapper_hpp
#define MtkUsbDeviceWrapper_hpp

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOTypes.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <IOKit/usb/IOUSBHostInterface.h>

#include <HAL/MtkHalService.hpp>

class MtkUsbDeviceWrapper : public IOService {
    OSDeclareDefaultStructors(MtkUsbDeviceWrapper)

public:
    virtual IOService *probe(IOService *provider, SInt32 *score) override;
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    virtual IOWorkLoop *getWorkLoop() const override;
    virtual IOReturn setPowerState(unsigned long powerStateOrdinal,
                                   IOService *whatDevice) override;

    MtkHalService *fHalService;
    IOUSBHostDevice *usbNub;
    IOUSBHostInterface *usbInterface;

private:
    static void logTimerFired(OSObject *owner, IOTimerEventSource *sender);
    void publishLog();
    IOTimerEventSource *logTimer;
};

#endif /* MtkUsbDeviceWrapper_hpp */
