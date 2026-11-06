// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Ported for mt76wm (XNU) */
/* Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 *         Felix Fietkau <nbd@nbd.name>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "mt7615.h"
#include "mcu.h"

static const struct pci_device_id mt7615_pci_device_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7615) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7663) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7611) },
	{ },
};

static int mt7615_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	const u32 *map;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		return ret;

	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		goto error;

	mt76_pci_disable_aspm(pdev);

	map = id->device == 0x7663 ? mt7663e_reg_map : mt7615e_reg_map;
	ret = mt7615_mmio_probe(&pdev->dev, pcim_iomap_table(pdev)[0],
				pdev->irq, map);
	if (ret)
		goto error;

	return 0;
error:
	pci_free_irq_vectors(pdev);

	return ret;
}

static void mt7615_pci_remove(struct pci_dev *pdev)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);

	mt7615_unregister_device(dev);
	devm_free_irq(&pdev->dev, pdev->irq, dev);
	pci_free_irq_vectors(pdev);
}

#ifdef CONFIG_PM
static int mt7615_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	bool hif_suspend;
	int i, err;

	err = mt76_connac_pm_wake(&dev->mphy, &dev->pm);
	if (err < 0)
		return err;

	hif_suspend = !test_bit(MT76_STATE_SUSPEND, &dev->mphy.state) &&
		      mt7615_firmware_offload(dev);
	if (hif_suspend) {
		err = mt76_connac_mcu_set_hif_suspend(mdev, true, true);
		if (err)
			return err;
	}

	napi_disable(&mdev->tx_napi);
	mt76_worker_disable(&mdev->tx_worker);

	mt76_for_each_q_rx(mdev, i) {
		napi_disable(&mdev->napi[i]);
	}
	tasklet_kill(&mdev->irq_tasklet);

	mt7615_dma_reset(dev);

	err = mt7615_wait_pdma_busy(dev);
	if (err)
		goto restore;

	if (is_mt7663(mdev)) {
		mt76_set(dev, MT_PDMA_SLP_PROT, MT_PDMA_AXI_SLPPROT_ENABLE);
		if (!mt76_poll_msec(dev, MT_PDMA_SLP_PROT,
				    MT_PDMA_AXI_SLPPROT_RDY,
				    MT_PDMA_AXI_SLPPROT_RDY, 1000)) {
			dev_err(mdev->dev, "PDMA sleep protection failed\n");
			err = -EIO;
			goto restore;
		}
	}

	pci_enable_wake(pdev, pci_choose_state(pdev, state), true);
	pci_save_state(pdev);
	err = pci_set_power_state(pdev, pci_choose_state(pdev, state));
	if (err)
		goto restore;

	err = mt7615_mcu_set_fw_ctrl(dev);
	if (err)
		goto restore;

	return 0;

restore:
	mt76_for_each_q_rx(mdev, i) {
		napi_enable(&mdev->napi[i]);
	}
	napi_enable(&mdev->tx_napi);
	if (hif_suspend)
		mt76_connac_mcu_set_hif_suspend(mdev, false, true);

	return err;
}

static int mt7615_pci_resume(struct pci_dev *pdev)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	bool pdma_reset;
	int i, err;

	err = mt7615_mcu_set_drv_ctrl(dev);
	if (err < 0)
		return err;

	err = pci_set_power_state(pdev, PCI_D0);
	if (err)
		return err;

	pci_restore_state(pdev);

	if (is_mt7663(&dev->mt76)) {
		mt76_clear(dev, MT_PDMA_SLP_PROT, MT_PDMA_AXI_SLPPROT_ENABLE);
		mt76_wr(dev, MT_PCIE_IRQ_ENABLE, 1);
	}

	pdma_reset = !mt76_rr(dev, MT_WPDMA_TX_RING0_CTRL0) &&
		     !mt76_rr(dev, MT_WPDMA_TX_RING0_CTRL1);
	if (pdma_reset)
		dev_err(mdev->dev, "PDMA engine must be reinitialized\n");

	mt76_worker_enable(&mdev->tx_worker);

	mt76_for_each_q_rx(mdev, i) {
		napi_enable(&mdev->napi[i]);
	}
	napi_enable(&mdev->tx_napi);

	local_bh_disable();
	mt76_for_each_q_rx(mdev, i) {
		napi_schedule(&mdev->napi[i]);
	}
	napi_schedule(&mdev->tx_napi);
	local_bh_enable();

	if (!test_bit(MT76_STATE_SUSPEND, &dev->mphy.state) &&
	    mt7615_firmware_offload(dev))
		err = mt76_connac_mcu_set_hif_suspend(mdev, false, true);

	return err;
}
#endif /* CONFIG_PM */

