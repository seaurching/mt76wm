/*
 * mtk_net80211.c - net80211 (itl80211) glue for the ported mt76/mt7921 code.
 *
 * Ported for mt76wm (XNU), milestone M4: net80211 registration, the RX
 * lower half (mt76 RX -> ieee80211_inputm) and the software scan (per
 * channel dwell + mt7921 channel switch + beacons into the node table).
 *
 * This TU sees the itl80211 world (net80211/ieee80211_var.h) and the HAL
 * contract (hal_mt76/mt76_softc.h) only.  Everything that needs the
 * Linux/mac80211 shim headers lives in glue/mtk_port_funcs.c and is
 * reached through the plain-C bridge in glue/mtk_api.h (the two header
 * worlds both define struct ieee80211_channel & friends with different
 * layouts and cannot be mixed in one TU).
 */

#include <IOKit/IOLib.h>
#include <IOKit/IOCommandGate.h>
#include <sys/sockio.h>             /* SIOCSIFFLAGS et al (MacKernelSDK) */

#include "../hal_mt76/mt76_softc.h" /* struct mt76_softc + net80211 API  */
#include "mtk_api.h"

#ifndef IC2IFP
#define IC2IFP(_ic_)    (&(_ic_)->ic_if)
#endif

/*
 * The IOKit main command gate (defined in mtkwm/mtkwm.cpp, shared with
 * itl80211's timeout.c): running the TX drain through it serializes
 * if_start with the interrupt bottom half and the net80211 state timers,
 * which all run on the main workloop.  Same pattern as ItlIwn::iwn_start.
 */
extern IOCommandGate *_fCommandGate;

/*
 * RSSI convention: itl80211 wants "higher is better" in a normalized
 * 0..ic_max_rssi range (HeliPort converts back to dBm via IWM_MIN_DBM,
 * see mtkwm/MtkNetworkUserClient.cpp).  mt76 reports plain dBm.
 */
#define MTK_MIN_DBM         -100
#define MTK_MAX_RSSI        100

/* Per-channel dwell for the software scan. */
#define MTK_SCAN_DWELL_MS   200

/*
 * Single-device glue state.  mt76_softc has no room for driver-private
 * net80211 hooks (its layout is the HAL contract), so keep the small
 * amount of extra state here, keyed by the owning softc.
 */
static struct {
    struct mt76_softc *sc;
    void              *mdev;        /* struct mt792x_dev *               */
    int (*sc_newstate)(struct ieee80211com *, enum ieee80211_state, int);
    CTimeout          *scan_to;     /* per-channel dwell timer           */
    struct mbuf_list   ml;          /* RX batch for ieee80211_inputm()   */
    int                running;
    int                stopping;    /* teardown: no new tunes / re-arms  */
    int                tx_timer;    /* TX watchdog countdown (ticks)     */
} g_n;

/* ================================================================== */
/* forward declarations                                               */
/* ================================================================== */
static int  mtk_newstate(struct ieee80211com *, enum ieee80211_state, int);
static void mtk_scan_dwell(void *);
static void mtk_scan_tune(void);
static int  mtk_ioctl(struct _ifnet *, u_long, caddr_t);
static void mtk_start(struct _ifnet *);
static IOReturn mtk_start_task(OSObject *, void *, void *, void *, void *);
static void mtk_watchdog(struct _ifnet *);
static void mtk_newassoc(struct ieee80211com *, struct ieee80211_node *, int);
static void mtk_node_leave(struct ieee80211com *, struct ieee80211_node *);
static void mtk_updateslot(struct ieee80211com *);
static void mtk_updateedca(struct ieee80211com *);
static void mtk_updateprot(struct ieee80211com *);
static void mtk_update_chw(struct ieee80211com *);
static int  mtk_set_key(struct ieee80211com *, struct ieee80211_node *,
                        struct ieee80211_key *);
static void mtk_delete_key(struct ieee80211com *, struct ieee80211_node *,
                           struct ieee80211_key *);
static void mtk_fill_key_desc(const struct ieee80211_key *k,
                              struct mtk_key_desc *d);

/* ================================================================== */
/* channel table (itl80211 side)                                      */
/* ================================================================== */
static void
mtk_fill_channels(struct ieee80211com *ic, unsigned int bands)
{
    struct ieee80211_channel *c;
    unsigned int i;

    memset(ic->ic_channels, 0, sizeof(ic->ic_channels));

    if (bands & MTK_BAND_2GHZ) {
        for (i = 1; i <= 14; i++) {
            c = &ic->ic_channels[i];
            c->ic_freq = (i == 14) ? 2484 : 2407 + i * 5;
            c->ic_flags = IEEE80211_CHAN_CCK | IEEE80211_CHAN_OFDM |
                          IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ;
        }
    }
    if (bands & MTK_BAND_5GHZ) {
        /* UNII-1, UNII-2 (DFS), UNII-2e (DFS) and UNII-3. */
        for (i = 36; i <= 64; i += 4) {
            c = &ic->ic_channels[i];
            c->ic_freq = 5000 + i * 5;
            c->ic_flags = IEEE80211_CHAN_A;
            if (i >= 52) {
                c->ic_flags |= IEEE80211_CHAN_DFS | IEEE80211_CHAN_PASSIVE;
            }
        }
        for (i = 100; i <= 144; i += 4) {
            c = &ic->ic_channels[i];
            c->ic_freq = 5000 + i * 5;
            c->ic_flags = IEEE80211_CHAN_A |
                          IEEE80211_CHAN_DFS | IEEE80211_CHAN_PASSIVE;
        }
        for (i = 149; i <= 177; i += 4) {
            c = &ic->ic_channels[i];
            c->ic_freq = 5000 + i * 5;
            c->ic_flags = IEEE80211_CHAN_A;
        }
    }
    /* TODO(M6): 6 GHz channels (no itl80211 channel class yet). */
}

