/*
 * mtk_api.c - C-linkage entry points for the mt76wm XNU HAL.
 *
 * Ported for mt76wm (XNU).  This file is the only glue component that
 * sees both worlds: struct mt76_softc (hal_mt76/mt76_softc.h) and the
 * bridge into the ported mt76 code (mt7921/pci.c).
 */

#include "../hal_mt76/mt76_softc.h"
#include "mtk_api.h"

#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>

/*
 * Bridge into the ported driver (implemented at the bottom of
 * mtkwm/mt7921/pci.c).  The softc pointer is passed opaquely so this
 * file stays free of the Linux/mac80211 stub headers (name clashes).
 */
extern "C" int mt7921_xnu_probe(struct mt76_softc *sc, void *bar0_va,
                                unsigned int vendor, unsigned int device);
extern "C" void mt7921_xnu_remove(struct mt76_softc *sc);
extern "C" bool mt7921_xnu_intr_filter(struct mt76_softc *sc);
extern "C" void mt7921_xnu_intr_action(struct mt76_softc *sc);
extern "C" void *mt7921_xnu_get_mdev(struct mt76_softc *sc);

/* glue/mtk_glue.c: drain the TX skb-data DMA buffer cache (M7). */
extern void mtk_skb_data_cache_purge(void);

/* glue/mtk_glue.c: freeze/unfreeze work/timer/tasklet re-queueing. */
extern void mtk_quiesce_work(void);
extern void mtk_quiesce_end(void);

/*
 * sc->sc_mdev bookkeeping: mt76_softc.h (owned by the HAL side) does not
 * expose an mdev field yet, so keep a small side table here.  When the
 * HAL adds sc_mdev, mtk_pci_init should also set it directly.  TODO(M2).
 * The table also carries the per-family teardown/interrupt hooks so the
 * shared entry points below can dispatch (multi-family support).
 */
#define MTK_MAX_DEVS 4
static struct {
    struct mt76_softc *sc;
    void *mdev;
    const struct mtk_dev_ops *ops;
} g_dev_map[MTK_MAX_DEVS];

static void map_set(struct mt76_softc *sc, void *mdev,
                    const struct mtk_dev_ops *ops)
{
    int i;

    for (i = 0; i < MTK_MAX_DEVS; i++) {
        if (g_dev_map[i].sc == sc || !g_dev_map[i].sc) {
            g_dev_map[i].sc = sc;
            g_dev_map[i].mdev = mdev;
            g_dev_map[i].ops = ops;
            return;
        }
    }
}

static void map_clear(struct mt76_softc *sc)
{
    int i;

    for (i = 0; i < MTK_MAX_DEVS; i++)
        if (g_dev_map[i].sc == sc) {
            g_dev_map[i].sc = NULL;
            g_dev_map[i].mdev = NULL;
            g_dev_map[i].ops = NULL;
        }
}

static const struct mtk_dev_ops *map_ops(struct mt76_softc *sc)
{
    int i;

    for (i = 0; i < MTK_MAX_DEVS; i++)
        if (g_dev_map[i].sc == sc)
            return g_dev_map[i].ops;
    return NULL;
}

extern "C" void *mtk_get_mdev(struct mt76_softc *sc)
{
    int i;

    for (i = 0; i < MTK_MAX_DEVS; i++)
        if (g_dev_map[i].sc == sc)
            return g_dev_map[i].mdev;
    return NULL;
}

extern "C" const struct mtk_dev_ops *mtk_ops_for_mdev(void *mdev)
{
    int i;

    if (!mdev)
        return NULL;
    for (i = 0; i < MTK_MAX_DEVS; i++)
        if (g_dev_map[i].sc && g_dev_map[i].mdev == mdev)
            return g_dev_map[i].ops;
    return NULL;
}

extern "C" void mtk_dev_register(struct mt76_softc *sc, void *mdev,
                                 const struct mtk_dev_ops *ops)
{
    map_set(sc, mdev, ops);
}

extern "C" int mtk_pci_init(struct mt76_softc *sc)
{
    unsigned int vendor, device;
    void *bar0;
    int ret;

    if (!sc || !sc->sc_dev || !sc->sc_bar0) {
        XYLog("%s: invalid arguments\n", __FUNCTION__);
        return -1;
    }

    vendor = sc->sc_dev->configRead16(kIOPCIConfigVendorID);
    device = sc->sc_dev->configRead16(kIOPCIConfigDeviceID);
    bar0 = (void *)sc->sc_bar0->getVirtualAddress();

    XYLog("%s: BAR0 %p, %04x:%04x\n", __FUNCTION__, bar0, vendor, device);

    /* undo any teardown drain left over from a previous detach */
    mtk_quiesce_end();

    ret = mt7921_xnu_probe(sc, bar0, vendor, device);
    if (ret) {
        XYLog("%s: probe failed (%d)\n", __FUNCTION__, ret);
        return ret;
    }

    sc->sc_mdev = mt7921_xnu_get_mdev(sc);
    map_set(sc, sc->sc_mdev, NULL);    /* NULL ops: MT7921 defaults below */
    return 0;
}

extern "C" void mtk_pci_deinit(struct mt76_softc *sc)
{
    const struct mtk_dev_ops *ops = map_ops(sc);
    void *mdev = mtk_get_mdev(sc);

    /*
     * Stop-path hardening (mt76wm): before ripping the device down,
     * 1) make the scan machinery inert (no callback re-arms a timer
     *    behind the unload),
     * 2) abort every in-flight MCU waiter (blocked senders return at
     *    once instead of pinning this thread for seconds), and
     * 3) freeze work/timer/tasklet re-queueing so the cancels inside
     *    mt7921e_unregister_device() cannot race a self-rescheduling
     *    callback.  Without (2)/(3) a wedged kernel thread keeps the
     *    kext "in use or retained" and unload fails.
     */
    XYLog("mtk-deinit: quiesce\n");
    mtk_net80211_quiesce();
    mtk_mcu_abort(mdev, true);
    mtk_quiesce_work();

    XYLog("mtk-deinit: remove\n");
    if (ops && ops->remove)
        ops->remove(sc);
    else
        mt7921_xnu_remove(sc);
    XYLog("mtk-deinit: remove done\n");
    sc->sc_mdev = NULL;
    map_clear(sc);
    /* M7: drop the recycled TX DMA buffers once all skbs are gone. */
    mtk_skb_data_cache_purge();
    XYLog("mtk-deinit: done\n");
}

extern "C" bool mtk_intr_filter(struct mt76_softc *sc)
{
    static unsigned int mtk_dbg_irqs;
    if (mtk_dbg_irqs < 5)
        XYLog("mtk-dbg: intr_filter #%u\n", ++mtk_dbg_irqs);
    const struct mtk_dev_ops *ops = map_ops(sc);

    if (ops && ops->intr_filter)
        return ops->intr_filter(sc);
    return mt7921_xnu_intr_filter(sc);
}

extern "C" void mtk_intr_action(struct mt76_softc *sc)
{
    const struct mtk_dev_ops *ops = map_ops(sc);

    if (ops && ops->intr_action)
        ops->intr_action(sc);
    else
        mt7921_xnu_intr_action(sc);
}