struct pci_driver mt7615_pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt7615_pci_device_table,
	.probe		= mt7615_pci_probe,
	.remove		= mt7615_pci_remove,
#ifdef CONFIG_PM
	.suspend	= mt7615_pci_suspend,
	.resume		= mt7615_pci_resume,
#endif /* CONFIG_PM */
};

MODULE_DEVICE_TABLE(pci, mt7615_pci_device_table);
MODULE_FIRMWARE(MT7615_FIRMWARE_CR4);
MODULE_FIRMWARE(MT7615_FIRMWARE_N9);
MODULE_FIRMWARE(MT7615_ROM_PATCH);
MODULE_FIRMWARE(MT7663_OFFLOAD_FIRMWARE_N9);
MODULE_FIRMWARE(MT7663_OFFLOAD_ROM_PATCH);
MODULE_FIRMWARE(MT7663_FIRMWARE_N9);
MODULE_FIRMWARE(MT7663_ROM_PATCH);

/* ==================================================================== */
/* XNU bridge (Ported for mt76wm (XNU))                                 */
/*                                                                      */
/* These entry points are called by mtkwm/mt7615/mt7615_entry.c         */
/* (strong symbol mtk_mt7615_probe()).  The softc pointer is opaque      */
/* here (only the entry file sees the IOKit side).                      */
/* ==================================================================== */
static struct mt7615_dev *g_xnu_dev;
static struct pci_dev g_xnu_pdev;
static bool g_xnu_full_probe;

/*
 * debugfs is not ported into the kext (Linux mt7615/debugfs.c is
 * skipped); mt7615_register_device() only needs the symbol to exist.
 */
int mt7615_init_debugfs(struct mt7615_dev *dev)
{
	(void)dev;
	return 0;
}

extern "C" void *mt7615_xnu_get_mdev(struct mt76_softc *sc)
{
	(void)sc;
	return g_xnu_dev;
}

