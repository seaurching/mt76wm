/* SPDX-License-Identifier: GPL-2.0 */
/*
 * net/mac80211.h - minimal mac80211 stub for the mt76wm XNU port.
 * Ported for mt76wm (XNU)
 *
 * This is NOT Linux mac80211: it is a hand-written minimal stub that
 * declares just the types, fields, constants and entry points the ported
 * mt76 sources reference.  Struct field order/content is not ABI: the
 * whole tree is recompiled against this header.
 *
 * Functional APIs are inline no-ops or weak glue implementations with
 * "TODO: wire to net80211 (M4)" markers.
 */
#ifndef MTK_MAC80211_H
#define MTK_MAC80211_H

#include <linux/mtk_core.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/average.h>
/* enum tc_setup_type (referenced by struct ieee80211_ops below) is
 * defined in the mtk_wed stub; pull it in up front so the reference is
 * never a forward one (C++ rejects those). */
#include <linux/soc/mediatek/mtk_wed.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/* nl80211 constants                                                   */
/* ================================================================== */
enum nl80211_iftype {
    NL80211_IFTYPE_UNSPECIFIED,
    NL80211_IFTYPE_ADHOC,
    NL80211_IFTYPE_STATION,
    NL80211_IFTYPE_AP,
    NL80211_IFTYPE_AP_VLAN,
    NL80211_IFTYPE_WDS,
    NL80211_IFTYPE_MONITOR,
    NL80211_IFTYPE_MESH_POINT,
    NL80211_IFTYPE_P2P_CLIENT,
    NL80211_IFTYPE_P2P_GO,
    NL80211_IFTYPE_P2P_DEVICE,
    NL80211_IFTYPE_OCB,
    NL80211_IFTYPE_NAN,
    NL80211_IFTYPE_NAN_DATA,
    NUM_NL80211_IFTYPES,
    NL80211_IFTYPE_MAX = NUM_NL80211_IFTYPES - 1,
};

enum nl80211_band {
    NL80211_BAND_2GHZ,
    NL80211_BAND_5GHZ,
    NL80211_BAND_6GHZ,
    NL80211_BAND_60GHZ,
    NUM_NL80211_BANDS,
};

enum nl80211_chan_width {
    NL80211_CHAN_WIDTH_20_NOHT,
    NL80211_CHAN_WIDTH_20,
    NL80211_CHAN_WIDTH_40,
    NL80211_CHAN_WIDTH_80,
    NL80211_CHAN_WIDTH_80P80,
    NL80211_CHAN_WIDTH_160,
    NL80211_CHAN_WIDTH_5,
    NL80211_CHAN_WIDTH_10,
    NL80211_CHAN_WIDTH_1,
    NL80211_CHAN_WIDTH_2,
    NL80211_CHAN_WIDTH_4,
    NL80211_CHAN_WIDTH_8,
    NL80211_CHAN_WIDTH_320,
};

enum nl80211_dfs_regions {
    NL80211_DFS_UNSET,
    NL80211_DFS_FCC,
    NL80211_DFS_ETSI,
    NL80211_DFS_JP,
};

#define NL80211_MAX_SUPP_REG_RULES      32
#define NL80211_REPLAY_CTR_LEN          8
#define NL80211_MAX_SSID_LEN            32
#define NL80211_KEK_LEN                 16
#define NL80211_KCK_LEN                 16

enum nl80211_feature_flags {
    NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR = 1 << 0,
    NL80211_FEATURE_SCHED_SCAN_RANDOM_MAC_ADDR = 1 << 1,
    /* mt7615 init.c (firmware-offload capability) */
    NL80211_FEATURE_P2P_GO_CTWIN = 1 << 2,
    NL80211_FEATURE_P2P_GO_OPPPS = 1 << 3,
};

enum nl80211_ext_feature_index {
    NL80211_EXT_FEATURE_VHT_IBSS,
    NL80211_EXT_FEATURE_BEACON_RATE_LEGACY,
    NL80211_EXT_FEATURE_BEACON_RATE_HT,
    NL80211_EXT_FEATURE_BEACON_RATE_VHT,
    NL80211_EXT_FEATURE_BEACON_RATE_HE,
    NL80211_EXT_FEATURE_ACK_SIGNAL_SUPPORT,
    NL80211_EXT_FEATURE_SET_SCAN_DWELL,
    NL80211_EXT_FEATURE_CAN_REPLACE_PTK0,
    NL80211_EXT_FEATURE_MULTICAST_REGISTRATIONS,
    NL80211_EXT_FEATURE_SECURE_NAN,
    /* mt7615 init.c (MU-MIMO air sniffer capability) */
    NL80211_EXT_FEATURE_MU_MIMO_AIR_SNIFFER,
    /* mt7915 init.c (wiphy_ext_feature_set capability flags) */
    NL80211_EXT_FEATURE_BSS_COLOR,
    NL80211_EXT_FEATURE_FILS_DISCOVERY,
    NL80211_EXT_FEATURE_RADAR_BACKGROUND,
    NL80211_EXT_FEATURE_STA_TX_PWR,
    NL80211_EXT_FEATURE_UNSOL_BCAST_PROBE_RESP,
    NUM_NL80211_EXT_FEATURES,
};

enum nl80211_wowlan_triggers {
    NL80211_WOWLAN_TRIG_ANY = 1 << 0,
    NL80211_WOWLAN_TRIG_DISCONNECT = 1 << 1,
    NL80211_WOWLAN_TRIG_MAGIC_PKT = 1 << 2,
    NL80211_WOWLAN_TRIG_GTK_REKEY_FAILURE = 1 << 3,
    NL80211_WOWLAN_TRIG_NET_DETECT = 1 << 9,
};

enum nl80211_sta_info {
    NL80211_STA_INFO_INACTIVE_TIME,
    NL80211_STA_INFO_RX_BYTES,
    NL80211_STA_INFO_TX_BYTES,
    NL80211_STA_INFO_TX_BITRATE,
    NL80211_STA_INFO_SIGNAL,
    NL80211_STA_INFO_ACK_SIGNAL,
    NL80211_STA_INFO_ACK_SIGNAL_AVG,
    NL80211_STA_INFO_TX_RETRIES,
    NL80211_STA_INFO_TX_FAILED,
};

enum nl80211_cqm_rssi_threshold_event {
    NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW,
    NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH,
};

enum nl80211_rate_info_he_ru_alloc {
    NL80211_RATE_INFO_HE_RU_ALLOC_26,
    NL80211_RATE_INFO_HE_RU_ALLOC_52,
    NL80211_RATE_INFO_HE_RU_ALLOC_106,
    NL80211_RATE_INFO_HE_RU_ALLOC_242,
    NL80211_RATE_INFO_HE_RU_ALLOC_484,
    NL80211_RATE_INFO_HE_RU_ALLOC_996,
    NL80211_RATE_INFO_HE_RU_ALLOC_2x996,
};

enum nl80211_rate_info_he_gi {
    NL80211_RATE_INFO_HE_GI_0_8,
    NL80211_RATE_INFO_HE_GI_1_6,
    NL80211_RATE_INFO_HE_GI_3_2,
};

enum nl80211_eht_gi {
    NL80211_RATE_INFO_EHT_GI_0_8,
    NL80211_RATE_INFO_EHT_GI_1_6,
    NL80211_RATE_INFO_EHT_GI_3_2,
};

enum nl80211_eht_ru_alloc {
    NL80211_RATE_INFO_EHT_RU_ALLOC_26,
    NL80211_RATE_INFO_EHT_RU_ALLOC_52,
    NL80211_RATE_INFO_EHT_RU_ALLOC_106,
    NL80211_RATE_INFO_EHT_RU_ALLOC_242,
    NL80211_RATE_INFO_EHT_RU_ALLOC_484,
    NL80211_RATE_INFO_EHT_RU_ALLOC_996,
    NL80211_RATE_INFO_EHT_RU_ALLOC_2x996,
};

#define NL80211_RATE_INFO_HE_MCS_SHIFT 0

#define NL80211_WPA_VERSION_1 (1 << 0)
#define NL80211_WPA_VERSION_2 (1 << 1)

#define NL80211_REGDOM_SET_BY_USER 0
#define NL80211_REGDOM_SET_BY_CORE 1
#define NL80211_REGDOM_SET_BY_DRIVER 2
#define NL80211_REGDOM_SET_BY_COUNTRY_IE 3

/* ================================================================== */
/* rate_info                                                           */
/* ================================================================== */
#define RATE_INFO_FLAGS_MCS         (1 << 0)
#define RATE_INFO_FLAGS_VHT_MCS     (1 << 1)
#define RATE_INFO_FLAGS_SHORT_GI    (1 << 2)
#define RATE_INFO_FLAGS_DMG         (1 << 3)
#define RATE_INFO_FLAGS_HE_MCS      (1 << 4)
#define RATE_INFO_FLAGS_EDMG        (1 << 5)
#define RATE_INFO_FLAGS_EHT_MCS     (1 << 6)

enum rate_info_bw {
    RATE_INFO_BW_20,
    RATE_INFO_BW_40,
    RATE_INFO_BW_80,
    RATE_INFO_BW_160,
    RATE_INFO_BW_320,
    RATE_INFO_BW_HE_RU,
    RATE_INFO_BW_EHT_RU,
};

struct rate_info {
    u8 flags;
    u8 mcs;
    u8 nss;
    u8 bw;
    u16 legacy;
    u8 he_gi;
    u8 he_dcm;
    u8 he_ru_alloc;
    u8 eht_gi;
    u8 eht_ru_alloc;
};

/* ================================================================== */
/* RX status flags                                                     */
/* ================================================================== */
#define RX_FLAG_MMIC_STRIPPED       (1 << 0)
#define RX_FLAG_MMIC_ERROR          (1 << 1)
#define RX_FLAG_IV_STRIPPED         (1 << 2)
#define RX_FLAG_FAILED_FCS_CRC      (1 << 3)
#define RX_FLAG_FAILED_PLCP_CRC     (1 << 4)
#define RX_FLAG_MACTIME_START       (1 << 5)
#define RX_FLAG_MACTIME_END         (1 << 6)
#define RX_FLAG_AMPDU_DETAILS       (1 << 7)
#define RX_FLAG_AMPDU_REPORT_ZEROLEN (1 << 8)
#define RX_FLAG_SHORTPRE            (1 << 9)
#define RX_FLAG_STBC_MASK           (3 << 10)
#define RX_FLAG_10MHZ               (1 << 12)
#define RX_FLAG_5MHZ                (1 << 13)
#define RX_FLAG_AMSDU_MORE          (1 << 14)
#define RX_FLAG_RADIOTAP_HE         (1 << 15)
#define RX_FLAG_RADIOTAP_HE_MU      (1 << 16)
#define RX_FLAG_RADIOTAP_TLV_AT_END (1 << 17)
#define RX_FLAG_MIC_STRIPPED        (1 << 18)
#define RX_FLAG_ONLY_MONITOR        (1 << 19)
#define RX_FLAG_SKIP_MONITOR        (1 << 20)
#define RX_FLAG_8023                (1 << 21)
#define RX_FLAG_DUP_VALIDATED       (1 << 22)
#define RX_FLAG_AMPDU_EOF_BIT       (1 << 23)
#define RX_FLAG_DECRYPTED           (1 << 24)
#define RX_FLAG_AMSDU_MORE_8023     (1 << 25)

/* Ported for mt76wm (XNU): bit values aligned with Linux
 * enum mac80211_rx_encoding_flags (mt76x02 uses RX_ENC_FLAG_SHORTPRE and
 * RX_ENC_FLAG_STBC_SHIFT; mt76_connac uses RX_ENC_FLAG_STBC_MASK * n). */
#define RX_ENC_FLAG_SHORTPRE        (1 << 0)
#define RX_ENC_FLAG_SHORT_GI        (1 << 2)
#define RX_ENC_FLAG_HT_GF           (1 << 3)
#define RX_ENC_FLAG_STBC_MASK       ((1 << 4) | (1 << 5))
#define RX_ENC_FLAG_LDPC            (1 << 6)
#define RX_ENC_FLAG_BF              (1 << 7)
#define RX_ENC_FLAG_STBC_SHIFT      4

enum rx_enc_flags_pos {
    RX_ENC_FLAG_SHORT_GI_POS = 0,
};

enum nl80211_chan_width_to_rx {
    __RX_ENC_80211_MAX_PLACEHOLDER
};

enum {
    RX_ENC_LEGACY = 0,
    RX_ENC_HT,
    RX_ENC_VHT,
    RX_ENC_HE,
    RX_ENC_EHT,
};

enum {
    RX_FLAG_AMPDU_EOF_BIT_VALID = 0,
};

#define NL80211_CHAN_HT20 NL80211_CHAN_WIDTH_20

#define WIPHY_NAN_FLAGS_CONFIGURABLE_SYNC   (1 << 0)
#define WIPHY_NAN_FLAGS_USERSPACE_DE        (1 << 1)
#define NAN_OP_MODE_PHY_MODE_MASK           0x1
#define NAN_DEV_CAPA_EXT_KEY_ID_SUPPORTED   (1 << 0)

enum ieee80211_iter_flags {
    IEEE80211_IFACE_ITER_NORMAL = 0,
    IEEE80211_IFACE_ITER_RESUME_ALL = 1,
};

/* ================================================================== */
/* TX control / status flags                                           */
/* ================================================================== */
#define IEEE80211_TX_CTL_REQ_TX_STATUS     (1 << 0)
#define IEEE80211_TX_CTL_NO_ACK            (1 << 1)
#define IEEE80211_TX_CTL_CLEAR_PS_FILT     (1 << 2)
#define IEEE80211_TX_CTL_FIRST_FRAGMENT    (1 << 3)
#define IEEE80211_TX_CTL_SEND_AFTER_DTIM   (1 << 4)
#define IEEE80211_TX_CTL_AMPDU             (1 << 5)
#define IEEE80211_TX_CTL_INJECTED          (1 << 6)
#define IEEE80211_TX_CTL_TX_OFFCHAN        (1 << 7)
#define IEEE80211_TX_CTL_HW_80211_ENCAP    (1 << 8)
#define IEEE80211_TX_CTL_HW_80211_NO_ENCAP (1 << 9)
#define IEEE80211_TX_CTL_RATE_CTRL_PROBE   (1 << 10)
#define IEEE80211_TX_CTL_USE_MINRATE       (1 << 11)
#define IEEE80211_TX_CTL_ASSIGN_SEQ        (1 << 12)
#define IEEE80211_TX_CTL_DONTFRAG          (1 << 12)
#define IEEE80211_TX_CTL_NO_PS_BUFFER      (1 << 13)
#define IEEE80211_TX_CTL_MORE_FRAMES       (1 << 14)
#define IEEE80211_TX_CTL_LDPC              (1 << 15)
#define IEEE80211_TX_CTL_STBC              (1 << 16)

#define IEEE80211_TX_CTRL_MLO_LINK         (1 << 0)
#define IEEE80211_TX_CTRL_MLO_LINK_UNSPEC  0xf
#define IEEE80211_TX_CTRL_PS_RESPONSE      (1 << 1)
#define IEEE80211_TX_CTRL_DONT_USE_RATE_MASK (1 << 2)

#define IEEE80211_TX_STAT_ACK              (1 << 0)
#define IEEE80211_TX_STAT_AMPDU            (1 << 1)
#define IEEE80211_TX_STAT_TX_FILTERED      (1 << 2)
#define IEEE80211_TX_STAT_NOACK_TRANSMITTED (1 << 3)
#define IEEE80211_TX_STAT_TXD_DRIVER_INT   (1 << 4)

#define IEEE80211_TX_STATUS_EOSP           (1 << 0)

#define IEEE80211_TX_STAT_AMPDU_NO_BACK    (1 << 5)

/* ================================================================== */
/* 802.11 frame constants                                              */
/* ================================================================== */
#define IEEE80211_FTYPE_MGMT    0x0000
#define IEEE80211_FTYPE_CTL     0x0004
#define IEEE80211_FTYPE_DATA    0x0008

#define IEEE80211_FCTL_TODS     0x0100
#define IEEE80211_FCTL_FROMDS   0x0200
#define IEEE80211_FCTL_MOREFRAGS 0x0400
#define IEEE80211_FCTL_RETRY    0x0800
#define IEEE80211_FCTL_PM       0x1000
#define IEEE80211_FCTL_MOREDATA 0x2000
#define IEEE80211_FCTL_PROTECTED 0x4000
#define IEEE80211_FCTL_ORDER    0x8000
#define IEEE80211_FCTL_FTYPE    0x000c
#define IEEE80211_FCTL_STYPE    0x00f0

#define IEEE80211_STYPE_ASSOC_REQ   0x0000
#define IEEE80211_STYPE_ASSOC_RESP  0x0010
#define IEEE80211_STYPE_REASSOC_REQ 0x0020
#define IEEE80211_STYPE_REASSOC_RESP 0x0030
#define IEEE80211_STYPE_PROBE_REQ   0x0040
#define IEEE80211_STYPE_PROBE_RESP  0x0050
#define IEEE80211_STYPE_BEACON      0x0080
#define IEEE80211_STYPE_ATIM        0x0090
#define IEEE80211_STYPE_DISASSOC    0x00a0
#define IEEE80211_STYPE_AUTH        0x00b0
#define IEEE80211_STYPE_DEAUTH      0x00c0
#define IEEE80211_STYPE_ACTION      0x00d0

