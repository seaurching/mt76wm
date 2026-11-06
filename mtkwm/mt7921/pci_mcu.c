// Ported for mt76wm (XNU).
// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Copyright (C) 2021 MediaTek Inc. */

#include "mt7921.h"
#include "mcu.h"

int mt7921e_driver_own(struct mt792x_dev *dev)
{
	u32 reg = mt7921_reg_map_l1(dev, MT_TOP_LPCR_HOST_BAND0);
	int attempt;

	for (attempt = 0; attempt < 3; attempt++) {
		mt76_wr(dev, reg, MT_TOP_LPCR_HOST_DRV_OWN);
		if (mt76_poll_msec(dev, reg, MT_TOP_LPCR_HOST_FW_OWN,
				   0, 500))
			return 0;
		XYLog("mtk-dbg: driver_own attempt %d: lpcr=0x%x (reg 0x%x), value now 0x%x\n",
		      attempt, (unsigned)MT_TOP_LPCR_HOST_DRV_OWN,
		      reg, mt76_rr(dev, reg));
	}
	dev_err(dev->mt76.dev, "Timeout for driver own\n");
	return -EIO;
}

static int
mt7921_mcu_send_message(struct mt76_dev *mdev, struct sk_buff *skb,
			int cmd, int *seq)
{
	struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);
	enum mt76_mcuq_id txq = MT_MCUQ_WM;
	int ret;

	ret = mt76_connac2_mcu_fill_message(mdev, skb, cmd, seq);
	if (ret)
		return ret;

	mdev->mcu.timeout = 3 * HZ;

	if (cmd == MCU_CMD(FW_SCATTER))
		txq = MT_MCUQ_FWDL;

	return mt76_tx_queue_skb_raw(dev, mdev->q_mcu[txq], skb, 0);
}

int mt7921e_mcu_init(struct mt792x_dev *dev)
{
	static const struct mt76_mcu_ops mt7921_mcu_ops = {
		.headroom = sizeof(struct mt76_connac2_mcu_txd),
		.mcu_skb_send_msg = mt7921_mcu_send_message,
		.mcu_parse_response = mt7921_mcu_parse_response,
	};
	int err;

	dev->mt76.mcu_ops = &mt7921_mcu_ops;

	/* NOTE: a wfsys-reset prologue here was tried and REVERTED -- without
	 * Linux's full post-reset re-init (mmio_probe register/clock bring-up)
	 * it leaves the MCU in a worse state than the dirty-state problem it
	 * tried to solve (the ROM went silent on the semaphore exchange). */
	err = mt7921e_driver_own(dev);
	XYLog("mtk-dbg: mt7921e_driver_own -> %d\n", err);
	if (err)
		return err;

	mt76_rmw_field(dev, MT_PCIE_MAC_PM, MT_PCIE_MAC_PM_L0S_DIS, 1);

	err = mt7921_run_firmware(dev);

	mt76_queue_tx_cleanup(dev, dev->mt76.q_mcu[MT_MCUQ_FWDL], false);

	return err;
}