/* ================================================================== */
/* net80211 registration                                              */
/* ================================================================== */
extern "C" int
mtk_net80211_attach(struct mt76_softc *sc)
{
    struct ieee80211com *ic;
    struct _ifnet *ifp;

    if (!sc)
        return -1;

    memset(&g_n, 0, sizeof(g_n));
    g_n.sc = sc;
    g_n.mdev = mtk_get_mdev(sc);

    ic = &sc->sc_ic;
    ifp = IC2IFP(ic);

    /* ---- ieee80211com pre-attach state ---- */
    ic->ic_phytype = IEEE80211_T_OFDM;
    ic->ic_opmode = IEEE80211_M_STA;
    ic->ic_state = IEEE80211_S_INIT;

    /*
     * NB: IEEE80211_C_SCANALL(_BAND) are NOT set: the driver tunes one
     * channel per dwell (software scan), the stack must keep the
     * per-frame channel consistency checks enabled.
     */
    ic->ic_caps =
        IEEE80211_C_WEP |               /* WEP                          */
        IEEE80211_C_RSN |               /* WPA/RSN                      */
        IEEE80211_C_SHSLOT |            /* short slot time supported    */
        IEEE80211_C_SHPREAMBLE;         /* short preamble supported     */
    /* TODO(M5/M6): QoS/HT/VHT caps, PMGT, AMPDU. */

    /* Supported legacy rates (11b/g/a). */
    ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
    ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;
    ic->ic_sup_rates[IEEE80211_MODE_11A] = ieee80211_std_rateset_11a;

    /* Channel set (also what the scan walks). */
    mtk_fill_channels(ic, mtk_bands(g_n.mdev));
    ic->ic_ibss_chan = &ic->ic_channels[0];

    /* RSSI normalization: rxi_rssi is delivered 0..ic_max_rssi. */
    ic->ic_max_rssi = MTK_MAX_RSSI;

    /* MAC address recorded by the mt76 probe (mt76_phy.macaddr). */
    if (mtk_get_mac(g_n.mdev, (unsigned char *)ic->ic_myaddr) != 0 ||
        IEEE80211_ADDR_EQ(ic->ic_myaddr, etheranyaddr)) {
        /* No valid address: locally-administered placeholder. */
        memset(ic->ic_myaddr, 0, IEEE80211_ADDR_LEN);
        ic->ic_myaddr[0] = 0x02;
    }

    /* ---- ifp setup (mirrors ItlIwn::attach) ---- */
    ifp->if_softc = sc;
    ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST | IFF_DEBUG;
    ifp->if_ioctl = mtk_ioctl;
    ifp->if_start = mtk_start;
    ifp->if_watchdog = mtk_watchdog;
    strlcpy(ifp->if_xname, "mtk0", IFNAMSIZ);

    if_attach(ifp);
    ieee80211_ifattach(ifp, sc->sc_ether);

    /* ---- net80211 driver callbacks ---- */
    ic->ic_newassoc = mtk_newassoc;
    ic->ic_node_leave = mtk_node_leave;
    ic->ic_updateslot = mtk_updateslot;
    ic->ic_updateedca = mtk_updateedca;
    ic->ic_updateprot = mtk_updateprot;
    ic->ic_update_chw = mtk_update_chw;
    ic->ic_set_key = mtk_set_key;
    ic->ic_delete_key = mtk_delete_key;
    /* TODO(M5/M6): ic_ampdu_*, ic_bgscan_start, ic_event_handler. */

    /* Override the 802.11 state transition machine. */
    g_n.sc_newstate = ic->ic_newstate;
    ic->ic_newstate = mtk_newstate;

    ieee80211_media_init(ifp);

    /* Per-channel scan dwell timer. */
    timeout_set(&g_n.scan_to, mtk_scan_dwell, sc);

    XYLog("%s: net80211 attached (%s)\n", __FUNCTION__, ifp->if_xname);
    return 0;
}

/* ================================================================== */
/* enable/disable (if_init/if_stop semantics for the HAL)              */
/* ================================================================== */
extern "C" void
mtk_net80211_quiesce(void)
{
    /*
     * Make the scan machinery inert before teardown: stop new tunes and
     * delete the dwell timer so no callback can re-arm behind the unload
     * path ("取消后回调不再入队").  Idempotent.
     */
    g_n.stopping = 1;
    if (g_n.scan_to) {
        timeout_del(&g_n.scan_to);
        /*
         * Also free: a leaked CTimeout instance pins the kext
         * ("Can't unload kext ... class CTimeout has instances") and
         * blocks every later unload/reload cycle.
         */
        timeout_free(&g_n.scan_to);
    }
}

