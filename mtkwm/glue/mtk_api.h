/*
 * mtk_api.h - public C API between the XNU HAL layer (mtkwm/hal_mt76) and
 * the ported Linux mt76 core.
 *
 * Ported for mt76wm (XNU).
 */

#ifndef mtk_api_h
#define mtk_api_h

struct mt76_softc;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Called by ItlMt76 after BAR0 is mapped and the MSI interrupt source is
 * established.  Completes mt792x_dev allocation, bus_ops/regs setup and
 * the MCU/firmware bring-up as far as it can (M1: chip id read + log).
 */
int mtk_pci_init(struct mt76_softc *sc);

void mtk_pci_deinit(struct mt76_softc *sc);

/* Interrupt filter (primary context): returns true if the bottom half
 * (mtk_intr_action) should run. */
bool mtk_intr_filter(struct mt76_softc *sc);

/* Interrupt bottom half: NAPI polling for RX/TX. */
void mtk_intr_action(struct mt76_softc *sc);

/* Retrieve the mt792x_dev associated with an mt76_softc (NULL if none). */
void *mtk_get_mdev(struct mt76_softc *sc);

/* ================================================================== */
/* MCU / firmware readiness + stop-path helpers (glue/mtk_port_funcs.c) */
/* ================================================================== */

/* Called from mt7921_init_work() when the firmware bring-up finished
 * (ret == 0), the probe fallback runs without any firmware
 * (ret == MTK_MCU_INIT_NOMCU), or bring-up permanently failed (ret < 0). */
#define MTK_MCU_INIT_NOMCU  2
void mtk_mcu_init_done(void *mdev, int ret);

/* Bounded wait for the firmware bring-up: 0 when the MCU is up. */
int mtk_mcu_wait_ready(unsigned int timeout_ms);

/* True when firmware init completed successfully. */
bool mtk_mcu_is_ready(void);

/* Wake/abort all in-flight MCU waiters.  With @final the device is also
 * marked removed so no chip reset can be scheduled during teardown. */
void mtk_mcu_abort(void *mdev, bool final);

/* Make the net80211 scan machinery inert (delete the dwell timer, stop
 * re-arming).  Safe to call more than once. */
void mtk_net80211_quiesce(void);

/* ================================================================== */
/* multi-family dispatch (mtkwm/glue/mtk_api.c)                       */
/* ================================================================== */

/* forward decls for the bridge hooks below (defined further down) */
struct mtk_tx_cookie;
struct mtk_key_desc;
struct mtk_edca_ac;

/*
 * Per-family teardown/interrupt hooks registered by the family entry
 * (e.g. mtkwm/mt7925/mt7925_entry.c).  mtk_pci_deinit()/mtk_intr_filter()/
 * mtk_intr_action() dispatch through the hooks registered for the device;
 * devices without registered hooks use the MT7921 defaults.
 *
 * The net80211 bridge hooks (net80211 -> driver) are optional: when NULL
 * (all mt76-core families) glue/mtk_port_funcs.c runs the built-in
 * mt792x implementation; a family that does not use the mt76 core (the
 * independent mt7601u USB driver) overrides the whole set.  Additive
 * extension - existing families leave these NULL and are unaffected.
 */
struct mtk_dev_ops {
    void (*remove)(struct mt76_softc *sc);
    bool (*intr_filter)(struct mt76_softc *sc);
    void (*intr_action)(struct mt76_softc *sc);

