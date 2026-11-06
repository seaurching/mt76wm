/*
* Copyright (C) 2020  钟先耀
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
#include "mtkwmUSB.hpp"
#include <linux/types.h>
#include <linux/kernel.h>

#include <IOKit/IOCommandGate.h>
#include <IOKit/network/IONetworkMedium.h>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <IOKit/usb/IOUSBHostInterface.h>
#include <net/ethernet.h>
#include <crypto/sha1.h>
#include <net80211/ieee80211_node.h>
#include <net80211/ieee80211_ioctl.h>

#define super IOEthernetController
OSDefineMetaClassAndStructors(mtkwmUSB, IOEthernetController)

IOWorkLoop *_fWorkloopUSB;
IOCommandGate *_fCommandGateUSB;

/* ================================================================== */
/* USB device id table (Linux drivers/net/wireless/mediatek/mt7601u/  */
/* usb.c:mt7601u_device_table)                                         */
/* ================================================================== */

struct mtk_usb_id {
    UInt16 idVendor;
    UInt16 idProduct;
};

static const struct mtk_usb_id mt7601u_usb_ids[] = {
    { 0x0b05, 0x17d3 },     /* ASUS USB-N13 rev C1   */
    { 0x0e8d, 0x760a },     /* MediaTek              */
    { 0x0e8d, 0x760b },     /* MediaTek              */
    { 0x13d3, 0x3431 },     /* AzureWave             */
    { 0x13d3, 0x3434 },     /* AzureWave             */
    { 0x148f, 0x7601 },     /* Ralink                */
    { 0x148f, 0x760a },     /* Ralink                */
    { 0x148f, 0x760b },     /* Ralink                */
    { 0x148f, 0x760c },     /* Ralink                */
    { 0x148f, 0x760d },     /* Ralink                */
    { 0x2001, 0x3d04 },     /* D-Link DWA-127        */
    { 0x2717, 0x4106 },     /* Xiaomi                */
    { 0x2955, 0x0001 },     /* XiaoMi MiWiFi         */
    { 0x2955, 0x1001 },     /* XiaoMi MiWiFi         */
    { 0x2955, 0x1003 },     /* XiaoMi MiWiFi         */
    { 0x2a5f, 0x1000 },     /* HP                    */
    { 0x7392, 0x7710 },     /* Edimax EW-7711MAC     */
};

bool mtkwmUSB::init(OSDictionary *properties)
{
    usbNub = NULL;
    usbInterface = NULL;
    return super::init(properties);
}

static bool mtkwmUSB_matchId(UInt16 vid, UInt16 pid)
{
    for (size_t i = 0; i < sizeof(mt7601u_usb_ids) / sizeof(mt7601u_usb_ids[0]); i++) {
        if (mt7601u_usb_ids[i].idVendor == vid &&
            mt7601u_usb_ids[i].idProduct == pid)
            return true;
    }
    return false;
}

IOService* mtkwmUSB::probe(IOService *provider, SInt32 *score)
{
    super::probe(provider, score);
    IOUSBHostDevice *device = OSDynamicCast(IOUSBHostDevice, provider);
    if (!device) {
        return NULL;
    }
    const StandardUSB::DeviceDescriptor *desc = device->getDeviceDescriptor();
    if (!desc) {
        return NULL;
    }
    if (!mtkwmUSB_matchId(desc->idVendor, desc->idProduct)) {
        return NULL;
    }
    XYLog("%s matched MT7601U USB device %04x:%04x\n", __FUNCTION__,
          desc->idVendor, desc->idProduct);
    return this;
}

bool mtkwmUSB::configureInterface(IONetworkInterface *netif)
{
    IONetworkData *nd;
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;

    if (super::configureInterface(netif) == false) {
        XYLog("super failed\n");
        return false;
    }

    nd = netif->getParameter(kIONetworkStatsKey);
    if (!nd || !(fpNetStats = (IONetworkStats *)nd->getBuffer())) {
        XYLog("network statistics buffer unavailable?\n");
        return false;
    }
    ifp->netStat = fpNetStats;
    ether_ifattach(ifp, OSDynamicCast(IOEthernetInterface, netif));
    fpNetStats->collisions = 0;
#ifdef __PRIVATE_SPI__
    netif->configureOutputPullModel(fHalService->getDriverInfo()->getTxQueueSize(), 0, 0, IOEthernetInterface::kOutputPacketSchedulingModelNormal, 0);
#endif

    return true;
}

