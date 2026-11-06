// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Ported for mt76wm (XNU) */
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "mt76x0.h"
#include "mcu.h"

static int mt76x0e_start(struct ieee80211_hw *hw)
{
	struct mt76x02_dev *dev = (struct mt76x02_dev *)hw->priv;

	mt76x02_mac_start(dev);
	mt76x0_phy_calibrate(dev, true);
	ieee80211_queue_delayed_work(dev->mt76.hw, &dev->mphy.mac_work,
				     MT_MAC_WORK_INTERVAL);
	ieee80211_queue_delayed_work(dev->mt76.hw, &dev->cal_work,
				     MT_CALIBRATE_INTERVAL);
	set_bit(MT76_STATE_RUNNING, &dev->mphy.state);

	return 0;
}

static void mt76x0e_stop_hw(struct mt76x02_dev *dev)
{
	cancel_delayed_work_sync(&dev->cal_work);
	cancel_delayed_work_sync(&dev->mphy.mac_work);
	clear_bit(MT76_RESTART, &dev->mphy.state);

	if (!mt76_poll(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_TX_DMA_BUSY,
		       0, 1000))
		dev_warn(dev->mt76.dev, "TX DMA did not stop\n");
	mt76_clear(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_TX_DMA_EN);

	mt76x0_mac_stop(dev);

	if (!mt76_poll(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_RX_DMA_BUSY,
		       0, 1000))
		dev_warn(dev->mt76.dev, "TX DMA did not stop\n");
	mt76_clear(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_RX_DMA_EN);
}

static void mt76x0e_stop(struct ieee80211_hw *hw, bool suspend)
{
	struct mt76x02_dev *dev = (struct mt76x02_dev *)hw->priv;

	clear_bit(MT76_STATE_RUNNING, &dev->mphy.state);
	mt76x0e_stop_hw(dev);
}

static void
mt76x0e_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	      u32 queues, bool drop)
{
}

static const struct ieee80211_ops mt76x0e_ops = {
	.add_chanctx = ieee80211_emulate_add_chanctx,
	.remove_chanctx = ieee80211_emulate_remove_chanctx,
	.change_chanctx = ieee80211_emulate_change_chanctx,
	.switch_vif_chanctx = ieee80211_emulate_switch_vif_chanctx,
	.tx = mt76x02_tx,
	.start = mt76x0e_start,
	.stop = mt76x0e_stop,
	.add_interface = mt76x02_add_interface,
	.remove_interface = mt76x02_remove_interface,
	.config = mt76x0_config,
	.configure_filter = mt76x02_configure_filter,
	.bss_info_changed = mt76x02_bss_info_changed,
	.sta_state = mt76_sta_state,
	.sta_pre_rcu_remove = mt76_sta_pre_rcu_remove,
	.set_key = mt76x02_set_key,
	.conf_tx = mt76x02_conf_tx,
	.sw_scan_start = mt76_sw_scan,
	.sw_scan_complete = mt76x02_sw_scan_complete,
	.ampdu_action = mt76x02_ampdu_action,
	.sta_rate_tbl_update = mt76x02_sta_rate_tbl_update,
	.wake_tx_queue = mt76_wake_tx_queue,
	.get_survey = mt76_get_survey,
	.get_txpower = mt76_get_txpower,
	.flush = mt76x0e_flush,
	.set_tim = mt76_set_tim,
	.release_buffered_frames = mt76_release_buffered_frames,
	.set_coverage_class = mt76x02_set_coverage_class,
	.set_rts_threshold = mt76x02_set_rts_threshold,
	.get_antenna = mt76_get_antenna,
	.reconfig_complete = mt76x02_reconfig_complete,
	.set_sar_specs = mt76x0_set_sar_specs,
};

static int mt76x0e_init_hardware(struct mt76x02_dev *dev, bool resume)
{
	int err;

	mt76x0_chip_onoff(dev, true, false);
	if (!mt76x02_wait_for_mac(&dev->mt76))
		return -ETIMEDOUT;

	mt76x02_dma_disable(dev);
	err = mt76x0e_mcu_init(dev);
	if (err < 0)
		return err;

	if (!resume) {
		err = mt76x02_dma_init(dev);
		if (err < 0)
			return err;
	}

	err = mt76x0_init_hardware(dev);
	if (err < 0)
		return err;

	mt76x02e_init_beacon_config(dev);

	if (mt76_chip(&dev->mt76) == 0x7610) {
		u16 val;

		mt76_clear(dev, MT_COEXCFG0, BIT(0));

		val = mt76x02_eeprom_get(dev, MT_EE_NIC_CONF_0);
		if (!(val & MT_EE_NIC_CONF_0_PA_IO_CURRENT))
			mt76_set(dev, MT_XO_CTRL7, 0xc03);
	}

	mt76_clear(dev, 0x110, BIT(9));
	mt76_set(dev, MT_MAX_LEN_CFG, BIT(13));

	return 0;
}