/*
 * Full teardown for the unload path (mirror of the mtk_net80211_attach
 * ifattach).  ieee80211_ifdetach releases ic_bgscan_timeout and
 * if_slowtimo — without it those CTimeout instances survive and
 * kmutil unload fails with "class CTimeout has instances".  Idempotent.
 */
extern "C" void
mtk_net80211_ifdetach(void)
{
    struct mt76_softc *sc = g_n.sc;
    struct ieee80211com *ic;
    struct _ifnet *ifp;

    if (!sc)
        return;

    ic = &sc->sc_ic;
    ifp = IC2IFP(ic);

    mtk_net80211_quiesce();
    ieee80211_ifdetach(ifp);

    g_n.sc = NULL;
    g_n.mdev = NULL;
    g_n.running = 0;
}

extern "C" int
mtk_net80211_enable(struct mt76_softc *sc)
{
    struct ieee80211com *ic;
    struct _ifnet *ifp;

    if (!sc || sc != g_n.sc)
        return -1;

    ic = &sc->sc_ic;
    ifp = IC2IFP(ic);

    if (g_n.running)
        return 0;

    g_n.stopping = 0;
    if (mtk_hw_start(g_n.mdev) != 0) {
        XYLog("%s: could not start the device\n", __FUNCTION__);
        return -1;
    }
    g_n.running = 1;

    ifq_clr_oactive(&ifp->if_snd);
    ifp->if_flags |= IFF_RUNNING;

    /* Reset to INIT then start the software scan loop. */
    ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
    ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
    return 0;
}

extern "C" void
mtk_net80211_disable(struct mt76_softc *sc)
{
    struct ieee80211com *ic;
    struct _ifnet *ifp;
    mbuf_t m;

    if (!sc || sc != g_n.sc)
        return;

    ic = &sc->sc_ic;
    ifp = IC2IFP(ic);

    if (!g_n.running)
        return;

    /*
     * Order matters: first make the scan machinery inert and abort any
     * in-flight MCU waiter (a blocked mt76_mcu_send_and_get_msg would
     * otherwise keep this teardown hostage for up to 3 s per command),
     * then drain the net80211/mt76 queues.
     */
    mtk_net80211_quiesce();
    mtk_mcu_abort(g_n.mdev, false);
    ieee80211_new_state(ic, IEEE80211_S_INIT, -1);

    ifp->if_flags &= ~IFF_RUNNING;
    ifq_clr_oactive(&ifp->if_snd);
    g_n.tx_timer = 0;

    /* Dropped mgmt frames own a node reference (ieee80211_mgmt_output). */
    while ((m = mq_dequeue(&ic->ic_mgtq)) != NULL) {
        struct ieee80211_node *ni =
            (struct ieee80211_node *)mbuf_pkthdr_rcvif(m);

        mbuf_freem(m);
        if (ni != NULL)
            ieee80211_release_node(ic, ni);
    }
    while ((m = mq_dequeue(&ic->ic_pwrsaveq)) != NULL)
        mbuf_freem(m);

    /* Drop anything still batched for if_input(). */
    while ((m = ml_dequeue(&g_n.ml)) != NULL)
        mbuf_freem(m);
    ml_init(&g_n.ml);

    /*
     * M5: complete everything still inside the mt76 TX pipeline (DMA /
     * token / TXS pending).  Each completed frame runs
     * mtk_net80211_tx_done() which frees the mbuf and releases its node
     * reference.
     */
    mtk_tx_flush(g_n.mdev);

    mtk_hw_stop(g_n.mdev);
    g_n.running = 0;
}

/* ================================================================== */
/* state machine                                                      */
/* ================================================================== */
static void
mtk_scan_tune(void)
{
    struct ieee80211com *ic = &g_n.sc->sc_ic;
    struct ieee80211_channel *c = ic->ic_bss->ni_chan;
    unsigned int freq;

    if (g_n.stopping)
        return;
    if (c == NULL)
        return;
    freq = c->ic_freq;
    if (freq == 0) {
        /* ieee80211_reset_scan()'s one-past-end sentinel. */
        return;
    }
    /* Scan hops are offchannel: TX sent from the dwell rides the
     * offchannel path and is not held back by phy->offchannel. */
    if (mtk_chan_switch(g_n.mdev, freq, 1) != 0)
        XYLog("%s: tune to %u MHz failed\n", __FUNCTION__, freq);
}

static void
mtk_scan_dwell(void *arg)
{
    struct mt76_softc *sc = (struct mt76_softc *)arg;
    struct ieee80211com *ic = &sc->sc_ic;

    if (g_n.stopping)
        return;
    if (ic->ic_state != IEEE80211_S_SCAN)
        return;
    /* Advance the channel bitmap; re-enters mtk_newstate() per hop and
     * ends in ieee80211_end_scan() when the map is exhausted. */
    ieee80211_next_scan(IC2IFP(ic));
}

