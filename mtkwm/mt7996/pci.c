// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Ported for mt76wm (XNU) */
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "mt7996.h"
#include "mac.h"
#include "../mt76core/trace.h"

static LIST_HEAD(hif_list);
static DEFINE_SPINLOCK(hif_lock);
static u32 hif_idx;

static const struct pci_device_id mt7996_pci_device_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, MT7996_DEVICE_ID) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, MT7992_DEVICE_ID) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, MT7990_DEVICE_ID) },
	{ },
};

static const struct pci_device_id mt7996_hif_device_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, MT7996_DEVICE_ID_2) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, MT7992_DEVICE_ID_2) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, MT7990_DEVICE_ID_2) },
	{ },
};

/*
 * Ported for mt76wm (XNU): the dual-HIF companion lookup
 * (mt7996_pci_get_hif2 / pci_get_device) needs cross-device PCI
 * enumeration which the kext does not have - run in single-HIF mode.
 * TODO(M2): dual-HIF (MT7996/7992/7990 two-endpoint operation).
 */
static struct mt7996_hif *mt7996_pci_get_hif2(u32 idx)
{
	(void)idx;
	return NULL;
}

static void mt7996_put_hif2(struct mt7996_hif *hif)
{
	/* no reference counting in the port */
	(void)hif;
}

static struct mt7996_hif *mt7996_pci_init_hif2(struct pci_dev *pdev)
{
	(void)pdev;
	return NULL;
}

static int mt7996_pci_hif2_probe(struct pci_dev *pdev)
{
	struct mt7996_hif *hif;

	hif = devm_kzalloc(&pdev->dev, sizeof(*hif), GFP_KERNEL);
	if (!hif)
		return -ENOMEM;

	hif->dev = &pdev->dev;
	hif->regs = pcim_iomap_table(pdev)[0];
	hif->irq = pdev->irq;
	spin_lock_bh(&hif_lock);
	list_add(&hif->list, &hif_list);
	spin_unlock_bh(&hif_lock);
	pci_set_drvdata(pdev, hif);

	return 0;
}

static int mt7996_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	struct pci_dev *hif2_dev;
	struct mt7996_hif *hif2;
	struct mt7996_dev *dev;
	int irq, hif2_irq, ret;
	struct mt76_dev *mdev;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(36));
	if (ret)
		return ret;

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	mt76_pci_disable_aspm(pdev);

	if (id->device == MT7996_DEVICE_ID_2 ||
	    id->device == MT7992_DEVICE_ID_2 ||
	    id->device == MT7990_DEVICE_ID_2)
		return mt7996_pci_hif2_probe(pdev);

	dev = mt7996_mmio_probe(&pdev->dev, pcim_iomap_table(pdev)[0],
				id->device);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	mdev = &dev->mt76;
	mt7996_wfsys_reset(dev);
	hif2 = mt7996_pci_init_hif2(pdev);
	dev->hif2 = hif2;

	/* Ported for mt76wm (XNU): mt76_npu_init() (Airoha NPU offload)
	 * dropped - NPU is not supported in the kext. */

	ret = mt7996_mmio_wed_init(dev, pdev, false, &irq);
	if (ret < 0)
		goto free_wed_or_irq_vector;

	if (!ret) {
		ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
		if (ret < 0)
			goto free_device;

		irq = pdev->irq;
	}

	ret = devm_request_irq(mdev->dev, irq, mt7996_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto free_wed_or_irq_vector;

	mt76_wr(dev, MT_INT_MASK_CSR, 0);
	/* master switch of PCIe interrupt enable */
	mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);

	if (hif2) {
		hif2_dev = container_of(hif2->dev, struct pci_dev, dev);

		ret = mt7996_mmio_wed_init(dev, hif2_dev, true, &hif2_irq);
		if (ret < 0)
			goto free_hif2_wed_irq_vector;

		if (!ret) {
			ret = pci_alloc_irq_vectors(hif2_dev, 1, 1,
						    PCI_IRQ_ALL_TYPES);
			if (ret < 0)
				goto free_hif2;

			dev->hif2->irq = hif2_dev->irq;
			hif2_irq = dev->hif2->irq;
		}

		ret = devm_request_irq(mdev->dev, hif2_irq, mt7996_irq_handler,
				       IRQF_SHARED, KBUILD_MODNAME "-hif",
				       dev);
		if (ret)
			goto free_hif2_wed_irq_vector;

		mt76_wr(dev, MT_INT1_MASK_CSR, 0);
		/* master switch of PCIe tnterrupt enable */
		mt76_wr(dev, MT_PCIE1_MAC_INT_ENABLE, 0xff);
	}

	ret = mt7996_register_device(dev);
	if (ret)
		goto free_hif2_irq;

	return 0;

free_hif2_irq:
	if (dev->hif2)
		devm_free_irq(mdev->dev, hif2_irq, dev);