struct _ifnet *mtkwmUSB::getIfp()
{
    return &fHalService->get80211Controller()->ic_ac.ac_if;
}

IOEthernetInterface *mtkwmUSB::getNetworkInterface()
{
    return getIfp()->iface;
}

bool mtkwmUSB::createMediumTables(const IONetworkMedium **primary)
{
    IONetworkMedium    *medium;

    OSDictionary *mediumDict = OSDictionary::withCapacity(1);
    if (mediumDict == NULL) {
        XYLog("Cannot allocate OSDictionary\n");
        return false;
    }

    medium = IONetworkMedium::medium(kIOMediumEthernetAuto, 100 * 1000000);
    IONetworkMedium::addMedium(mediumDict, medium);
    medium->release();
    if (primary) {
        *primary = medium;
    }

    bool result = publishMediumDictionary(mediumDict);
    if (!result) {
        XYLog("Cannot publish medium dictionary!\n");
    }

    mediumDict->release();
    return result;
}

void mtkwmUSB::joinSSID(const char *ssid_name, const char *ssid_pwd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();

    if (strlen(ssid_pwd) == 0) {
        memset(&nwkey, 0, sizeof(ieee80211_nwkey));
        nwkey.i_wepon = IEEE80211_NWKEY_OPEN;
        nwkey.i_defkid = 0;
        memcpy(join.i_nwid, ssid_name, strlen(ssid_name));
        join.i_len = strlen(ssid_name);
        join.i_flags = IEEE80211_JOIN_NWKEY;
    } else {
        memset(&wpa, 0, sizeof(ieee80211_wpaparams));
        wpa.i_enabled = 1;
        wpa.i_ciphers = 0;
        wpa.i_groupcipher = 0;
        wpa.i_protos = IEEE80211_WPA_PROTO_WPA1 | IEEE80211_WPA_PROTO_WPA2;
        wpa.i_akms = IEEE80211_WPA_AKM_PSK | IEEE80211_WPA_AKM_8021X | IEEE80211_WPA_AKM_SHA256_PSK | IEEE80211_WPA_AKM_SHA256_8021X;
        memcpy(wpa.i_name, "zxy", strlen("zxy"));
        memset(&psk, 0, sizeof(ieee80211_wpapsk));
        memcpy(psk.i_name, "zxy", strlen("zxy"));
        psk.i_enabled = 1;
        pbkdf2_sha1(ssid_pwd, (const uint8_t*)ssid_name, strlen(ssid_name),
                    4096, psk.i_psk , 32);
        memset(&nwkey, 0, sizeof(ieee80211_nwkey));
        nwkey.i_wepon = 0;
        nwkey.i_defkid = 0;
        memset(&join, 0, sizeof(ieee80211_join));
        join.i_wpaparams = wpa;
        join.i_wpapsk = psk;
        join.i_flags = IEEE80211_JOIN_WPAPSK | IEEE80211_JOIN_ANY | IEEE80211_JOIN_WPA | IEEE80211_JOIN_8021X;
        join.i_nwkey = nwkey;
        join.i_len = strlen(ssid_name);
        memcpy(join.i_nwid, ssid_name, join.i_len);
    }
    if (ieee80211_add_ess(ic, &join) == 0)
        ic->ic_flags |= IEEE80211_F_AUTO_JOIN;
}

