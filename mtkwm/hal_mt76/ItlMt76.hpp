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
 * ItlMt76 - HAL facade for MediaTek MT7921/MT7922 (and later MT792x) devices.
 * Modeled on itlwm's ItlIwn (Copyright (C) 2020 钟先耀, GPL-2.0):
 * one class implementing ItlHalService + ItlDriverInfo + ItlDriverController.
 */

#ifndef ItlMt76_hpp
#define ItlMt76_hpp

#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/network/IOEthernetController.h>

#include "ItlHalService.hpp"
#include "ItlDriverInfo.hpp"
#include "ItlDriverController.hpp"
#include "mt76_softc.h"

class ItlMt76 : public ItlHalService, ItlDriverInfo, ItlDriverController {
    OSDeclareDefaultStructors(ItlMt76)

public:
    /* PCI ID match (MT7921/7922/7920/7902 family) */
    static bool mt76_match(IOPCIDevice *device);

    /* IOFilterInterruptEventSource callbacks */
    static bool intrFilter(OSObject *object, IOFilterInterruptEventSource *src);
    static void intrAction(OSObject *object, IOFilterInterruptEventSource *src, int count);

    /* ItlHalService */
    virtual bool attach(IOPCIDevice *device) override;
    virtual void detach(IOPCIDevice *device) override;
    virtual IOReturn enable(IONetworkInterface *interface) override;
    virtual IOReturn disable(IONetworkInterface *interface) override;
    virtual struct ieee80211com *get80211Controller() override;
    virtual ItlDriverInfo *getDriverInfo() override;
    virtual ItlDriverController *getDriverController() override;

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

private:
    /* Per-segment MMIO windows for truncated BAR mappings (see mtk_regwin). */
    enum { kMaxRegWins = 4 };
    IOMemoryMap *fRegWinMaps[kMaxRegWins];
    IOMemoryDescriptor *fRegWinMds[kMaxRegWins];
    int fNRegWins;
};

#endif /* ItlMt76_hpp */