    /* net80211 -> driver bridge overrides (all optional, NULL = default) */
    int (*tx_frame)(void *mdev, const struct mtk_tx_cookie *ck,
                    const void *frame, unsigned int len,
                    unsigned int flags, unsigned int prio,
                    const struct mtk_key_desc *hwkey);
    void (*tx_status_check)(void *mdev);
    void (*tx_flush)(void *mdev);
    int (*sta_add)(void *mdev, void *ni, const unsigned char *mac,
                   unsigned int aid, const unsigned char *rates,
                   unsigned int nrates);
    void (*sta_remove)(void *mdev, void *ni);
    int (*bss_setup)(void *mdev, const unsigned char *bssid,
                     unsigned int aid, unsigned int bintval,
                     unsigned int dtim, unsigned int qos,
                     unsigned int basic_rates);
    void (*bss_teardown)(void *mdev);
    int (*key_add)(void *mdev, void *ni, const struct mtk_key_desc *k);
    int (*key_delete)(void *mdev, void *ni, const struct mtk_key_desc *k);
    int (*set_edca)(void *mdev, const struct mtk_edca_ac *ac4);
    int (*set_slot)(void *mdev, int short_slot);
    int (*chan_switch)(void *mdev, unsigned int freq, unsigned int offchannel);
    unsigned int (*bands)(void *mdev);
    int (*hw_start)(void *mdev);
    void (*hw_stop)(void *mdev);
    int (*get_mac)(void *mdev, unsigned char *out);
};

void mtk_dev_register(struct mt76_softc *sc, void *mdev,
                      const struct mtk_dev_ops *ops);

/* Look up the mtk_dev_ops registered for a given mdev (NULL if none). */
const struct mtk_dev_ops *mtk_ops_for_mdev(void *mdev);

/* ================================================================== */
/* net80211 registration (glue/mtk_net80211.c)                        */
/* ================================================================== */

/*
 * Register the interface with the itl80211 stack (strong definition of the
 * HAL contract in hal_mt76/mt76_softc.h): fill sc->sc_ic, hook the
 * net80211 callbacks, run ieee80211_ifattach()/ieee80211_media_init().
 */
int mtk_net80211_attach(struct mt76_softc *sc);

/*
 * if_init/if_stop semantics for the HAL enable()/disable() path: power the
 * device up (RX on) and kick the net80211 state machine into scanning.
 */
int mtk_net80211_enable(struct mt76_softc *sc);
void mtk_net80211_disable(struct mt76_softc *sc);

/* ================================================================== */
/* RX bridge: mt76 RX path -> net80211 (itl80211)                      */
/* ================================================================== */

/* Subset of the Linux RX_FLAG_* values, remapped so this header stays free
 * of both the linuxcompat and the itl80211 headers. */
#define MTK_RX_FLAG_DECRYPTED     0x01    /* hw decrypted the frame        */
#define MTK_RX_FLAG_IV_STRIPPED   0x02    /* hw removed the IV/CCMP header */
#define MTK_RX_FLAG_MMIC_STRIPPED 0x04
#define MTK_RX_FLAG_MIC_STRIPPED  0x08
#define MTK_RX_FLAG_FAILED_FCS    0x10    /* FCS error                     */
#define MTK_RX_FLAG_MMIC_ERROR    0x20    /* TKIP MIC failure              */
#define MTK_RX_FLAG_8023          0x40    /* hdr translated to 802.3       */
#define MTK_RX_FLAG_ONLY_MONITOR  0x80    /* only deliverable in monitor   */

/* Per-frame RX status, extracted from mt76_rx_status by mt76_rx(). */
struct mtk_rx_meta {
    unsigned int   freq;           /* receive frequency (MHz)             */
    unsigned int   timestamp;      /* mac timestamp (low 32 bits)         */
    int            rssi_dbm;       /* average chain signal (dBm, <= 0)    */
    unsigned int   flags;          /* MTK_RX_FLAG_*                       */
    unsigned char  rate_idx;
    unsigned char  nss;
    unsigned char  encoding;
    unsigned char  bw;
    unsigned char  enc_flags;
    unsigned char  chains;
    signed char    chain_signal[4];
};

/*
 * Hand one received 802.11 frame (mt76 RX path -> net80211 glue).  The
 * frame is copied into an mbuf and dispatched via ieee80211_inputm().
 * Defined in glue/mtk_net80211.c, called from glue/mtk_port_funcs.c.
 */
void mtk_net80211_rx(void *mdev, const void *frame, unsigned int len,
                     const struct mtk_rx_meta *meta);

/* Flush the per-NAPI-batch mbuf list into the stack (if_input). */
void mtk_net80211_rx_flush(void *mdev);

/* ================================================================== */
/* TX bridge: net80211 -> mt76 -> net80211 completion                  */
/* ================================================================== */

