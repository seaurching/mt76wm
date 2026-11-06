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
 * ItlMt7601u - HAL facade for the MT7601U USB devices.  Mirrors
 * hal_mt76/ItlMt76 but the transport is USB (IOUSBHostFamily) instead
 * of PCI, so attach() takes the USB nubs (mtkwmUSB passes them through).
 */

#ifndef ItlMt7601u_hpp
#define ItlMt7601u_hpp

#include <IOKit/IOService.h>
#include <IOKit/network/IOEthernetController.h>

#include "ItlHalService.hpp"
#include "ItlDriverInfo.hpp"
#include "ItlDriverController.hpp"
#include "../hal_mt76/mt76_softc.h"

class ItlMt7601u : public ItlHalService, ItlDriverInfo, ItlDriverController {
    OSDeclareDefaultStructors(ItlMt7601u)

public:
    /* ItlHalService - the PCI variants are not used for USB devices. */
    virtual bool attach(IOPCIDevice *device) override;
    virtual void detach(IOPCIDevice *device) override;
    virtual IOReturn enable(IONetworkInterface *interface) override;
    virtual IOReturn disable(IONetworkInterface *interface) override;
    virtual struct ieee80211com *get80211Controller() override;
    virtual ItlDriverInfo *getDriverInfo() override;
    virtual ItlDriverController *getDriverController() override;

    /* USB attach/detach (@usbDevice: IOUSBHostDevice*,
     * @usbInterface: IOUSBHostInterface*, @client: IOService* owning the
     * open session - all kept opaque so this header stays free of the
     * IOUSBHostFamily includes). */
    bool attachUSB(void *usbDevice, void *usbInterface, void *client);
    void detachUSB();

    /* ItlDriverInfo */
    virtual const char *getFirmwareVersion() override;
    virtual int16_t getBSSNoise() override;
    virtual bool is5GBandSupport() override;
    virtual int getTxNSS() override;
    virtual const char *getFirmwareName() override;
    virtual UInt32 supportedFeatures() override;
    virtual const char *getFirmwareCountryCode() override;
    virtual uint32_t getTxQueueSize() override;

    /* ItlDriverController */
    virtual void clearScanningFlags() override;
    virtual IOReturn setMulticastList(IOEthernetAddress *addr, int count) override;

public:
    struct mt76_softc com;
};

#endif /* ItlMt7601u_hpp */