static int mt76x0e_register_device(struct mt76x02_dev *dev)
{
	int err;

	err = mt76x0e_init_hardware(dev, false);
	if (err < 0)
		return err;

	err = mt76x0_register_device(dev);
	if (err < 0)
		return err;

	set_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);

	return 0;
}

static int
mt76x0e_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	static const struct mt76_driver_ops drv_ops = {
		.txwi_size = sizeof(struct mt76x02_txwi),
		.drv_flags = MT_DRV_TX_ALIGNED4_SKBS |
			     MT_DRV_SW_RX_AIRTIME |
			     MT_DRV_IGNORE_TXS_FAILED,
		.survey_flags = SURVEY_INFO_TIME_TX,
		.update_survey = mt76x02_update_channel,
		.set_channel = mt76x0_set_channel,
		.tx_prepare_skb = mt76x02_tx_prepare_skb,
		.tx_complete_skb = mt76x02_tx_complete_skb,
		.rx_skb = mt76x02_queue_rx_skb,
		.rx_poll_complete = mt76x02_rx_poll_complete,
		.sta_ps = mt76x02_sta_ps,
		.sta_add = mt76x02_sta_add,
		.sta_remove = mt76x02_sta_remove,
	};
	struct mt76x02_dev *dev;
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

	mt76_pci_disable_aspm(pdev);

	mdev = mt76_alloc_device(&pdev->dev, sizeof(*dev), &mt76x0e_ops,
				 &drv_ops);
	if (!mdev)
		return -ENOMEM;

	dev = container_of(mdev, struct mt76x02_dev, mt76);
	mutex_init(&dev->phy_mutex);

	mt76_mmio_init(mdev, pcim_iomap_table(pdev)[0]);

	mdev->rev = mt76_rr(dev, MT_ASIC_VERSION);
	dev_info(mdev->dev, "ASIC revision: %08x\n", mdev->rev);

	mt76_wr(dev, MT_INT_MASK_CSR, 0);

	ret = devm_request_irq(mdev->dev, pdev->irq, mt76x02_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto error;

	ret = mt76x0e_register_device(dev);
	if (ret < 0)
		goto error;

	return 0;

error:
	mt76_free_device(&dev->mt76);

	return ret;
}

static void mt76x0e_cleanup(struct mt76x02_dev *dev)
{
	clear_bit(MT76_STATE_INITIALIZED, &dev->mphy.state);
	tasklet_disable(&dev->mt76.pre_tbtt_tasklet);
	mt76x0_chip_onoff(dev, false, false);
	mt76x0e_stop_hw(dev);
	mt76_dma_cleanup(&dev->mt76);
	mt76x02_mcu_cleanup(dev);
}

static void
mt76x0e_remove(struct pci_dev *pdev)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev, mt76);

	mt76_unregister_device(mdev);
	mt76x0e_cleanup(dev);
	mt76_free_device(mdev);
}

#ifdef CONFIG_PM
static int mt76x0e_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev, mt76);
	int i;

	mt76_worker_disable(&mdev->tx_worker);
	for (i = 0; i < ARRAY_SIZE(mdev->phy.q_tx); i++)
		mt76_queue_tx_cleanup(dev, mdev->phy.q_tx[i], true);
	for (i = 0; i < ARRAY_SIZE(mdev->q_mcu); i++)
		mt76_queue_tx_cleanup(dev, mdev->q_mcu[i], true);
	napi_disable(&mdev->tx_napi);

	mt76_for_each_q_rx(mdev, i)
		napi_disable(&mdev->napi[i]);

	mt76x02_dma_disable(dev);
	mt76x02_mcu_cleanup(dev);
	mt76x0_chip_onoff(dev, false, false);

	pci_enable_wake(pdev, pci_choose_state(pdev, state), true);
	pci_save_state(pdev);

	return pci_set_power_state(pdev, pci_choose_state(pdev, state));
}

static int mt76x0e_resume(struct pci_dev *pdev)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev, mt76);
	int err, i;

	err = pci_set_power_state(pdev, PCI_D0);
	if (err)
		return err;

	pci_restore_state(pdev);

	mt76_worker_enable(&mdev->tx_worker);

	mt76_for_each_q_rx(mdev, i) {
		mt76_queue_rx_reset(dev, i);
		napi_enable(&mdev->napi[i]);
	}
	napi_enable(&mdev->tx_napi);

	local_bh_disable();
	mt76_for_each_q_rx(mdev, i) {
		napi_schedule(&mdev->napi[i]);
	}
	napi_schedule(&mdev->tx_napi);
	local_bh_enable();

	return mt76x0e_init_hardware(dev, true);
}
#endif /* CONFIG_PM */