/*
 * Per-frame TX completion cookie.  The net80211 side (glue/mtk_net80211.c)
 * fills it when dequeuing an mbuf from if_snd/ic_mgtq; the mt76 side
 * (glue/mtk_port_funcs.c) copies it into the skb and hands it back through
 * mtk_net80211_tx_done() when the frame completes.  All members are opaque
 * to the linux world (mbuf_t / struct ieee80211_node * live only in the
 * net80211 world).
 */
struct mtk_tx_cookie {
    void *mdev;             /* struct mt792x_dev * (bridge target)      */
    void *m;                /* mbuf_t being freed on completion         */
    void *ni;               /* struct ieee80211_node * (ref released)   */
};

/* mtk_tx_frame() flags. */
#define MTK_TX_FLAG_MGMT    0x01    /* management frame (PSD/ALTX queue)  */
#define MTK_TX_FLAG_NO_ACK  0x02    /* no 802.11 ACK expected             */
#define MTK_TX_FLAG_STATUS  0x04    /* request per-frame TX status (TXS)  */
#define MTK_TX_FLAG_OFFCHAN 0x08    /* offchannel TX (scan), bypasses the
                                     * phy->offchannel TX hold            */

/* mtk_net80211_tx_done() status bits. */
#define MTK_TX_DONE_ACKED   0x01    /* 802.11 ACK seen (TXS)              */
#define MTK_TX_DONE_FAILED  0x02    /* TXS reports failure                */

/*
 * Hand one 802.11 frame (already encapsulated by net80211) to the mt76 TX
 * pipeline: mbuf data is copied into an skb, tagged with @ck and queued
 * through mt76_tx() -> TXWI -> WPDMA.  On any (sync or async) completion
 * mtk_net80211_tx_done() is invoked exactly once with @ck.  @prio is the
 * 802.11 QoS TID (0-7, 0 for non-QoS frames) used for the AC mapping.
 * @hwkey, when non-NULL, marks the frame for hardware (firmware) crypto:
 * the frame stays a plaintext 802.11 MPDU and the key identified by
 * @hwkey->key_id / @hwkey->flags (installed earlier via mtk_key_add())
 * is used by the firmware for per-frame encryption.  Returns 0 if the
 * frame was accepted (the completion callback is then guaranteed),
 * negative errno otherwise (nothing is handed back).  Defined in
 * glue/mtk_port_funcs.c.
 */
struct mtk_key_desc;

int mtk_tx_frame(void *mdev, const struct mtk_tx_cookie *ck,
                 const void *frame, unsigned int len,
                 unsigned int flags, unsigned int prio,
                 const struct mtk_key_desc *hwkey);

/*
 * TX completion notification (mt76 -> net80211).  Frees the mbuf, releases
 * the node reference and updates the net80211 counters.  Defined in
 * glue/mtk_net80211.c, called from glue/mtk_glue.c (skb release) with the
 * status bits that glue/mtk_mac80211.c extracted from the TXS report.
 */
void mtk_net80211_tx_done(const struct mtk_tx_cookie *ck, unsigned int status);

/* Reap stale TX-status entries (call periodically, e.g. from the watchdog). */
void mtk_tx_status_check(void *mdev);

/* Flush all in-flight TX (completes their cookies via mtk_net80211_tx_done)
 * and drop anything parked on the wcid TX lists.  Used on interface down. */
void mtk_tx_flush(void *mdev);

/* ================================================================== */
/* M6: station / BSS / key / EDCA (net80211 -> mt76)                   */
/* ================================================================== */

/* Cipher identifiers for struct mtk_key_desc (bridge-local numbering). */
#define MTK_KEY_CIPHER_NONE    0
#define MTK_KEY_CIPHER_WEP40   1
#define MTK_KEY_CIPHER_TKIP    2
#define MTK_KEY_CIPHER_CCMP    3
#define MTK_KEY_CIPHER_WEP104  4
#define MTK_KEY_CIPHER_BIP     5    /* BIP-CMAC-128 (group mgmt / IGTK) */
#define MTK_KEY_CIPHER_CCMP256 6
#define MTK_KEY_CIPHER_GCMP    7

