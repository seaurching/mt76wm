// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Ported for mt76wm (XNU) */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "mt7603.h"

static const struct pci_device_id mt76pci_device_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7603) },
	{ },
};

static int
mt76pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct mt7603_dev *dev;
	struct mt76_dev *mdev;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	mdev = mt76_alloc_device(&pdev->dev, sizeof(*dev), &mt7603_ops,
				 &mt7603_drv_ops);
	if (!mdev)
		return -ENOMEM;

	dev = container_of(mdev, struct mt7603_dev, mt76);
	mt76_mmio_init(mdev, pcim_iomap_table(pdev)[0]);

	mdev->rev = (mt76_rr(dev, MT_HW_CHIPID) << 16) |
		    (mt76_rr(dev, MT_HW_REV) & 0xff);
	dev_info(mdev->dev, "ASIC revision: %04x\n", mdev->rev);

	mt76_wr(dev, MT_INT_MASK_CSR, 0);

	ret = devm_request_irq(mdev->dev, pdev->irq, mt7603_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto error;

	ret = mt7603_register_device(dev);
	if (ret)
		goto error;

	return 0;
error:
	mt76_free_device(&dev->mt76);

	return ret;
}

static void
mt76pci_remove(struct pci_dev *pdev)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt7603_dev *dev = container_of(mdev, struct mt7603_dev, mt76);

	mt7603_unregister_device(dev);
}

MODULE_DEVICE_TABLE(pci, mt76pci_device_table);
MODULE_FIRMWARE(MT7603_FIRMWARE_E1);
MODULE_FIRMWARE(MT7603_FIRMWARE_E2);

struct pci_driver mt7603_pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt76pci_device_table,
	.probe		= mt76pci_probe,
	.remove		= mt76pci_remove,
};

/* ==================================================================== */
/* XNU bridge (Ported for mt76wm (XNU))                                 */
/*                                                                      */
/* These entry points are called by mtkwm/mt7603/mt7603_entry.c         */
/* (strong symbol mtk_mt7603_probe()).  The softc pointer is opaque      */
/* here (only the entry file sees the IOKit side).                      */
/* ==================================================================== */
static struct mt7603_dev *g_xnu_dev;
static struct pci_dev g_xnu_pdev;
static bool g_xnu_full_probe;

/*
 * debugfs is not available in the kext (Linux mt7603/debugfs.c is not
 * ported); the register path only needs the symbol to exist.
 */
void mt7603_init_debugfs(struct mt7603_dev *dev)
{
	(void)dev;
}

extern "C" void *mt7603_xnu_get_mdev(struct mt76_softc *sc)
{
	(void)sc;
	return g_xnu_dev;
}

extern "C" int mt7603_xnu_probe(struct mt76_softc *sc, void *bar0_va,
				unsigned int vendor, unsigned int device)
{
	const struct pci_device_id *id = NULL;
	unsigned int i;
	int ret;

	(void)sc;
	for (i = 0; i < ARRAY_SIZE(mt76pci_device_table); i++) {
		const struct pci_device_id *e = &mt76pci_device_table[i];

		if (!e->vendor && !e->device)
			break;
		if (e->vendor == vendor && e->device == device) {
			id = e;
			break;
		}
	}
	if (!id) {
		/*
		 * The linuxcompat PCI_VENDOR_ID_MEDIATEK macro does not match
		 * the real PCIe vendor id (0x14c3), so fall back to matching
		 * on the device id only.
		 */
		for (i = 0; i < ARRAY_SIZE(mt76pci_device_table); i++) {
			const struct pci_device_id *e = &mt76pci_device_table[i];

			if (!e->vendor && !e->device)
				break;
			if (e->device == device) {
				id = e;
				XYLog("%s: matched device %04x on id table "
				      "vendor %04x (runtime %04x)\n",
				      __FUNCTION__, device, e->vendor, vendor);
				break;
			}
		}
	}
	if (!id) {
		XYLog("%s: unknown device %04x:%04x\n", __FUNCTION__,
		      vendor, device);
		return -ENODEV;
	}

	memset(&g_xnu_pdev, 0, sizeof(g_xnu_pdev));
	g_xnu_pdev.vendor = vendor;
	g_xnu_pdev.device = device;
	g_xnu_pdev.irq = 0;
	g_xnu_pdev.regs = bar0_va;
	g_xnu_pdev.dev.name = "mt7603";
	g_xnu_full_probe = false;

	ret = mt76pci_probe(&g_xnu_pdev, id);
	if (!ret) {
		struct mt76_dev *mdev = pci_get_drvdata(&g_xnu_pdev);

		g_xnu_dev = container_of(mdev, struct mt7603_dev, mt76);
		g_xnu_full_probe = true;
		XYLog("%s: probe ok, ASIC revision %04x\n", __FUNCTION__,
		      mdev->rev);
		return 0;
	}

	/*
	 * M1 fallback: the full probe sequence (eeprom/MCU/firmware) is not
	 * expected to complete yet, so fall back to the minimum bring-up
	 * contract: allocate the mt7603_dev, wire regs/bus_ops, read
	 * MT_HW_CHIPID into the log, set up the DMA rings and try the
	 * firmware download as far as it goes.  TODO(M2): drop once the
	 * eeprom/MCU path works.
	 */
	{
		struct mt76_dev *mdev;
		struct mt7603_dev *dev;
		u16 chipid;
		int fret;

		XYLog("%s: full probe failed (%d), using M1 fallback\n",
		      __FUNCTION__, ret);

		mdev = mt76_alloc_device(&g_xnu_pdev.dev,
					 sizeof(struct mt7603_dev),
					 &mt7603_ops, &mt7603_drv_ops);
		if (!mdev)
			return -ENOMEM;

		dev = container_of(mdev, struct mt7603_dev, mt76);
		mt76_mmio_init(mdev, g_xnu_pdev.regs);
		dev->bus_ops = dev->mt76.bus;

		chipid = mt76_rr(dev, MT_HW_CHIPID);
		mdev->rev = (chipid << 16) |
			    (mt76_rr(dev, MT_HW_REV) & 0xff);
		XYLog("%s: ASIC revision: %04x (chipid 0x%04x)\n",
		      __FUNCTION__, mdev->rev, chipid);

		mt76_wr(dev, MT_INT_MASK_CSR, 0);
		mt76_wr(dev, MT_INT_SOURCE_CSR, ~0);

		fret = mt7603_dma_init(dev);
		if (fret) {
			XYLog("%s: DMA init failed (%d)\n", __FUNCTION__, fret);
			mt76_free_device(mdev);
			return fret;
		}
		XYLog("%s: DMA rings up\n", __FUNCTION__);

		/* Firmware: best effort, stay alive without it (M1). */
		fret = mt7603_mcu_init(dev);
		if (fret)
			XYLog("%s: firmware/MCU bring-up failed (%d), "
			      "staying in M1 state\n", __FUNCTION__, fret);
		else
			XYLog("%s: firmware init done\n", __FUNCTION__);

		g_xnu_dev = dev;
		return 0;
	}
}