void mtkwmUSB::associateSSID(const char *ssid, const char *pwd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (strlen(pwd) == 0) {
        memcpy(nwid.i_nwid, ssid, 32);
        nwid.i_len = strlen((char *)nwid.i_nwid);
        memset(ic->ic_des_essid, 0, IEEE80211_NWID_LEN);
        ic->ic_des_esslen = nwid.i_len;
        memcpy(ic->ic_des_essid, nwid.i_nwid, nwid.i_len);
        if (ic->ic_des_esslen > 0) {
            /* 'nwid' disables auto-join magic */
            ic->ic_flags &= ~IEEE80211_F_AUTO_JOIN;
        } else if (!TAILQ_EMPTY(&ic->ic_ess)) {
            /* '-nwid' re-enables auto-join */
            ic->ic_flags |= IEEE80211_F_AUTO_JOIN;
        }
        /* disable WPA/WEP */
        ieee80211_disable_rsn(ic);
        ieee80211_disable_wep(ic);
    } else {
        memset(&psk, 0, sizeof(psk));
        memcpy(nwid.i_nwid, ssid, 32);
        nwid.i_len = strlen((char *)nwid.i_nwid);
        memset(ic->ic_des_essid, 0, IEEE80211_NWID_LEN);
        ic->ic_des_esslen = nwid.i_len;
        memcpy(ic->ic_des_essid, nwid.i_nwid, nwid.i_len);
        if (ic->ic_des_esslen > 0) {
            /* 'nwid' disables auto-join magic */
            ic->ic_flags &= ~IEEE80211_F_AUTO_JOIN;
        } else if (!TAILQ_EMPTY(&ic->ic_ess)) {
            /* '-nwid' re-enables auto-join */
            ic->ic_flags |= IEEE80211_F_AUTO_JOIN;
        }
        /* disable WPA/WEP */
        ieee80211_disable_rsn(ic);
        ieee80211_disable_wep(ic);
        size_t passlen = strlen(pwd);
        /* Parse a WPA passphrase */
        if (passlen < 8 || passlen > 63)
            XYLog("wpakey: passphrase must be between "
                  "8 and 63 characters");
        if (nwid.i_len == 0)
            XYLog("wpakey: nwid not set");
        pbkdf2_sha1(pwd, (const uint8_t*)ssid, nwid.i_len, 4096,
                    psk.i_psk, 32);
        psk.i_enabled = 1;
        if (psk.i_enabled) {
            ic->ic_flags |= IEEE80211_F_PSK;
            memcpy(ic->ic_psk, psk.i_psk, sizeof(ic->ic_psk));
            if (ic->ic_flags & IEEE80211_F_WEPON)
                ieee80211_disable_wep(ic);
        } else {
            ic->ic_flags &= ~IEEE80211_F_PSK;
            memset(ic->ic_psk, 0, sizeof(ic->ic_psk));
        }
        memset(&wpa, 0, sizeof(wpa));
        ieee80211_ioctl_getwpaparms(ic, &wpa);
        wpa.i_enabled = psk.i_enabled;
        wpa.i_ciphers = 0;
        wpa.i_groupcipher = 0;
        wpa.i_protos = IEEE80211_WPA_PROTO_WPA1 | IEEE80211_WPA_PROTO_WPA2;
        wpa.i_akms = IEEE80211_WPA_AKM_PSK | IEEE80211_WPA_AKM_8021X | IEEE80211_WPA_AKM_SHA256_PSK | IEEE80211_WPA_AKM_SHA256_8021X;
        ieee80211_ioctl_setwpaparms(ic, &wpa);
    }
    if (ic->ic_state > IEEE80211_S_AUTH && ic->ic_bss != NULL)
        IEEE80211_SEND_MGMT(ic, ic->ic_bss, IEEE80211_FC0_SUBTYPE_DEAUTH, IEEE80211_REASON_AUTH_LEAVE);
    ieee80211_del_ess(ic, NULL, 0, 1);
    struct ieee80211_node *selbs = ieee80211_node_choose_bss(ic, 0, NULL);
    if (selbs == NULL) {
        if (ic->ic_state != IEEE80211_S_SCAN) {
            ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
        }
    } else {
        if (ic->ic_state > IEEE80211_S_AUTH) {
            ieee80211_node_join_bss(ic, selbs, 1);
            fHalService->getDriverController()->clearScanningFlags();
        } else {
            if (ic->ic_state != IEEE80211_S_SCAN) {
                ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
            }
        }
    }
}