#define IEEE80211_STYPE_CTL_WRAPPER 0x0074
#define IEEE80211_STYPE_BACK_REQ    0x0084
#define IEEE80211_STYPE_BACK        0x0094
#define IEEE80211_STYPE_PSPOLL      0x00a4
#define IEEE80211_STYPE_RTS         0x00b4
#define IEEE80211_STYPE_CTS         0x00c4
#define IEEE80211_STYPE_ACK         0x00d4
#define IEEE80211_STYPE_CFEND       0x00e4
#define IEEE80211_STYPE_CFENDACK    0x00f4

#define IEEE80211_STYPE_QOS_DATA    0x0088
#define IEEE80211_STYPE_DATA        0x0000
#define IEEE80211_STYPE_NULLFUNC    0x0048
#define IEEE80211_STYPE_QOS_NULLFUNC 0x00c8

#define IEEE80211_FTYPE_EXT     0x000c

#define IEEE80211_QOS_CTL_LEN           2
#define IEEE80211_QOS_CTL_TID_MASK      0x000f
#define IEEE80211_QOS_CTL_TAG1D_MASK    0x0007
#define IEEE80211_QOS_CTL_EOSP          0x0010
#define IEEE80211_QOS_CTL_ACK_POLICY_MASK 0x0060
#define IEEE80211_QOS_CTL_ACK_POLICY_NOACK 0x0060

#define IEEE80211_HT_CTL_LEN            4
#define IEEE80211_MIN_ACTION_SIZE(type) offsetofend(struct ieee80211_mgmt, u.action.type)
#define IEEE80211_MAX_DATA_LEN          2304
#define IEEE80211_MAX_SSID_LEN          32
#define IEEE80211_MAX_MPDU_LEN_VHT_7991 7991
#define IEEE80211_MAX_MPDU_LEN_VHT_11454 11454

#define IEEE80211_SEQ_TO_SN(seq)    (((seq) & IEEE80211_SCTL_SEQ) >> 4)
#define IEEE80211_SN_TO_SEQ(sn)     (((sn) << 4) & IEEE80211_SCTL_SEQ)
#define IEEE80211_SCTL_SEQ          0xfff0

static inline int ieee80211_sn_less(u16 sn1, u16 sn2)
{
    return ((sn1 - sn2) & 0xfff) > 0x800;
}
static inline u16 ieee80211_sn_inc(u16 sn)
{
    return (sn + 1) & 0xfff;
}
static inline u16 ieee80211_sn_sub(u16 sn1, u16 sn2)
{
    return (sn1 - sn2) & 0xfff;
}

struct ieee80211_hdr {
    __le16 frame_control;
    __le16 duration_id;
    u8 addr1[ETH_ALEN];
    u8 addr2[ETH_ALEN];
    u8 addr3[ETH_ALEN];
    __le16 seq_ctrl;
    u8 addr4[ETH_ALEN];
} __packed;

struct ieee80211_hdr_3addr {
    __le16 frame_control;
    __le16 duration_id;
    u8 addr1[ETH_ALEN];
    u8 addr2[ETH_ALEN];
    u8 addr3[ETH_ALEN];
    __le16 seq_ctrl;
} __packed;

struct ieee80211_bar {
    __le16 frame_control;
    __le16 duration;
    u8 ra[ETH_ALEN];
    u8 ta[ETH_ALEN];
    __le16 control;
    __le16 start_seq_num;
} __packed;

struct ieee80211_mgmt {
    __le16 frame_control;
    __le16 duration;
    u8 da[ETH_ALEN];
    u8 sa[ETH_ALEN];
    u8 bssid[ETH_ALEN];
    __le16 seq_ctrl;
    union {
        struct {
            u8 category;
            u8 action_code;
            union {
                struct {
                    __le16 capab;
                    __le16 timeout;
                    __le16 start_seq_num;
                } __packed addba_req;
                struct {
                    __le16 capab;
                    __le16 status;
                } __packed addba_resp;
                struct {
                    __le16 params;
                    __le16 reason_code;
                } __packed delba;
                u8 variable[0];
            };
        } __packed action;
        struct {
            __le16 auth_alg;
            __le16 auth_transaction;
            __le16 status_code;
            u8 variable[];
        } __packed auth;
        struct {
            __le16 reason_code;
        } __packed deauth;
        struct {
            __le16 capab_info;
            __le16 beacon_int;
            u8 timestamp[8];
            u8 variable[];
        } __packed beacon;
        struct {
            __le16 capab_info;
            __le16 status_code;
            __le16 aid;
            u8 variable[];
        } __packed assoc_resp;
        struct {
            u8 variable[];
        } __packed probe_req;
        u8 payload[0];
    } u;
} __packed;

#define IEEE80211_BAR_CTRL_TID_INFO_MASK 0x0007
#define IEEE80211_BAR_CTRL_TID_INFO_SHIFT 0

#define WLAN_CATEGORY_BACK      3
#define WLAN_CATEGORY_SPECTRUM_MGMT 0
#define WLAN_ACTION_ADDBA_REQ   0
#define WLAN_ACTION_ADDBA_RESP  1
#define WLAN_ACTION_DELBA_REQ   2
#define WLAN_ACTION_DELBA_RESP  3

/* frame type predicates */
#define IEEE80211_FTYPE(fc)  ((fc) & cpu_to_le16(IEEE80211_FCTL_FTYPE))
#define IEEE80211_STYPE(fc)  ((fc) & cpu_to_le16(IEEE80211_FCTL_STYPE))

static inline int ieee80211_is_mgmt(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x000c) == IEEE80211_FTYPE_MGMT;
}
static inline int ieee80211_is_ctl(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x000c) == IEEE80211_FTYPE_CTL;
}
static inline int ieee80211_is_data(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x000c) == IEEE80211_FTYPE_DATA;
}
static inline int ieee80211_is_data_present(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x004c) == IEEE80211_FTYPE_DATA;
}
static inline int ieee80211_is_data_qos(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x008c) == (IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_DATA);
}
static inline int ieee80211_is_qos_nullfunc(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_NULLFUNC);
}
static inline int ieee80211_is_beacon(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_BEACON);
}
static inline int ieee80211_is_probe_req(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_REQ);
}
static inline int ieee80211_is_probe_resp(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_RESP);
}
static inline bool ieee80211_has_protected(__le16 fc)
{
    return (le16_to_cpu(fc) & IEEE80211_FCTL_PROTECTED) != 0;
}
static inline int ieee80211_is_auth(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_AUTH);
}
static inline int ieee80211_is_deauth(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_DEAUTH);
}
static inline int ieee80211_is_disassoc(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_DISASSOC);
}
static inline int ieee80211_is_assoc_resp(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ASSOC_RESP);
}
static inline int ieee80211_is_action(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ACTION);
}
static inline int ieee80211_is_back_req(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_CTL | IEEE80211_STYPE_BACK_REQ);
}
static inline int ieee80211_is_back(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_CTL | IEEE80211_STYPE_BACK);
}
static inline int ieee80211_is_pspoll(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_CTL | IEEE80211_STYPE_PSPOLL);
}
static inline int ieee80211_is_rts(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_CTL | IEEE80211_STYPE_RTS);
}
static inline int ieee80211_is_cts(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_CTL | IEEE80211_STYPE_CTS);
}
static inline int ieee80211_is_ack(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_CTL | IEEE80211_STYPE_ACK);
}
static inline int ieee80211_is_cfend(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_CTL | IEEE80211_STYPE_CFEND);
}
static inline int ieee80211_is_bufferable_ctrl(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_CTL | IEEE80211_STYPE_BACK_REQ) ||
           (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_CTL | IEEE80211_STYPE_PSPOLL) ||
           (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_CTL | IEEE80211_STYPE_CFEND);
}
static inline bool ieee80211_is_bufferable_mmpdu(struct sk_buff *skb)
{
    struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
    __le16 fc = hdr->frame_control;

    return ieee80211_is_qos_nullfunc(fc) || ieee80211_is_bufferable_ctrl(fc);
}
static inline bool ieee80211_is_robust_mgmt_frame(struct sk_buff *skb)
{
    struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
    __le16 fc = hdr->frame_control;

    if (!ieee80211_is_mgmt(fc))
        return 0;
    if ((le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_DEAUTH) ||
        (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_DISASSOC))
        return 1;
    return (le16_to_cpu(fc) & 0x00fc) == (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ACTION);
}