#define MTK_KEY_FLAG_GROUP 0x1     /* group (GTK) key                   */
#define MTK_KEY_FLAG_TX    0x2     /* valid for TX                      */
#define MTK_KEY_FLAG_IGTK  0x4     /* integrity group key               */

struct mtk_key_desc {
    unsigned int cipher;           /* MTK_KEY_CIPHER_*                  */
    unsigned int key_id;
    unsigned int key_len;
    unsigned int flags;            /* MTK_KEY_FLAG_*                    */
    unsigned char key[32];
};

/*
 * mtk_sta_add() - build the per-node station state (WTBL).
 * @ni is the opaque net80211 node pointer used as the lookup key for TX
 * wcid selection and teardown.  @rates/@nrates is the 802.11 rate list
 * (500kbps units, IEEE80211_RATE_BASIC flags stripped) used to build the
 * legacy rate bitmaps for the firmware RA record.  Returns 0 or -errno.
 */
int  mtk_sta_add(void *mdev, void *ni, const unsigned char *mac,
                 unsigned int aid, const unsigned char *rates,
                 unsigned int nrates);

/* Tear down the station state (STA_REC remove + WTBL release). */
void mtk_sta_remove(void *mdev, void *ni);

/*
 * BSS bring-up for the RUN state (the bss_info_changed(BSS_CHANGED_ASSOC)
 * + start_ap-ish equivalents): DEV/BSS records, BSS power-save state,
 * the BSS station record and the channel context.
 */
int  mtk_bss_setup(void *mdev, const unsigned char *bssid, unsigned int aid,
                   unsigned int bintval, unsigned int dtim,
                   unsigned int qos, unsigned int basic_rates);
void mtk_bss_teardown(void *mdev);

/* Key install/removal (STA_REC_KEY_V2).  @ni selects the per-node wcid
 * (NULL: group keys go on the BSS wcid). */
int  mtk_key_add(void *mdev, void *ni, const struct mtk_key_desc *k);
int  mtk_key_delete(void *mdev, void *ni, const struct mtk_key_desc *k);

/* EDCA parameters, MTK_AC_* indexed (cw in 2^n-1 form, txop in 32us). */
#define MTK_AC_VO 0
#define MTK_AC_VI 1
#define MTK_AC_BE 2
#define MTK_AC_BK 3

struct mtk_edca_ac {
    unsigned int cw_min;
    unsigned int cw_max;
    unsigned int aifs;
    unsigned int txop;
    unsigned int acm;
};

int  mtk_set_edca(void *mdev, const struct mtk_edca_ac *ac4);

/* Slot timing (short/long slot) -> mt792x_mac_set_timeing(). */
int  mtk_set_slot(void *mdev, int short_slot);

/* ================================================================== */
/* net80211 -> mt76 services (glue/mtk_port_funcs.c)                   */
/* ================================================================== */

/* Band bitmask for mtk_bands(). */
#define MTK_BAND_2GHZ 0x1
#define MTK_BAND_5GHZ 0x2
#define MTK_BAND_6GHZ 0x4

/* Radio tune: switch the PHY to the given center frequency (MHz), 20 MHz
 * no-HT.  Uses mt7921_mcu_set_chan_info().  @offchannel != 0 marks the
 * tune as offchannel (software scan hop): frames queued while offchannel
 * are held back by the mt76 TX scheduler.  A BSS channel switch passes 0
 * so TX is not stalled. */
int mtk_chan_switch(void *mdev, unsigned int freq, unsigned int offchannel);

/* Bands the hardware reports (falls back to 2/5 GHz if the MCU capability
 * query did not run). */
unsigned int mtk_bands(void *mdev);

/* Bring the datapath up/down (sets MT76_STATE_RUNNING, rx filter). */
int mtk_hw_start(void *mdev);
void mtk_hw_stop(void *mdev);

/* Station MAC address recorded during probe (mt76_phy.macaddr). */
int mtk_get_mac(void *mdev, unsigned char *out /* ETH_ALEN bytes */);

#ifdef __cplusplus
}
#endif

#endif /* mtk_api_h */