static int
mtk_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
    if (nstate != IEEE80211_S_SCAN)
        timeout_del(&g_n.scan_to);

    /*
     * M6: RUN entry/exit drives the firmware BSS + WTBL state (the
     * bss_info_changed(BSS_CHANGED_ASSOC) / sta_state semantics of
     * Linux mt7921/main.c).  The firmware commands run before the
     * default handler chains (same order as ItlIwn::iwn_run), so the
     * datapath is programmed before the stack can queue EAPOL/data.
     */
    if (nstate == IEEE80211_S_RUN && ic->ic_state != IEEE80211_S_RUN) {
        struct ieee80211_node *ni = ic->ic_bss;
        unsigned int basic = 0;
        int i;

        for (i = 0; i < ni->ni_rates.rs_nrates; i++)
            if (ni->ni_rates.rs_rates[i] & IEEE80211_RATE_BASIC)
                basic |= 1u << i;

        /*
         * Fake a join (iwn_run pattern): net80211 never calls
         * ic_newassoc for the STA-mode BSS node, so create the WTBL /
         * STA record here, then program the BSS context.
         */
        mtk_newassoc(ic, ni, 1);
        if (mtk_bss_setup(g_n.mdev, ni->ni_bssid,
                          IEEE80211_AID(ni->ni_associd),
                          ni->ni_intval, ni->ni_dtimperiod,
                          (ni->ni_flags & IEEE80211_NODE_QOS) ? 1 : 0,
                          basic) != 0)
            XYLog("%s: BSS setup failed\n", __FUNCTION__);
    } else if (ic->ic_state == IEEE80211_S_RUN &&
               nstate != IEEE80211_S_RUN) {
        mtk_bss_teardown(g_n.mdev);
        mtk_sta_remove(g_n.mdev, ic->ic_bss);
    }

    if (nstate == IEEE80211_S_SCAN && ic->ic_state == IEEE80211_S_SCAN) {
        int error;

        /*
         * Per-channel hop: ieee80211_next_scan() has already updated
         * ic_bss->ni_chan.  Tune the radio and dwell for a while; the
         * dwell timeout advances to the next channel.
         *
         * M5: the mgmt TX path works, so chain to the default handler
         * as well - it sends the per-hop probe request (active scan,
         * IEEE80211_F_ASCAN).  The probe request is broadcast and
         * queued to ic_mgtq by ieee80211_mgmt_output(); mtk_start()
         * hands it to the mt76 TX path immediately.  On the second
         * (passive) sweep ieee80211_end_scan() has cleared ASCAN and
         * no probe requests are sent.
         */
        mtk_scan_tune();
        error = g_n.sc_newstate(ic, nstate, arg);
        /* Never re-arm behind a teardown. */
        if (!g_n.stopping)
            timeout_add_msec(&g_n.scan_to, MTK_SCAN_DWELL_MS);
        return error;
    }

    if (nstate == IEEE80211_S_SCAN) {
        int error;

        XYLog("%s: entering scan (from %d)\n", __FUNCTION__, ic->ic_state);
        /* Let the default handler bootstrap the scan (begin_scan ->
         * next_scan -> per-channel hops above).  begin_scan() sets
         * IEEE80211_F_ASCAN: the first sweep is an active scan with
         * per-hop probe requests (M5), the follow-up sweeps are
         * passive (see ieee80211_end_scan()). */
        error = g_n.sc_newstate(ic, nstate, arg);
        return error;
    }

    /* Everything else is net80211's default handler. */
    return g_n.sc_newstate(ic, nstate, arg);
}