static inline int ieee80211_has_morefrags(__le16 fc)
{
    return le16_to_cpu(fc) & IEEE80211_FCTL_MOREFRAGS;
}
static inline int ieee80211_has_a4(__le16 fc)
{
    u16 tmp = le16_to_cpu(fc);
    return (tmp & (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) ==
           (IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS);
}
static inline int ieee80211_has_order(__le16 fc)
{
    return le16_to_cpu(fc) & IEEE80211_FCTL_ORDER;
}
static inline int ieee80211_is_first_frag(__le16 fc)
{
    return !ieee80211_has_morefrags(fc);
}

static inline int ieee80211_hdrlen(__le16 fc)
{
    u16 tmp = le16_to_cpu(fc);
    int hdrlen = 24;

    if (ieee80211_is_ctl(fc)) {
        if ((tmp & 0x00fc) == (IEEE80211_FTYPE_CTL | IEEE80211_STYPE_CTS) ||
            (tmp & 0x00fc) == (IEEE80211_FTYPE_CTL | IEEE80211_STYPE_ACK))
            return 10;
        return 16;
    }
    if (ieee80211_is_data(fc) && ieee80211_has_a4(fc))
        hdrlen = 30;
    if (ieee80211_is_data_qos(fc)) {
        hdrlen += 2;
        if (ieee80211_has_order(fc))
            hdrlen += 4;
    }
    return hdrlen;
}

static inline int ieee80211_get_hdrlen_from_skb(struct sk_buff *skb)
{
    struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
    return ieee80211_hdrlen(hdr->frame_control);
}

static inline u8 *ieee80211_get_qos_ctl(struct ieee80211_hdr *hdr)
{
    return (u8 *)(hdr + 1) - 2;
}

/* sequence helpers */
static inline u16 ieee80211_get_seq_ctl(struct ieee80211_hdr *hdr)
{
    return le16_to_cpu(hdr->seq_ctrl);
}

#define IEEE80211_WMM_IE_STA_QOSINFO_AC_VO   0x01
#define IEEE80211_WMM_IE_STA_QOSINFO_AC_VI   0x02
#define IEEE80211_WMM_IE_STA_QOSINFO_AC_BK   0x04
#define IEEE80211_WMM_IE_STA_QOSINFO_AC_BE   0x08

/* ================================================================== */
/* channel / bands                                                     */
/* ================================================================== */
#define IEEE80211_CHAN_DISABLED     (1 << 0)
#define IEEE80211_CHAN_NO_IR        (1 << 1)
#define IEEE80211_CHAN_RADAR        (1 << 2)
#define IEEE80211_CHAN_NO_HT40PLUS  (1 << 3)
#define IEEE80211_CHAN_NO_HT40MINUS (1 << 4)
#define IEEE80211_CHAN_NO_OFDM      (1 << 5)
#define IEEE80211_CHAN_NO_80MHZ     (1 << 6)
#define IEEE80211_CHAN_NO_160MHZ    (1 << 7)
#define IEEE80211_CHAN_INDOOR_ONLY  (1 << 8)
#define IEEE80211_CHAN_NO_EHT       (1 << 9)
#define IEEE80211_CHAN_NO_6GHZ_VLP  (1 << 10)

/* HT capability: MPDU density (IEEE80211_HT_MPDU_DENSITY_*) */
#define IEEE80211_HT_MPDU_DENSITY_NONE 0
#define IEEE80211_HT_MPDU_DENSITY_0_5  1
#define IEEE80211_HT_MPDU_DENSITY_1    2
#define IEEE80211_HT_MPDU_DENSITY_2    4
#define IEEE80211_HT_MPDU_DENSITY_4    5
#define IEEE80211_HT_MPDU_DENSITY_8    6
#define IEEE80211_HT_MPDU_DENSITY_16   7

/* uapi/linux/nl80211.h: per-channel DFS/CAC state (mt76x2/mt76x02) */
enum nl80211_dfs_state {
    NL80211_DFS_USABLE,
    NL80211_DFS_AVAILABLE,
    NL80211_DFS_UNAVAILABLE,
};

struct ieee80211_channel {
    enum nl80211_band band;
    u16 center_freq;
    u16 hw_value;
    u32 flags;
    u32 orig_flags;
    int max_antenna_gain;
    int max_power;
    int max_reg_power;
    int orig_mag;
    int orig_mpwr;
    enum nl80211_dfs_state dfs_state;
};

struct ieee80211_rate {
    u16 bitrate;
    u16 hw_value;
    u16 hw_value_short;
    u16 flags;
};

#define IEEE80211_RATE_SHORT_PREAMBLE (1 << 0)
#define IEEE80211_RATE_ERP_G          (1 << 1)

/* ---- HT / VHT / HE / EHT capabilities ---- */
struct ieee80211_mcs_info {
    u8 rx_mask[4];
    __le16 rx_highest;
    u8 tx_params;
    u8 tx_mcs[4];
};

struct ieee80211_sta_ht_cap {
    u16 cap;
    bool ht_supported;
    u8 ampdu_factor;
    u8 ampdu_density;
    struct ieee80211_mcs_info mcs;
};

#define IEEE80211_HT_CAP_LDPC_CODING        0x0001
#define IEEE80211_HT_CAP_SUP_WIDTH_20_40    0x0002
#define IEEE80211_HT_CAP_SGI_20             0x0020
#define IEEE80211_HT_CAP_SGI_40             0x0040
#define IEEE80211_HT_CAP_TX_STBC            0x0080
#define IEEE80211_HT_CAP_RX_STBC            0x0300
#define IEEE80211_HT_CAP_MAX_AMSDU          0x0800
#define IEEE80211_HT_CAP_DSSSCCK40          0x1000

/* HT operation mode bits (mt7615 mcu.c protection setup) */
#define IEEE80211_HT_OP_MODE_PROTECTION             0x0003
#define IEEE80211_HT_OP_MODE_PROTECTION_NONMEMBER   0x0001
#define IEEE80211_HT_OP_MODE_PROTECTION_20MHZ       0x0002
#define IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED 0x0003
#define IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT       0x0004

#define IEEE80211_HT_AMPDU_PARM_FACTOR      0x03
#define IEEE80211_HT_AMPDU_PARM_DENSITY     0x1c

struct ieee80211_vht_mcs_info {
    __le16 rx_mcs_map;
    __le16 rx_highest;
    __le16 tx_mcs_map;
    __le16 tx_highest;
};

struct ieee80211_sta_vht_cap {
    bool vht_supported;
    u32 cap;
    struct ieee80211_vht_mcs_info vht_mcs;
};

#define IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895     0x00000000
#define IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991     0x00000001
#define IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454    0x00000002
#define IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ   0x00000004
#define IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ 0x00000008
#define IEEE80211_VHT_CAP_RXLDPC                   0x00000010
#define IEEE80211_VHT_CAP_SHORT_GI_80              0x00000020
#define IEEE80211_VHT_CAP_SHORT_GI_160             0x00000040
#define IEEE80211_VHT_CAP_TXSTBC                   0x00000080
#define IEEE80211_VHT_CAP_RXSTBC                   0x00000700
#define IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE    0x00000800
#define IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE    0x00001000
#define IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT     13
#define IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK      0x0000e000
#define IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE    0x00080000
#define IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE    0x00100000
#define IEEE80211_VHT_CAP_VHT_EXT_NSS_BW_SHIFT     29
#define IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK 0x78000000

#define IEEE80211_VHT_MCS_NOT_SUPPORTED            0xffff

struct ieee80211_he_cap_elem {
    u8 mac_cap_info[6];
    u8 phy_cap_info[11];
} __packed;

struct ieee80211_he_mcs_nss_supp {
    __le16 rx_mcs_80;
    __le16 rx_mcs_160;
    __le16 rx_mcs_80p80;
    __le16 tx_mcs_80;
    __le16 tx_mcs_160;
    __le16 tx_mcs_80p80;
} __packed;

struct ieee80211_he_6ghz_capa {
    __le16 capa;
};

struct ieee80211_he_cap {
    bool has_he;
    struct ieee80211_he_cap_elem he_cap_elem;
    struct ieee80211_he_mcs_nss_supp he_mcs_nss_supp;
    u8 ppe_thres[25];
} __packed;

/* backwards-friendly alias used by some call sites */
struct ieee80211_sta_he_cap {
    bool has_he;
    struct ieee80211_he_cap_elem he_cap_elem;
    struct ieee80211_he_mcs_nss_supp he_mcs_nss_supp;
    u8 ppe_thres[25];
};

#define IEEE80211_HE_MAC_CAP0_HTC_HE               0x01
#define IEEE80211_HE_MAC_CAP2_BSR                  0x08
#define IEEE80211_HE_MAC_CAP3_OMI_CONTROL          0x02
#define IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU       0x80
#define IEEE80211_HE_MAC_CAP4_BQR                  0x01

#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_2G   0x02
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_5G   0x04
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G 0x20
#define IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD               0x80
#define IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8_US    0x0c
#define IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ                  0x20
#define IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ                  0x10
#define IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2_US                0x08
#define IEEE80211_HE_PHY_CAP2_NDP_4                                0x08
#define IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE                 0x08
#define IEEE80211_HE_PHY_CAP7_STBC_RX_ABOVE_80MHZ                  0x01
#define IEEE80211_HE_PHY_CAP7_STBC_TX_ABOVE_80MHZ                  0x02
#define IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI     0x08
#define IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_1XLTF_AND_08_US_GI     0x04
#define IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI     0x08
#define IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU    0x08
#define IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU    0x10
#define IEEE80211_HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK           0x20
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_16US             0x03

#define IEEE80211_HE_6GHZ_CAP_MIN_MPDU_START    0x0007
#define IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP 0x0038

#define IEEE80211_PPE_THRES_RU_INDEX_BITMASK_MASK 0x0f
#define IEEE80211_PPE_THRES_NSS_MASK              0x07
#define IEEE80211_PPE_THRES_INFO_PPET_SIZE        0x03

struct ieee80211_eht_mcs_nss_supp_20mhz_only {
    union {
        struct {
            u8 rx_tx_mcs7_max_nss;
            u8 rx_tx_mcs9_max_nss;
            u8 rx_tx_mcs11_max_nss;
            u8 rx_tx_mcs13_max_nss;
        };
        u8 rx_tx_max_nss[4];
    };
} __packed;

struct ieee80211_eht_mcs_nss_supp_bw {
    union {
        struct {
            u8 rx_tx_mcs9_max_nss;
            u8 rx_tx_mcs11_max_nss;
            u8 rx_tx_mcs13_max_nss;
        };
        u8 rx_tx_max_nss[3];
    };
} __packed;

struct ieee80211_eht_mcs_nss_supp {
    union {
        struct ieee80211_eht_mcs_nss_supp_20mhz_only only_20mhz;
        struct {
            struct ieee80211_eht_mcs_nss_supp_bw _80;
            struct ieee80211_eht_mcs_nss_supp_bw _160;
            struct ieee80211_eht_mcs_nss_supp_bw _320;
        } __packed bw;
    } __packed;
};

struct ieee80211_eht_cap_elem_fixed {
    u8 mac_cap_info[2];
    u8 phy_cap_info[9];
} __packed;

struct ieee80211_sta_eht_cap {
    bool has_eht;
    struct ieee80211_eht_cap_elem_fixed eht_cap_elem;
    struct ieee80211_eht_mcs_nss_supp eht_mcs_nss_supp;
    u8 eht_ppe_thres[32];
};

#define IEEE80211_EHT_PHY_CAP0_320MHZ_IN_6GHZ 0x02

#define IEEE80211_MAX_AMPDU_BUF_HE      256
#define IEEE80211_MAX_AMPDU_BUF_EHT     1024

#define IEEE80211_OPMODE_NOTIF_RX_NSS_SHIFT 4

struct ieee80211_sband_iftype_data {
    u16 types_mask;
    struct ieee80211_sta_he_cap he_cap;
    struct ieee80211_sta_eht_cap eht_cap;
    struct ieee80211_he_6ghz_capa he_6ghz_capa;
};

struct ieee80211_supported_band {
    struct ieee80211_channel *channels;
    struct ieee80211_rate *bitrates;
    enum nl80211_band band;
    int n_channels;
    int n_bitrates;
    struct ieee80211_sta_ht_cap ht_cap;
    struct ieee80211_sta_vht_cap vht_cap;
    const struct ieee80211_sband_iftype_data *iftype_data;
    unsigned int n_iftype_data;
};

/* ================================================================== */
/* wiphy                                                               */
/* ================================================================== */
#define WIPHY_FLAG_IBSS_RSN                 (1 << 0)
#define WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL    (1 << 1)
#define WIPHY_FLAG_SUPPORTS_EXT_KEK_KCK     (1 << 2)
#define WIPHY_FLAG_AP_UAPSD                 (1 << 3)
#define WIPHY_FLAG_HAS_CHANNEL_SWITCH       (1 << 4)

#define WIPHY_FLAG_SPLIT_SCAN_DWELL         (1 << 5)
#define WIPHY_FLAG_4ADDR_AP                 (1 << 6)
#define WIPHY_FLAG_4ADDR_STATION            (1 << 7)
#define WIPHY_FLAG_SPLIT_SCAN_6GHZ          (1 << 8)
#define WIPHY_FLAG_SUPPORTS_MLO             (1 << 9)

enum wiphy_flags_bit {
    WIPHY_FLAG_BIT_IBSS_RSN,
};

#define WIPHY_FLAG_SUPPORTS_EXT_KEK_KCK_OTA 0

#define REGULATORY_WIPHY_SELF_MANAGED       (1 << 0)
#define REGULATORY_CUSTOM_REG               (1 << 1)
#define REGULATORY_STRICT_REG               (1 << 2)
#define REGULATORY_DISABLE_BEACON_HINTS     (1 << 3)
#define REGULATORY_COUNTRY_IE_FOLLOW_POWER  (1 << 4)
#define REGULATORY_COUNTRY_IE_IGNORE        (1 << 5)

#define WIPHY_MAX_ACK_RANDOM_LEVEL 10

#define NL80211_RRF_NO_IR       (1 << 0)
#define NL80211_RRF_DFS         (1 << 1)
#define NL80211_RRF_NO_OFDM     (1 << 2)
#define NL80211_RRF_NO_160MHZ   (1 << 3)
#define NL80211_RRF_NO_80MHZ    (1 << 4)
#define NL80211_RRF_NO_HE       (1 << 5)
#define NL80211_RRF_NO_320MHZ   (1 << 6)
#define NL80211_RRF_NO_EHT      (1 << 7)
#define NL80211_RRF_PSD         (1 << 9)
#define NL80211_RRF_DFS_CONCURRENT (1 << 10)

#define MHZ_TO_KHZ(mhz) ((mhz) * 1000)
#define KHZ_TO_MHZ(khz) ((khz) / 1000)
#define DBM_TO_MBM(dbm) ((dbm) * 100)
#define MBM_TO_DBM(mbm) ((mbm) / 100)
#define DBI_TO_MBI(dbi) ((dbi) * 2)
#define MBI_TO_DBI(mbi) ((mbi) / 2)

struct ieee80211_freq_range {
    u32 start_freq_khz;
    u32 end_freq_khz;
    u32 max_bandwidth_khz;
};

struct ieee80211_power_rule {
    u32 max_antenna_gain;
    u32 max_eirp;
};

struct ieee80211_reg_rule {
    struct ieee80211_freq_range freq_range;
    struct ieee80211_power_rule power_rule;
    u32 flags;
};

/* positional to stay C++14-friendly (no nested designated inits) */
#define REG_RULE(start, end, bw, gain, eirp, flags) \
    { { (start), (end), (bw) }, { (gain), (eirp) }, (flags) }

struct ieee80211_regdomain {
    u32 n_reg_rules;
    char alpha2[3];
    enum nl80211_dfs_regions dfs_region;
    struct ieee80211_reg_rule reg_rules[NL80211_MAX_SUPP_REG_RULES];
};

/* ================================================================== */
/* cfg80211 channel definition / survey / SAR                          */
/* ================================================================== */
struct cfg80211_chan_def {
    struct ieee80211_channel *chan;
    enum nl80211_chan_width width;
    u32 center_freq1;
    u32 center_freq2;
    u16 punctured, npca_punctured;
};

/* chan request (operating + AP definitions) */
struct ieee80211_chan_req {
    struct cfg80211_chan_def oper;
    struct cfg80211_chan_def ap;
    bool require_npca;
};

struct cfg80211_ssid {
    u8 ssid[NL80211_MAX_SSID_LEN];
    u8 ssid_len;
};

#define CFG80211_NAN_SCHED_NUM_TIME_SLOTS 16
#define CFG80211_NAN_MAX_PEER_MAPS 4

enum environment_cap {
    ENVIRON_ANY,
    ENVIRON_INDOOR,
    ENVIRON_OUTDOOR,
};

struct survey_info {
    struct ieee80211_channel *channel;
    u64 time;
    u64 time_busy;
    u64 time_rx;
    u64 time_tx;
    u64 time_bss_rx;
    u64 time_scan;
    u32 filled;
    s8 noise;
};

#define SURVEY_INFO_NOISE_DBM       (1 << 0)
#define SURVEY_INFO_IN_USE          (1 << 1)
#define SURVEY_INFO_TIME            (1 << 2)
#define SURVEY_INFO_TIME_BUSY       (1 << 3)
#define SURVEY_INFO_TIME_RX         (1 << 4)
#define SURVEY_INFO_TIME_TX         (1 << 5)
#define SURVEY_INFO_TIME_BSS_RX     (1 << 6)
#define SURVEY_INFO_TIME_SCAN       (1 << 7)

struct cfg80211_sar_freq_ranges {
    u32 start_freq;
    u32 end_freq;
};

struct cfg80211_sar_specs {
    u32 type;
    u32 num_sub_specs;
    struct cfg80211_sar_sub_specs {
        s32 power;
        u32 freq_range_index;
    } sub_specs[];
};

struct cfg80211_scan_info {
    bool aborted;
};

struct cfg80211_scan_request {
    struct cfg80211_ssid *ssids;
    int n_ssids;
    int n_channels;
    u8 n_6ghz_params;
    struct cfg80211_scan_6ghz_params *scan_6ghz_params;
    u32 flags;
    struct ieee80211_channel **channels;
    const u8 *bssid;
    const u8 *ie;
    size_t ie_len;
    u8 mac_addr[ETH_ALEN];
    u8 mac_addr_mask[ETH_ALEN];
};

#define NL80211_SCAN_FLAG_RANDOM_ADDR       (1 << 0)
#define NL80211_SCAN_FLAG_LOW_SPAN          (1 << 10)

struct cfg80211_match_set {
    struct cfg80211_ssid ssid;
    u8 bssid[ETH_ALEN];
    s32 rssi_thold;
};

struct cfg80211_sched_scan_plan {
    u32 interval;
    u32 iterations;
};

struct cfg80211_sched_scan_request {
    struct cfg80211_ssid *ssids;
    int n_ssids;
    int n_channels;
    struct ieee80211_channel **channels;
    u32 flags;
    u8 mac_addr[ETH_ALEN];
    u8 mac_addr_mask[ETH_ALEN];
    const u8 *ie;
    size_t ie_len;
    u32 interval;
    u32 delay;
    u32 min_delay;
    u32 max_delay;
    int n_scan_plans;
    struct cfg80211_sched_scan_plan *scan_plans;
    struct cfg80211_match_set *match_sets;
    int n_match_sets;
    s32 min_rssi_thold;
};

/* ================================================================== */
/* station info (cfg80211)                                             */
/* ================================================================== */
struct station_info {
    u64 filled;
    u64 connected_time;
    u32 inactive_time;
    u32 rx_bytes;
    u32 tx_bytes;
    u32 tx_retries;
    u32 tx_failed;
    u32 rx_dropped_misc;
    struct rate_info txrate;
    s8 ack_signal;
    s8 ack_signal_avg;
    s8 avg_ack_signal;
    s8 signal;
};





/* ================================================================== */
/* radiotap HE                                                         */
/* ================================================================== */
#define IEEE80211_RADIOTAP_TIMESTAMP_UNIT_US (1 << 2)

/*
 * Field bit positions: self-consistent minimal layout (semantics only
 * need to round-trip within this port; TODO: exact radiotap encoding).
 */
#define IEEE80211_RADIOTAP_HE_DATA1_FORMAT_SU            (1 << 0)
#define IEEE80211_RADIOTAP_HE_DATA1_FORMAT_EXT_SU        (1 << 1)
#define IEEE80211_RADIOTAP_HE_DATA1_FORMAT_MU            (1 << 2)
#define IEEE80211_RADIOTAP_HE_DATA1_FORMAT_TRIG          (1 << 3)
#define IEEE80211_RADIOTAP_HE_DATA1_BSS_COLOR_KNOWN      (1 << 4)
#define IEEE80211_RADIOTAP_HE_DATA1_BEAM_CHANGE_KNOWN    (1 << 5)
#define IEEE80211_RADIOTAP_HE_DATA1_UL_DL_KNOWN          (1 << 6)
#define IEEE80211_RADIOTAP_HE_DATA1_DATA_MCS_KNOWN       (1 << 7)
#define IEEE80211_RADIOTAP_HE_DATA1_DATA_DCM_KNOWN       (1 << 8)
#define IEEE80211_RADIOTAP_HE_DATA1_CODING_KNOWN         (1 << 9)
#define IEEE80211_RADIOTAP_HE_DATA1_LDPC_XSYMSEG_KNOWN   (1 << 10)
#define IEEE80211_RADIOTAP_HE_DATA1_STBC_KNOWN           (1 << 11)
#define IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE_KNOWN     (1 << 12)
#define IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE2_KNOWN    (1 << 13)
#define IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE3_KNOWN    (1 << 14)
#define IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE4_KNOWN    (1 << 15)

#define IEEE80211_RADIOTAP_HE_DATA2_BW_RU_ALLOC_KNOWN    (1 << 0)
#define IEEE80211_RADIOTAP_HE_DATA2_GI_KNOWN             (1 << 1)
#define IEEE80211_RADIOTAP_HE_DATA2_TXBF_KNOWN           (1 << 2)
#define IEEE80211_RADIOTAP_HE_DATA2_PE_DISAMBIG_KNOWN    (1 << 3)
#define IEEE80211_RADIOTAP_HE_DATA2_TXOP_KNOWN           (1 << 4)
#define IEEE80211_RADIOTAP_HE_DATA2_RU_OFFSET_KNOWN      (1 << 5)
#define IEEE80211_RADIOTAP_HE_DATA2_RU_OFFSET            GENMASK(15, 6)
#define IEEE80211_RADIOTAP_HE_DATA2_DATA_BW_RU_ALLOC     GENMASK(15, 6)

#define IEEE80211_RADIOTAP_HE_DATA3_BSS_COLOR            GENMASK(5, 0)
#define IEEE80211_RADIOTAP_HE_DATA3_BEAM_CHANGE          BIT(6)
#define IEEE80211_RADIOTAP_HE_DATA3_UL_DL                BIT(7)
#define IEEE80211_RADIOTAP_HE_DATA3_LDPC_XSYMSEG         BIT(8)
#define IEEE80211_RADIOTAP_HE_DATA3_DATA_MCS             GENMASK(15, 9)

#define IEEE80211_RADIOTAP_HE_DATA4_SU_MU_SPTL_REUSE     GENMASK(3, 0)
#define IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE1       GENMASK(7, 4)
#define IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE2       GENMASK(11, 8)
#define IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE3       GENMASK(15, 12)
#define IEEE80211_RADIOTAP_HE_DATA4_MU_STA_ID            GENMASK(15, 4)
#define IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE4       GENMASK(15, 12)

#define IEEE80211_RADIOTAP_HE_DATA5_DATA_DCM             BIT(0)
#define IEEE80211_RADIOTAP_HE_DATA5_CODING               BIT(1)
#define IEEE80211_RADIOTAP_HE_DATA5_LDPC_XSYMSEG         BIT(2)
#define IEEE80211_RADIOTAP_HE_DATA5_STBC                 BIT(3)
#define IEEE80211_RADIOTAP_HE_DATA5_PE_DISAMBIG          BIT(4)
#define IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE             GENMASK(7, 5)
#define IEEE80211_RADIOTAP_HE_DATA5_TXBF                 BIT(8)

#define IEEE80211_RADIOTAP_HE_DATA6_TXOP                 GENMASK(6, 0)
#define IEEE80211_RADIOTAP_HE_DATA6_DOPPLER              BIT(7)


/* extra radiotap field bits used by mt76 (self-consistent layout) */
#define IEEE80211_RADIOTAP_HE_DATA1_BW_RU_ALLOC_KNOWN (1u << (0 % 15))
#define IEEE80211_RADIOTAP_HE_DATA1_DOPPLER_KNOWN (1u << (1 % 15))

struct ieee80211_radiotap_he {
    __le16 data1;
    __le16 data2;
    __le16 data3;
    __le16 data4;
    __le16 data5;
    __le16 data6;
    __le16 data7;
    __le16 data8;
} __packed;

#define IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_MCS_KNOWN       (1 << 0)
#define IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_DCM_KNOWN       (1 << 1)
#define IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH1_RU_KNOWN          (1 << 2)
#define IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH2_RU_KNOWN          (1 << 3)
#define IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_COMP_KNOWN      (1 << 4)
#define IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_SYMS_USERS_KNOWN (1 << 5)
#define IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_MCS             GENMASK(15, 8)
#define IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_DCM             BIT(6)
#define IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_SYMS_USERS      GENMASK(15, 8)

#define IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_KNOWN (1 << 0)
#define IEEE80211_RADIOTAP_HE_MU_FLAGS2_PUNC_FROM_SIG_A_BW_KNOWN (1 << 1)
#define IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW      GENMASK(5, 2)
#define IEEE80211_RADIOTAP_HE_MU_FLAGS2_PUNC_FROM_SIG_A_BW    GENMASK(9, 6)
#define IEEE80211_RADIOTAP_HE_MU_FLAGS2_SIG_B_SYMS_USERS      GENMASK(15, 8)

struct ieee80211_radiotap_he_mu {
    __le16 flags1;
    __le16 flags2;
    u8 ru_ch1[3];
    u8 ru_ch2[3];
    u8 padding[2];
} __packed;

struct ieee80211_radiotap_timestamp {
    u64 tsf;
    u16 units_pos;
    u8 accuracy;
} __packed;

/* radiotap TLV / EHT (Wi-Fi 7) definitions */
#include <net/ieee80211_radiotap.h>

/* ================================================================== */
/* basic MAC80211 numbers                                               */
/* ================================================================== */
#define IEEE80211_NUM_ACS    4
#define IEEE80211_NUM_TIDS   16
#define IEEE80211_MAX_CHAINS 4
#define IEEE80211_MLD_MAX_NUM_LINKS 15
#define IEEE80211_LINK_UNSPECIFIED  15

enum ieee80211_ac_numbers {
    IEEE80211_AC_VO = 0,
    IEEE80211_AC_VI,
    IEEE80211_AC_BE,
    IEEE80211_AC_BK,
};

enum ieee80211_sta_rx_bandwidth {
    IEEE80211_STA_RX_BW_20,
    IEEE80211_STA_RX_BW_40,
    IEEE80211_STA_RX_BW_80,
    IEEE80211_STA_RX_BW_160,
    IEEE80211_STA_RX_BW_320,
};

enum ieee80211_smps_mode {
    IEEE80211_SMPS_AUTOMATIC,
    IEEE80211_SMPS_OFF,
    IEEE80211_SMPS_STATIC,
    IEEE80211_SMPS_DYNAMIC,
    IEEE80211_SMPS_NUM_MODES,
};

enum ieee80211_sta_state {
    IEEE80211_STA_NOTEXIST,
    IEEE80211_STA_NONE,
    IEEE80211_STA_AUTH,
    IEEE80211_STA_ASSOC,
    IEEE80211_STA_AUTHORIZED,
};

enum ieee80211_frame_release_type {
    IEEE80211_FRAME_RELEASE_PSPOLL,
    IEEE80211_FRAME_RELEASE_UAPSD,
};

enum ieee80211_roc_type {
    IEEE80211_ROC_TYPE_NORMAL,
    IEEE80211_ROC_TYPE_MGMT_TX,
};

enum ieee80211_chanctx_switch_mode {
    CHANCTX_SWMODE_REASSIGN_VIF,
    CHANCTX_SWMODE_SWAP_CONTEXTS,
};

enum set_key_cmd {
    SET_KEY,
    DISABLE_KEY,
    DELETE_KEY,
};

enum sta_notify_cmd {
    STA_NOTIFY_SLEEP,
    STA_NOTIFY_AWAKE,
};

#define IEEE80211_VIF_BEACON_FILTER     (1 << 0)
#define IEEE80211_VIF_SUPPORTS_CQM_RSSI (1 << 1)
#define IEEE80211_VIF_MFP_ENABLED       (1 << 2)
#define IEEE80211_VIF_SUPPORTS_UAPSD    (1 << 3)
#define IEEE80211_VIF_GET_RLM_DATA      (1 << 4)

#define IEEE80211_KEY_FLAG_PAIRWISE     (1 << 0)
#define IEEE80211_KEY_FLAG_GENERATE_IV  (1 << 1)
#define IEEE80211_KEY_FLAG_GENERATE_MMIC (1 << 2)
#define IEEE80211_KEY_FLAG_PUT_IV_SPACE (1 << 3)
/* mt7615 main.c (BIP/GMAC group management keys) */
#define IEEE80211_KEY_FLAG_GENERATE_MMIE (1 << 4)
/* mt76x02_util.c (stub-local numbering; Linux puts these on BIT(4)/BIT(6)) */
#define IEEE80211_KEY_FLAG_SW_MGMT_TX   (1 << 5)
#define IEEE80211_KEY_FLAG_RX_MGMT      (1 << 6)

/* linux/ieee80211-ht.h */
#define IEEE80211_MIN_AMPDU_BUF         0x8

#define IEEE80211_BSS_ARP_ADDR_LIST_LEN 4

#define IEEE80211_CONF_MONITOR      (1 << 0)
#define IEEE80211_CONF_IDLE         (1 << 1)
#define IEEE80211_CONF_PS           (1 << 2)

/* BSS_CHANGED_* (bss_info_changed bits) */
#define BSS_CHANGED_ASSOC          (1ULL << 0)
#define BSS_CHANGED_ERP_CTS_PROT   (1ULL << 1)
#define BSS_CHANGED_ERP_PREAMBLE   (1ULL << 2)
#define BSS_CHANGED_ERP_SLOT       (1ULL << 3)
#define BSS_CHANGED_HT             (1ULL << 4)
#define BSS_CHANGED_BASIC_RATES    (1ULL << 5)
#define BSS_CHANGED_BEACON_INT     (1ULL << 6)
#define BSS_CHANGED_BSSID          (1ULL << 7)
#define BSS_CHANGED_BEACON         (1ULL << 9)
#define BSS_CHANGED_BEACON_ENABLED (1ULL << 10)
#define BSS_CHANGED_P2P_PS         (1ULL << 11)
/* mt7615 main.c (PS / ARP filter / MU groups) */
#define BSS_CHANGED_PS             (1ULL << 8)
#define BSS_CHANGED_ARP_FILTER     (1ULL << 12)
#define BSS_CHANGED_MU_GROUPS      (1ULL << 14)
#define BSS_CHANGED_SSID           (1ULL << 13)
#define BSS_CHANGED_MCAST_RATE     (1ULL << 15)
#define BSS_CHANGED_FILS_DISCOVERY (1ULL << 19)
#define BSS_CHANGED_UNSOL_BCAST_PROBE_RESP (1ULL << 20)
#define BSS_CHANGED_QOS            (1ULL << 23)
#define BSS_CHANGED_TXPOWER        (1ULL << 27)

/* ================================================================== */
/* core structs                                                        */
/* ================================================================== */
struct ieee80211_chanctx_conf {
    struct cfg80211_chan_def def;
    struct ieee80211_channel *chan;
    u8 drv_priv[] __aligned(8);
};

/* 802.11 cipher suites (u32 suite ids) */
#define WLAN_CIPHER_SUITE_USE_GROUP     0x000FAC00
#define WLAN_CIPHER_SUITE_WEP40         0x000FAC01
#define WLAN_CIPHER_SUITE_TKIP          0x000FAC02
#define WLAN_CIPHER_SUITE_CCMP          0x000FAC04
#define WLAN_CIPHER_SUITE_WEP104        0x000FAC05
#define WLAN_CIPHER_SUITE_BIP_CMAC_128  0x000FAC06
#define WLAN_CIPHER_SUITE_GCMP          0x000FAC08
#define WLAN_CIPHER_SUITE_GCMP_256      0x000FAC09
#define WLAN_CIPHER_SUITE_CCMP_256      0x000FAC0A
#define WLAN_CIPHER_SUITE_BIP_GMAC_128  0x000FAC0B
#define WLAN_CIPHER_SUITE_BIP_GMAC_256  0x000FAC0C
#define WLAN_CIPHER_SUITE_BIP_CMAC_256  0x000FAC0D
#define WLAN_CIPHER_SUITE_SMS4          0x00147201
#define WLAN_CIPHER_SUITE_AES_CMAC      WLAN_CIPHER_SUITE_BIP_CMAC_128

struct regulatory_request {
    int initiator;
    char alpha2[2];
    enum environment_cap country_ie_env;
    u32 wiphy_idx;
    enum nl80211_dfs_regions dfs_region;
    bool intersect;
};

/* mt7915 mcu.c fixed-rate control guard-interval selection */
enum nl80211_txrate_gi {
    NL80211_TXRATE_DEFAULT_GI,
    NL80211_TXRATE_FORCE_SGI,
    NL80211_TXRATE_FORCE_LGI,
};

struct cfg80211_bitrate_mask {
    struct {
        u32 legacy;
        u16 ht_mcs[10];
        u16 vht_mcs[10];
        u16 he_mcs[12];
        u16 eht_mcs[16];
        /* mt7915 mcu.c (fixed rate control) */
        enum nl80211_txrate_gi gi;
        u8 he_gi;
        u8 he_ltf;
    } control[NUM_NL80211_BANDS];
};

struct ieee80211_p2p_noa_attr {
    u8 index;
    u8 oppps_ctwindow;
};

struct ieee80211_he_bss_color {
    u8 color;
    u8 enabled;
    bool partial;
};

struct cfg80211_scan_6ghz_params {
    u8 bssid[ETH_ALEN];
    u8 channel_idx;
};

/* mt7615 main.c (ieee80211_ops testmode slots: testmode is not ported) */
#ifndef CFG80211_TESTMODE_CMD
#define CFG80211_TESTMODE_CMD(_op)
#endif
#ifndef CFG80211_TESTMODE_DUMP
#define CFG80211_TESTMODE_DUMP(_op)
#endif

/* MU-MIMO group info (mt7615 main.c bss_info_changed) */
#define WLAN_MEMBERSHIP_LEN     8
#define WLAN_USER_POSITION_LEN  16

struct ieee80211_mu_group_info {
    u8 membership[WLAN_MEMBERSHIP_LEN];
    u8 position[WLAN_USER_POSITION_LEN];
};

struct ieee80211_bss_conf {
    struct ieee80211_vif *vif;
    u8 addr[ETH_ALEN];
    u8 bssid[ETH_ALEN];
    u16 aid;
    struct ieee80211_chan_req chanreq;
    u8 link_id;
    u8 bssid_index;
    bool idle;
    bool assoc;
    bool ibss_joined;
    bool ibss_creator;
    bool enable_beacon;
    bool allow_p2p_go;
    bool csa_active;
    bool color_change_active;
    u8 dtim_period;
    u8 sync_dtim_count;
    u16 sync_device_ts;
    u64 sync_tsf;
    u32 basic_rates;
    u32 mcast_rate[NUM_NL80211_BANDS];
    bool qos;
    bool use_short_slot;
    bool use_short_preamble;
    u16 beacon_int;
    u8 htc_trig_based_pkt_ext;
    u8 frame_time_rts_th;
    struct ieee80211_he_bss_color he_bss_color;
    struct ieee80211_p2p_noa_attr p2p_noa_attr;
    u8 bssid_indicator;
    struct cfg80211_bitrate_mask beacon_tx_rate;
    u8 bss_color;
    u8 he_support;
    u8 eht_support;
    u8 twt_requester;
    u8 ema_ap;
    u16 max_idle_period;
    u8 p2p_oppps;
    u32 cqm_rssi_min;
    s32 cqm_rssi_thold;
    u32 cqm_rssi_hyst;
    /* mt7615: HT operation mode / ERP CTS protection state */
    u16 ht_operation_mode;
    bool use_cts_prot;
    struct ieee80211_mu_group_info mu_group;
    struct ieee80211_chanctx_conf *chanctx_conf;

    /* mt7915 (multi-BSSID / FILS discovery / unsolicited broadcast) */
    bool nontransmitted;
    struct {
        u16 min_interval;
        u16 max_interval;
    } fils_discovery;
    u16 unsol_bcast_probe_resp_interval;

    /* mt7996: AP-mode BSS capability bits (Linux net/mac80211.h) */
    bool ht_ldpc;
    bool vht_ldpc;
    bool he_ldpc;
    bool vht_su_beamformer;
    bool vht_su_beamformee;
    bool vht_mu_beamformer;
    bool vht_mu_beamformee;
    bool he_su_beamformer;
    bool he_su_beamformee;
    bool he_mu_beamformer;
    bool he_full_ul_mumimo;
    bool eht_su_beamformer;
    bool eht_su_beamformee;
    bool eht_mu_beamformer;
    bool eht_80mhz_full_bw_ul_mumimo;
    bool eht_disable_mcs15;
};

/* ---- vif config ---- */
struct ieee80211_vif_cfg {
    u16 aid;
    u16 eml_cap;
    bool assoc;
    bool ibss_joined;
    bool idle;
    bool ps;
    u8 arp_addr_cnt;
    struct {
        u8 addr[4]; /* IPv4 */
    } arp_addr_list[IEEE80211_BSS_ARP_ADDR_LIST_LEN];
};

/* Linux net/mac80211.h: vif offload flags (mt7996) */
enum ieee80211_offload_flags {
    IEEE80211_OFFLOAD_ENCAP_ENABLED = (1 << 0),
    IEEE80211_OFFLOAD_ENCAP_4ADDR   = (1 << 1),
    IEEE80211_OFFLOAD_ENCAP_MCAST   = (1 << 3),
};

struct ieee80211_vif {
    enum nl80211_iftype type;
    u8 addr[ETH_ALEN];
    u16 valid_links;
    u16 active_links;
    u8 deflink_id;
    bool p2p;
    bool csa_active;
    bool probe_req_reg;
    bool offchannel;
    u64 driver_flags;
    u32 offload_flags;
    struct ieee80211_vif_cfg cfg;
    struct ieee80211_bss_conf bss_conf;
    struct ieee80211_bss_conf *link_conf[IEEE80211_MLD_MAX_NUM_LINKS];
    struct ieee80211_bss_conf *deflink_conf;
    struct ieee80211_chanctx_conf *chanctx_conf;
    struct ieee80211_txq *txq[IEEE80211_NUM_TIDS];
    u8 cab_queue;
    u8 hw_queue[IEEE80211_NUM_ACS];
    u8 drv_priv[] __aligned(8);
};

static inline bool ieee80211_vif_is_mld(struct ieee80211_vif *vif)
{
    return vif->valid_links != 0;
}

/* ---- stations ---- */
struct ieee80211_link_sta {
    u8 addr[ETH_ALEN];
    u8 link_id;
    u8 mfp;
    bool tdls;
    u32 supp_rates[NUM_NL80211_BANDS];
    struct ieee80211_sta_ht_cap ht_cap;
    struct ieee80211_sta_vht_cap vht_cap;
    struct ieee80211_sta_he_cap he_cap;
    struct ieee80211_sta_eht_cap eht_cap;
    struct ieee80211_he_6ghz_capa he_6ghz_capa;
    enum ieee80211_sta_rx_bandwidth bandwidth;
    enum ieee80211_smps_mode smps_mode;
    u8 rx_nss;
    u8 tx_nss;
    u8 max_amsdu_subframes;
    struct {
        u16 max_amsdu_len;
        u16 max_amsdu_count;
    } agg;
    s8 ack_signal;
    int avg_ack_signal;
    unsigned long last_txs;
    struct mt76_wcid *wcid;
    struct ieee80211_sta *sta;
};

/* ---- per-STA rate table (rate control) ---- */
struct ieee80211_sta_rates {
    struct {
        s8 idx;
        u8 count;
        u8 flags;
    } rate[12];
};

struct ieee80211_sta {
    struct ieee80211_link_sta deflink;
    struct ieee80211_link_sta *link[IEEE80211_MLD_MAX_NUM_LINKS];
    u16 valid_links;
    u8 deflink_id;
    u8 addr[ETH_ALEN];
    u16 aid;
    u16 max_amsdu_subframes;
    u16 max_tid_amsdu_len[IEEE80211_NUM_TIDS];
    u16 rx_nss;
    enum ieee80211_sta_rx_bandwidth bandwidth;
    enum ieee80211_smps_mode smps_mode;
    bool tdls;
    bool tdls_initiator;
    bool mfp;
    bool wme;
    bool mlo;               /* Linux: STA is an MLO station (mt7996) */
    u8 uapsd_queues;
    u8 max_sp;
    u8 supp_rates[NUM_NL80211_BANDS];
    struct ieee80211_txq *txq[IEEE80211_NUM_TIDS];
    struct ieee80211_sta_rates *rates;
    u32 txpwr;
    u8 drv_priv[] __aligned(8);
};

/* ---- TX queues ---- */
struct ieee80211_txq {
    struct ieee80211_vif *vif;
    struct ieee80211_sta *sta;
    u8 tid;
    u8 ac;
    u8 drv_priv[] __aligned(8);
};

static inline struct ieee80211_txq *mtxq_to_txq_stub(void *p)
{
    return (struct ieee80211_txq *)p;
}

/* ---- keys ---- */
struct ieee80211_key_conf {
    u32 cipher;
    u8 icv_len;
    u8 iv_len;
    u8 hw_key_idx;
    u8 flags;
    s8 keyidx;
    u8 key_id;
    u8 keylen;
    u8 key_len;
    u8 link_id;             /* Linux: MLO link id (mt7996) */
    s8 key[32];
    atomic64_t tx_pn;   /* Linux: per-key TX PN counter (mt7603 write_txwi) */
};

/* ---- rates ---- */
struct ieee80211_tx_rate {
    s8 idx;
    u8 count;
    u16 flags;
};

/* Linux net/mac80211.h VHT rate helpers (mt7615 mac.c) */
static inline void ieee80211_rate_set_vht(struct ieee80211_tx_rate *rate,
                                          u8 mcs, u8 nss)
{
    rate->idx = ((nss - 1) << 4) | (mcs & 0xF);
}

static inline u8
ieee80211_rate_get_vht_mcs(const struct ieee80211_tx_rate *rate)
{
    return rate->idx & 0xF;
}

static inline u8
ieee80211_rate_get_vht_nss(const struct ieee80211_tx_rate *rate)
{
    return (rate->idx >> 4) + 1;
}

#define IEEE80211_TX_RC_USE_RTS_CTS     (1 << 0)
#define IEEE80211_TX_RC_USE_CTS_PROTECT (1 << 1)
#define IEEE80211_TX_RC_USE_SHORT_PREAMBLE (1 << 2)
#define IEEE80211_TX_RC_MCS              (1 << 3)
#define IEEE80211_TX_RC_VHT_MCS          (1 << 4)
#define IEEE80211_TX_RC_SHORT_GI         (1 << 5)
#define IEEE80211_TX_RC_DUP_DATA         (1 << 6)
#define IEEE80211_TX_RC_40_MHZ_WIDTH     (1 << 7)
#define IEEE80211_TX_RC_80_MHZ_WIDTH     (1 << 8)
#define IEEE80211_TX_RC_160_MHZ_WIDTH    (1 << 9)
#define IEEE80211_TX_RC_GREEN_FIELD      (1 << 10)
#define IEEE80211_TX_RC_MCS              (1 << 3)

#define IEEE80211_TX_RATE_TABLE_SIZE 4

struct ieee80211_rate_status {
    struct rate_info rate_idx;
    u8 attempt_count;
    u8 success_count;
};

/* ---- TX info (skb->cb) ---- */
struct ieee80211_tx_info {
    u32 flags;
    u8 band;
    u8 hw_queue;
    u32 tx_time_est;
    union {
        struct {
            union {
                u32 flags;
                void *vif_dummy;
            };
            struct ieee80211_vif *vif;
            struct ieee80211_key_conf *hw_key;
            struct ieee80211_tx_rate rates[IEEE80211_TX_RATE_TABLE_SIZE];
            u8 rts_cts_rates_idx;
            u8 use_rts;
            u8 use_cts_prot;
            u8 short_preamble;
            u8 skip_table;
            struct ieee80211_vif *vif_mld;
        } control;
        struct {
            struct ieee80211_tx_rate rates[IEEE80211_TX_RATE_TABLE_SIZE];
            int ack_signal;
            u8 ampdu_ack_len;
            u8 ampdu_len;
            u8 antenna;
            u8 tx_flags;
            unsigned long status_driver_data[15];
        } status;
        struct {
            struct ieee80211_tx_rate driver_rates[IEEE80211_TX_RATE_TABLE_SIZE];
            u8 pad[4];
        } rates;
    };
};

#define IEEE80211_SKB_CB(skb) ((struct ieee80211_tx_info *)(skb)->cb)

struct ieee80211_tx_status {
    struct ieee80211_sta *sta;
    struct sk_buff *skb;
    struct ieee80211_tx_info *info;
    struct ieee80211_rate_status *rates;
    int n_rates;
    s8 ack_signal;
    u8 ack_signal_valid;
    struct list_head *free_list;
};

struct ieee80211_rx_status {
    u64 mactime;
    u64 device_timestamp;
    u32 ampdu_reference;
    u32 flag;
    u16 freq;
    u8 band;
    u8 encoding;
    u8 bw;
    u8 enc_flags;
    u8 rate_idx;
    u8 nss;
    s8 signal;
    u8 chains;
    s8 chain_signal[IEEE80211_MAX_CHAINS];
    u8 antenna;
    u8 zero_length_psdu_type;
    u8 link_valid;
    u8 link_id;
    u8 rx_flags;
};

/* ---- AMPDU / TXQ params / scan ---- */
enum ieee80211_ampdu_mlme_action {
    IEEE80211_AMPDU_RX_START,
    IEEE80211_AMPDU_RX_STOP,
    IEEE80211_AMPDU_TX_START,
    IEEE80211_AMPDU_TX_STOP_CONT,
    IEEE80211_AMPDU_TX_STOP_FLUSH,
    IEEE80211_AMPDU_TX_STOP_FLUSH_CONT,
    IEEE80211_AMPDU_TX_OPERATIONAL,
};

#define IEEE80211_AMPDU_TX_START_IMMEDIATE 1

struct ieee80211_ampdu_params {
    struct ieee80211_sta *sta;
    u16 tid;
    u16 ssn;
    u16 buf_size;
    bool amsdu;
    enum ieee80211_ampdu_mlme_action action;
};

struct ieee80211_he_mu_edca_param_ac_rec {
    u8 ecw_min_max;
    u8 aifsn;
    u8 mu_edca_timer;
} __packed;

struct ieee80211_tx_queue_params {
    u16 txop;
    u16 cw_min;
    u16 cw_max;
    u8 aifs;
    bool acm;
    bool uapsd;
    bool mu_edca;
    struct ieee80211_he_mu_edca_param_ac_rec mu_edca_param_rec;
};

struct ieee80211_scan_ies {
    const u8 *ies[NUM_NL80211_BANDS];
    size_t len[NUM_NL80211_BANDS];
    const u8 *common_ies;
    size_t common_ie_len;
};

struct ieee80211_scan_request {
    struct cfg80211_scan_request req;
    struct ieee80211_channel *channels[64];
    struct ieee80211_scan_ies ies;
};

struct ieee80211_mutable_offsets {
    u16 tim_offset;
    u16 tim_length;
    u16 csa_counter_offs[2];
    u16 cntdwn_counter_offs[2];
    u16 csa_counter_offs_presp;
    /* mt7915 (multi-BSSID profile offset) */
    int mbssid_off;
};

struct ieee80211_low_level_stats {
    u32 dot11ACKFailureCount;
    u32 dot11RTSFailureCount;
    u32 dot11RTSSuccessCount;
    u32 dot11FCSErrorCount;
};

struct ieee80211_iface_limit {
    u16 max;
    u16 types;
};

struct ieee80211_iface_combination {
    const struct ieee80211_iface_limit *limits;
    u32 num_different_channels;
    u16 max_interfaces;
    u8 n_limits;
    bool beacon_int_infra_match;
    u8 radar_detect_widths;
    u8 radar_detect_regions;
    u32 beacon_int_min_gcd;
};

struct ieee80211_vif_chanctx_switch {
    struct ieee80211_vif *vif;
    struct ieee80211_bss_conf *link_conf;
    struct ieee80211_chanctx_conf *old_ctx;
    struct ieee80211_chanctx_conf *new_ctx;
};

/* ---- ieee80211_conf / hw / wiphy ---- */
typedef u64 netdev_features_t;
struct ieee80211_conf {
    u32 flags;
    int power_level;
    int dynamic_ps_timeout;
    u16 listen_interval;
    u8 ps_dtim_period;
    struct ieee80211_channel *smps;
};

enum ieee80211_hw_flags {
    IEEE80211_HW_HAS_RATE_CONTROL,
    IEEE80211_HW_RX_INCLUDES_FCS,
    IEEE80211_HW_AMPDU_AGGREGATION,
    IEEE80211_HW_REPORTS_TX_ACK_STATUS,
    IEEE80211_HW_MFP_CAPABLE,
    IEEE80211_HW_SIGNAL_DBM,
    IEEE80211_HW_SUPPORTS_PS,
    IEEE80211_HW_SUPPORTS_DYNAMIC_PS,
    IEEE80211_HW_SUPPORTS_VHT_EXT_NSS_BW,
    IEEE80211_HW_CONNECTION_MONITOR,
    IEEE80211_HW_SUPPORTS_MULTI_BSSID,
    IEEE80211_HW_SUPPORTS_ONLY_HE_MULTI_BSSID,
    IEEE80211_HW_CHANCTX_STA_CSA,
    IEEE80211_HW_SINGLE_SCAN_ON_ALL_BANDS,
    IEEE80211_HW_NO_VIRTUAL_MONITOR,
    IEEE80211_HW_WANT_MONITOR_VIF,
    IEEE80211_HW_SUPPORTS_TX_ENCAP_OFFLOAD,
    IEEE80211_HW_SUPPORTS_RX_DECAP_OFFLOAD,
    IEEE80211_HW_TDLS_SUPPORT,
    IEEE80211_HW_QUEUE_CONTROL,
    IEEE80211_HW_SW_CRYPTO_CONTROL,
    IEEE80211_HW_SUPPORTS_PER_STA_GTK,
    /* mt7915 init.c */
    IEEE80211_HW_SUPPORTS_TX_FRAG,
    IEEE80211_HW_AP_LINK_PS,
    IEEE80211_HW_INACTIVITY_TIMER,
    IEEE80211_HW_TX_STATUS_NO_AMPDU_LEN,
    IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING,
    IEEE80211_HW_NEEDS_UNIQUE_STA_ADDR,
    /* mt76x02_util.c */
    IEEE80211_HW_SUPPORTS_HT_CCK_RATES,
};

#define NETIF_F_RXCSUM (1 << 0)
/* mt7915 init.c (WED hw tc offload flag; WED is disabled in the port) */
#define NETIF_F_HW_TC (1 << 1)

struct ieee80211_hw {
    struct ieee80211_conf conf;
    u32 flags;
    u64 driver_flags;
    unsigned int queues;
    unsigned int max_rates;
    unsigned int max_report_rates;
    unsigned int max_rate_tries;
    unsigned int extra_tx_headroom;
    unsigned int max_tx_fragments;
    unsigned int max_tx_aggregation_subframes;
    unsigned int max_rx_aggregation_subframes;
    u16 max_listen_interval;
    s8 max_signal;
    netdev_features_t netdev_features;
    void *priv;
    struct wiphy *wiphy;
    /* NB: Linux sizes these u8 -> u16/u32; mt76x02_vif is 816 bytes and
     * must not truncate. */
    unsigned int vif_data_size;
    unsigned int sta_data_size;
    u8 chanctx_data_size;
    u8 txq_data_size;
    u8 uapsd_max_sp_len;
    u8 uapsd_queues;
    struct ieee80211_radiotap_timestamp radiotap_timestamp;
};

#define ieee80211_hw_set(hw, flg) ((hw)->flags |= (1u << IEEE80211_HW_##flg))
#define ieee80211_hw_clear(hw, flg) ((hw)->flags &= ~(1u << IEEE80211_HW_##flg))
#define ieee80211_hw_check(hw, flg) (!!((hw)->flags & (1u << IEEE80211_HW_##flg)))


struct wiphy_iftype_ext_capab {
    enum nl80211_iftype iftype;
    const u8 *extended_capabilities;
    const u8 *extended_capabilities_mask;
    u8 extended_capabilities_len;
    u16 eml_capabilities;
    u16 mld_capa_and_ops;
    u16 ext_mld_capa_and_ops;
    bool cip_supported;
    u8 cip_capabilities;
};

struct wiphy {
    u64 flags;
    u64 regulatory_flags;
    u64 features;
    u64 interface_modes;
    u64 software_iftypes;
    u64 nan_supported_bands;
    u32 max_scan_ssids;
    u32 max_scan_ie_len;
    u32 max_match_sets;
    u32 max_sched_scan_ssids;
    u32 max_sched_scan_reqs;
    u32 max_sched_scan_plan_interval;
    u32 max_sched_scan_plan_iterations;
    u32 max_sched_scan_ie_len;
    u32 n_addresses;
    u32 max_num_csa_counters;
    u32 bss_select_support;
    u32 mbssid_max_interfaces;
    u32 max_nan_de_entries;
    struct {
        u32 flags;
        u32 op_mode;
        u32 n_antennas;
        u32 max_channel_switch_time;
        u32 dev_capabilities;
        struct {
            struct ieee80211_sta_ht_cap ht;
            struct ieee80211_sta_vht_cap vht;
            struct ieee80211_sta_he_cap he;
        } phy;
    } nan_capa;
    const struct ieee80211_iface_combination *iface_combinations;
    int n_iface_combinations;
    const struct wiphy_iftype_ext_capab *iftype_ext_capab;
    u8 num_iftype_ext_capab;
    struct ieee80211_supported_band *bands[NUM_NL80211_BANDS];
    struct device dev;
    u32 available_antennas_rx;
    u32 available_antennas_tx;
    u32 max_ap_assoc_sta;
    int (*reg_notifier)(struct wiphy *wiphy, struct regulatory_request *req);
    u8 perm_addr[ETH_ALEN];
    struct mac_address *addresses;
    unsigned long ext_features[BITS_TO_LONGS(NUM_NL80211_EXT_FEATURES)];
    char fw_version[64];
    void *priv;
    u32 txq_memory_limit;
    u32 n_radio;
    struct wiphy_radio *radio;
    struct ieee80211_hw *hw;
    /* mt7915 mcu.c (protection config reads the rts threshold) */
    u32 rts_threshold;
};

static inline struct ieee80211_hw *wiphy_to_ieee80211_hw(struct wiphy *wiphy)
{
    return wiphy->hw;
}

static inline const char *wiphy_name(const struct wiphy *wiphy)
{
    return dev_name(&((struct wiphy *)wiphy)->dev);
}

static inline void wiphy_ext_feature_set(struct wiphy *wiphy,
                                         enum nl80211_ext_feature_index idx)
{
    set_bit(idx, wiphy->ext_features);
}

static inline bool wiphy_ext_feature_isset(struct wiphy *wiphy,
                                           enum nl80211_ext_feature_index idx)
{
    return test_bit(idx, wiphy->ext_features);
}

static inline void wiphy_rfkill_stop_polling(struct wiphy *wiphy)
{
    (void)wiphy;
}

#define wiphy_info(wiphy, fmt, ...) dev_info(&wiphy->dev, fmt, ##__VA_ARGS__)

static inline struct ieee80211_channel *
ieee80211_get_channel(struct wiphy *wiphy, int freq)
{
    (void)wiphy; (void)freq;
    return NULL;
}

int ieee80211_channel_to_frequency(int chan, int band);
int ieee80211_frequency_to_channel(int freq);

const struct ieee80211_sband_iftype_data *
ieee80211_get_he_iftype_cap(const struct ieee80211_supported_band *sband,
                            enum nl80211_iftype type);
const struct ieee80211_sband_iftype_data *
ieee80211_get_eht_iftype_cap(const struct ieee80211_supported_band *sband,
                             enum nl80211_iftype type);

static inline struct ieee80211_link_sta *
link_sta_dereference_protected(struct ieee80211_sta *sta, unsigned int link_id)
{
    return sta->link[link_id];
}

static inline struct ieee80211_bss_conf *
link_conf_dereference_protected(struct ieee80211_vif *vif, unsigned int link_id)
{
    return vif->link_conf[link_id];
}

/* ---- mac80211 callbacks (ieee80211_ops) ---- */
/* Linux net/mac80211.h: reconfig completion type (mt76x02_mmio.c) */
enum ieee80211_reconfig_type {
    IEEE80211_RECONFIG_TYPE_RESTART,
    IEEE80211_RECONFIG_TYPE_SUSPEND,
};

struct ieee80211_ops {
    void (*tx)(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
               struct sk_buff *skb);
    void (*wake_tx_queue)(struct ieee80211_hw *hw, struct ieee80211_txq *txq);
    int (*start)(struct ieee80211_hw *hw);
    void (*stop)(struct ieee80211_hw *hw, bool suspend);
    int (*add_interface)(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
    int (*change_interface)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                            enum nl80211_iftype new_type, bool new_p2p);
    void (*remove_interface)(struct ieee80211_hw *hw,
                             struct ieee80211_vif *vif);
    int (*config)(struct ieee80211_hw *hw, int radio_idx, u32 changed);
    void (*reconfig_complete)(struct ieee80211_hw *hw,
                              enum ieee80211_reconfig_type reconfig_type);
    void (*bss_info_changed)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                             struct ieee80211_bss_conf *info, u64 changed);
    void (*configure_filter)(struct ieee80211_hw *hw, unsigned int changed_flags,
                             unsigned int *total_flags, u64 multicast);
    int (*conf_tx)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                   unsigned int link_id, u16 ac,
                   const struct ieee80211_tx_queue_params *params);
    int (*sta_state)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                     struct ieee80211_sta *sta,
                     enum ieee80211_sta_state old_state,
                     enum ieee80211_sta_state new_state);
    void (*sta_pre_rcu_remove)(struct ieee80211_hw *hw,
                               struct ieee80211_vif *vif,
                               struct ieee80211_sta *sta);
    void (*sta_rcu_update)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                           struct ieee80211_sta *sta);
    int (*sta_add)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                   struct ieee80211_sta *sta);
    int (*sta_remove)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                      struct ieee80211_sta *sta);
    void (*sta_notify)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                       enum sta_notify_cmd cmd, struct ieee80211_sta *sta);
    int (*set_key)(struct ieee80211_hw *hw, enum set_key_cmd cmd,
                   struct ieee80211_vif *vif, struct ieee80211_sta *sta,
                   struct ieee80211_key_conf *key);
    void (*update_tkip_key)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                            struct ieee80211_key_conf *key,
                            struct ieee80211_sta *sta, u32 iv32, u16 *phase1key);
    void (*set_rekey_data)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                           struct cfg80211_gtk_rekey_data *data);
    void (*set_default_unicast_key)(struct ieee80211_hw *hw,
                                    struct ieee80211_vif *vif, int key_idx);
    int (*ampdu_action)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                        struct ieee80211_ampdu_params *params);
    int (*get_antenna)(struct ieee80211_hw *hw, int radio_idx,
                       u32 *tx_ant, u32 *rx_ant);
    int (*set_antenna)(struct ieee80211_hw *hw, u32 tx_ant, u32 rx_ant);
    void (*sta_rate_tbl_update)(struct ieee80211_hw *hw,
                                struct ieee80211_vif *vif,
                                struct ieee80211_sta *sta);
    int (*get_survey)(struct ieee80211_hw *hw, int idx,
                      struct survey_info *survey);
    void (*rfkill_poll)(struct ieee80211_hw *hw);
    void (*sw_scan_start)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                          const u8 *mac_addr);
    void (*sw_scan_complete)(struct ieee80211_hw *hw,
                             struct ieee80211_vif *vif);
    int (*hw_scan)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                   struct ieee80211_scan_request *req);
    void (*cancel_hw_scan)(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
    int (*sched_scan_start)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                            struct cfg80211_sched_scan_request *req,
                            struct ieee80211_sched_scan_ies *ies);
    void (*sched_scan_stop)(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
    void (*remain_on_channel)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                              struct ieee80211_channel *chan, int duration,
                              enum ieee80211_roc_type type);
    int (*remain_on_channel_duration)(struct ieee80211_hw *hw,
                                      struct ieee80211_vif *vif,
                                      struct ieee80211_channel *chan,
                                      int duration, enum ieee80211_roc_type type);
    int (*cancel_remain_on_channel)(struct ieee80211_hw *hw,
                                    struct ieee80211_vif *vif);
    int (*set_tim)(struct ieee80211_hw *hw, struct ieee80211_sta *sta, bool set);
    int (*set_rts_threshold)(struct ieee80211_hw *hw, int radio_idx, u32 value);
    void (*set_coverage_class)(struct ieee80211_hw *hw, int radio_idx,
                               s16 coverage_class);
    void (*sta_set_buffered)(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
                             u8 tid, bool buffered);
    void (*release_buffered_frames)(struct ieee80211_hw *hw,
                                    struct ieee80211_sta *sta, u16 tids,
                                    int nframes,
                                    enum ieee80211_frame_release_type reason,
                                    bool more_data);
    int (*get_txpower)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                       unsigned int link_id, int *dbm);
    int (*set_wakeup)(struct ieee80211_hw *hw, bool enabled);
    int (*set_sar_specs)(struct ieee80211_hw *hw,
                         const struct cfg80211_sar_specs *sar);
    int (*set_qos_map)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                       struct cfg80211_qos_map *qos_map);
    void (*channel_switch_beacon)(struct ieee80211_hw *hw,
                                  struct ieee80211_vif *vif,
                                  struct cfg80211_csa_settings *ch_switch);
    int (*pre_channel_switch)(struct ieee80211_hw *hw,
                              struct ieee80211_vif *vif,
                              struct ieee80211_channel_switch *ch_switch);
    int (*post_channel_switch)(struct ieee80211_hw *hw,
                               struct ieee80211_vif *vif);
    int (*channel_switch_rx_beacon)(struct ieee80211_hw *hw,
                                    struct ieee80211_vif *vif,
                                    struct ieee80211_channel_switch *ch_switch);
    void (*channel_switch_beacon2)(struct ieee80211_hw *hw,
                                   struct ieee80211_vif *vif);
    void (*abort_channel_switch)(struct ieee80211_hw *hw,
                                 struct ieee80211_vif *vif);
    int (*add_chanctx)(struct ieee80211_hw *hw,
                       struct ieee80211_chanctx_conf *conf);
    void (*remove_chanctx)(struct ieee80211_hw *hw,
                           struct ieee80211_chanctx_conf *conf);
    void (*change_chanctx)(struct ieee80211_hw *hw,
                           struct ieee80211_chanctx_conf *conf, u32 changed);
    int (*assign_vif_chanctx)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                              struct ieee80211_bss_conf *link_conf,
                              struct ieee80211_chanctx_conf *conf);
    void (*unassign_vif_chanctx)(struct ieee80211_hw *hw,
                                 struct ieee80211_vif *vif,
                                 struct ieee80211_bss_conf *link_conf,
                                 struct ieee80211_chanctx_conf *conf);
    int (*switch_vif_chanctx)(struct ieee80211_hw *hw,
                              struct ieee80211_vif_chanctx_switch *vifs,
                              int n_vifs,
                              enum ieee80211_chanctx_switch_mode mode);
    void (*link_info_changed)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                              struct ieee80211_bss_conf *link_conf, u64 changed);
    void (*mgd_prepare_tx)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                           struct ieee80211_prep_tx_info *info);
    void (*mgd_complete_tx)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                            struct ieee80211_prep_tx_info *info);
    void (*mgd_protect_tdls_discover)(struct ieee80211_hw *hw,
                                      struct ieee80211_vif *vif);
    int (*start_ap)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                    struct ieee80211_bss_conf *link_conf);
    void (*stop_ap)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                    struct ieee80211_bss_conf *link_conf);
    int (*get_et_strings)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                          u32 sset, u8 *data);
    void (*get_et_stats)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                         struct ethtool_stats *stats, u64 *data);
    int (*get_et_sset_count)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                             int sset);
    void (*get_tsf)(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
    void (*set_tsf)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                    u64 timestamp);
    int (*tx_last_beacon)(struct ieee80211_hw *hw);
    int (*testmode_cmd)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                        void *data, int len);
    int (*testmode_dump)(struct ieee80211_hw *hw, struct sk_buff *skb,
                         struct netlink_callback *cb, void *data, int len);
    void (*flush)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                  u32 queues, bool drop);
    void (*sta_set_decap_offload)(struct ieee80211_hw *hw,
                                  struct ieee80211_vif *vif,
                                  struct ieee80211_sta *sta, bool enabled);
    void (*event_callback)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                           const struct ieee80211_event *event);
    void (*allow_buffered_frames)(struct ieee80211_hw *hw,
                                  struct ieee80211_sta *sta, u16 tids,
                                  int num_frames,
                                  enum ieee80211_frame_release_type reason,
                                  bool more_data);
    void (*release_buffered_frames2)(struct ieee80211_hw *hw,
                                     struct ieee80211_sta *sta, u16 tids,
                                     int num_frames,
                                     enum ieee80211_frame_release_type reason,
                                     bool more_data);
    void (*sta_statistics)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                           struct ieee80211_sta *sta, struct station_info *sinfo);
    void (*net_setup_tc)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                         struct net_device *dev, enum tc_setup_type type,
                         void *type_data);
    int (*net_fill_forward_path)(struct ieee80211_hw *hw,
                                 struct ieee80211_vif *vif,
                                 struct ieee80211_sta *sta,
                                 struct net_device_path_ctx *ctx,
                                 struct net_device_path *path);
    int (*change_vif_links)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                            u16 old_valid_links, u16 new_valid_links,
                            struct ieee80211_bss_conf *old_links[IEEE80211_MLD_MAX_NUM_LINKS],
                            struct ieee80211_bss_conf *new_links[IEEE80211_MLD_MAX_NUM_LINKS]);
    int (*change_sta_links)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                            struct ieee80211_sta *sta, u16 old_valid_links,
                            u16 new_valid_links,
                            struct ieee80211_link_sta *old_links[IEEE80211_MLD_MAX_NUM_LINKS],
                            struct ieee80211_link_sta *new_links[IEEE80211_MLD_MAX_NUM_LINKS]);
    void (*set_hw_timestamp)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                             struct ieee80211_sta *sta,
                             struct cfg80211_wowlan_nd_match *match);
    int (*start_nan)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                     struct cfg80211_nan_conf *conf);
    void (*stop_nan)(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
    void (*nan_change_conf)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                            struct cfg80211_nan_conf *conf, u64 changes);
    int (*add_nan_func)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                        struct ieee80211_sta *sta,
                        struct cfg80211_nan_func *func);
    int (*del_nan_func)(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                        u64 inst_id);
};