extern "C" int mt7615_xnu_probe(struct mt76_softc *sc, void *bar0_va,
				unsigned int vendor, unsigned int device)
{
	const struct pci_device_id *id = NULL;
	const u32 *map;
	unsigned int i;
	int ret;

	(void)sc;
	for (i = 0; i < ARRAY_SIZE(mt7615_pci_device_table); i++) {
		const struct pci_device_id *e = &mt7615_pci_device_table[i];

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
		 * on the device id only (same as the mt7603 bridge).
		 */
		for (i = 0; i < ARRAY_SIZE(mt7615_pci_device_table); i++) {
			const struct pci_device_id *e = &mt7615_pci_device_table[i];

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

	/* device-series discrimination: MT7663 vs MT7615 reg maps */
	map = id->device == 0x7663 ? mt7663e_reg_map : mt7615e_reg_map;
	XYLog("%s: %s series device %04x\n", __FUNCTION__,
	      id->device == 0x7663 ? "MT7663" : "MT7615", id->device);

	memset(&g_xnu_pdev, 0, sizeof(g_xnu_pdev));
	g_xnu_pdev.vendor = vendor;
	g_xnu_pdev.device = device;
	g_xnu_pdev.irq = 0;
	g_xnu_pdev.regs = bar0_va;
	g_xnu_pdev.dev.name = "mt7615";
	g_xnu_full_probe = false;

	ret = mt7615_pci_probe(&g_xnu_pdev, id);
	if (!ret) {
		struct mt76_dev *mdev = pci_get_drvdata(&g_xnu_pdev);

		g_xnu_dev = container_of(mdev, struct mt7615_dev, mt76);
		g_xnu_full_probe = true;
		XYLog("%s: probe ok, ASIC revision %04x\n", __FUNCTION__,
		      mdev->rev);
		return 0;
	}

	/*
	 * M1 fallback: the full probe sequence (eeprom/register_device/MCU)
	 * is not expected to complete yet, so fall back to the minimum
	 * bring-up contract: allocate the mt7615_dev, wire regs/bus_ops,
	 * read MT_HW_CHIPID into the log, set up the DMA rings and try the
	 * firmware download as far as it goes.  TODO(M2): drop once the
	 * eeprom/MCU path works.
	 */
	{
		static const struct mt76_driver_ops xnu_drv_ops = {
			/* txwi_size = txd size + txp size */
			.txwi_size = MT_TXD_SIZE +
				     sizeof(struct mt76_connac_txp_common),
			.drv_flags = MT_DRV_TXWI_NO_FREE | MT_DRV_HW_MGMT_TXQ,
			.survey_flags = SURVEY_INFO_TIME_TX |
					SURVEY_INFO_TIME_RX |
					SURVEY_INFO_TIME_BSS_RX,
			.token_size = MT7615_TOKEN_SIZE,
			.tx_prepare_skb = mt7615_tx_prepare_skb,
			.tx_complete_skb = mt76_connac_tx_complete_skb,
			.rx_check = mt7615_rx_check,
			.rx_skb = mt7615_queue_rx_skb,
			.sta_add = mt7615_mac_sta_add,
			.sta_remove = mt7615_mac_sta_remove,
			.update_survey = mt7615_update_channel,
			.set_channel = mt7615_set_channel,
		};
		struct mt76_dev *mdev;
		struct mt7615_dev *dev;
		struct ieee80211_ops *ops;
		u16 chipid;
		int fret;

		XYLog("%s: full probe failed (%d), using M1 fallback\n",
		      __FUNCTION__, ret);

		ops = (struct ieee80211_ops *)devm_kmemdup(&g_xnu_pdev.dev,
				&mt7615_ops, sizeof(mt7615_ops), GFP_KERNEL);
		if (!ops)
			return -ENOMEM;

		mdev = mt76_alloc_device(&g_xnu_pdev.dev,
					 sizeof(struct mt7615_dev),
					 ops, &xnu_drv_ops);
		if (!mdev)
			return -ENOMEM;

		dev = container_of(mdev, struct mt7615_dev, mt76);
		mt76_mmio_init(mdev, g_xnu_pdev.regs);
		dev->reg_map = map;
		dev->ops = ops;
		dev->bus_ops = dev->mt76.bus;

		chipid = mt76_rr(dev, MT_HW_CHIPID);
		mdev->rev = (chipid << 16) |
			    (mt76_rr(dev, MT_HW_REV) & 0xff);
		XYLog("%s: ASIC revision: %04x (chipid 0x%04x, %s series, "
		      "dbdc %s)\n", __FUNCTION__, mdev->rev, chipid,
		      is_mt7663(mdev) ? "MT7663" :
		      is_mt7622(mdev) ? "MT7622" : "MT7615",
		      is_mt7615(mdev) ? "capable" : "n/a");

		mt76_wr(dev, MT_INT_MASK_CSR, 0);
		mt76_wr(dev, MT_INT_SOURCE_CSR, ~0);

		fret = mt7615_dma_init(dev);
		if (fret) {
			XYLog("%s: DMA init failed (%d)\n", __FUNCTION__, fret);
			mt76_free_device(mdev);
			return fret;
		}
		XYLog("%s: DMA rings up\n", __FUNCTION__);

		/* Firmware: best effort, stay alive without it (M1). */
		fret = mt7615_mcu_init(dev);
		if (fret)
			XYLog("%s: firmware/MCU bring-up failed (%d), "
			      "staying in M1 state\n", __FUNCTION__, fret);
		else
			XYLog("%s: firmware init done\n", __FUNCTION__);

		g_xnu_dev = dev;
		return 0;
	}
}

extern "C" void mt7615_xnu_remove(struct mt76_softc *sc)
{
	(void)sc;
	if (!g_xnu_dev)
		return;

	if (g_xnu_full_probe) {
		mt7615_pci_remove(&g_xnu_pdev);
	} else {
		mt7615_dma_cleanup(g_xnu_dev);
		mt76_free_device(&g_xnu_dev->mt76);
	}
	g_xnu_dev = NULL;
	g_xnu_full_probe = false;
}

/*
 * Interrupt filter (primary context): mask + ack the interrupt status and
 * stash the masked cause for the workloop action (Linux
 * mt7615_irq_handler()/mt7615_irq_tasklet() split in two halves).
 */
static u32 g_xnu_intr;

extern "C" bool mt7615_xnu_intr_filter(struct mt76_softc *sc)
{
	struct mt7615_dev *dev = g_xnu_dev;
	u32 intr;

	(void)sc;
	if (!dev)
		return false;

	mt76_wr(dev, MT_INT_MASK_CSR, 0);

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mphy.state))
		return false;

	intr = mt76_rr(dev, MT_INT_SOURCE_CSR);
	intr &= dev->mt76.mmio.irqmask;
	mt76_wr(dev, MT_INT_SOURCE_CSR, intr);
	if (!intr)
		return false;

	g_xnu_intr |= intr;
	return true;
}