/* ================================================================== */
/* RX lower half                                                      */
/* ================================================================== */
extern "C" void
mtk_net80211_rx(void *mdev, const void *frame, unsigned int len,
                const struct mtk_rx_meta *meta)
{
    struct mt76_softc *sc = g_n.sc;
    struct ieee80211com *ic;
    struct ieee80211_node *ni;
    struct ieee80211_rxinfo rxi;
    struct ieee80211_frame *wh;
    mbuf_t m;
    int rssi;

    if (!sc || mdev != g_n.mdev || frame == NULL || meta == NULL)
        return;
    if (len < 2)
        return;
    ic = &sc->sc_ic;

    if (meta->flags & MTK_RX_FLAG_FAILED_FCS) {
        ic->ic_stats.is_rx_decryptcrc++;    /* FCS/CRC error */
        return;
    }
    if (meta->flags & MTK_RX_FLAG_ONLY_MONITOR) {
        /* ICV/error frames: monitor only. */
        return;
    }
    if (meta->flags & MTK_RX_FLAG_8023) {
        /* M5: hardware RX header translation is disabled
         * (MT_MDP_DCR0_RX_HDR_TRANS_EN is not set in mt7921_mac_init),
         * so data frames arrive as 802.11 and this should not trigger.
         * Keep the guard as a safety net: 802.3 frames cannot go
         * through ieee80211_inputm(). */
        ic->ic_stats.is_rx_decap++;
        return;
    }

    /* Discard frames that are too short (control frames are shorter
     * than struct ieee80211_frame but are still valid). */
    if (len < sizeof(struct ieee80211_frame) &&
        (((const struct ieee80211_frame *)frame)->i_fc[0] &
         IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_CTL) {
        ic->ic_stats.is_rx_tooshort++;
        return;
    }

    /* skb -> mbuf copy. */
    if (mbuf_gethdr(MBUF_DONTWAIT, MT_DATA, &m) != 0 || m == NULL) {
        ic->ic_stats.is_rx_nombuf++;
        return;
    }
    if (len > mbuf_get_mhlen()) {
        mbuf_mclget(MBUF_DONTWAIT, MT_DATA, &m);
        if (!(mbuf_flags(m) & MBUF_EXT)) {
            mbuf_free(m);
            ic->ic_stats.is_rx_nombuf++;
            return;
        }
    }
    if (len > mbuf_len(m)) {
        /* > MCLBYTES frame (should not happen for mgmt).  TODO(M5):
         * chain a second cluster if jumbo frames must be supported. */
        mbuf_free(m);
        ic->ic_stats.is_rx_tooshort++;
        return;
    }
    memcpy(mtod(m, void *), frame, len);
    mbuf_pkthdr_setlen(m, len);
    mbuf_setlen(m, len);

    /*
     * Grab a reference to the source node (beacons from unknown APs
     * resolve to ic_bss; ieee80211_recv_probe_resp() creates the real
     * per-AP node entries in the tree from there).
     */
    wh = mtod(m, struct ieee80211_frame *);
    ni = ieee80211_find_rxnode(ic, wh);

    memset(&rxi, 0, sizeof(rxi));
    if (meta->flags & MTK_RX_FLAG_DECRYPTED) {
        rxi.rxi_flags |= IEEE80211_RXI_HWDEC;
        if (meta->flags & MTK_RX_FLAG_IV_STRIPPED) {
            /*
             * M6: the hardware removed the CCMP IV/MIC.  Clear the
             * Protected bit so ieee80211_input_hwdecrypt() takes its
             * "hardware stripped the IV, trust its replay check" path
             * instead of misparsing the payload as a CCMP header.
             * ieee80211_get_rxkey() still resolves the key (unicast:
             * ni_pairwise_key; group: key id fallback tolerates the
             * cleared bit).
             */
            struct ieee80211_frame *w = mtod(m, struct ieee80211_frame *);

            w->i_fc[1] &= ~IEEE80211_FC1_PROTECTED;
        }
    }
    rxi.rxi_tstamp = meta->timestamp;

    /* dBm -> normalized 0..ic_max_rssi (higher is better). */
    rssi = meta->rssi_dbm - MTK_MIN_DBM;
    if (rssi < 0)
        rssi = 0;
    if (rssi > ic->ic_max_rssi)
        rssi = ic->ic_max_rssi;
    rxi.rxi_rssi = rssi;

    rxi.rxi_chan = ieee80211_mhz2ieee(meta->freq, 0);
    if (rxi.rxi_chan > IEEE80211_CHAN_MAX)
        rxi.rxi_chan = 0;

    ieee80211_inputm(IC2IFP(ic), m, ni, &rxi, &g_n.ml);
    ieee80211_release_node(ic, ni);
}

extern "C" void
mtk_net80211_rx_flush(void *mdev)
{
    struct mt76_softc *sc = g_n.sc;
    struct _ifnet *ifp;

    if (!sc || mdev != g_n.mdev)
        return;
    if (ml_empty(&g_n.ml))
        return;

    ifp = IC2IFP(&sc->sc_ic);
    if (ifp->iface == NULL) {
        /* Not attached to the IOEthernetInterface yet. */
        mbuf_t m;

        while ((m = ml_dequeue(&g_n.ml)) != NULL)
            mbuf_freem(m);
        return;
    }
    if_input(ifp, &g_n.ml);
    ml_init(&g_n.ml);
}

/* ================================================================== */
/* TX completion (mt76 -> net80211)                                    */
/* ================================================================== */
extern "C" void
mtk_net80211_tx_done(const struct mtk_tx_cookie *ck, unsigned int status)
{
    struct mt76_softc *sc = g_n.sc;
    struct ieee80211com *ic;
    struct _ifnet *ifp;
    mbuf_t m = (mbuf_t)ck->m;
    struct ieee80211_node *ni = (struct ieee80211_node *)ck->ni;

    /*
     * The 802.11 ACK/retry is done by the firmware; here we only free
     * the mbuf, release the node reference and account for the frame.
     * Called from the NAPI/mt76 completion path (workloop context), the
     * same place ItlIwn frees its TX ring mbufs.
     */
    if (sc == NULL || ck->mdev != g_n.mdev) {
        /* Detached: drop what we can without touching net80211. */
        if (m != NULL)
            mbuf_freem(m);
        return;
    }
    ic = &sc->sc_ic;
    ifp = IC2IFP(ic);

    if (status & MTK_TX_DONE_FAILED) {
        /* TXS reported no 802.11 ACK (firmware exhausted its retries).
         * itl80211 keeps no per-node ni_tx_* counters, so account the
         * failure on the interface counter like ItlIwn does. */
        ifp->netStat->outputErrors++;
    }

    if (m != NULL)
        mbuf_freem(m);
    if (ni != NULL)
        ieee80211_release_node(ic, ni);
}

/* ================================================================== */
/* ifp callbacks                                                      */
/* ================================================================== */
static int
mtk_ioctl(struct _ifnet *ifp, u_long cmd, caddr_t data)
{
    struct ieee80211com *ic = (struct ieee80211com *)ifp;
    int error = 0;
    int s = splnet();

    switch (cmd) {
    case SIOCSIFADDR:
        ifp->if_flags |= IFF_UP;
        /* FALLTHROUGH */
    case SIOCSIFFLAGS:
        if (ifp->if_flags & IFF_UP) {
            if (!(ifp->if_flags & IFF_RUNNING))
                error = mtk_net80211_enable(g_n.sc) ? EIO : 0;
        } else {
            if (ifp->if_flags & IFF_RUNNING)
                mtk_net80211_disable(g_n.sc);
        }
        break;

    default:
        error = ieee80211_ioctl(ifp, cmd, data);
        break;
    }

    splx(s);
    return error;
}

/*
 * TX drain: dequeue pending management frames (ic_mgtq) and data frames
 * (if_snd) and hand them to the mt76 TX path.  Mirrors ItlIwn's
 * _iwn_start_task(): mgmt first, data only in RUN state, mbuf freed and
 * node released on the completion callback (mtk_net80211_tx_done).
 */
static IOReturn
mtk_start_task(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3)
{
    struct _ifnet *ifp = (struct _ifnet *)arg0;
    struct ieee80211com *ic = (struct ieee80211com *)ifp;
    struct ieee80211_node *ni;
    struct mtk_tx_cookie ck;
    mbuf_t m;
    unsigned int flags, prio, len;
    u_int8_t type;

    (void)owner; (void)arg1; (void)arg2; (void)arg3;

    if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
        return kIOReturnError;

    for (;;) {
        if (g_n.running == 0)
            break;

        /* Send pending management frames first (probe request, auth,
         * assoc, deauth, ...).  The node reference rides in rcvif and
         * is ours to release (ieee80211_mgmt_output contract). */
        m = mq_dequeue(&ic->ic_mgtq);
        if (m != NULL) {
            ni = (struct ieee80211_node *)mbuf_pkthdr_rcvif(m);
            goto sendit;
        }

        /* Encapsulate and send data frames. */
        if (ic->ic_state != IEEE80211_S_RUN ||
            (ic->ic_xflags & IEEE80211_F_TX_MGMT_ONLY))
            break;
        m = ifq_dequeue(&ifp->if_snd);
        if (m == NULL)
            break;
        if ((m = ieee80211_encap(ifp, m, &ni)) == NULL)
            continue;   /* freed + node released by ieee80211_encap() */

sendit:
        len = mbuf_pkthdr_len(m);
        if (ni == NULL || len < sizeof(struct ieee80211_frame) ||
            len > 4096) {
            mbuf_freem(m);
            if (ni != NULL)
                ieee80211_release_node(ic, ni);
            ifp->netStat->outputErrors++;
            continue;
        }

        /* The TX bridge copies from one contiguous buffer; fold a
         * multi-segment mbuf chain into a single cluster mbuf. */
        if (mbuf_next(m) != NULL) {
            mbuf_t m0 = NULL;

            if (mbuf_gethdr(MBUF_DONTWAIT, MT_DATA, &m0) != 0 || m0 == NULL) {
                mbuf_freem(m);
                ieee80211_release_node(ic, ni);
                ifp->netStat->outputErrors++;
                continue;
            }
            if (len > mbuf_get_mhlen()) {
                mbuf_mclget(MBUF_DONTWAIT, MT_DATA, &m0);
                if (!(mbuf_flags(m0) & MBUF_EXT)) {
                    mbuf_freem(m0);
                    mbuf_freem(m);
                    ieee80211_release_node(ic, ni);
                    ifp->netStat->outputErrors++;
                    continue;
                }
            }
            if (len > mbuf_len(m0)) {
                mbuf_freem(m0);
                mbuf_freem(m);
                ieee80211_release_node(ic, ni);
                ifp->netStat->outputErrors++;
                continue;
            }
            mbuf_copydata(m, 0, len, mtod(m0, void *));
            mbuf_pkthdr_setlen(m0, len);
            mbuf_setlen(m0, len);
            mbuf_pkthdr_setrcvif(m0, mbuf_pkthdr_rcvif(m));
            mbuf_freem(m);
            m = m0;
        }

        /*
         * Classify the frame for the TX bridge: mgmt frames ride the
         * PSD/ALTX queue (MTK_TX_FLAG_MGMT), unicast mgmt asks for
         * per-frame TX status (TXS -> M6 needs auth/assoc feedback),
         * broadcast mgmt (probe request) and QoS data map the AC from
         * the TID.
         */
        {
            const struct ieee80211_frame *wh =
                mtod(m, const struct ieee80211_frame *);

            type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
            flags = 0;
            prio = 0;
            if (type == IEEE80211_FC0_TYPE_MGT) {
                flags |= MTK_TX_FLAG_MGMT | MTK_TX_FLAG_OFFCHAN;
                if (IEEE80211_IS_MULTICAST(wh->i_addr1))
                    flags |= MTK_TX_FLAG_NO_ACK;
                else
                    flags |= MTK_TX_FLAG_STATUS;
            } else if (type == IEEE80211_FC0_TYPE_DATA) {
                if (ieee80211_has_qos(wh))
                    prio = ieee80211_get_qos(wh) & 0x7;
            } else {
                /* control frames are not injected by net80211 */
                mbuf_freem(m);
                ieee80211_release_node(ic, ni);
                ifp->netStat->outputErrors++;
                continue;
            }
        }

        ck.mdev = g_n.mdev;
        ck.ni = ni;
        {
            /*
             * M6 TX crypto (iwn_tx pattern): net80211 marks protected
             * frames but leaves them in the clear.  Keys flagged
             * IEEE80211_KEY_SWCRYPTO are encrypted here; everything
             * else (installed to the firmware WTBL via mtk_key_add())
             * is encrypted per-frame by the firmware - the key
             * reference rides in the TX info so the TXD gets
             * MT_TXD3_PROTECT_FRAME.
             */
            struct mtk_key_desc kd;
            const struct mtk_key_desc *hwkey = NULL;
            const struct ieee80211_frame *wh2 =
                mtod(m, const struct ieee80211_frame *);

            if ((wh2->i_fc[1] & IEEE80211_FC1_PROTECTED) != 0) {
                struct ieee80211_key *k =
                    ieee80211_get_txkey(ic, wh2, ni);

                if (k != NULL && (k->k_flags & IEEE80211_KEY_SWCRYPTO)) {
                    m = ieee80211_encrypt(ic, m, k);
                    if (m == NULL)
                        continue;   /* freed by ieee80211_encrypt() */
                    len = mbuf_pkthdr_len(m);
                } else if (k != NULL) {
                    mtk_fill_key_desc(k, &kd);
                    hwkey = &kd;
                }
            }
            ck.m = m;

            if (mtk_tx_frame(g_n.mdev, &ck, mtod(m, const void *), len,
                             flags, prio, hwkey) != 0) {
                /* Not accepted: no completion callback will come. */
                mbuf_freem(m);
                ieee80211_release_node(ic, ni);
                ifp->netStat->outputErrors++;
                continue;
            }
        }
        ifp->netStat->outputPackets++;

        g_n.tx_timer = 5;
        ifp->if_timer = 1;
    }

    return kIOReturnSuccess;
}

static void
mtk_start(struct _ifnet *ifp)
{
    /*
     * Serialize with the interrupt bottom half / net80211 timers by
     * running the drain on the main command gate (both live on the main
     * workloop).  Falls back to a direct call when the gate is not up
     * yet (early scan frames).
     */
    if (_fCommandGate != NULL) {
        _fCommandGate->runAction(mtk_start_task, ifp, NULL, NULL, NULL);
    } else {
        mtk_start_task(NULL, ifp, NULL, NULL, NULL);
    }
}

static void
mtk_watchdog(struct _ifnet *ifp)
{
    ifp->if_timer = 0;

    if (g_n.tx_timer > 0) {
        if (--g_n.tx_timer == 0) {
            XYLog("%s: TX timeout (device not completing frames)\n",
                  __FUNCTION__);
            ifp->netStat->outputErrors++;
        } else {
            ifp->if_timer = 1;
        }
    }

    /* Reap stale TX-status entries and give the TX scheduler a round. */
    mtk_tx_status_check(g_n.mdev);

    ieee80211_watchdog(ifp);
}

/* ================================================================== */
/* net80211 driver callbacks                                          */
/* ================================================================== */

/*
 * net80211 key struct -> bridge key description.  Only the ciphers the
 * MT7921 firmware offloads are mapped; anything else (or anything the
 * firmware rejects) falls back to net80211 software crypto via
 * ieee80211_set_key() (which sets IEEE80211_KEY_SWCRYPTO).
 */
static void
mtk_fill_key_desc(const struct ieee80211_key *k, struct mtk_key_desc *d)
{
    memset(d, 0, sizeof(*d));
    switch (k->k_cipher) {
    case IEEE80211_CIPHER_WEP40:
        d->cipher = MTK_KEY_CIPHER_WEP40;
        break;
    case IEEE80211_CIPHER_TKIP:
        d->cipher = MTK_KEY_CIPHER_TKIP;
        break;
    case IEEE80211_CIPHER_CCMP:
        d->cipher = MTK_KEY_CIPHER_CCMP;
        break;
    case IEEE80211_CIPHER_WEP104:
        d->cipher = MTK_KEY_CIPHER_WEP104;
        break;
    case IEEE80211_CIPHER_BIP:
        d->cipher = MTK_KEY_CIPHER_BIP;
        break;
    default:
        d->cipher = MTK_KEY_CIPHER_NONE;
        break;
    }
    d->key_id = k->k_id;
    d->key_len = k->k_len > 32 ? 32 : k->k_len;
    if (k->k_flags & IEEE80211_KEY_GROUP)
        d->flags |= MTK_KEY_FLAG_GROUP;
    if (k->k_flags & IEEE80211_KEY_TX)
        d->flags |= MTK_KEY_FLAG_TX;
    if (k->k_flags & IEEE80211_KEY_IGTK)
        d->flags |= MTK_KEY_FLAG_IGTK;
    memcpy(d->key, k->k_key, d->key_len);
}

static void
mtk_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni, int isnew)
{
    (void)isnew;

    /*
     * M6: build the WTBL / STA record.  Only the BSS node matters in
     * STA mode (scan-time RX nodes never get a WTBL entry).
     */
    if (ni != ic->ic_bss)
        return;
    if (mtk_sta_add(g_n.mdev, ni, ni->ni_macaddr,
                    IEEE80211_AID(ni->ni_associd),
                    ni->ni_rates.rs_rates, ni->ni_rates.rs_nrates) != 0)
        XYLog("%s: sta add failed\n", __FUNCTION__);
}

