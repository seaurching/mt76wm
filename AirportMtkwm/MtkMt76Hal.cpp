/*
 * MtkMt76Hal.cpp — HAL adapter for the native AirPort form (AirportMtkwm).
 */
#include "MtkMt76Hal.hpp"
#include "ItlMt7601u.hpp"
#include <net80211/ieee80211_var.h>

#define super Mt76HalService
OSDefineMetaClassAndStructors(MtkMt76Hal, Mt76HalService)
OSDefineMetaClassAndStructors(MtkUsbMt76Hal, MtkMt76Hal)

char nmx_wifi_band;	/* NVRAM nullmoth-wifi-band: '5', '2' or 0 (prefer 5 GHz) */

/* Prefer-5 GHz mode: a 2.4 GHz node is a twin to hide when the same SSID is
 * on 5 GHz within 15 RSSI points. */
bool
nmx_hide_24ghz_twin(struct ieee80211com *ic, const struct ieee80211_node *node)
{
	struct ieee80211_node *o;

	if (nmx_wifi_band == '2' || nmx_wifi_band == '5' || node->ni_esslen == 0 ||
	    node->ni_chan == NULL || node->ni_chan == IEEE80211_CHAN_ANYC ||
	    !IEEE80211_IS_CHAN_2GHZ(node->ni_chan))
		return false;
	RB_FOREACH(o, ieee80211_tree, &ic->ic_tree) {
		if (o == node || o->ni_chan == NULL || o->ni_chan == IEEE80211_CHAN_ANYC ||
		    !IEEE80211_IS_CHAN_5GHZ(o->ni_chan) || o->ni_esslen != node->ni_esslen ||
		    memcmp(o->ni_essid, node->ni_essid, node->ni_esslen) != 0)
			continue;
		if (o->ni_rssi + 15 >= node->ni_rssi)
			return true;
	}
	return false;
}

ItlHalService *MtkMt76Hal::createInner()
{
    return new ItlMt76;
}

bool MtkMt76Hal::initWithController(IOEthernetController *controller,
                                    IOWorkLoop *workloop,
                                    IOCommandGate *commandGate)
{
    fInner = createInner();
    if (fInner == NULL)
        return false;
    /* The same controller/workloop/gate must reach both layers: the inner
     * facade's attach() reads getController() into com.sc_ether (the
     * IO80211Controller instance) and hangs the MSI source on
     * getMainWorkLoop(). */
    if (!super::initWithController(controller, workloop, commandGate))
        return false;
    return fInner->initWithController(controller, workloop, commandGate);
}

bool MtkMt76Hal::attach(IOPCIDevice *device)
{
    return fInner->attach(device);
}

void MtkMt76Hal::detach(IOPCIDevice *device)
{
    fInner->detach(device);
}

IOReturn MtkMt76Hal::enable(IONetworkInterface *interface)
{
    return fInner->enable(interface);
}

IOReturn MtkMt76Hal::disable(IONetworkInterface *interface)
{
    return fInner->disable(interface);
}

struct ieee80211com *MtkMt76Hal::get80211Controller()
{
    return fInner->get80211Controller();
}

const char *MtkMt76Hal::getFirmwareVersion()
{
    return fInner->getDriverInfo()->getFirmwareVersion();
}

int16_t MtkMt76Hal::getBSSNoise()
{
    return fInner->getDriverInfo()->getBSSNoise();
}

bool MtkMt76Hal::is5GBandSupport()
{
    return fInner->getDriverInfo()->is5GBandSupport();
}

int MtkMt76Hal::getTxNSS()
{
    return fInner->getDriverInfo()->getTxNSS();
}

const char *MtkMt76Hal::getFirmwareName()
{
    return fInner->getDriverInfo()->getFirmwareName();
}

UInt32 MtkMt76Hal::supportedFeatures()
{
    return fInner->getDriverInfo()->supportedFeatures();
}

const char *MtkMt76Hal::getFirmwareCountryCode()
{
    return fInner->getDriverInfo()->getFirmwareCountryCode();
}

uint32_t MtkMt76Hal::getTxQueueSize()
{
    return fInner->getDriverInfo()->getTxQueueSize();
}

void MtkMt76Hal::clearScanningFlags()
{
    fInner->getDriverController()->clearScanningFlags();
}

IOReturn MtkMt76Hal::setMulticastList(IOEthernetAddress *addr, int count)
{
    return fInner->getDriverController()->setMulticastList(addr, count);
}

void MtkMt76Hal::free()
{
    if (fInner) {
        fInner->release();
        fInner = NULL;
    }
    super::free();
}

/* ---------------------------- USB variant ---------------------------- */

ItlHalService *MtkUsbMt76Hal::createInner()
{
    return new ItlMt7601u;
}

void MtkUsbMt76Hal::setUSB(void *usbDevice, void *usbInterface, IOService *client)
{
    fUsbDevice = usbDevice;
    fUsbInterface = usbInterface;
    fClient = client;
}

bool MtkUsbMt76Hal::attach(IOPCIDevice *device)
{
    (void)device;   /* USB attaches through the host-device pair. */
    return ((ItlMt7601u *)inner())->attachUSB(fUsbDevice, fUsbInterface, fClient);
}

void MtkUsbMt76Hal::detach(IOPCIDevice *device)
{
    (void)device;
    ((ItlMt7601u *)inner())->detachUSB();
}
