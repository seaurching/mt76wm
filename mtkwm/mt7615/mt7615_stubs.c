/*
 * mt7615_stubs.c - link fill-ins for the MT7615 family port.
 *
 * Ported for mt76wm (XNU).
 *
 * Two groups:
 *
 * 1. mt76 core helpers (Linux mt76/mac80211.c) that the ported mt76core/
 *    does not carry yet: ext-PHY registration, DFS state and survey
 *    accounting.  Small ones are faithful ports of the Linux originals,
 *    the rest are TODO-marked placeholders for the net80211 milestone.
 *
 * 2. mac80211 shim fill-ins (Linux helpers declared in
 *    glue/linuxcompat/net/mac80211.h but not implemented in
 *    glue/mtk_mac80211.c): ROC expiry / sched-scan / radar reporting and
 *    the AMPDU TX-stop callback.  Semantics marked TODO(M2) are
 *    placeholders - they are only reached from the net80211 glue
 *    milestones, which are out of scope for the hardware bring-up.
 *
 * All fill-ins are weak definitions: when the shared glue gains the real
 * implementations (or another family port provides its own copies) those
 * win at link time and these are discarded - no symbol collisions between
 * parallel family ports.
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

#include "mt7615.h"

/* ================================================================== */
/* mt76 core fill-ins (Linux mt76/mac80211.c)                         */
/*                                                                      */
/* These are shared helpers the ported mt76core/glue does not carry     */
/* yet (mt7603 never called them; mt7615 uses them for the ext PHY,     */
/* DFS and survey accounting).  Small ones are faithful ports of the    */
/* Linux originals, the rest are TODO-marked placeholders for the       */
/* net80211 milestones.  Weak: a future strong implementation in        */
/* glue/mtk_port_funcs.c or mt76core wins at link time.                 */
/* ================================================================== */

/* Linux mt76/mac80211.c: mt76_alloc_phy */
__attribute__((weak)) struct mt76_phy *
mt76_alloc_phy(struct mt76_dev *dev, unsigned int size,
	       const struct ieee80211_ops *ops, u8 band_idx)
{
	struct ieee80211_hw *hw;
	unsigned int phy_size;
	struct mt76_phy *phy;

	phy_size = ALIGN(sizeof(*phy), 8);
	hw = ieee80211_alloc_hw(size + phy_size, ops);
	if (!hw)
		return NULL;

	phy = (struct mt76_phy *)hw->priv;
	phy->dev = dev;
	phy->hw = hw;
	phy->priv = (u8 *)hw->priv + phy_size;
	phy->band_idx = band_idx;

	hw->wiphy->flags |= WIPHY_FLAG_IBSS_RSN;
	hw->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_AP) |
		BIT(NL80211_IFTYPE_P2P_CLIENT) |
		BIT(NL80211_IFTYPE_P2P_GO) |
		BIT(NL80211_IFTYPE_ADHOC);

	return phy;
}

/*
 * Linux mt76/mac80211.c: mt76_register_phy.  TODO(M2): the sband init
 * (mt76_init_sband_2g/5g/6g + mt76_check_sband) is not ported - the ext
 * PHY registers without per-band channel tables for now.
 */
__attribute__((weak)) int
mt76_register_phy(struct mt76_phy *phy, bool vht,
		  struct ieee80211_rate *rates, int n_rates)
{
	int ret;

	(void)vht;
	(void)rates;
	(void)n_rates;

	if ((void *)phy == phy->hw->priv) {
		ret = ieee80211_register_hw(phy->hw);
		if (ret)
			return ret;
	}

	set_bit(MT76_STATE_REGISTERED, &phy->state);
	phy->dev->phys[phy->band_idx] = phy;

	return 0;
}

/* Linux mt76/mac80211.c: mt76_unregister_phy */
__attribute__((weak)) void
mt76_unregister_phy(struct mt76_phy *phy)
{
	struct mt76_dev *dev = phy->dev;

	if (!test_bit(MT76_STATE_REGISTERED, &phy->state))
		return;

	if ((void *)phy == phy->hw->priv)
		ieee80211_unregister_hw(phy->hw);
	dev->phys[phy->band_idx] = NULL;
}

/* Linux mt76/mac80211.c: mt76_update_survey_active_time */
__attribute__((weak)) void
mt76_update_survey_active_time(struct mt76_phy *phy, ktime_t time)
{
	struct mt76_channel_state *state = phy->chan_state;

	if (!state)
		return;

	state->cc_active += ktime_to_us(ktime_sub(time, phy->survey_time));
	phy->survey_time = time;
}

/* Linux mt76/mac80211.c: mt76_phy_dfs_state.
 * TODO(M2): the cfg80211_reg_can_beacon() CAC check is not ported. */
__attribute__((weak)) enum mt76_dfs_state
mt76_phy_dfs_state(struct mt76_phy *phy)
{
	struct ieee80211_hw *hw = phy->hw;
	struct mt76_dev *dev = phy->dev;

	if (dev->region == NL80211_DFS_UNSET ||
	    test_bit(MT76_SCANNING, &phy->state))
		return MT_DFS_STATE_DISABLED;

	if (!phy->radar_enabled) {
		if ((hw->conf.flags & IEEE80211_CONF_MONITOR) &&
		    (phy->chandef.chan->flags & IEEE80211_CHAN_RADAR))
			return MT_DFS_STATE_ACTIVE;

		return MT_DFS_STATE_DISABLED;
	}

	return MT_DFS_STATE_ACTIVE;
}

/* ================================================================== */
/* mac80211 shim fill-ins (glue/mtk_mac80211.c)                       */
/* ================================================================== */

/* Linux mac80211.h: AMPDU TX-stop callback (mt7603/mt7615 ampdu path) */
__attribute__((weak)) void
ieee80211_stop_tx_ba_cb_irqsafe(struct ieee80211_vif *vif, const u8 *ra,
				u16 tid)
{
	(void)vif;
	(void)ra;
	(void)tid;	/* TODO(M2): BA session stop completion */
}

__attribute__((weak)) void
ieee80211_remain_on_channel_expired(struct ieee80211_hw *hw)
{
	(void)hw;	/* TODO(M2): ROC expiry notification */
}

__attribute__((weak)) void
ieee80211_sched_scan_results(struct ieee80211_hw *hw)
{
	(void)hw;	/* TODO(M2): sched-scan results notification */
}

__attribute__((weak)) void
ieee80211_radar_detected(struct ieee80211_hw *hw,
			 struct ieee80211_chanctx_conf *chanctx_conf)
{
	(void)hw;
	(void)chanctx_conf;	/* TODO(M2): DFS radar event to cfg80211 */
}
