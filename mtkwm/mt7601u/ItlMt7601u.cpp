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

#include "ItlMt7601u.hpp"
#include "mt7601u_xnu.h"

#include <compat.h>
#include <linux/kernel.h>

#define super ItlHalService
OSDefineMetaClassAndStructors(ItlMt7601u, ItlHalService)

bool ItlMt7601u::attach(IOPCIDevice *device)
{
    /* USB devices do not attach through the PCI entry point. */
    (void)device;
    return false;
}

void ItlMt7601u::detach(IOPCIDevice *device)
{
    (void)device;
}

bool ItlMt7601u::attachUSB(void *usbDevice, void *usbInterface, void *client)
{
    com.sc_dev = NULL;
    com.sc_bar0 = NULL;
    com.sc_intr = NULL;
    com.sc_ether = getController();

    /* Hardware bring-up: USB transport, firmware, DMA, EEPROM, PHY. */
    if (mtk_mt7601u_usb_probe(&com, usbDevice, usbInterface, client) != 0) {
        XYLog("%s: mtk_mt7601u_usb_probe failed\n", __FUNCTION__);
        return false;
    }
    /* Register with the itl80211 (net80211) stack. */
    if (mtk_net80211_attach(&com) != 0) {
        XYLog("%s: mtk_net80211_attach failed\n", __FUNCTION__);
        mtk_mt7601u_usb_remove(&com);
        return false;
    }
    XYLog("%s: attached MT7601U USB device\n", __FUNCTION__);
    return true;
}

void ItlMt7601u::detachUSB()
{
    XYLog("%s\n", __FUNCTION__);
    mtk_mt7601u_usb_remove(&com);
}

IOReturn ItlMt7601u::enable(IONetworkInterface *interface)
{
    return mtk_net80211_enable(&com) == 0 ? kIOReturnSuccess : kIOReturnError;
}

IOReturn ItlMt7601u::disable(IONetworkInterface *interface)
{
    mtk_net80211_disable(&com);
    return kIOReturnSuccess;
}

struct ieee80211com *ItlMt7601u::get80211Controller()
{
    return &com.sc_ic;
}

ItlDriverInfo *ItlMt7601u::getDriverInfo()
{
    return this;
}

ItlDriverController *ItlMt7601u::getDriverController()
{
    return this;
}

const char *ItlMt7601u::getFirmwareVersion()
{
    return "mt7601u.bin";
}

int16_t ItlMt7601u::getBSSNoise()
{
    return -95;
}

bool ItlMt7601u::is5GBandSupport()
{
    return false;
}

int ItlMt7601u::getTxNSS()
{
    return 1;
}

const char *ItlMt7601u::getFirmwareName()
{
    return "mt7601u.bin";
}

UInt32 ItlMt7601u::supportedFeatures()
{
    return 0;
}

const char *ItlMt7601u::getFirmwareCountryCode()
{
    return "XX";
}

uint32_t ItlMt7601u::getTxQueueSize()
{
    return 64;
}

void ItlMt7601u::clearScanningFlags()
{
}

IOReturn ItlMt7601u::setMulticastList(IOEthernetAddress *addr, int count)
{
    return kIOReturnSuccess;
}
