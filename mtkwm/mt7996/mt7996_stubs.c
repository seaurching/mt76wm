/*
 * mt7996_stubs.c - minimal replacements for the mt7996 sources that are
 * not ported into the kext (Linux mt7996/main.c, mt7996/debugfs.c).
 *
 * Ported for mt76wm (XNU).  Mirrors the MT7915/MT7925 pattern: the
 * mac80211 ieee80211_ops body is net80211-side work (glue/mtk_net80211.c),
 * so only the hardware bring-up pieces referenced by the ported
 * probe/MCU/DMA code need to exist here.  Semantics marked TODO(M2) are
 * placeholders.
 *
 * All fill-ins are weak definitions so the shared mt76core/glue or a
 * later full port may override them at link time (same convention as
 * mtkwm/mt7915/mt7915_stubs.c).
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

#include "mt7996.h"
#include "mac.h"
#include "mcu.h"
#include "eeprom.h"

/* ================================================================== */
/* Linux mt7996/main.c (ieee80211_ops + mac80211 callbacks)           */
/* ================================================================== */

/* TODO(M2): replace with the real ieee80211_ops (mt7996/main.c). */
__attribute__((weak)) const struct ieee80211_ops mt7996_ops = { };

/*
 * mt7996_run() is the hardware "start" sequence (Linux mt7996/main.c)
 * and is reached from the firmware recovery path (mt7996_mac_reset_work
 * -> mt7996/mac.c).  Minimal bring-up: mark the PHY running so RX
 * descriptors are accepted.  TODO(M2): RTS threshold, radio enable,
 * channel info, thermal throttling and TX power ctrl from the full
 * mt7996_run().
 */
__attribute__((weak)) int mt7996_run(struct mt7996_phy *phy)
{
	mutex_lock(&phy->dev->mt76.mutex);
	set_bit(MT76_STATE_RUNNING, &phy->mt76->state);
	mutex_unlock(&phy->dev->mt76.mutex);
	return 0;
}

/* Driver .set_channel hook (mmio.c drv_ops).  TODO(M2): program the
 * chandef staged on hw->conf via mt7996_mcu_set_chan_info(). */
__attribute__((weak)) int mt7996_set_channel(struct mt76_phy *mphy)
{
	(void)mphy;
	return 0;
}

/*
 * MLO vif link add/remove (mmio.c drv_ops).  TODO(M2): OM/MLD address
 * programming and BSS record bring-up from the full mt7996/main.c.
 */
__attribute__((weak)) int
mt7996_vif_link_add(struct mt76_phy *mphy, struct ieee80211_vif *vif,
		    struct ieee80211_bss_conf *link_conf,
		    struct mt76_vif_link *mlink)
{
	(void)mphy; (void)vif; (void)link_conf; (void)mlink;
	return 0;
}

__attribute__((weak)) void
mt7996_vif_link_remove(struct mt76_phy *mphy, struct ieee80211_vif *vif,
		       struct ieee80211_bss_conf *link_conf,
		       struct mt76_vif_link *mlink)
{
	(void)mphy; (void)vif; (void)link_conf; (void)mlink;
}

/*
 * Per-link STA teardown (mac.c recovery path) and active-link mask
 * (mcu.c STA_REC updates).  TODO(M2): WTBL/STA_REC programming.
 */
__attribute__((weak)) void
mt7996_mac_sta_remove_link(struct mt7996_dev *dev, struct ieee80211_sta *sta,
			   unsigned int link_id, bool flush)
{
	(void)dev; (void)sta; (void)link_id; (void)flush;
}

__attribute__((weak)) u16
mt7996_mac_sta_links(struct mt7996_dev *dev, struct ieee80211_vif *vif,
		     struct ieee80211_sta *sta)
{
	(void)dev; (void)vif;
	/* non-MLD stations are on link 0 only */
	return sta->valid_links ? sta->valid_links : BIT(0);
}

/* TSF readout used by the TWT teardown path (mt7996/mac.c).
 * TODO(M2): read LPON_T0CR/T1CR via the __mt7996_get_tsf() sequence. */
__attribute__((weak)) u64 __mt7996_get_tsf(struct ieee80211_hw *hw,
					   struct mt7996_vif_link *link)
{
	(void)hw; (void)link;
	return 0;
}

/* ethtool strings/stats ops (mt7996/main.c) - only referenced from the
 * unported ieee80211_ops body. */
__attribute__((weak)) void
mt7996_get_et_strings(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      u32 sset, u8 *data)
{
	(void)hw; (void)vif; (void)sset; (void)data;
}

__attribute__((weak)) int
mt7996_get_et_sset_count(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 int sset)
{
	(void)hw; (void)vif; (void)sset;
	return 0;
}

__attribute__((weak)) void
mt7996_get_et_stats(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    struct ethtool_stats *stats, u64 *data)
{
	(void)hw; (void)vif; (void)stats; (void)data;
}

/* ================================================================== */
/* Linux mt7996/debugfs.c                                             */
/* ================================================================== */

__attribute__((weak)) int mt7996_init_debugfs(struct mt7996_dev *dev)
{
	(void)dev;	/* debugfs not available in the kext */
	return 0;
}

__attribute__((weak)) void
mt7996_debugfs_rx_fw_monitor(struct mt7996_dev *dev, const void *data, int len)
{
	/* Caller-owned buffer; fw monitor data is dropped. */
	(void)dev; (void)data; (void)len;
}

__attribute__((weak)) bool mt7996_debugfs_rx_log(struct mt7996_dev *dev,
						 const void *data, int len)
{
	(void)dev; (void)data; (void)len;
	return false;	/* not consumed: caller keeps the skb */
}

#ifdef CONFIG_MAC80211_DEBUGFS
__attribute__((weak)) void
mt7996_sta_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, struct dentry *dir)
{
	(void)hw; (void)vif; (void)sta; (void)dir;
}

__attribute__((weak)) void
mt7996_link_sta_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_link_sta *link_sta,
			    struct dentry *dir)
{
	(void)hw; (void)vif; (void)link_sta; (void)dir;
}
#endif /* CONFIG_MAC80211_DEBUGFS */