extern "C" void mt7615_xnu_intr_action(struct mt76_softc *sc)
{
	struct mt7615_dev *dev = g_xnu_dev;
	u32 intr = g_xnu_intr;
	u32 mask = 0, tx_mcu_mask, mcu_int;

	(void)sc;
	g_xnu_intr = 0;
	if (!dev || !intr)
		return;

	/* same dispatch as Linux mt7615_irq_tasklet() */
	tx_mcu_mask = mt7615_tx_mcu_int_mask(dev);

	mask |= intr & MT_INT_RX_DONE_ALL;
	if (intr & tx_mcu_mask)
		mask |= tx_mcu_mask;
	mt76_set_irq_mask(&dev->mt76, MT_INT_MASK_CSR, mask, 0);

	if (intr & tx_mcu_mask)
		napi_schedule(&dev->mt76.tx_napi);

	if (intr & MT_INT_RX_DONE(0))
		napi_schedule(&dev->mt76.napi[0]);

	if (intr & MT_INT_RX_DONE(1))
		napi_schedule(&dev->mt76.napi[1]);

	if (!(intr & (MT_INT_MCU_CMD | MT7663_INT_MCU_CMD)))
		return;

	if (is_mt7663(&dev->mt76)) {
		mcu_int = mt76_rr(dev, MT_MCU2HOST_INT_STATUS);
		mcu_int &= MT7663_MCU_CMD_ERROR_MASK;
		mt76_wr(dev, MT_MCU2HOST_INT_STATUS, mcu_int);
	} else {
		mcu_int = mt76_rr(dev, MT_MCU_CMD);
		mcu_int &= MT_MCU_CMD_ERROR_MASK;
	}

	if (!mcu_int)
		return;

	/*
	 * reset_work/reset_wait are only initialized by
	 * mt7615_register_device(); in the M1 fallback just log the MCU
	 * error cause.
	 */
	if (g_xnu_full_probe) {
		dev->reset_state = mcu_int;
		queue_work(dev->mt76.wq, &dev->reset_work);
		wake_up(&dev->reset_wait);
	} else {
		XYLog("%s: MCU command error 0x%08x (M1: no reset work)\n",
		      __FUNCTION__, mcu_int);
	}
}