free_hif2_wed_irq_vector:
	if (dev->hif2) {
		if (mtk_wed_device_active(&dev->mt76.mmio.wed_hif2))
			mtk_wed_device_detach(&dev->mt76.mmio.wed_hif2);
		else
			pci_free_irq_vectors(hif2_dev);
	}
free_hif2:
	if (dev->hif2)
		mt7996_put_hif2(dev->hif2);
	devm_free_irq(mdev->dev, irq, dev);
free_wed_or_irq_vector:
	if (mtk_wed_device_active(&dev->mt76.mmio.wed))
		mtk_wed_device_detach(&dev->mt76.mmio.wed);
	else
		pci_free_irq_vectors(pdev);
free_device:
	mt76_free_device(&dev->mt76);

	return ret;
}

static void mt7996_hif_remove(struct pci_dev *pdev)
{
	struct mt7996_hif *hif = pci_get_drvdata(pdev);

	list_del(&hif->list);
}

static void mt7996_pci_remove(struct pci_dev *pdev)
{
	struct mt76_dev *mdev;
	struct mt7996_dev *dev;

	mdev = pci_get_drvdata(pdev);
	dev = container_of(mdev, struct mt7996_dev, mt76);
	mt7996_put_hif2(dev->hif2);
	mt7996_unregister_device(dev);
}

struct pci_driver mt7996_hif_driver = {
	.name		= KBUILD_MODNAME "_hif",
	.id_table	= mt7996_hif_device_table,
	.probe		= mt7996_pci_probe,
	.remove		= mt7996_hif_remove,
};

struct pci_driver mt7996_pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt7996_pci_device_table,
	.probe		= mt7996_pci_probe,
	.remove		= mt7996_pci_remove,
};

MODULE_DEVICE_TABLE(pci, mt7996_pci_device_table);
MODULE_DEVICE_TABLE(pci, mt7996_hif_device_table);
MODULE_FIRMWARE(MT7996_FIRMWARE_WA);
MODULE_FIRMWARE(MT7996_FIRMWARE_WM);
MODULE_FIRMWARE(MT7996_FIRMWARE_DSP);
MODULE_FIRMWARE(MT7996_ROM_PATCH);
MODULE_FIRMWARE(MT7992_FIRMWARE_WA);
MODULE_FIRMWARE(MT7992_FIRMWARE_WM);
MODULE_FIRMWARE(MT7992_FIRMWARE_DSP);
MODULE_FIRMWARE(MT7992_ROM_PATCH);
MODULE_FIRMWARE(MT7990_FIRMWARE_WM);
MODULE_FIRMWARE(MT7990_ROM_PATCH);

/* ==================================================================== */
/* XNU bridge (Ported for mt76wm (XNU))                                 */
/*                                                                      */
/* These entry points are called by mtkwm/mt7996/mt7996_entry.c         */
/* (strong symbol mtk_mt7996_probe()).  The softc pointer is opaque      */
/* here (only the entry file sees the IOKit side).                      */
/* ==================================================================== */
static struct mt7996_dev *g_xnu_dev;
static struct pci_dev g_xnu_pdev;
static bool g_xnu_full_probe;

/*
 * debugfs is not available in the kext (Linux mt7996/debugfs.c is not
 * ported); the register path only needs the symbol to exist - provided
 * by mt7996_stubs.c.
 */

extern "C" void *mt7996_xnu_get_mdev(struct mt76_softc *sc)
{
	(void)sc;
	return g_xnu_dev;
}