static void
mtk_node_leave(struct ieee80211com *ic, struct ieee80211_node *ni)
{
    (void)ic;
    /* M6: WTBL teardown for bound nodes (no-op for scan nodes). */
    mtk_sta_remove(g_n.mdev, ni);
}

static void
mtk_updateslot(struct ieee80211com *ic)
{
    if (ic->ic_state != IEEE80211_S_RUN)
        return;
    /* Short/long slot time -> mt792x_mac_set_timeing(). */
    mtk_set_slot(g_n.mdev, (ic->ic_flags & IEEE80211_F_SHSLOT) ? 1 : 0);
}

static void
mtk_updateedca(struct ieee80211com *ic)
{
    struct mtk_edca_ac ac[4];
    /* WME_AC_{BE,BK,VI,VO} -> MTK_AC_{BE,BK,VI,VO} numbering. */
    static const int wme2mtk[4] = { MTK_AC_BE, MTK_AC_BK, MTK_AC_VI,
                                    MTK_AC_VO };
    int i;

    if (ic->ic_state != IEEE80211_S_RUN)
        return;
    for (i = 0; i < 4; i++) {
        const struct ieee80211_edca_ac_params *p = &ic->ic_edca_ac[i];
        struct mtk_edca_ac *d = &ac[wme2mtk[i]];

        d->cw_min = (1u << (p->ac_ecwmin & 0xf)) - 1;
        d->cw_max = (1u << (p->ac_ecwmax & 0xf)) - 1;
        d->aifs = p->ac_aifsn;
        d->txop = p->ac_txoplimit;
        d->acm = p->ac_acm;
    }
    mtk_set_edca(g_n.mdev, ac);
}

