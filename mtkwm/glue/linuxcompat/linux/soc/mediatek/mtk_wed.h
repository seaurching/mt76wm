/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_MTK_WED_H
#define MTK_LC_MTK_WED_H

#include <linux/mtk_core.h>

/* Wireless Ethernet Dispatch (hardware offload) - not supported; the
 * struct is embedded by value in mt76_mmio so it must be complete. */

#define WED_WO_STA_REC     0x6
#define WED_WO_STA_REC_MAX  0x7

enum tc_setup_type {
    TC_SETUP_QDISC_MQPRIO,
    TC_SETUP_BLOCK,
};

struct mtk_wed_device {
    struct {
        struct mtk_wed_device *hif2;
        int wcid0;              /* placeholder */
    } wlan;
    int dummy;
};

static inline bool mtk_wed_device_active(struct mtk_wed_device *wed)
{
    (void)wed;
    return false;
}

static inline u32 mtk_wed_device_reg_read(struct mtk_wed_device *wed, u32 reg)
{
    (void)wed; (void)reg;
    return 0;
}

static inline void mtk_wed_device_reg_write(struct mtk_wed_device *wed,
                                            u32 reg, u32 val)
{
    (void)wed; (void)reg; (void)val;
}

static inline void mtk_wed_device_irq_set_mask(struct mtk_wed_device *wed,
                                               u32 mask)
{
    (void)wed; (void)mask;
}

static inline int mtk_wed_device_update_msg(struct mtk_wed_device *wed,
                                            int cmd, const void *data,
                                            int len)
{
    (void)wed; (void)cmd; (void)data; (void)len;
    return -ENODEV;
}

static inline void mtk_wed_device_detach(struct mtk_wed_device *wed)
{
    (void)wed;
}

static inline u32 mtk_wed_get_rx_capa(struct mtk_wed_device *wed)
{
    (void)wed;
    return 0;
}

/* ---- stubs referenced by the mt7915 port (WED disabled, but the dead
 * branches must link; all are no-ops since mtk_wed_device_active() is
 * always false). ---- */
static inline u32 mtk_wed_device_irq_get(struct mtk_wed_device *wed, u32 mask)
{
    (void)wed; (void)mask;
    return 0;
}

static inline void mtk_wed_device_start(struct mtk_wed_device *wed, u32 mask)
{
    (void)wed; (void)mask;
}

static inline void mtk_wed_device_stop(struct mtk_wed_device *wed)
{
    (void)wed;
}

static inline void mtk_wed_device_dma_reset(struct mtk_wed_device *wed)
{
    (void)wed;
}

static inline int mtk_wed_device_attach(struct mtk_wed_device *wed)
{
    (void)wed;
    return -ENODEV;
}

static inline void mtk_wed_device_ppe_check(struct mtk_wed_device *wed,
                                            struct sk_buff *skb,
                                            u32 reason, u32 entry)
{
    (void)wed; (void)skb; (void)reason; (void)entry;
}

/* WED hardware RRO (mt7996/dma.c); never active in the port. */
static inline void mtk_wed_device_start_hw_rro(struct mtk_wed_device *wed,
                                               u32 mask, bool reset)
{
    (void)wed; (void)mask; (void)reset;
}

#endif
