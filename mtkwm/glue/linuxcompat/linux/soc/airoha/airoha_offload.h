/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_AIROHA_OFFLOAD_H
#define MTK_LC_AIROHA_OFFLOAD_H

#include <linux/mtk_core.h>
#include <linux/regmap.h>

/* Airoha PPE/NPU offload - not supported on the XNU port. */

enum airoha_npu_wlan_set_cmd {
    AIROHA_NPU_WLAN_SET_CMD_TXRX_ADDR,
    AIROHA_NPU_WLAN_SET_CMD_NUM,
};

enum airoha_npu_wlan_get_cmd {
    AIROHA_NPU_WLAN_GET_CMD_NUM,
};

struct airoha_ppe_dev {
    int dummy;
};

struct airoha_npu_tx_dma_desc {
    u32 data[8];
};

struct airoha_npu_rx_dma_desc {
    u32 data[8];
};

struct airoha_npu {
    struct regmap *regmap;
    int dummy;
};

static inline int airoha_npu_wlan_send_msg(struct airoha_npu *npu, int ifindex,
                                           enum airoha_npu_wlan_set_cmd cmd,
                                           u32 *val, int len, gfp_t gfp)
{
    (void)npu; (void)ifindex; (void)cmd; (void)val; (void)len; (void)gfp;
    return -ENODEV;
}

static inline int airoha_npu_wlan_get_msg(struct airoha_npu *npu, int ifindex,
                                          enum airoha_npu_wlan_get_cmd cmd,
                                          u32 *val, int len, gfp_t gfp)
{
    (void)npu; (void)ifindex; (void)cmd; (void)val; (void)len; (void)gfp;
    return -ENODEV;
}

static inline void airoha_npu_wlan_enable_irq(struct airoha_npu *npu, int q)
{
    (void)npu; (void)q;
}

#endif