static void
mtk_updateprot(struct ieee80211com *ic)
{
    (void)ic;
    /*
     * TODO(M6): 11g protection mode.  mt7921 firmware handles ERP
     * protection internally; this tree has no MCU command for it
     * (no mt7921_mcu_set_timing/set_prot in mt7921/mcu.c).
     */
}

static void
mtk_update_chw(struct ieee80211com *ic)
{
    (void)ic;
    /*
     * TODO(M6): 20/40 MHz channel width change.  The port runs 20 MHz
     * no-HT only; reissuing the channel context would go through
     * mt76_connac_mcu_uni_add_bss() when HT is enabled.
     */
}

static int
mtk_set_key(struct ieee80211com *ic, struct ieee80211_node *ni,
            struct ieee80211_key *k)
{
    struct mtk_key_desc d;
    void *key_ni;
    int ret;

    mtk_fill_key_desc(k, &d);
    if (d.cipher == MTK_KEY_CIPHER_NONE)
        return ieee80211_set_key(ic, ni, k);    /* software crypto */

    /* PTKs ride the peer node; GTK/IGTK ride the BSS wcid. */
    key_ni = (k->k_flags & (IEEE80211_KEY_GROUP | IEEE80211_KEY_IGTK)) ?
             NULL : (void *)ni;
    ret = mtk_key_add(g_n.mdev, key_ni, &d);
    if (ret == -EOPNOTSUPP || ret == -ENXIO || ret == -ENOENT)
        return ieee80211_set_key(ic, ni, k);    /* software crypto */
    /* itl80211 convention: 0 / EBUSY / positive errno. */
    return ret < 0 ? -ret : ret;
}

static void
mtk_delete_key(struct ieee80211com *ic, struct ieee80211_node *ni,
               struct ieee80211_key *k)
{
    struct mtk_key_desc d;
    void *key_ni;

    if (k->k_flags & IEEE80211_KEY_SWCRYPTO) {
        ieee80211_delete_key(ic, ni, k);
        return;
    }
    mtk_fill_key_desc(k, &d);
    key_ni = (k->k_flags & (IEEE80211_KEY_GROUP | IEEE80211_KEY_IGTK)) ?
             NULL : (void *)ni;
    if (mtk_key_delete(g_n.mdev, key_ni, &d) != 0)
        XYLog("%s: key delete failed (id %u)\n", __FUNCTION__, d.key_id);
}