static const struct pci_device_id mt76x0e_device_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7610) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7630) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7650) },
	{ },
};

MODULE_DEVICE_TABLE(pci, mt76x0e_device_table);
MODULE_FIRMWARE(MT7610E_FIRMWARE);
MODULE_FIRMWARE(MT7650E_FIRMWARE);
MODULE_DESCRIPTION("MediaTek MT76x0E (PCIe) wireless driver");
MODULE_LICENSE("Dual BSD/GPL");

static struct pci_driver mt76x0e_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt76x0e_device_table,
	.probe		= mt76x0e_probe,
	.remove		= mt76x0e_remove,
#ifdef CONFIG_PM
	.suspend	= mt76x0e_suspend,
	.resume		= mt76x0e_resume,
#endif /* CONFIG_PM */
};

module_pci_driver(mt76x0e_driver);

/* ==================================================================== */
/* XNU bridge (Ported for mt76wm (XNU))                                 */
/*                                                                      */
/* These entry points are called by mtkwm/mt76x0/mt76x0_entry.c         */
/* (strong symbol mtk_mt76x0_probe()).  The softc pointer is opaque      */
/* here (only the entry file sees the IOKit side).  Modeled on the       */
/* mt7603 bridge (mtkwm/mt7603/pci.c).                                  */
/* ==================================================================== */
static struct mt76x02_dev *g_xnu_dev;
static struct pci_dev g_xnu_pdev;
static bool g_xnu_full_probe;

extern "C" void *mt76x0_xnu_get_mdev(struct mt76_softc *sc)
{
	(void)sc;
	return g_xnu_dev;
}