/* opaque types referenced only through pointers */
struct cfg80211_gtk_rekey_data;
struct cfg80211_sched_scan_request;
struct ieee80211_sched_scan_ies;
struct cfg80211_qos_map;
struct cfg80211_csa_settings;
struct ieee80211_channel_switch;
struct ieee80211_prep_tx_info;
struct ieee80211_event;
struct ethtool_stats;
struct net_device_path_ctx;
struct net_device_path;
struct cfg80211_wowlan_nd_match;
struct cfg80211_nan_conf;
struct cfg80211_nan_func;
struct ieee80211_tx_control {
    struct ieee80211_vif *vif;
    struct ieee80211_sta *sta;
    u64 flags;
    int link_id;
};

struct ethtool_stats {
    u32 n_stats;
};

/* ================================================================== */
/* mac80211 API                                                        */
/* ================================================================== */
struct ieee80211_hw *ieee80211_alloc_hw(size_t priv_data_len,
                                        const struct ieee80211_ops *ops);
struct ieee80211_hw *ieee80211_alloc_hw_nm(size_t priv_data_len,
                                            const struct ieee80211_ops *ops,
                                            const char *requested_name);
void ieee80211_free_hw(struct ieee80211_hw *hw);
int ieee80211_register_hw(struct ieee80211_hw *hw);
void ieee80211_unregister_hw(struct ieee80211_hw *hw);
void ieee80211_restart_hw(struct ieee80211_hw *hw);
void ieee80211_restart_hw_timer(struct ieee80211_hw *hw, unsigned int delay);