bool mtkwmUSB::start(IOService *provider)
{
    int boot_value = 0;
    OSObject *wifiEntryObject = NULL;
    OSDictionary *wifiEntry = NULL;
    OSString *entryKey = NULL;
    OSDictionary *wifiDict = NULL;
    if (!super::start(provider)) {
        return false;
    }
    usbNub = OSDynamicCast(IOUSBHostDevice, provider);
    if (!usbNub) {
        return false;
    }
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

    if (_fWorkloopUSB == NULL) {
        XYLog("No _fWorkloop!!\n");
        goto fail;
    }
    _fCommandGateUSB = IOCommandGate::commandGate(this, (IOCommandGate::Action)mtkwmUSB::tsleepHandler);
    if (_fCommandGateUSB == 0) {
        XYLog("No command gate!!\n");
        goto fail;
    }
    _fWorkloopUSB->addEventSource(_fCommandGateUSB);
    {
        const IONetworkMedium *primaryMedium;
        if (!createMediumTables(&primaryMedium) ||
            !setCurrentMedium(primaryMedium) || !setSelectedMedium(primaryMedium)) {
            XYLog("setup medium fail\n");
            goto fail;
        }
    }
    fHalService = new ItlMt7601u;
    fHalService->initWithController(this, _fWorkloopUSB, _fCommandGateUSB);

    if (PE_parse_boot_argn("-novht", &boot_value, sizeof(boot_value)))
        fHalService->get80211Controller()->ic_userflags |= IEEE80211_F_NOVHT;
    if (PE_parse_boot_argn("-noht40", &boot_value, sizeof(boot_value)))
        fHalService->get80211Controller()->ic_userflags |= IEEE80211_F_NOHT40;

    if (!((ItlMt7601u *)fHalService)->attachUSB(usbNub, usbInterface, this)) {
        XYLog("attach fail\n");
        goto fail;
    }
    if (!attachInterface((IONetworkInterface **)&fNetIf, true)) {
        XYLog("attach to interface fail\n");
        ((ItlMt7601u *)fHalService)->detachUSB();
        goto fail;
    }
    fWatchdogWorkLoop = IOWorkLoop::workLoop();
    if (fWatchdogWorkLoop == NULL) {
        XYLog("init watchdog workloop fail\n");
        ((ItlMt7601u *)fHalService)->detachUSB();
        goto fail;
    }
    watchdogTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &mtkwmUSB::watchdogAction));
    if (!watchdogTimer) {
        XYLog("init watchdog fail\n");
        ((ItlMt7601u *)fHalService)->detachUSB();
        goto fail;
    }
    fWatchdogWorkLoop->addEventSource(watchdogTimer);
    setLinkStatus(kIONetworkLinkValid);
    wifiDict = OSDynamicCast(OSDictionary, getProperty("WiFiConfig"));
    if (wifiDict != NULL) {
        OSCollectionIterator *iterator = OSCollectionIterator::withCollection(wifiDict);
        while ((wifiEntryObject = iterator->getNextObject())) {
            entryKey = OSDynamicCast(OSString, wifiEntryObject);
            if (entryKey == NULL) {
                continue;
            }
            wifiEntry = OSDynamicCast(OSDictionary, wifiDict->getObject(entryKey));
            if (wifiEntry == NULL) {
                continue;
            }
            OSString *ssidObj = OSDynamicCast(OSString, wifiEntry->getObject("ssid"));
            OSString *pwdObj = OSDynamicCast(OSString, wifiEntry->getObject("password"));
            if (ssidObj == NULL || pwdObj == NULL || ssidObj->isEqualTo("")) {
                continue;
            }

            joinSSID(ssidObj->getCStringNoCopy(), pwdObj->getCStringNoCopy());
        }
        iterator->release();
    }
    if (TAILQ_EMPTY(&fHalService->get80211Controller()->ic_ess)) {
        fHalService->get80211Controller()->ic_flags |= IEEE80211_F_AUTO_JOIN;
    }
    registerService();
    fNetIf->registerService();
    return true;

fail:
    releaseAll();
    if (usbInterface) {
        usbInterface->close(this);
        usbInterface = NULL;
    }
    if (usbNub) {
        usbNub->close(this);
        usbNub = NULL;
    }
    super::stop(provider);
    return false;
}

