/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * mt7601u_xnu.h - XNU-facing entry points of the MT7601U family port.
 * Called by the IOKit HAL class (mtkwm/mt7601u/ItlMt7601u.cpp).
 */

#ifndef mt7601u_xnu_h
#define mt7601u_xnu_h

struct mt76_softc;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Full probe: USB transport attach, chip identification, firmware +
 * hardware bring-up, net80211-independent registration and the
 * mtk_dev_ops hookup (glue/mtk_api.h).  @io_device is the
 * IOUSBHostDevice, @io_interface the IOUSBHostInterface carrying the
 * bulk endpoints, @io_client the IOService owning the open session.
 * Returns 0 on success.
 */
int mtk_mt7601u_usb_probe(struct mt76_softc *sc, void *io_device,
                          void *io_interface, void *io_client);

/* Tear down what mtk_mt7601u_usb_probe() set up. */
void mtk_mt7601u_usb_remove(struct mt76_softc *sc);

#ifdef __cplusplus
}
#endif

#endif /* mt7601u_xnu_h */