void ieee80211_rx(struct ieee80211_hw *hw, struct sk_buff *skb);
void ieee80211_rx_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb);
void ieee80211_rx_napi(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
                       struct sk_buff *skb, struct napi_struct *napi);
void ieee80211_list_rx(struct ieee80211_hw *hw, struct sk_buff *skb);
void ieee80211_tx_status(struct ieee80211_hw *hw, struct sk_buff *skb);
void ieee80211_tx_status_ext(struct ieee80211_hw *hw,
                             struct ieee80211_tx_status *status);
void ieee80211_free_txskb(struct ieee80211_hw *hw, struct sk_buff *skb);
void ieee80211_tx_rate_update(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                              struct ieee80211_sta *sta);

void ieee80211_wake_queues(struct ieee80211_hw *hw);
void ieee80211_stop_queues(struct ieee80211_hw *hw);
void ieee80111_stop_queue_stub(void);
void ieee80211_stop_queue(struct ieee80211_hw *hw, int queue);
void ieee80211_wake_queue(struct ieee80211_hw *hw, int queue);

void ieee80211_iterate_active_interfaces(struct ieee80211_hw *hw,
                                         u32 iter_flags,
                                         void (*iter)(void *data, u8 *mac,
                                                      struct ieee80211_vif *vif),
                                         void *data);
