/*
 * mt7603_stubs.c - link fill-ins for the MT7603 family port.
 *
 * Ported for mt76wm (XNU).
 *
 * Two groups:
 *
 * 1. mt76 core helpers (Linux mt76/mac80211.c) that the ported mt76core/
 *    does not carry yet but mt7603's ieee80211_ops / IRQ / beacon code
 *    references.  Small ones are faithful ports of the Linux originals,
 *    the rest are TODO-marked placeholders for the net80211 milestone.
 *
 * 2. mac80211 shim helpers (beacon / CSA / TX-status) normally living in
 *    glue/mtk_mac80211.c.
 *
 * All fill-ins are weak definitions: when the shared mt76core/glue gains
 * the real implementations (or the mt7925 port provides its own copies)
 * those win at link time and these are discarded - no symbol collisions
 * between parallel family ports.
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

#include "mt7603.h"

/* ================================================================== */
/* mt76 core fill-ins (Linux mt76/mac80211.c)                         */
/* ================================================================== */

/* Linux mt76/mac80211.c: mt76_sw_scan / mt76_sw_scan_complete */
__attribute__((weak)) void
mt76_sw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	     const u8 *mac)
{
	struct mt76_phy *phy = (struct mt76_phy *)hw->priv;

	(void)vif;
	(void)mac;
	set_bit(MT76_SCANNING, &phy->state);
}

__attribute__((weak)) void
mt76_sw_scan_complete(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt76_phy *phy = (struct mt76_phy *)hw->priv;

	(void)vif;
	clear_bit(MT76_SCANNING, &phy->state);
}

/* Linux mt76/mac80211.c: the TIM bit is delivered by set_tim()/beacon */
__attribute__((weak)) int
mt76_set_tim(struct ieee80211_hw *hw, struct ieee80211_sta *sta, bool set)
{
	(void)hw;
	(void)sta;
	(void)set;
	return 0;
}

/* Linux mt76/mac80211.c: mt76_get_antenna */
__attribute__((weak)) int
mt76_get_antenna(struct ieee80211_hw *hw, int radio_idx, u32 *tx_ant,
		 u32 *rx_ant)
{
	struct mt76_phy *phy = (struct mt76_phy *)hw->priv;
	struct mt76_dev *dev = phy->dev;
	int i;

	(void)radio_idx;
	mutex_lock(&dev->mutex);
	*tx_ant = 0;
	for (i = 0; i < ARRAY_SIZE(dev->phys); i++)
		if (dev->phys[i] && dev->phys[i]->hw == hw)
			*tx_ant |= dev->phys[i]->chainmask;
	*rx_ant = *tx_ant;
	mutex_unlock(&dev->mutex);

	return 0;
}

/* Linux mt76/mac80211.c: CSA countdown tracking (beacon.c/core.c) */
static void
mt7603_csa_finish_iter(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	(void)priv;
	(void)mac;
	if (vif->bss_conf.csa_active &&
	    ieee80211_beacon_cntdwn_is_complete(vif, 0))
		ieee80211_csa_finish(vif, 0);
}

__attribute__((weak)) void mt76_csa_finish(struct mt76_dev *dev)
{
	if (!dev->csa_complete)
		return;

	ieee80211_iterate_active_interfaces_atomic(dev->hw,
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt7603_csa_finish_iter, dev);

	dev->csa_complete = 0;
}

static void
mt7603_csa_check_iter(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt76_dev *dev = (struct mt76_dev *)priv;

	(void)mac;
	if (!vif->bss_conf.csa_active)
		return;

	dev->csa_complete |= ieee80211_beacon_cntdwn_is_complete(vif, 0);
}

__attribute__((weak)) void mt76_csa_check(struct mt76_dev *dev)
{
	ieee80211_iterate_active_interfaces_atomic(dev->hw,
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt7603_csa_check_iter, dev);
}

/*
 * TODO(M2): real ports of the remaining mt76/mac80211.c helpers.  These
 * are only reached from the net80211 glue (STA lifecycle, survey, SAR),
 * which is out of scope for the hardware bring-up milestone; the mt7603
 * hardware paths call the driver ops (mt7603_sta_add etc.) directly.
 */
__attribute__((weak)) int
mt76_sta_state(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       struct ieee80211_sta *sta,
	       enum ieee80211_sta_state old_state,
	       enum ieee80211_sta_state new_state)
{
	(void)hw;
	(void)vif;
	(void)sta;
	(void)old_state;
	(void)new_state;
	return 0;	/* TODO(M2): mt76_sta_add/mt76_sta_remove wiring */
}

__attribute__((weak)) void
mt76_sta_pre_rcu_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta)
{
	(void)hw;
	(void)vif;
	(void)sta;
	/* TODO(M2): drop the wcid from dev->wcid[] under status_lock */
}

__attribute__((weak)) int
mt76_get_survey(struct ieee80211_hw *hw, int idx,
		struct survey_info *survey)
{
	(void)hw;
	(void)idx;
	(void)survey;
	return -ENOENT;	/* TODO(M2): per-channel survey accounting */
}

__attribute__((weak)) int
mt76_init_sar_power(struct ieee80211_hw *hw,
		    const struct cfg80211_sar_specs *sar)
{
	(void)hw;
	(void)sar;
	return 0;	/* TODO(M2): SAR power tables */
}

__attribute__((weak)) int mt76_update_channel(struct mt76_phy *phy)
{
	(void)phy;
	return 0;	/* TODO(M2): program the chandef staged on hw->conf */
}

/* ================================================================== */
/* mac80211 shim fill-ins (glue/mtk_mac80211.c)                       */
/* ================================================================== */

__attribute__((weak)) struct sk_buff *
ieee80211_beacon_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		     unsigned int link_id)
{
	(void)hw;
	(void)vif;
	(void)link_id;
	return NULL;	/* TODO(M2): AP beacons */
}

__attribute__((weak)) struct sk_buff *
ieee80211_get_buffered_bc(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	(void)hw;
	(void)vif;
	return NULL;	/* TODO(M2): buffered broadcast for CAB queue */
}

__attribute__((weak)) void
ieee80211_send_eosp_nullfunc(struct ieee80211_sta *sta, int tid)
{
	(void)sta;
	(void)tid;	/* TODO(M2): U-APSD EOSP */
}

__attribute__((weak)) void
ieee80211_csa_finish(struct ieee80211_vif *vif, unsigned int link_id)
{
	(void)vif;
	(void)link_id;	/* TODO(M2): CSA completion */
}

__attribute__((weak)) bool
ieee80211_beacon_cntdwn_is_complete(struct ieee80211_vif *vif,
				    unsigned int link_id)
{
	(void)vif;
	(void)link_id;
	return false;
}

__attribute__((weak)) void
ieee80211_tx_status_noskb(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
			  struct ieee80211_tx_info *info)
{
	struct ieee80211_tx_status status = {
		.sta = sta,
		.info = info,
	};

	ieee80211_tx_status_ext(hw, &status);
}
