// SPDX-License-Identifier: GPL-2.0-only
/*
 * mt7601u_entry.c - XNU family entry for the MT7601U USB driver (mt76wm).
 *
 * Mirrors the Linux mt7601u_probe() flow (usb.c) and registers the
 * net80211 bridge hooks through the family hook table
 * (glue/mtk_api.h:struct mtk_dev_ops).  Called by the IOKit HAL class
 * mtkwm/mt7601u/ItlMt7601u.cpp.
 */

#include <linux/slab.h>

/*
 * NOTE: this file stays in the mac80211-shim world (mt7601u.h) and only
 * touches struct mt76_softc through the opaque pointers of glue/mtk_api.h
 * (hal_mt76/mt76_softc.h pulls in the itl80211 header world which cannot
 * be mixed with the mac80211 shim in one translation unit).
 */
#include "../glue/mtk_api.h"

#include "mt7601u.h"
#include "usb.h"
#include "mcu.h"
#include "mac.h"
#include "mt7601u_xnu.h"

/* ================================================================== */
/* mtk_dev_ops: the net80211 -> driver bridge (glue/mtk_api.h)         */
/* ================================================================== */

static void mt7601u_teardown(struct mt7601u_dev *dev)
{
    if (!dev)
        return;
    mt7601u_cleanup(dev);
    mt7601u_usb_detach(dev);
    destroy_workqueue(dev->stat_wq);
    ieee80211_free_hw(dev->hw);
}

static void mt7601u_xnu_remove(struct mt76_softc *sc)
{
    mt7601u_teardown((struct mt7601u_dev *)mtk_get_mdev(sc));
}

static int mt7601u_xnu_tx_frame(void *mdev, const struct mtk_tx_cookie *ck,
                const void *frame, unsigned int len,
                unsigned int flags, unsigned int prio,
                const struct mtk_key_desc *hwkey)
{
    return mt7601u_glue_tx_frame((struct mt7601u_dev *)mdev, ck, frame, len,
                     flags, prio, hwkey);
}

static void mt7601u_xnu_tx_status_check(void *mdev)
{
    /* TX_STAT_FIFO is drained by the stat_work delayed work. */
    (void)mdev;
}

static void mt7601u_xnu_tx_flush(void *mdev)
{
    mt7601u_glue_tx_flush((struct mt7601u_dev *)mdev);
}

static int mt7601u_xnu_sta_add(void *mdev, void *ni, const unsigned char *mac,
                   unsigned int aid, const unsigned char *rates,
                   unsigned int nrates)
{
    (void)rates;
    (void)nrates;
    return mt7601u_glue_sta_add((struct mt7601u_dev *)mdev, ni, mac, aid);
}

static void mt7601u_xnu_sta_remove(void *mdev, void *ni)
{
    mt7601u_glue_sta_remove((struct mt7601u_dev *)mdev, ni);
}

static int mt7601u_xnu_bss_setup(void *mdev, const unsigned char *bssid,
                 unsigned int aid, unsigned int bintval,
                 unsigned int dtim, unsigned int qos,
                 unsigned int basic_rates)
{
    return mt7601u_glue_bss((struct mt7601u_dev *)mdev, bssid, aid, bintval,
                dtim, qos, basic_rates, true);
}

static void mt7601u_xnu_bss_teardown(void *mdev)
{
    mt7601u_glue_bss((struct mt7601u_dev *)mdev, NULL, 0, 0, 0, 0, 0, false);
}

static int mt7601u_xnu_key_add(void *mdev, void *ni,
                   const struct mtk_key_desc *k)
{
    return mt7601u_glue_set_key((struct mt7601u_dev *)mdev, ni, k, true);
}

static int mt7601u_xnu_key_delete(void *mdev, void *ni,
                  const struct mtk_key_desc *k)
{
    return mt7601u_glue_set_key((struct mt7601u_dev *)mdev, ni, k, false);
}

static int mt7601u_xnu_set_edca(void *mdev, const struct mtk_edca_ac *ac4)
{
    return mt7601u_glue_set_edca((struct mt7601u_dev *)mdev, ac4);
}

static int mt7601u_xnu_set_slot(void *mdev, int short_slot)
{
    return mt7601u_glue_set_slot((struct mt7601u_dev *)mdev, short_slot);
}

static int mt7601u_xnu_chan_switch(void *mdev, unsigned int freq,
                   unsigned int offchannel)
{
    return mt7601u_glue_chan_switch((struct mt7601u_dev *)mdev, freq,
                    offchannel);
}

static unsigned int mt7601u_xnu_bands(void *mdev)
{
    (void)mdev;
    return MTK_BAND_2GHZ;
}

static int mt7601u_xnu_hw_start(void *mdev)
{
    return mt7601u_glue_start((struct mt7601u_dev *)mdev);
}