void ieee80211_iterate_active_interfaces_atomic(struct ieee80211_hw *hw,
                                                u32 iter_flags,
                                                void (*iter)(void *data, u8 *mac,
                                                             struct ieee80211_vif *vif),
                                                void *data);
void ieee80211_iterate_stations_atomic(struct ieee80211_hw *hw,
                                       void (*iter)(void *data,
                                                    struct ieee80211_sta *sta),
                                       void *data);
void ieee80211_iter_keys(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                         void (*iter)(struct ieee80211_hw *hw,
                                      struct ieee80211_vif *vif,
                                      struct ieee80211_sta *sta,
                                      struct ieee80211_key_conf *key,
                                      void *data),
                         void *data);
void ieee80211_iter_keys_rcu(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                             void (*iter)(struct ieee80211_hw *hw,
                                          struct ieee80211_vif *vif,
                                          struct ieee80211_sta *sta,
                                          struct ieee80211_key_conf *key,
                                          void *data),
                             void *data);

/* Linux net/mac80211.h: TX airtime estimate (mt76x02_mac.c) */
u32 ieee80211_calc_tx_airtime(struct ieee80211_hw *hw,
                              struct ieee80211_tx_info *info, int len);

void ieee80211_get_tx_rates(struct ieee80211_vif *vif, struct ieee80211_sta *sta,
                            struct sk_buff *skb,
                            struct ieee80211_tx_rate *dest, int count);
void ieee80211_get_rx_rates(struct ieee80211_vif *vif, struct ieee80211_sta *sta,
                            struct sk_buff *skb,
                            struct ieee80211_rx_status *status,
                            struct rate_info *dest, int count);

struct sk_buff *ieee80211_tx_dequeue(struct ieee80211_hw *hw,
                                     struct ieee80211_txq *txq);
void ieee80211_schedule_txq(struct ieee80211_hw *hw, struct ieee80211_txq *txq);
void ieee80211_return_txq(struct ieee80211_hw *hw, struct ieee80211_txq *txq,
                          bool force);
struct ieee80211_txq *ieee80211_next_txq(struct ieee80211_hw *hw, int ac);
void ieee80211_txq_schedule_start(struct ieee80211_hw *hw, u8 ac);
void ieee80211_txq_schedule_end(struct ieee80211_hw *hw, u8 ac);
u32 ieee80211_txq_aql_pending(struct ieee80211_hw *hw, struct ieee80211_txq *txq);
void ieee80211_txq_set_active(struct ieee80211_hw *hw, struct ieee80211_txq *txq,
                              bool active);

void ieee80211_queue_work(struct ieee80211_hw *hw, struct work_struct *work);
void ieee80211_queue_delayed_work(struct ieee80211_hw *hw,
                                  struct delayed_work *dwork,
                                  unsigned long delay);

void ieee80211_scan_completed(struct ieee80211_hw *hw,
                              struct cfg80211_scan_info *info);
void ieee80211_ready_on_channel(struct ieee80211_hw *hw);
void ieee80211_ready_on_channel_expired(struct ieee80211_hw *hw);
/* Linux API names used by mt7615 main.c (no-op stubs live in the family
 * stubs file, mtkwm/mt7615/mt7615_stubs.c). */
void ieee80211_remain_on_channel_expired(struct ieee80211_hw *hw);
void ieee80211_sched_scan_results(struct ieee80211_hw *hw);
void ieee80211_radar_detected(struct ieee80211_hw *hw,
                              struct ieee80211_chanctx_conf *chanctx_conf);
void ieee80211_sta_eosp(struct ieee80211_sta *sta);
void ieee80211_sta_register_airtime(struct ieee80211_sta *sta, u8 tid,
                                    u32 tx_airtime, u32 rx_airtime);
void ieee80211_send_bar(struct ieee80211_vif *vif, u8 *ra, u16 tid, u16 ssn);
int ieee80211_start_tx_ba_session(struct ieee80211_sta *sta, u16 tid,
                                  u16 timeout);
int ieee80211_stop_tx_ba_session(struct ieee80211_sta *sta, u16 tid);
void ieee80211_stop_tx_ba_cb_irqsafe(struct ieee80211_vif *vif, const u8 *ra,
                                     u16 tid);
void ieee80211_start_tx_ba_cb_irqsafe(struct ieee80211_vif *vif, const u8 *ra,
                                      u16 tid);
void ieee80211_refresh_tx_agg_session_timer(struct ieee80211_sta *sta, u16 tid);
void ieee80211_beacon_loss(struct ieee80211_vif *vif);
void ieee80211_connection_loss(struct ieee80211_vif *vif);
void ieee80211_disconnect(struct ieee80211_vif *vif, bool reconnect);
void ieee80211_cqm_rssi_notify(struct ieee80211_vif *vif,
                               enum nl80211_cqm_rssi_threshold_event rssi_event,
                               s32 rssi_level, gfp_t gfp);
struct ieee80211_sta *ieee80211_find_sta(struct ieee80211_vif *vif,
                                         const u8 *addr);
struct ieee80211_sta *ieee80211_find_sta_by_ifaddr(struct ieee80211_hw *hw,
                                                   const u8 *addr,
                                                   const u8 *ifaddr);
struct ieee80211_sta *ieee80211_find_sta_by_idx(struct ieee80211_hw *hw,
                                                int idx);
void ieee80211_sta_ps_transition(struct ieee80211_sta *sta, bool sleeping);
void ieee80211_sta_ps_transition_wk(struct ieee80211_vif *vif, bool sleeping);
void ieee80211_sta_set_buffered(struct ieee80211_sta *sta, u8 tid, bool buffered);
void ieee80211_get_key_rx_seq(struct ieee80211_key_conf *key, int tid,
                              struct ieee80211_key_seq *seq);
void ieee80211_gtk_rekey_notify(struct ieee80211_vif *vif, const u8 *bssid,
                                const u8 *replay_ctr, gfp_t gfp);
void ieee80211_chswitch_done(struct ieee80211_vif *vif, bool success);
void ieee80211_beacon_get_tim(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                              struct ieee80211_mutable_offsets *offs,
                              struct sk_buff *bcn_skb, u64 *cookie, int link_id);
struct sk_buff *ieee80211_beacon_get_template(struct ieee80211_hw *hw,
                                              struct ieee80211_vif *vif,
                                              struct ieee80211_mutable_offsets *offs,
                                              int link_id);
struct sk_buff *ieee80211_beacon_get(struct ieee80211_hw *hw,
                                     struct ieee80211_vif *vif,
                                     unsigned int link_id);
struct sk_buff *ieee80211_get_buffered_bc(struct ieee80211_hw *hw,
                                          struct ieee80211_vif *vif);
void ieee80211_send_eosp_nullfunc(struct ieee80211_sta *sta, int tid);
void ieee80211_csa_finish(struct ieee80211_vif *vif, unsigned int link_id);
bool ieee80211_beacon_cntdwn_is_complete(struct ieee80211_vif *vif,
                                         unsigned int link_id);
void ieee80211_tx_status_noskb(struct ieee80211_hw *hw,
                                struct ieee80211_sta *sta,
                                struct ieee80211_tx_info *info);