void mtkwmUSB::watchdogAction(IOTimerEventSource *timer)
{
    struct _ifnet *ifp = getIfp();
    (*ifp->if_watchdog)(ifp);
    watchdogTimer->setTimeoutMS(kWatchDogTimerPeriod);
}

const OSString * mtkwmUSB::newVendorString() const
{
    return OSString::withCString("Apple");
}

const OSString * mtkwmUSB::newModelString() const
{
    return OSString::withCString("MediaTek Wireless USB Card");
}

bool mtkwmUSB::createWorkLoop()
{
    _fWorkloopUSB = IOWorkLoop::workLoop();
    return _fWorkloopUSB != 0;
}

IOWorkLoop *mtkwmUSB::getWorkLoop() const
{
    return _fWorkloopUSB;
}

IOReturn mtkwmUSB::selectMedium(const IONetworkMedium *medium) {
    setSelectedMedium(medium);
    return kIOReturnSuccess;
}

void mtkwmUSB::stop(IOService *provider)
{
    XYLog("%s\n", __FUNCTION__);
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    super::stop(provider);
    setLinkStatus(kIONetworkLinkValid);
    ((ItlMt7601u *)fHalService)->detachUSB();
    ether_ifdetach(ifp);
    detachInterface(fNetIf, true);
    OSSafeReleaseNULL(fNetIf);
    releaseAll();
    if (usbInterface) {
        usbInterface->close(this);
        usbInterface = NULL;
    }
    if (usbNub) {
        usbNub->close(this);
        usbNub = NULL;
    }
}

void mtkwmUSB::releaseAll()
{
    if (fHalService) {
        fHalService->release();
        fHalService = NULL;
    }
    if (_fWorkloopUSB) {
        if (_fCommandGateUSB) {
            _fWorkloopUSB->removeEventSource(_fCommandGateUSB);
            _fCommandGateUSB->release();
            _fCommandGateUSB = NULL;
        }
        if (fWatchdogWorkLoop && watchdogTimer) {
            watchdogTimer->cancelTimeout();
            fWatchdogWorkLoop->removeEventSource(watchdogTimer);
            watchdogTimer->release();
            watchdogTimer = NULL;
            fWatchdogWorkLoop->release();
            fWatchdogWorkLoop = NULL;
        }
        _fWorkloopUSB->release();
        _fWorkloopUSB = NULL;
    }
    unregistPM();
}

void mtkwmUSB::free()
{
    XYLog("%s\n", __FUNCTION__);
    if (fHalService != NULL) {
        fHalService->release();
        fHalService = NULL;
    }
    super::free();
}

IOReturn mtkwmUSB::enable(IONetworkInterface *netif)
{
    XYLog("%s\n", __FUNCTION__);
    super::enable(netif);
    _fCommandGateUSB->enable();
    fHalService->enable(netif);
    watchdogTimer->setTimeoutMS(kWatchDogTimerPeriod);
    watchdogTimer->enable();
    return kIOReturnSuccess;
}

IOReturn mtkwmUSB::disable(IONetworkInterface *netif)
{
    XYLog("%s\n", __FUNCTION__);
    super::disable(netif);
    watchdogTimer->cancelTimeout();
    watchdogTimer->disable();
    fHalService->disable(netif);
    setLinkStatus(kIONetworkLinkValid);
    return kIOReturnSuccess;
}

bool mtkwmUSB::
setLinkStatus(UInt32 status, const IONetworkMedium * activeMedium, UInt64 speed, OSData * data)
{
    _ifnet *ifq = &fHalService->get80211Controller()->ic_ac.ac_if;
    bool ret = super::setLinkStatus(status, activeMedium, speed, data);
    if (fNetIf) {
        if (status & kIONetworkLinkActive) {
#ifdef __PRIVATE_SPI__
            fNetIf->startOutputThread();
#endif
        } else if (!(status & kIONetworkLinkNoNetworkChange)) {
#ifdef __PRIVATE_SPI__
            fNetIf->stopOutputThread();
            fNetIf->flushOutputQueue();
#endif
            ifq_flush(&ifq->if_snd);
            mq_purge(&fHalService->get80211Controller()->ic_mgtq);
        }
    }
    return ret;
}