static void mt7601u_xnu_hw_stop(void *mdev)
{
    mt7601u_glue_stop((struct mt7601u_dev *)mdev);
}

static int mt7601u_xnu_get_mac(void *mdev, unsigned char *out)
{
    struct mt7601u_dev *dev = (struct mt7601u_dev *)mdev;

    memcpy(out, dev->macaddr, ETH_ALEN);
    return 0;
}

static const struct mtk_dev_ops mt7601u_dev_ops = {
    .remove = mt7601u_xnu_remove,
    .intr_filter = NULL,            /* USB: no MSI interrupts */
    .intr_action = NULL,
    .tx_frame = mt7601u_xnu_tx_frame,
    .tx_status_check = mt7601u_xnu_tx_status_check,
    .tx_flush = mt7601u_xnu_tx_flush,
    .sta_add = mt7601u_xnu_sta_add,
    .sta_remove = mt7601u_xnu_sta_remove,
    .bss_setup = mt7601u_xnu_bss_setup,
    .bss_teardown = mt7601u_xnu_bss_teardown,
    .key_add = mt7601u_xnu_key_add,
    .key_delete = mt7601u_xnu_key_delete,
    .set_edca = mt7601u_xnu_set_edca,
    .set_slot = mt7601u_xnu_set_slot,
    .chan_switch = mt7601u_xnu_chan_switch,
    .bands = mt7601u_xnu_bands,
    .hw_start = mt7601u_xnu_hw_start,
    .hw_stop = mt7601u_xnu_hw_stop,
    .get_mac = mt7601u_xnu_get_mac,
};

/* ================================================================== */
/* probe / remove (Linux mt7601u_probe / mt7601u_disconnect)           */
/* ================================================================== */

extern "C" int mtk_mt7601u_usb_probe(struct mt76_softc *sc, void *io_device,
                     void *io_interface, void *io_client)
{
    struct mt7601u_dev *dev;
    u32 asic_rev, mac_rev;
    int ret;

    if (!sc)
        return -EINVAL;

    dev = mt7601u_alloc_device(NULL);
    if (!dev)
        return -ENOMEM;

    ret = mt7601u_usb_attach(io_device, io_interface, io_client, dev);
    if (ret)
        goto err;

    usb_reset_device(mt7601u_to_usb_dev(dev));

    dev->vend_buf = devm_kmalloc(dev->dev, MT_VEND_BUF, GFP_KERNEL);
    if (!dev->vend_buf) {
        ret = -ENOMEM;
        goto err_usb;
    }

    ret = mt7601u_wait_asic_ready(dev);
    if (ret)
        goto err_usb;

    asic_rev = mt7601u_rr(dev, MT_ASIC_VERSION);
    mac_rev = mt7601u_rr(dev, MT_MAC_CSR0);
    dev_info(dev->dev, "ASIC revision: %08x MAC revision: %08x\n",
         asic_rev, mac_rev);
    if ((asic_rev >> 16) != 0x7601) {
        ret = -ENODEV;
        goto err_usb;
    }

    /* Note: vendor driver skips this check for MT7601U */
    if (!(mt7601u_rr(dev, MT_EFUSE_CTRL) & MT_EFUSE_CTRL_SEL))
        dev_warn(dev->dev, "Warning: eFUSE not present\n");

    ret = mt7601u_init_hardware(dev);
    if (ret)
        goto err_usb;
    ret = mt7601u_register_device(dev);
    if (ret)
        goto err_hw;

    set_bit(MT7601U_STATE_INITIALIZED, &dev->state);

    /* sc->sc_mdev is intentionally not set: struct mt76_softc lives in the
     * itl80211 header world; the glue keeps the dev pointer in its device
     * map (mtk_get_mdev) which is all the net80211 bridge needs. */
    mtk_dev_register(sc, dev, &mt7601u_dev_ops);

    XYLog("%s: MT7601U USB device attached (%02x:%02x:%02x:%02x:%02x:%02x)\n",
          __FUNCTION__, dev->macaddr[0], dev->macaddr[1], dev->macaddr[2],
          dev->macaddr[3], dev->macaddr[4], dev->macaddr[5]);
    return 0;

err_hw:
    mt7601u_cleanup(dev);
err_usb:
    mt7601u_usb_detach(dev);
err:
    destroy_workqueue(dev->stat_wq);
    ieee80211_free_hw(dev->hw);
    return ret;
}

extern "C" void mtk_mt7601u_usb_remove(struct mt76_softc *sc)
{
    if (!sc)
        return;
    /* dispatches to mt7601u_xnu_remove() through the hook table and
     * clears the glue device map (mtk_skb_data_cache_purge included). */
    mtk_pci_deinit(sc);
}