/* Linux include/net/mac80211.h: hardware RX filter flags */
enum ieee80211_filter_flags {
    FIF_ALLMULTI             = 1 << 1,
    FIF_FCSFAIL              = 1 << 2,
    FIF_PLCPFAIL             = 1 << 3,
    FIF_BCN_PRBRESP_PROMISC  = 1 << 4,
    FIF_CONTROL              = 1 << 5,
    FIF_OTHER_BSS            = 1 << 6,
    FIF_PSPOLL               = 1 << 7,
    FIF_PROBE_REQ            = 1 << 8,
    FIF_MCAST_ACTION         = 1 << 9,
};

/* Linux include/net/mac80211.h: ieee80211_config_changed bits */
#define IEEE80211_CONF_CHANGE_SMPS            (1 << 1)
#define IEEE80211_CONF_CHANGE_LISTEN_INTERVAL (1 << 2)
#define IEEE80211_CONF_CHANGE_MONITOR         (1 << 3)
#define IEEE80211_CONF_CHANGE_PS              (1 << 4)
#define IEEE80211_CONF_CHANGE_POWER           (1 << 5)
#define IEEE80211_CONF_CHANGE_CHANNEL         (1 << 6)
#define IEEE80211_CONF_CHANGE_RETRY_LIMITS    (1 << 7)
#define IEEE80211_CONF_CHANGE_IDLE            (1 << 8)
enum nl80211_nan_func_term_reason {
    NL80211_NAN_FUNC_TERM_REASON_USER_REQUEST,
    NL80211_NAN_FUNC_TERM_REASON_TIMEOUT,
};

void ieee80211_nan_func_terminated(struct ieee80211_vif *vif, u64 inst_id,
                                   enum nl80211_nan_func_term_reason reason,
                                   gfp_t gfp);
void ieee80211_nan_func_match(struct ieee80211_vif *vif,
                              struct sk_buff *info, u64 inst_id, u8 peer_addr[ETH_ALEN],
                              gfp_t gfp);
void ieee80211_tx_delayed(struct ieee80211_vif *vif, struct sk_buff *skb,
                          unsigned long delay);

int ieee80211_channel_to_frequency_stub(int chan, enum nl80211_band band);

/* chanctx emulation helpers (used by mt792x_get_mac80211_ops) */
int ieee80211_emulate_add_chanctx(struct ieee80211_hw *hw,
                                  struct ieee80211_chanctx_conf *conf);
void ieee80211_emulate_remove_chanctx(struct ieee80211_hw *hw,
                                      struct ieee80211_chanctx_conf *conf);
void ieee80211_emulate_change_chanctx(struct ieee80211_hw *hw,
                                      struct ieee80211_chanctx_conf *conf,
                                      u32 changed);
int ieee80211_emulate_switch_vif_chanctx(struct ieee80211_hw *hw,
                                         struct ieee80211_vif_chanctx_switch *vifs,
                                         int n_vifs,
                                         enum ieee80211_chanctx_switch_mode mode);

/* regulatory */
int regulatory_hint(struct wiphy *wiphy, const char *alpha2);
int regulatory_set_wiphy_regd(struct wiphy *wiphy,
                              struct ieee80211_regdomain *rd);
int regulatory_set_wiphy_regd_sync_rtnl(struct wiphy *wiphy,
                                        struct ieee80211_regdomain *rd);
void cfg80211_chandef_create(struct cfg80211_chan_def *chandef,
                             struct ieee80211_channel *chan,
                             enum nl80211_chan_width width);
bool cfg80211_reg_can_beacon(struct wiphy *wiphy,
                             struct cfg80211_chan_def *chandef,
                             enum nl80211_iftype iftype);
bool cfg80211_chandef_valid(const struct cfg80211_chan_def *chandef);


struct ieee80211_key_seq;


/* ================================================================== */
/* Wi-Fi 7 / MLO: EML + MLD capability bits (linux/ieee80211-eht.h)    */
/* ================================================================== */
#define IEEE80211_EML_CAP_EMLSR_SUPP            0x0001
#define IEEE80211_EML_CAP_EML_PADDING_DELAY     0x000e
#define IEEE80211_EML_CAP_EML_TRANSITION_DELAY  0x0070
#define IEEE80211_EML_CAP_EMLMR_SUPPORT         0x0080
#define IEEE80211_EML_CAP_TRANSITION_TIMEOUT    0x7800

#define IEEE80211_MLD_CAP_OP_MAX_SIMUL_LINKS    0x000f
#define IEEE80211_MLD_CAP_OP_FREQ_SEP_TYPE_IND  0x0f80

/* HT MPDU length limits (linux/ieee80211-ht.h) */
#define IEEE80211_MAX_MPDU_LEN_HT_BA            4095
#define IEEE80211_MAX_MPDU_LEN_HT_3839          3839
#define IEEE80211_MAX_MPDU_LEN_HT_7935          7935

/* ================================================================== */
/* MLO helpers                                                         */
/* ================================================================== */
static inline struct ieee80211_link_sta *
link_sta_dereference_check(struct ieee80211_sta *sta, unsigned int link_id)
{
    return sta->link[link_id];
}

#define for_each_sta_active_link(vif, sta, link_sta, link_id)                   \
    for (link_id = 0; link_id < ARRAY_SIZE((sta)->link); link_id++)             \
        if ((!(vif)->active_links ||                                            \
             (vif)->active_links & BIT(link_id)) &&                             \
            ((link_sta) = link_sta_dereference_check(sta, link_id)))

static inline enum nl80211_iftype
ieee80211_vif_type_p2p(struct ieee80211_vif *vif)
{
    if (vif->p2p) {
        switch (vif->type) {
        case NL80211_IFTYPE_STATION:
            return NL80211_IFTYPE_P2P_CLIENT;
        case NL80211_IFTYPE_AP:
            return NL80211_IFTYPE_P2P_GO;
        default:
            break;
        }
    }
    return vif->type;
}

static inline __le16
ieee80211_get_he_6ghz_capa(const struct ieee80211_supported_band *sband,
                           enum nl80211_iftype type)
{
    /* TODO(M4): read the per-iftype HE 6 GHz capability */
    (void)sband; (void)type;
    return cpu_to_le16(0);
}

static inline const struct wiphy_iftype_ext_capab *
cfg80211_get_iftype_ext_capa(const struct wiphy *wiphy,
                             enum nl80211_iftype iftype)
{
    /* TODO(M4): scan wiphy->iftype_ext_capab */
    (void)wiphy; (void)iftype;
    return NULL;
}

#ifdef __cplusplus
}
#endif

/* ------------------------------------------------------------------ */
/* mt7915 additions (Ported for mt76wm (XNU))                          */
/* ------------------------------------------------------------------ */
#ifndef MTK_LC_MAC80211_MT7915_ADDITIONS
#define MTK_LC_MAC80211_MT7915_ADDITIONS

/* 802.11 information element iteration (Linux include/linux/ieee80211.h) */
#ifndef MTK_LC_STRUCT_ELEMENT_DEFINED
#define MTK_LC_STRUCT_ELEMENT_DEFINED
struct element {
    u8 id;
    u8 datalen;
    u8 data[];
};
#endif

#ifndef WLAN_EID_EXT_CAPABILITY
#define WLAN_EID_EXT_CAPABILITY    127
#endif

/* Extended Capabilities octet 10, bit 79: OBSS Narrow Bandwidth RU
 * Tolerance Support */
#ifndef WLAN_EXT_CAPA10_OBSS_NARROW_BW_RU_TOLERANCE_SUPPORT
#define WLAN_EXT_CAPA10_OBSS_NARROW_BW_RU_TOLERANCE_SUPPORT    BIT(7)
#endif

/* minimal wireless_dev (mt7915 wed RX path: skb->dev = wdev->netdev) */
#ifndef MTK_LC_WIRELESS_DEV_DEFINED
#define MTK_LC_WIRELESS_DEV_DEFINED
struct wireless_dev {
    struct net_device *netdev;
};
#endif

/* The ported ieee80211_vif has no wdev back-pointer; the only user is
 * the (dead) WED PPE path in mt7915/mac.c.  TODO(M2): real wdev. */
static inline struct wireless_dev *
ieee80211_vif_to_wdev(struct ieee80211_vif *vif)
{
    (void)vif;
    return NULL;
}

/* mac80211/cfg80211 helpers used by mt7915/mcu.c; weak fill-ins live in
 * mtkwm/mt7915/mt7915_stubs.c. */
void ieee80211_color_change_finish(struct ieee80211_vif *vif,
                                   unsigned int link_id);
const struct element *ieee80211_bss_get_elem(struct cfg80211_bss *bss, u8 id);
void cfg80211_background_radar_event(struct wiphy *wiphy,
                                     const struct cfg80211_chan_def *chandef,
                                     gfp_t gfp);
void cfg80211_bss_iter(struct wiphy *wiphy,
                       const struct cfg80211_chan_def *chandef,
                       void (*iter)(struct wiphy *wiphy,
                                    struct cfg80211_bss *bss, void *data),
                       void *data);

/* Linux net/mac80211.c helper: attach the per-iftype HE/EHT cap data */
static inline void
_ieee80211_set_sband_iftype_data(struct ieee80211_supported_band *sband,
                                 const struct ieee80211_sband_iftype_data *data,
                                 unsigned int n)
{
    sband->iftype_data = data;
    sband->n_iftype_data = n;
}

/* ---- mt7915 TWT setup (Linux net/ieee80211.h) ---- */
#ifndef MTK_LC_IEEE80211_TWT_DEFINED
#define MTK_LC_IEEE80211_TWT_DEFINED
enum ieee80211_twt_setup_cmd {
    TWT_SETUP_CMD_REQUEST = 0,
    TWT_SETUP_CMD_SUGGEST = 1,
    TWT_SETUP_CMD_ACCEPT = 2,
    TWT_SETUP_CMD_DICTATE = 3,
    TWT_SETUP_CMD_REJECT = 4,
};

struct ieee80211_twt_params {
    __le16 req_type;
    __le64 twt;
    u8 min_twt_dur;
    __le16 mantissa;
    u8 channel;
} __packed;

struct ieee80211_twt_setup {
    u8 dialog_token;
    u8 control;
    u8 params[];
};
#endif

/* ---- mt7915 spatial-reuse / multi-BSSID helpers ---- */
#ifndef MTK_LC_IEEE80211_HE_OBSS_PD_DEFINED
#define MTK_LC_IEEE80211_HE_OBSS_PD_DEFINED
struct ieee80211_he_obss_pd {
    bool enable;
    u8 max_offset;
    u8 min_offset;
    u8 sr_ctrl;
    u8 non_srg_max_offset;
    u8 bss_color_bitmap[8];
    u8 partial_bssid_bitmap[8];
};

struct ieee80211_bssid_index {
    u8 bssid_index;
    u8 bssid_count;
    u8 profile_period;
    u8 profile_num;
};

struct cfg80211_he_bss_color {
    u8 color;
    bool enabled;
    bool partial;
};
#endif

#ifndef WLAN_EID_MULTIPLE_BSSID
#define WLAN_EID_MULTIPLE_BSSID    55
#endif
#ifndef WLAN_EID_MULTI_BSSID_IDX
#define WLAN_EID_MULTI_BSSID_IDX   85
#endif

/* element iteration (Linux include/linux/ieee80211.h) */
#ifndef for_each_element
#define for_each_element(_elem, _data, _len)                            \
    for (_elem = (const struct element *)(_data);                       \
         (const u8 *)(_elem) + sizeof(*(_elem)) <=                      \
             (const u8 *)(_data) + (_len) &&                            \
         (const u8 *)(_elem) + sizeof(*(_elem)) + (_elem)->datalen <=   \
             (const u8 *)(_data) + (_len);                              \
         _elem = (const struct element *)((const u8 *)(_elem) +         \
                 sizeof(*(_elem)) + (_elem)->datalen))
#endif
#ifndef for_each_element_id
#define for_each_element_id(_elem, _id, _data, _len)                    \
    for_each_element(_elem, _data, _len)                                \
        if ((_elem)->id == (_id))
#endif

/* rate-control change flags (mt7915 mac.c sta_rc_update) */
#ifndef IEEE80211_RC_SUPP_RATES_CHANGED
#define IEEE80211_RC_SUPP_RATES_CHANGED    BIT(0)
#define IEEE80211_RC_SMPS_CHANGED          BIT(1)
#define IEEE80211_RC_NSS_CHANGED           BIT(2)
#define IEEE80211_RC_BW_CHANGED            BIT(3)
#endif

/* beacon template helpers (mt7915 mcu.c); weak fill-ins in
 * mtkwm/mt7915/mt7915_stubs.c */
struct sk_buff *ieee80211_get_fils_discovery_tmpl(struct ieee80211_hw *hw,
                                                  struct ieee80211_vif *vif,
                                                  unsigned int link_id);
struct sk_buff *
ieee80211_get_unsol_bcast_probe_resp_tmpl(struct ieee80211_hw *hw,
                                          struct ieee80211_vif *vif,
                                          unsigned int link_id);

/* cfg80211 IE lookup (mt7915 mcu.c multi-BSSID parse); weak fill-in in
 * mtkwm/mt7915/mt7915_stubs.c */
const u8 *cfg80211_find_ie(u8 eid, const u8 *ies, int len);

#endif /* MTK_MAC80211_MT7915_ADDITIONS */

/* ================================================================== */
/* MTK_MAC80211_MT7996_ADDITIONS (Ported for mt76wm (XNU))            */
/* ================================================================== */

/* per-radio concurrent-channel description (Linux include/net/cfg80211.h) */
struct wiphy_radio_freq_range {
    u32 start_freq;
    u32 end_freq;
};

struct wiphy_radio {
    const struct wiphy_radio_freq_range *freq_range;
    int n_freq_range;

    const struct ieee80211_iface_combination *iface_combinations;
    int n_iface_combinations;

    u32 antenna_mask;
};

/* EHT Operating mode notification parameters (Linux include/net/mac80211.h) */
struct ieee80211_eml_params {
    u8 link_id;
    u8 control;
    u16 link_bitmap;
    u8 emlmr_mcs_map_count;
    u8 emlmr_mcs_map_bw[9];
};

/* extended capability bits (Linux include/linux/ieee80211.h), mt7996 init.c */
#ifndef WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING
#define WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING    BIT(2)
#endif
#ifndef WLAN_EXT_CAPA3_MULTI_BSSID_SUPPORT
#define WLAN_EXT_CAPA3_MULTI_BSSID_SUPPORT      BIT(6)
#endif
#ifndef WLAN_EXT_CAPA8_OPMODE_NOTIF
#define WLAN_EXT_CAPA8_OPMODE_NOTIF             BIT(6)
#endif

/* beamform capability bits (Linux include/linux/ieee80211-he.h and
 * ieee80211-vht.h), mt7996 init.c stream caps */
#ifndef IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT
#define IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT    16
#define IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK    (7 << 16)
#endif
#ifndef IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO
#define IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO       0x40
#endif
#ifndef IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER
#define IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER            0x80
#endif
#ifndef IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE
#define IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE            0x01
#endif
#ifndef IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER
#define IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER            0x02
#endif
#ifndef IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4    0x0c
#endif
#ifndef IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_5
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_5    0x10
#endif
#ifndef IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_4
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_4    0x60
#endif
#ifndef IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_5
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_5    0x80
#endif
#ifndef IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK  0x07
#endif
#ifndef IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_MASK
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_MASK  0x38
#endif
#ifndef IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU
#define IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU      0x01
#endif
#ifndef IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU
#define IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU      0x02
#endif
#ifndef IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB
#define IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB   0x04
#endif
#ifndef IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB
#define IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB   0x08
#endif
#ifndef IEEE80211_HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO
#define IEEE80211_HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO          0x40
#endif
#ifndef IEEE80211_HE_PHY_CAP7_MAX_NC_MASK
#define IEEE80211_HE_PHY_CAP7_MAX_NC_MASK              0x38
#endif
#ifndef IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO
#define IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO    0x80
#endif
#ifndef IEEE80211_HE_MAC_CAP2_MU_CASCADING
#define IEEE80211_HE_MAC_CAP2_MU_CASCADING             0x40
#endif
#ifndef IEEE80211_HE_MAC_CAP3_OFDMA_RA
#define IEEE80211_HE_MAC_CAP3_OFDMA_RA                 0x04
#endif
#ifndef IEEE80211_HE_MAC_CAP3_RX_CTRL_FRAME_TO_MULTIBSS
#define IEEE80211_HE_MAC_CAP3_RX_CTRL_FRAME_TO_MULTIBSS 0x80
#endif
#ifndef IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_MASK
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_MASK  0x1c
#endif
#ifndef IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_MASK
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_MASK  0xe0
#endif
#ifndef IEEE80211_EHT_PHY_CAP3_SOUNDING_DIM_320MHZ_MASK
#define IEEE80211_EHT_PHY_CAP3_SOUNDING_DIM_320MHZ_MASK 0x01
#endif

/* ================================================================== */
/* MTK_MAC80211_MT7996_CAPS (Ported for mt76wm (XNU))                 */
/* Capability/enum constants referenced by the mt7996 port.  Values    */
/* are taken from the Linux headers (ieee80211-he.h, ieee80211-eht.h, */
/* ieee80211-vht.h, net/mac80211.h) so the built HE/EHT capability    */
/* bitmaps are semantically correct.  All guarded with #ifndef so the  */
/* existing families are unaffected (additive only).                  */
/* ================================================================== */