extern "C" void mt7603_xnu_remove(struct mt76_softc *sc)
{
	(void)sc;
	if (!g_xnu_dev)
		return;

	if (g_xnu_full_probe) {
		mt76pci_remove(&g_xnu_pdev);
	} else {
		mt7603_dma_cleanup(g_xnu_dev);
		mt76_free_device(&g_xnu_dev->mt76);
	}
	g_xnu_dev = NULL;
	g_xnu_full_probe = false;
}

/*
 * Interrupt filter (primary context): ack the interrupt status and stash
 * the masked cause for the workloop action (Linux mt7603_irq_handler()
 * split in two halves).
 */
static u32 g_xnu_intr;

extern "C" bool mt7603_xnu_intr_filter(struct mt76_softc *sc)
{
	struct mt7603_dev *dev = g_xnu_dev;
	u32 intr;

	(void)sc;
	if (!dev)
		return false;

	intr = mt76_rr(dev, MT_INT_SOURCE_CSR);
	mt76_wr(dev, MT_INT_SOURCE_CSR, intr);

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mphy.state))
		return false;

	intr &= dev->mt76.mmio.irqmask;
	if (!intr)
		return false;

	g_xnu_intr |= intr;
	return true;
}

extern "C" void mt7603_xnu_intr_action(struct mt76_softc *sc)
{
	struct mt7603_dev *dev = g_xnu_dev;
	u32 intr = g_xnu_intr;

	(void)sc;
	g_xnu_intr = 0;
	if (!dev || !intr)
		return;

	/* same dispatch as Linux mt7603_irq_handler() */
	if (intr & MT_INT_MAC_IRQ3) {
		u32 hwintr = mt76_rr(dev, MT_HW_INT_STATUS(3));

		mt76_wr(dev, MT_HW_INT_STATUS(3), hwintr);
		if (hwintr & MT_HW_INT3_PRE_TBTT0)
			tasklet_schedule(&dev->mt76.pre_tbtt_tasklet);

		if ((hwintr & MT_HW_INT3_TBTT0) && dev->mt76.csa_complete)
			mt76_csa_finish(&dev->mt76);
	}

	if (intr & MT_INT_TX_DONE_ALL) {
		mt7603_irq_disable(dev, MT_INT_TX_DONE_ALL);
		napi_schedule(&dev->mt76.tx_napi);
	}

	if (intr & MT_INT_RX_DONE(0)) {
		dev->rx_pse_check = 0;
		mt7603_irq_disable(dev, MT_INT_RX_DONE(0));
		napi_schedule(&dev->mt76.napi[0]);
	}

	if (intr & MT_INT_RX_DONE(1)) {
		dev->rx_pse_check = 0;
		mt7603_irq_disable(dev, MT_INT_RX_DONE(1));
		napi_schedule(&dev->mt76.napi[1]);
	}
}
