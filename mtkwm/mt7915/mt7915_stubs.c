/*
 * mt7915_stubs.c - minimal replacements for the mt7915 sources that are
 * not ported into the kext (Linux mt7915/main.c, mt7915/debugfs.c,
 * mt7915/testmode.c).
 *
 * Ported for mt76wm (XNU).  Mirrors the MT7925 pattern: the mac80211
 * ieee80211_ops body is net80211-side work (glue/mtk_net80211.c), so only
 * the hardware bring-up pieces referenced by the ported probe/MCU/DMA code
 * need to exist here.  Semantics marked TODO(M2) are placeholders.
 *
 * All fill-ins are weak definitions where the shared mt76core/glue may
 * later grow the real implementation (same convention as
 * mtkwm/mt7603/mt7603_stubs.c): strong definitions win at link time.
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

#include "mt7915.h"
#include "mac.h"
#include "mcu.h"

/* ================================================================== */
/* Linux mt7915/main.c (ieee80211_ops + mac80211 callbacks)           */
/* ================================================================== */

/* TODO(M2): replace with the real ieee80211_ops (mt7915/main.c). */
const struct ieee80211_ops mt7915_ops = { };

/*
 * mt7915_run() is the hardware "start" sequence (Linux mt7915/main.c) and
 * is reached from the firmware recovery path (mt7915_mac_reset_work ->
 * mt7915_mac.c).  Minimal bring-up: mark the PHY running so RX
 * descriptors are accepted.  TODO(M2): MCU rx filter defaults, MT_MAC
 * timing, thermal, beacon config from the full mt7915_run().
 */
int mt7915_run(struct ieee80211_hw *hw)
{
	struct mt7915_dev *dev = mt7915_hw_dev(hw);

	mutex_lock(&dev->mt76.mutex);
	set_bit(MT76_STATE_RUNNING, &dev->mphy.state);
	mutex_unlock(&dev->mt76.mutex);
	return 0;
}

/* Driver .set_channel hook (mmio.c drv_ops).  TODO(M2): program the
 * chandef staged on hw->conf via mt7915_mcu_set_chan_info(). */
int mt7915_set_channel(struct mt76_phy *mphy)
{
	(void)mphy;
	return 0;
}

/* Driver .sta_add/.sta_event/.sta_remove hooks (mmio.c drv_ops).
 * TODO(M2): WTBL/STA_REC programming (mt7915_mcu_add_sta). */
int mt7915_mac_sta_add(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta)
{
	(void)mdev; (void)vif; (void)sta;
	return 0;
}

int mt7915_mac_sta_event(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta, enum mt76_sta_event ev)
{
	(void)mdev; (void)vif; (void)sta; (void)ev;
	return 0;
}

void mt7915_mac_sta_remove(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta)
{
	(void)mdev; (void)vif; (void)sta;
}

/* TSF readout used by the TWT teardown path (mt7915/mac.c).
 * TODO(M2): read LPON_T0CR/T1CR via the mt7915_get_tsf() sequence. */
u64 __mt7915_get_tsf(struct ieee80211_hw *hw, struct mt7915_vif *mvif)
{
	(void)hw; (void)mvif;
	return 0;
}

/* ================================================================== */
/* Linux mt7915/debugfs.c                                             */
/* ================================================================== */

int mt7915_init_debugfs(struct mt7915_phy *phy)
{
	(void)phy;	/* debugfs not available in the kext */
	return 0;
}

void mt7915_debugfs_rx_fw_monitor(struct mt7915_dev *dev, const void *data,
				  int len)
{
	/* Caller-owned buffer; fw monitor data is dropped. */
	(void)dev; (void)data; (void)len;
}

bool mt7915_debugfs_rx_log(struct mt7915_dev *dev, const void *data, int len)
{
	(void)dev; (void)data; (void)len;
	return false;	/* not consumed: caller keeps the skb */
}

/* ================================================================== */
/* Linux mt7915/testmode.c                                            */
/* ================================================================== */

/*
 * Referenced from mt7915.h (extern) and from init.c under
 * CONFIG_NL80211_TESTMODE (disabled in the port).  Empty ops: testmode
 * commands are rejected by the mt76 testmode core.
 */
const struct mt76_testmode_ops mt7915_testmode_ops = { };

/* ================================================================== */
/* mac80211/cfg80211 helpers used by mcu.c (declared in the            */
/* linuxcompat mac80211 stub); weak fill-ins.                          */
/* ================================================================== */

__attribute__((weak)) void
ieee80211_color_change_finish(struct ieee80211_vif *vif, unsigned int link_id)
{
	(void)vif;
	(void)link_id;	/* TODO(M2): BSS color change completion */
}

__attribute__((weak)) const struct element *
ieee80211_bss_get_elem(struct cfg80211_bss *bss, u8 id)
{
	(void)bss;
	(void)id;
	return NULL;	/* TODO(M2): parse the BSS IEs */
}

__attribute__((weak)) void
cfg80211_background_radar_event(struct wiphy *wiphy,
			       const struct cfg80211_chan_def *chandef,
			       gfp_t gfp)
{
	(void)wiphy;
	(void)chandef;
	(void)gfp;	/* TODO(M2): background radar notification */
}

__attribute__((weak)) void
cfg80211_bss_iter(struct wiphy *wiphy, const struct cfg80211_chan_def *chandef,
		  void (*iter)(struct wiphy *wiphy, struct cfg80211_bss *bss,
			       void *data),
		  void *data)
{
	(void)wiphy;
	(void)chandef;
	(void)iter;
	(void)data;	/* TODO(M2): iterate the scanned BSS list */
}

__attribute__((weak)) struct sk_buff *
ieee80211_get_fils_discovery_tmpl(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  unsigned int link_id)
{
	(void)hw;
	(void)vif;
	(void)link_id;
	return NULL;	/* TODO(M2): FILS discovery template */
}

__attribute__((weak)) struct sk_buff *
ieee80211_get_unsol_bcast_probe_resp_tmpl(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif,
					  unsigned int link_id)
{
	(void)hw;
	(void)vif;
	(void)link_id;
	return NULL;	/* TODO(M2): unsolicited broadcast probe resp */
}

__attribute__((weak)) const u8 *
cfg80211_find_ie(u8 eid, const u8 *ies, int len)
{
	(void)eid;
	(void)ies;
	(void)len;
	return NULL;	/* TODO(M2): parse IEs from a BSS entry */
}