IOReturn mtkwmUSB::getHardwareAddress(IOEthernetAddress *addrP)
{
    if (IEEE80211_ADDR_EQ(etheranyaddr, fHalService->get80211Controller()->ic_myaddr)) {
        return kIOReturnError;
    } else {
        IEEE80211_ADDR_COPY(addrP, fHalService->get80211Controller()->ic_myaddr);
        return kIOReturnSuccess;
    }
}

IOReturn mtkwmUSB::setHardwareAddress(const IOEthernetAddress *addrP)
{
    if (!fNetIf || !addrP)
        return kIOReturnError;
    if_setlladdr(&fHalService->get80211Controller()->ic_ac.ac_if, addrP->bytes);
    if (fHalService->get80211Controller()->ic_state > IEEE80211_S_INIT) {
        fHalService->disable(fNetIf);
        fHalService->enable(fNetIf);
    }
    return kIOReturnSuccess;
}

#ifdef __PRIVATE_SPI__
IOReturn mtkwmUSB::outputStart(IONetworkInterface *interface, IOOptionBits options)
{
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    mbuf_t m = NULL;
    if (ifq_is_oactive(&ifp->if_snd))
        return kIOReturnNoResources;
    while (kIOReturnSuccess == interface->dequeueOutputPackets(1, &m)) {
        if (outputPacket(m, NULL)!= kIOReturnOutputSuccess ||
            ifq_is_oactive(&ifp->if_snd))
            return kIOReturnNoResources;
    }
    return kIOReturnSuccess;
}
#endif

