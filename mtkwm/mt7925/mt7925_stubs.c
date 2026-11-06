/*
 * mt7925_stubs.c - minimal replacements for the mt7925 sources that are
 * not ported into the kext (Linux mt7925/main.c, mt7925/nan.c,
 * mt7925/debugfs.c, mt7925/testmode.c).
 *
 * Ported for mt76wm (XNU).  Mirrors the MT7921 pattern: the mac80211
 * ieee80211_ops body is net80211-side work (glue/mtk_net80211.c), so only
 * the hardware bring-up pieces referenced by the ported probe/MCU/DMA code
 * need to exist here.  Semantics marked TODO(M4) are placeholders.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/workqueue.h>

#include "mt7925.h"
#include "mcu.h"
#include "nan.h"
#include "regd.h"

/* ================================================================== */
/* Linux mt7925/main.c (ieee80211_ops + mac80211 callbacks)           */
/* ================================================================== */

/* TODO(M4): replace with the real ieee80211_ops (mt7925/main.c). */
const struct ieee80211_ops mt7925_ops = { };

int __mt7925_start(struct mt792x_phy *phy)
{
	struct mt792x_dev *dev = phy->dev;

	/* Minimal start sequence (same contract as the MT7921 stub):
	 * mark the PHY running so RX descriptors are accepted and hold
	 * the connac PM reference.  TODO(M5/M6): MCU nic capability,
	 * rx filter defaults, thermal. */
	mt792x_mutex_acquire(dev);
	set_bit(MT76_STATE_RUNNING, &phy->mt76->state);
	mt792x_mutex_release(dev);
	return 0;
}

int mt7925_init_mlo_caps(struct mt792x_phy *phy)
{
	(void)phy;
	return 0;	/* TODO(M4): MLO capabilities */
}

int mt7925_mac_sta_add(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta)
{
	(void)mdev; (void)vif; (void)sta;
	return 0;	/* TODO(M6): WTBL/STA_REC */
}

int mt7925_mac_sta_event(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta, enum mt76_sta_event ev)
{
	(void)mdev; (void)vif; (void)sta; (void)ev;
	return 0;	/* TODO(M6) */
}

void mt7925_mac_sta_remove(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta)
{
	(void)mdev; (void)vif; (void)sta;	/* TODO(M6) */
}

void mt7925_set_stream_he_eht_caps(struct mt792x_phy *phy)
{
	(void)phy;	/* TODO(M4): build HE/EHT capa per band */
}

int mt7925_set_tx_sar_pwr(struct ieee80211_hw *hw,
			  const struct cfg80211_sar_specs *sar)
{
	(void)hw; (void)sar;
	return 0;	/* TODO(M4) */
}

void mt7925_scan_work(struct work_struct *work)
{
	(void)work;	/* TODO(M4): HW scan sequencing */
}

void mt7925_roc_work(struct work_struct *work)
{
	(void)work;	/* TODO(M4): remain-on-channel */
}

void mt7925_roc_abort_sync(struct mt792x_dev *dev)
{
	(void)dev;	/* TODO(M4) */
}

void mt7925_csa_work(struct work_struct *work)
{
	(void)work;	/* TODO(M4) */
}

void mt7925_set_runtime_pm(struct mt792x_dev *dev)
{
	(void)dev;	/* TODO(M4) */
}

void mt7925_mlo_pm_work(struct work_struct *work)
{
	(void)work;	/* TODO(M4): MLO power-save */
}

/* ================================================================== */
/* Linux mt7925/debugfs.c                                             */
/* ================================================================== */

int mt7925_init_debugfs(struct mt792x_dev *dev)
{
	(void)dev;	/* debugfs not available in the kext */
	return 0;
}

/* ================================================================== */
/* Linux mt7925/nan.c                                                 */
/* ================================================================== */

void mt7925_nan_mcu_event(struct mt792x_dev *dev, struct sk_buff *skb)
{
	/* The caller frees the skb; NAN events are dropped.  TODO(M4). */
	(void)dev; (void)skb;
}

void mt7925_nan_deferred_work(struct work_struct *work)
{
	(void)work;	/* TODO(M4): deferred NAN MCU events */
}
