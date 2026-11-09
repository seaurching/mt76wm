//
//  MtkPciDeviceWrapper.hpp
//  AirportMtkwm-Sonoma
//
//  Created by qcwap on 2023/6/27.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#ifndef MtkPciDeviceWrapper_hpp
#define MtkPciDeviceWrapper_hpp

#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOTypes.h>
#include <IOKit/IOTimerEventSource.h>

#include <HAL/MtkHalService.hpp>

class MtkPciDeviceWrapper : public IOService {
    OSDeclareDefaultStructors(MtkPciDeviceWrapper)
    
public:
    virtual IOService* probe(IOService* provider, SInt32* score) override;
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    virtual IOWorkLoop* getWorkLoop() const override;
    virtual IOReturn setPowerState(
        unsigned long powerStateOrdinal,
                                   IOService *   whatDevice ) override;
    
public:
    MtkHalService *fHalService;
    IOPCIDevice *pciNub;

private:
    /*
     * The driver log, published as MtkDebugLog on the PCI device every 2 s
     * (tools/mtklog.sh): IOLog from an OpenCore-injected kext never reaches
     * the unified log, also when the Wi-Fi controller fails to start.
     */
    IOTimerEventSource *logTimer;
    void publishLog();
    static void logTimerFired(OSObject *owner, IOTimerEventSource *sender);
};

#endif /* MtkPciDeviceWrapper_hpp */