extern "C" int mt7996_xnu_probe(struct mt76_softc *sc, void *bar0_va,
				unsigned int vendor, unsigned int device)
{
	const struct pci_device_id *id = NULL;
	unsigned int i;
	int ret;

	(void)sc;
	for (i = 0; i < ARRAY_SIZE(mt7996_pci_device_table); i++) {
		const struct pci_device_id *e = &mt7996_pci_device_table[i];

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
		 * on the device id only (same as the MT7915 bridge).
		 */
		for (i = 0; i < ARRAY_SIZE(mt7996_pci_device_table); i++) {
			const struct pci_device_id *e = &mt7996_pci_device_table[i];

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
		/*
		 * HIF companion devices (MT7996/7992/7990 second PCIe
		 * endpoint): register as secondary HIF only, exactly like
		 * the Linux probe does.  No mt7996_dev is attached to this
		 * device.  Single-HIF mode in the port: accepted and logged.
		 */
		for (i = 0; i < ARRAY_SIZE(mt7996_hif_device_table); i++) {
			const struct pci_device_id *e =
				&mt7996_hif_device_table[i];

			if (!e->vendor && !e->device)
				break;
			if (e->device == device) {
				memset(&g_xnu_pdev, 0, sizeof(g_xnu_pdev));
				g_xnu_pdev.vendor = vendor;
				g_xnu_pdev.device = device;
				g_xnu_pdev.irq = 0;
				g_xnu_pdev.regs = bar0_va;
				g_xnu_pdev.dev.name = "mt7996-hif";

				XYLog("%s: device %04x registered as HIF2 "
				      "companion (single-HIF mode)\n",
				      __FUNCTION__, device);
				return mt7996_pci_hif2_probe(&g_xnu_pdev);
			}
		}
		XYLog("%s: unknown device %04x:%04x\n", __FUNCTION__,
		      vendor, device);
		return -ENODEV;
	}

	memset(&g_xnu_pdev, 0, sizeof(g_xnu_pdev));
	g_xnu_pdev.vendor = vendor;
	g_xnu_pdev.device = device;
	g_xnu_pdev.irq = 0;
	g_xnu_pdev.regs = bar0_va;
	g_xnu_pdev.dev.name = "mt7996";
	g_xnu_full_probe = false;

	ret = mt7996_pci_probe(&g_xnu_pdev, id);
	if (!ret) {
		struct mt76_dev *mdev = pci_get_drvdata(&g_xnu_pdev);

		g_xnu_dev = container_of(mdev, struct mt7996_dev, mt76);
		g_xnu_full_probe = true;
		XYLog("%s: probe ok, ASIC revision %04x\n", __FUNCTION__,
		      mdev->rev);
		return 0;
	}

	/*
	 * M1 fallback: the full probe sequence (eeprom/MCU/firmware) is not
	 * expected to complete yet, so fall back to the minimum bring-up
	 * contract: allocate the mt7996_dev, wire regs/bus_ops (the
	 * mt7996_rr/wr/rmw remap layer), log the chip id and set up the DMA
	 * rings.  The firmware/MCU bring-up is then attempted best-effort.
	 * TODO(M2): drop once the eeprom/MCU path works.
	 */
	{
		struct mt7996_dev *dev;
		u32 chipid;
		int dret, fret;

		XYLog("%s: full probe failed (%d), using M1 fallback\n",
		      __FUNCTION__, ret);

		dev = mt7996_mmio_probe(&g_xnu_pdev.dev, g_xnu_pdev.regs,
					device);
		if (IS_ERR(dev)) {
			XYLog("%s: mmio probe failed (%ld)\n", __FUNCTION__,
			      PTR_ERR(dev));
			return PTR_ERR(dev);
		}

		dev->phy.dev = dev;
		dev->phy.mt76 = &dev->mt76.phy;
		dev->mt76.phy.priv = &dev->phy;

		mt7996_wfsys_reset(dev);

		chipid = mt76_rr(dev, MT_HW_REV);
		XYLog("%s: ASIC revision: %04x (chipid 0x%08x)\n",
		      __FUNCTION__, dev->mt76.rev, chipid);

		mt76_wr(dev, MT_INT_MASK_CSR, 0);
		/* master switch of PCIe interrupt enable */
		mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);
		mt76_wr(dev, MT_INT_SOURCE_CSR, ~0);

		dret = mt7996_dma_init(dev);
		if (dret) {
			XYLog("%s: DMA init failed (%d)\n", __FUNCTION__,
			      dret);
			mt76_free_device(&dev->mt76);
			return dret;
		}
		XYLog("%s: DMA rings up\n", __FUNCTION__);

		/* Firmware: best effort, stay alive without it (M1). */
		fret = mt7996_mcu_init(dev);
		if (fret)
			XYLog("%s: firmware/MCU bring-up failed (%d), "
			      "staying in M1 state\n", __FUNCTION__, fret);
		else
			XYLog("%s: firmware init done\n", __FUNCTION__);

		g_xnu_dev = dev;
		return 0;
	}
}

extern "C" void mt7996_xnu_remove(struct mt76_softc *sc)
{
	(void)sc;
	if (!g_xnu_dev)
		return;

	if (g_xnu_full_probe) {
		mt7996_pci_remove(&g_xnu_pdev);
	} else {
		/* M1 fallback never reached mt7996_register_device(). */
		mt7996_mcu_exit(g_xnu_dev);
		mt7996_dma_cleanup(g_xnu_dev);
		mt76_free_device(&g_xnu_dev->mt76);
	}
	g_xnu_dev = NULL;
	g_xnu_full_probe = false;
}

/*
 * Interrupt filter (primary context): mask the PCIe interrupt master
 * switch (Linux mt7996_irq_handler() first half) and report whether the
 * workloop action should run.
 */
extern "C" bool mt7996_xnu_intr_filter(struct mt76_softc *sc)
{
	struct mt7996_dev *dev = g_xnu_dev;

	(void)sc;
	if (!dev)
		return false;

	/* mask + ack, then let the workloop action dispatch (irq_tasklet) */
	mt76_wr(dev, MT_INT_MASK_CSR, 0);
	if (dev->hif2)
		mt76_wr(dev, MT_INT1_MASK_CSR, 0);

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mphy.state))
		return false;

	return true;
}

extern "C" void mt7996_xnu_intr_action(struct mt76_softc *sc)
{
	struct mt7996_dev *dev = g_xnu_dev;

	(void)sc;
	if (!dev)
		return;

	/* bottom half: read/ack the interrupt status and run NAPI */
	mt7996_irq_tasklet(&dev->mt76.irq_tasklet);
}