UInt32 mtkwmUSB::outputPacket(mbuf_t m, void *param)
{
    IOReturn ret = kIOReturnOutputSuccess;
    _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;

    if (fHalService->get80211Controller()->ic_state != IEEE80211_S_RUN || ifp->if_snd.queue == NULL) {
        if (m && mbuf_type(m) != MBUF_TYPE_FREE) {
            freePacket(m);
        }
        return kIOReturnOutputDropped;
    }
    if (m == NULL) {
        XYLog("%s m==NULL!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        ret = kIOReturnOutputDropped;
    }
    if (!(mbuf_flags(m) & MBUF_PKTHDR) ){
        XYLog("%s pkthdr is NULL!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        freePacket(m);
        ret = kIOReturnOutputDropped;
    }
    if (mbuf_type(m) == MBUF_TYPE_FREE) {
        XYLog("%s mbuf is FREE!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        ret = kIOReturnOutputDropped;
    }
    if (!ifp->if_snd.queue->lockEnqueue(m)) {
        freePacket(m);
        ret = kIOReturnOutputDropped;
    }
    (*ifp->if_start)(ifp);
    return ret;
}

UInt32 mtkwmUSB::getFeatures() const
{
    return fHalService->getDriverInfo()->supportedFeatures();
}

IOReturn mtkwmUSB::setPromiscuousMode(IOEnetPromiscuousMode mode)
{
    return kIOReturnSuccess;
}

IOReturn mtkwmUSB::setMulticastMode(IOEnetMulticastMode mode)
{
    return kIOReturnSuccess;
}

IOReturn mtkwmUSB::setMulticastList(IOEthernetAddress* addr, UInt32 len)
{
    return fHalService->getDriverController()->setMulticastList(addr, len);
}

IOReturn mtkwmUSB::getPacketFilters(const OSSymbol *group, UInt32 *filters) const
{
    IOReturn    rtn = kIOReturnSuccess;
    if (group == gIOEthernetWakeOnLANFilterGroup && magicPacketSupported) {
        *filters = kIOEthernetWakeOnMagicPacket;
    } else if (group == gIONetworkFilterGroup) {
        *filters = kIOPacketFilterMulticast | kIOPacketFilterPromiscuous;
    } else {
        rtn = IOEthernetController::getPacketFilters(group, filters);
    }
    return rtn;
}

IOReturn mtkwmUSB::
tsleepHandler(OSObject* owner, void* arg0, void* arg1, void* arg2, void* arg3)
{
    mtkwmUSB* dev = OSDynamicCast(mtkwmUSB, owner);
    if (dev == 0)
        return kIOReturnError;

    if (arg1 == 0) {
        if (_fCommandGateUSB->commandSleep(arg0, THREAD_INTERRUPTIBLE) == THREAD_AWAKENED)
            return kIOReturnSuccess;
        else
            return kIOReturnTimeout;
    } else {
        AbsoluteTime deadline;
        clock_interval_to_deadline((*(int*)arg1), kNanosecondScale, reinterpret_cast<uint64_t*> (&deadline));
        if (_fCommandGateUSB->commandSleep(arg0, deadline, THREAD_INTERRUPTIBLE) == THREAD_AWAKENED)
            return kIOReturnSuccess;
        else
            return kIOReturnTimeout;
    }
}

/* ---- power management (USB) ---- */

static IOPMPowerState powerStateArrayUSB[kPowerStateCountUSB] =
{
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMDeviceUsable, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
};

void mtkwmUSB::unregistPM()
{
    if (powerOffThreadCall) {
        thread_call_free(powerOffThreadCall);
        powerOffThreadCall = NULL;
    }
    if (powerOnThreadCall) {
        thread_call_free(powerOnThreadCall);
        powerOnThreadCall = NULL;
    }
}

IOReturn mtkwmUSB::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker)
{
    IOReturn result = IOPMAckImplied;

    if (pmPowerState == powerStateOrdinal) {
        return result;
    }
    switch (powerStateOrdinal) {
        case kPowerStateOffUSB:
            if (powerOffThreadCall) {
                retain();
                if (thread_call_enter(powerOffThreadCall)) {
                    release();
                }
                result = 5000000;
            }
            break;
        case kPowerStateOnUSB:
            if (powerOnThreadCall) {
                retain();
                if (thread_call_enter(powerOnThreadCall)) {
                    release();
                }
                result = 5000000;
            }
            break;

        default:
            break;
    }
    return result;
}

IOReturn mtkwmUSB::setWakeOnMagicPacket(bool active)
{
    magicPacketEnabled = active;
    return kIOReturnSuccess;
}

static void handleSetPowerStateOffUSB(thread_call_param_t param0,
                             thread_call_param_t param1)
{
    mtkwmUSB *self = (mtkwmUSB *)param0;

    if (param1 == 0)
    {
        self->getCommandGate()->runAction((IOCommandGate::Action)
                                           handleSetPowerStateOffUSB,
                                           (void *) 1);
    }
    else
    {
        self->setPowerStateOff();
        self->release();
    }
}

static void handleSetPowerStateOnUSB(thread_call_param_t param0,
                            thread_call_param_t param1)
{
    mtkwmUSB *self = (mtkwmUSB *) param0;

    if (param1 == 0)
    {
        self->getCommandGate()->runAction((IOCommandGate::Action)
                                           handleSetPowerStateOnUSB,
                                           (void *) 1);
    }
    else
    {
        self->setPowerStateOn();
        self->release();
    }
}

IOReturn mtkwmUSB::registerWithPolicyMaker(IOService *policyMaker)
{
    IOReturn ret;

    pmPowerState = kPowerStateOnUSB;
    pmPolicyMaker = policyMaker;

    powerOffThreadCall = thread_call_allocate(
                                            (thread_call_func_t)handleSetPowerStateOffUSB,
                                            (thread_call_param_t)this);
    powerOnThreadCall  = thread_call_allocate(
                                            (thread_call_func_t)handleSetPowerStateOnUSB,
                                              (thread_call_param_t)this);
    ret = pmPolicyMaker->registerPowerDriver(this,
                                             powerStateArrayUSB,
                                             kPowerStateCountUSB);
    return ret;
}

void mtkwmUSB::setPowerStateOff()
{
    pmPowerState = kPowerStateOffUSB;
    pmPolicyMaker->acknowledgeSetPowerState();
}

void mtkwmUSB::setPowerStateOn()
{
    pmPowerState = kPowerStateOnUSB;
    pmPolicyMaker->acknowledgeSetPowerState();
}