extern "C" int mt76x0_xnu_probe(struct mt76_softc *sc, void *bar0_va,
				unsigned int vendor, unsigned int device)
{
	const struct pci_device_id *id = NULL;
	unsigned int i;
	int ret;

	(void)sc;
	for (i = 0; i < ARRAY_SIZE(mt76x0e_device_table); i++) {
		const struct pci_device_id *e = &mt76x0e_device_table[i];

		if (!e->vendor && !e->device)
			break;
		if (e->device == device) {
			/*
			 * The linuxcompat PCI_VENDOR_ID_MEDIATEK macro does
			 * not match the real PCIe vendor id (0x14c3), so
			 * match on the device id only (same as mt7603).
			 */
			id = e;
			break;
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
	g_xnu_pdev.dev.name = "mt76x0";
	g_xnu_full_probe = false;

	ret = mt76x0e_probe(&g_xnu_pdev, id);
	if (!ret) {
		struct mt76_dev *mdev =
			(struct mt76_dev *)pci_get_drvdata(&g_xnu_pdev);

		g_xnu_dev = container_of(mdev, struct mt76x02_dev, mt76);
		g_xnu_full_probe = true;
		XYLog("%s: probe ok, ASIC revision %08x\n", __FUNCTION__,
		      mdev->rev);
		return 0;
	}

	/*
	 * M1 fallback: the full probe sequence (eeprom/MCU/firmware) is not
	 * expected to complete yet, so fall back to the minimum bring-up
	 * contract: allocate the mt76x02_dev, wire regs/bus_ops, read
	 * MT_ASIC_VERSION into the log, set up the DMA rings and try the
	 * firmware download as far as it goes.  TODO(M2): drop once the
	 * eeprom/MCU path works.
	 */
	{
		static const struct mt76_driver_ops drv_ops = {
			.txwi_size = sizeof(struct mt76x02_txwi),
			.drv_flags = MT_DRV_TX_ALIGNED4_SKBS |
				     MT_DRV_SW_RX_AIRTIME |
				     MT_DRV_IGNORE_TXS_FAILED,
			.survey_flags = SURVEY_INFO_TIME_TX,
			.update_survey = mt76x02_update_channel,
			.set_channel = mt76x0_set_channel,
			.tx_prepare_skb = mt76x02_tx_prepare_skb,
			.tx_complete_skb = mt76x02_tx_complete_skb,
			.rx_skb = mt76x02_queue_rx_skb,
			.rx_poll_complete = mt76x02_rx_poll_complete,
			.sta_ps = mt76x02_sta_ps,
			.sta_add = mt76x02_sta_add,
			.sta_remove = mt76x02_sta_remove,
		};
		struct mt76_dev *mdev;
		struct mt76x02_dev *dev;
		int fret;

		XYLog("%s: full probe failed (%d), using M1 fallback\n",
		      __FUNCTION__, ret);

		mdev = mt76_alloc_device(&g_xnu_pdev.dev,
					 sizeof(struct mt76x02_dev),
					 &mt76x0e_ops, &drv_ops);
		if (!mdev)
			return -ENOMEM;

		dev = container_of(mdev, struct mt76x02_dev, mt76);
		mutex_init(&dev->phy_mutex);
		mt76_mmio_init(mdev, g_xnu_pdev.regs);

		mt76x0_chip_onoff(dev, true, false);
		if (!mt76x02_wait_for_mac(&dev->mt76))
			XYLog("%s: MAC did not come up\n", __FUNCTION__);

		mdev->rev = mt76_rr(dev, MT_ASIC_VERSION);
		XYLog("%s: ASIC revision: %08x\n", __FUNCTION__, mdev->rev);

		mt76_wr(dev, MT_INT_MASK_CSR, 0);
		mt76_wr(dev, MT_INT_SOURCE_CSR, ~0);

		fret = mt76x02_dma_init(dev);
		if (fret) {
			XYLog("%s: DMA init failed (%d)\n", __FUNCTION__,
			      fret);
			mt76_free_device(mdev);
			return fret;
		}
		XYLog("%s: DMA rings up\n", __FUNCTION__);

		/* Firmware: best effort, stay alive without it (M1). */
		fret = mt76x0e_mcu_init(dev);
		if (fret)
			XYLog("%s: firmware/MCU bring-up failed (%d), "
			      "staying in M1 state\n", __FUNCTION__, fret);
		else
			XYLog("%s: firmware init done\n", __FUNCTION__);

		g_xnu_dev = dev;
		return 0;
	}
}

extern "C" void mt76x0_xnu_remove(struct mt76_softc *sc)
{
	(void)sc;
	if (!g_xnu_dev)
		return;

	if (g_xnu_full_probe) {
		mt76x0e_remove(&g_xnu_pdev);
	} else {
		mt76x0e_cleanup(g_xnu_dev);
		mt76_free_device(&g_xnu_dev->mt76);
	}
	g_xnu_dev = NULL;
	g_xnu_full_probe = false;
}

/*
 * Interrupt filter (primary context): ack the interrupt status and stash
 * the masked cause for the workloop action (Linux mt76x02_irq_handler()
 * split in two halves).
 */
static u32 g_xnu_intr;

extern "C" bool mt76x0_xnu_intr_filter(struct mt76_softc *sc)
{
	struct mt76x02_dev *dev = g_xnu_dev;
	u32 intr;

	(void)sc;
	if (!dev)
		return false;

	intr = mt76_rr(dev, MT_INT_SOURCE_CSR);
	intr &= dev->mt76.mmio.irqmask;
	mt76_wr(dev, MT_INT_SOURCE_CSR, intr);

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mphy.state))
		return false;

	if (!intr)
		return false;

	g_xnu_intr |= intr;
	return true;
}

extern "C" void mt76x0_xnu_intr_action(struct mt76_softc *sc)
{
	struct mt76x02_dev *dev = g_xnu_dev;
	u32 intr = g_xnu_intr;
	u32 mask;

	(void)sc;
	g_xnu_intr = 0;
	if (!dev || !intr)
		return;

	/* same dispatch as Linux mt76x02_irq_handler() */
	mask = intr & (MT_INT_RX_DONE_ALL | MT_INT_GPTIMER);
	if (intr & (MT_INT_TX_DONE_ALL | MT_INT_TX_STAT))
		mask |= MT_INT_TX_DONE_ALL;

	mt76x02_irq_disable(dev, mask);

	if (intr & MT_INT_RX_DONE(0))
		napi_schedule(&dev->mt76.napi[0]);

	if (intr & MT_INT_RX_DONE(1))
		napi_schedule(&dev->mt76.napi[1]);

	if (intr & MT_INT_PRE_TBTT)
		tasklet_schedule(&dev->mt76.pre_tbtt_tasklet);

	/* send buffered multicast frames now */
	if (intr & MT_INT_TBTT) {
		if (dev->mt76.csa_complete)
			mt76_csa_finish(&dev->mt76);
		else
			mt76_queue_kick(dev, dev->mphy.q_tx[MT_TXQ_PSD]);
	}

	if (intr & MT_INT_TX_STAT)
		mt76x02_mac_poll_tx_status(dev, true);

	if (intr & (MT_INT_TX_STAT | MT_INT_TX_DONE_ALL))
		napi_schedule(&dev->mt76.tx_napi);

	if (intr & MT_INT_GPTIMER)
		tasklet_schedule(&dev->dfs_pd.dfs_tasklet);
}
