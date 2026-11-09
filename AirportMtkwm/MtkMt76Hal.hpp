/*
 * MtkMt76Hal.hpp — HAL adapter for the native AirPort form (AirportMtkwm).
 *
 * Bridges the MtkHalService contract (expected by the AirportMtkWlan-derived
 * front end) onto the existing ItlMt76 facade.  Composition, not multiple
 * inheritance onto ItlMt76: both contracts derive from OSObject and a diamond
 * would be a metaclass/retain-count hazard.
 */
#ifndef MtkMt76Hal_hpp
#define MtkMt76Hal_hpp

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/network/IOEthernetController.h>

#include "MtkHalService.hpp"
#include "MtkDriverInfo.hpp"
#include "MtkDriverController.hpp"
#include "ItlMt76.hpp"

class MtkMt76Hal : public MtkHalService, public MtkDriverInfo, public MtkDriverController {
    OSDeclareDefaultStructors(MtkMt76Hal)

public:
    /* Same arguments are forwarded to both bases (see .cpp). */
    virtual bool initWithController(IOEthernetController *controller,
                                    IOWorkLoop *workloop,
                                    IOCommandGate *commandGate) override;

    /* MtkHalService */
    virtual bool attach(IOPCIDevice *device) override;
    virtual void detach(IOPCIDevice *device) override;
    virtual IOReturn enable(IONetworkInterface *interface) override;
    virtual IOReturn disable(IONetworkInterface *interface) override;
    virtual struct ieee80211com *get80211Controller() override;
    virtual MtkDriverInfo *getDriverInfo() override { return this; }
    virtual MtkDriverController *getDriverController() override { return this; }

    /* MtkDriverInfo — forwarded to the ItlMt76 getters. */
    virtual const char *getFirmwareVersion() override;
    virtual int16_t getBSSNoise() override;
    virtual bool is5GBandSupport() override;
    virtual int getTxNSS() override;
    virtual const char *getFirmwareName() override;
    virtual UInt32 supportedFeatures() override;
    virtual const char *getFirmwareCountryCode() override;
    virtual uint32_t getTxQueueSize() override;
    virtual uint32_t getRxRateKbps() override { return 0; }
    virtual int getRxRateMcs() override { return -1; }

    /* MtkDriverController */
    virtual void clearScanningFlags() override;
    virtual IOReturn setMulticastList(IOEthernetAddress *addr, int count) override;

    virtual void free() override;

private:
    ItlMt76 *fInner;
};

/* Prefer-5 GHz helper used by the WCL scan path (ported from MtkMwx). */
extern char nmx_wifi_band;
bool nmx_hide_24ghz_twin(struct ieee80211com *ic, const struct ieee80211_node *node);

#endif /* MtkMt76Hal_hpp */