#ifndef IEEE80211_AC_BE
#define IEEE80211_AC_BE 2
#endif
#ifndef IEEE80211_AC_BK
#define IEEE80211_AC_BK 3
#endif
#ifndef IEEE80211_AC_VI
#define IEEE80211_AC_VI 1
#endif
#ifndef IEEE80211_AC_VO
#define IEEE80211_AC_VO 0
#endif
#ifndef IEEE80211_EHT_MAC_CAP0_MAX_MPDU_LEN_11454
#define IEEE80211_EHT_MAC_CAP0_MAX_MPDU_LEN_11454 2
#endif
#ifndef IEEE80211_EHT_MAC_CAP0_MAX_MPDU_LEN_MASK
#define IEEE80211_EHT_MAC_CAP0_MAX_MPDU_LEN_MASK 0xc0
#endif
#ifndef IEEE80211_EHT_MAC_CAP0_OM_CONTROL
#define IEEE80211_EHT_MAC_CAP0_OM_CONTROL 0x02
#endif
#ifndef IEEE80211_EHT_MAC_CAP1_MAX_AMPDU_LEN_MASK
#define IEEE80211_EHT_MAC_CAP1_MAX_AMPDU_LEN_MASK 0x01
#endif
#ifndef IEEE80211_EHT_MCS_NSS_RX
#define IEEE80211_EHT_MCS_NSS_RX 0x0f
#endif
#ifndef IEEE80211_EHT_MCS_NSS_TX
#define IEEE80211_EHT_MCS_NSS_TX 0xf0
#endif
#ifndef IEEE80211_EHT_PHY_CAP0_BEAMFORMEE_SS_80MHZ_MASK
#define IEEE80211_EHT_PHY_CAP0_BEAMFORMEE_SS_80MHZ_MASK 0x80
#endif
#ifndef IEEE80211_EHT_PHY_CAP0_NDP_4_EHT_LFT_32_GI
#define IEEE80211_EHT_PHY_CAP0_NDP_4_EHT_LFT_32_GI 0x08
#endif
#ifndef IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMEE
#define IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMEE 0x40
#endif
#ifndef IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMER
#define IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMER 0x20
#endif
#ifndef IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_160MHZ_MASK
#define IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_160MHZ_MASK 0x1c
#endif
#ifndef IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_320MHZ_MASK
#define IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_320MHZ_MASK 0xe0
#endif
#ifndef IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_80MHZ_MASK
#define IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_80MHZ_MASK 0x03
#endif
#ifndef IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_160MHZ_MASK
#define IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_160MHZ_MASK 0x38
#endif
#ifndef IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_320MHZ_MASK
#define IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_320MHZ_MASK 0xc0
#endif
#ifndef IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_80MHZ_MASK
#define IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_80MHZ_MASK 0x07
#endif
#ifndef IEEE80211_EHT_PHY_CAP3_CODEBOOK_4_2_SU_FDBK
#define IEEE80211_EHT_PHY_CAP3_CODEBOOK_4_2_SU_FDBK 0x08
#endif
#ifndef IEEE80211_EHT_PHY_CAP3_CODEBOOK_7_5_MU_FDBK
#define IEEE80211_EHT_PHY_CAP3_CODEBOOK_7_5_MU_FDBK 0x10
#endif
#ifndef IEEE80211_EHT_PHY_CAP3_NG_16_MU_FEEDBACK
#define IEEE80211_EHT_PHY_CAP3_NG_16_MU_FEEDBACK 0x04
#endif
#ifndef IEEE80211_EHT_PHY_CAP3_NG_16_SU_FEEDBACK
#define IEEE80211_EHT_PHY_CAP3_NG_16_SU_FEEDBACK 0x02
#endif
#ifndef IEEE80211_EHT_PHY_CAP3_TRIG_MU_BF_PART_BW_FDBK
#define IEEE80211_EHT_PHY_CAP3_TRIG_MU_BF_PART_BW_FDBK 0x40
#endif
#ifndef IEEE80211_EHT_PHY_CAP3_TRIG_SU_BF_FDBK
#define IEEE80211_EHT_PHY_CAP3_TRIG_SU_BF_FDBK 0x20
#endif
#ifndef IEEE80211_EHT_PHY_CAP4_EHT_MU_PPDU_4_EHT_LTF_08_GI
#define IEEE80211_EHT_PHY_CAP4_EHT_MU_PPDU_4_EHT_LTF_08_GI 0x08
#endif
#ifndef IEEE80211_EHT_PHY_CAP4_MAX_NC_MASK
#define IEEE80211_EHT_PHY_CAP4_MAX_NC_MASK 0xf0
#endif
#ifndef IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_16US
#define IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_16US 2
#endif
#ifndef IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_MASK
#define IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_MASK 0x30
#endif
#ifndef IEEE80211_EHT_PHY_CAP5_MAX_NUM_SUPP_EHT_LTF_MASK
#define IEEE80211_EHT_PHY_CAP5_MAX_NUM_SUPP_EHT_LTF_MASK 0xc0
#endif
#ifndef IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_160MHZ
#define IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_160MHZ 0x20
#endif
#ifndef IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_320MHZ
#define IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_320MHZ 0x40
#endif
#ifndef IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_80MHZ
#define IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_80MHZ 0x10
#endif
#ifndef IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_MASK
#define IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_MASK 0x78
#endif
#ifndef IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_160MHZ
#define IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_160MHZ 0x20
#endif
#ifndef IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_320MHZ
#define IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_320MHZ 0x40
#endif
#ifndef IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_80MHZ
#define IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_80MHZ 0x10
#endif
#ifndef IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_160MHZ
#define IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_160MHZ 0x04
#endif
#ifndef IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_320MHZ
#define IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_320MHZ 0x08
#endif
#ifndef IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_80MHZ
#define IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_80MHZ 0x02
#endif
#ifndef IEEE80211_EML_CTRL_EMLSR_MODE
#define IEEE80211_EML_CTRL_EMLSR_MODE BIT(0)
#endif
#ifndef IEEE80211_HE_6GHZ_CAP_MAX_MPDU_LEN
#define IEEE80211_HE_6GHZ_CAP_MAX_MPDU_LEN 0x00c0
#endif
#ifndef IEEE80211_HE_6GHZ_CAP_RX_ANTPAT_CONS
#define IEEE80211_HE_6GHZ_CAP_RX_ANTPAT_CONS 0x1000
#endif
#ifndef IEEE80211_HE_6GHZ_CAP_TX_ANTPAT_CONS
#define IEEE80211_HE_6GHZ_CAP_TX_ANTPAT_CONS 0x2000
#endif
#ifndef IEEE80211_HE_MAC_CAP0_TWT_RES
#define IEEE80211_HE_MAC_CAP0_TWT_RES 0x04
#endif
#ifndef IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US 0x08
#endif
#ifndef IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3 0x18
#endif
#ifndef IEEE80211_HE_MAC_CAP5_OM_CTRL_UL_MU_DATA_DIS_RX
#define IEEE80211_HE_MAC_CAP5_OM_CTRL_UL_MU_DATA_DIS_RX 0x10
#endif
#ifndef IEEE80211_HE_MCS_NOT_SUPPORTED
#define IEEE80211_HE_MCS_NOT_SUPPORTED 3
#endif
#ifndef IEEE80211_HE_MCS_SUPPORT_0_11
#define IEEE80211_HE_MCS_SUPPORT_0_11 2
#endif
#ifndef IEEE80211_HE_MCS_SUPPORT_0_7
#define IEEE80211_HE_MCS_SUPPORT_0_7 0
#endif
#ifndef IEEE80211_HE_MCS_SUPPORT_0_9
#define IEEE80211_HE_MCS_SUPPORT_0_9 1
#endif
#ifndef IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G 0x08
#endif
#ifndef IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G 0x04
#endif
#ifndef IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G 0x02
#endif
#ifndef IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_QPSK
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_QPSK 0x10
#endif
#ifndef IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_QPSK
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_QPSK 0x02
#endif
#ifndef IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT
#define IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT 0x80
#endif
#ifndef IEEE80211_HE_PHY_CAP6_TRIG_CQI_FB
#define IEEE80211_HE_PHY_CAP6_TRIG_CQI_FB 0x10
#endif
#ifndef IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_SUPP
#define IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_SUPP 0x02
#endif
#ifndef IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU
#define IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU 0x04
#endif
#ifndef IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G
#define IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G 0x02
#endif
#ifndef IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU
#define IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU 0x08
#endif
#ifndef IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484 0x40
#endif
#ifndef IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM
#define IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM 0x01
#endif
#ifndef IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_MASK
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_MASK 0xc0
#endif
#ifndef IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB
#define IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB 0x10
#endif
#ifndef IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB
#define IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB 0x20
#endif
#ifndef IEEE80211_HE_SPR_HESIGA_SR_VAL15_ALLOWED
#define IEEE80211_HE_SPR_HESIGA_SR_VAL15_ALLOWED BIT(4)
#endif
#ifndef IEEE80211_HE_SPR_NON_SRG_OBSS_PD_SR_DISALLOWED
#define IEEE80211_HE_SPR_NON_SRG_OBSS_PD_SR_DISALLOWED BIT(1)
#endif
#ifndef IEEE80211_HE_SPR_NON_SRG_OFFSET_PRESENT
#define IEEE80211_HE_SPR_NON_SRG_OFFSET_PRESENT BIT(2)
#endif
#ifndef IEEE80211_HE_SPR_SRG_INFORMATION_PRESENT
#define IEEE80211_HE_SPR_SRG_INFORMATION_PRESENT BIT(3)
#endif
#ifndef IEEE80211_HT_CAP_GRN_FLD
#define IEEE80211_HT_CAP_GRN_FLD 0x0010
#endif
#ifndef IEEE80211_HT_MCS_TX_DEFINED
#define IEEE80211_HT_MCS_TX_DEFINED 0x01
#endif
#ifndef IEEE80211_HT_MCS_TX_MAX_STREAMS_MASK
#define IEEE80211_HT_MCS_TX_MAX_STREAMS_MASK 0x0C
#endif
#ifndef IEEE80211_HT_MCS_TX_RX_DIFF
#define IEEE80211_HT_MCS_TX_RX_DIFF 0x02
#endif
/* NB: IEEE80211_IFACE_ITER_RESUME_ALL is an enumerator further up; a
 * macro definition would shadow it in C++. */
#ifndef IEEE80211_IFACE_SKIP_SDATA_NOT_IN_DRIVER
#define IEEE80211_IFACE_SKIP_SDATA_NOT_IN_DRIVER BIT(2)
#endif
#ifndef IEEE80211_QOS_CTL_A_MSDU_PRESENT
#define IEEE80211_QOS_CTL_A_MSDU_PRESENT 0x0080
#endif
#ifndef IEEE80211_SCTL_FRAG
#define IEEE80211_SCTL_FRAG 0x000F
#endif
/* NB: IEEE80211_SMPS_* and IEEE80211_STA_RX_BW_* must NOT be #defines:
 * they are enumerators of enum ieee80211_smps_mode / ieee80211_sta_rx_bandwidth
 * above, and macro-shadowing them breaks every `enum_var = EXPR;` assignment
 * in C++ (regression hit in glue/mtk_port_funcs.c). */
#ifndef IEEE80211_TWT_CONTROL_NEG_TYPE_BROADCAST
#define IEEE80211_TWT_CONTROL_NEG_TYPE_BROADCAST BIT(3)
#endif
#ifndef IEEE80211_TWT_CONTROL_RX_DISABLED
#define IEEE80211_TWT_CONTROL_RX_DISABLED BIT(4)
#endif
#ifndef IEEE80211_TWT_CONTROL_WAKE_DUR_UNIT
#define IEEE80211_TWT_CONTROL_WAKE_DUR_UNIT BIT(5)
#endif
#ifndef IEEE80211_TWT_REQTYPE_FLOWID
#define IEEE80211_TWT_REQTYPE_FLOWID GENMASK(9, 7)
#endif
#ifndef IEEE80211_TWT_REQTYPE_FLOWTYPE
#define IEEE80211_TWT_REQTYPE_FLOWTYPE BIT(6)
#endif
#ifndef IEEE80211_TWT_REQTYPE_IMPLICIT
#define IEEE80211_TWT_REQTYPE_IMPLICIT BIT(5)
#endif
#ifndef IEEE80211_TWT_REQTYPE_PROTECTION
#define IEEE80211_TWT_REQTYPE_PROTECTION BIT(15)
#endif
#ifndef IEEE80211_TWT_REQTYPE_SETUP_CMD
#define IEEE80211_TWT_REQTYPE_SETUP_CMD GENMASK(3, 1)
#endif
#ifndef IEEE80211_TWT_REQTYPE_TRIGGER
#define IEEE80211_TWT_REQTYPE_TRIGGER BIT(4)
#endif
#ifndef IEEE80211_TWT_REQTYPE_WAKE_INT_EXP
#define IEEE80211_TWT_REQTYPE_WAKE_INT_EXP GENMASK(14, 10)
#endif
#ifndef IEEE80211_VHT_CAP_RXSTBC_MASK
#define IEEE80211_VHT_CAP_RXSTBC_MASK 0x00000700
#endif
#ifndef IEEE80211_VHT_MAX_AMPDU_1024K
#define IEEE80211_VHT_MAX_AMPDU_1024K 7
#endif
#ifndef IEEE80211_VHT_MCS_NOT_SUPPORTED
#define IEEE80211_VHT_MCS_NOT_SUPPORTED 3
#endif
#ifndef IEEE80211_VHT_MCS_SUPPORT_0_7
#define IEEE80211_VHT_MCS_SUPPORT_0_7 0
#endif
#ifndef IEEE80211_VHT_MCS_SUPPORT_0_8
#define IEEE80211_VHT_MCS_SUPPORT_0_8 1
#endif
#ifndef IEEE80211_VHT_MCS_SUPPORT_0_9
#define IEEE80211_VHT_MCS_SUPPORT_0_9 2
#endif

/* constant-expression variant of FIELD_PREP (Linux include/linux/bitfield.h) */
#ifndef FIELD_PREP_CONST
#define FIELD_PREP_CONST(_mask, _val) \
    (((typeof(_mask))(_val) * ((_mask) & ~((_mask) << 1))) & (_mask))
#endif

/* frame-control helpers used by mt7996/mac.c (Linux include/linux/ieee80211.h) */
static inline int ieee80211_has_tods(__le16 fc)
{
    return le16_to_cpu(fc) & IEEE80211_FCTL_TODS;
}

static inline int ieee80211_has_fromds(__le16 fc)
{
    return le16_to_cpu(fc) & IEEE80211_FCTL_FROMDS;
}

#ifndef IEEE80211_STYPE_NULLFUNC
#define IEEE80211_STYPE_NULLFUNC     0x0040
#endif
static inline int ieee80211_is_nullfunc(__le16 fc)
{
    return (le16_to_cpu(fc) & 0x00fc) ==
           (IEEE80211_FTYPE_DATA | IEEE80211_STYPE_NULLFUNC);
}

/* MLO link iteration (Linux include/net/mac80211.h), mt7996/mac.c */
#ifndef for_each_vif_active_link
#define for_each_vif_active_link(_vif, _link_conf, _link_id)                   \
    for (_link_id = 0; _link_id < IEEE80211_MLD_MAX_NUM_LINKS; _link_id++)    \
        if (((_link_conf) = link_conf_dereference_protected(_vif, _link_id)))
#endif

#endif /* MTK_MAC80211_H */

/* ---- auto-added IEEE80211_* tokens referenced by the port (values
 * are placeholders; TODO: exact semantics when wiring net80211) ---- */
#define IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US (1)
#define IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI (2)

/* ---- auto-added IEEE80211_* tokens referenced by the port (values
 * are placeholders; TODO: exact semantics when wiring net80211) ---- */
#define UNUSED_IEEE80211_AC_BE (1)
#define UNUSED_IEEE80211_AC_BK (2)
#define UNUSED_IEEE80211_AC_VI (3)
#define UNUSED_IEEE80211_AC_VO (4)
#define UNUSED_IEEE80211_FRAME_RELEASE_UAPSD (5)
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_MASK (6)
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_MASK (7)
#define UNUSED_IEEE80211_HE_MAC_m (8)
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_MASK (9)
#define IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A (10)
#define IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK (11)
#define IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US (12)
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_MASK (13)
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_MASK (14)
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_2 (15)
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_2 (16)
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_MASK (17)
#define UNUSED_IEEE80211_HE_PHY_p (18)
#define UNUSED_IEEE80211_IFACE_ITER_RESUME_ALL (19)
#define UNUSED_IEEE80211_SMPS_DYNAMIC (20)
#define UNUSED_IEEE80211_STA_ASSOC (21)
#define UNUSED_IEEE80211_STA_AUTHORIZED (22)
#define UNUSED_IEEE80211_STA_NONE (23)
#define UNUSED_IEEE80211_STA_NOTEXIST (24)
#define UNUSED_IEEE80211_STA_RX_BW_160 (25)
#define UNUSED_IEEE80211_STA_RX_BW_20 (26)
#define UNUSED_IEEE80211_STA_RX_BW_40 (27)
#define UNUSED_IEEE80211_STA_RX_BW_80 (28)
