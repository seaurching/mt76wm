/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * trace.h - no-op tracepoints for the mt76wm XNU port (Linux trace
 * infrastructure is dropped, see docs/porting-notes.md).
 */
#ifndef __MT7601U_TRACE_H
#define __MT7601U_TRACE_H

#include "mac.h"

struct mt7601u_dev;
struct urb;
struct sk_buff;
struct mt7601u_rxwi;
struct mt76_txwi;
struct mt76_sta;

#define trace_reg_read(dev, offset, val)                        do { } while (0)
#define trace_reg_write(dev, offset, val)                       do { } while (0)
#define trace_rf_read(dev, bank, offset, ret)                   do { } while (0)
#define trace_rf_write(dev, bank, offset, value)                do { } while (0)
#define trace_bbp_read(dev, offset, ret)                        do { } while (0)
#define trace_bbp_write(dev, offset, val)                       do { } while (0)
#define trace_read_temp(dev, temp)                              do { } while (0)
#define trace_temp_mode(dev, mode)                              do { } while (0)
#define trace_freq_cal_offset(dev, phy_mode, last_offset)       do { } while (0)
#define trace_freq_cal_adjust(dev, freq)                        do { } while (0)
#define trace_set_key(dev, idx)                                 do { } while (0)
#define trace_set_shared_key(dev, vif_idx, key_idx)             do { } while (0)
#define trace_mt_mcu_msg_send(dev, skb, csum, need_resp)        do { } while (0)
#define trace_mt_submit_urb(dev, urb)                           do { } while (0)
#define trace_mt_submit_urb_sync(dev, pipe, len)                do { } while (0)
#define trace_mt_vend_req(dev, pipe, req, req_type, val, offset, \
                          buf, buflen, ret)                     do { } while (0)
#define trace_mt_rx(dev, rxwi, fce_info)                        do { } while (0)
#define trace_mt_rx_dma_aggr(dev, cnt, copy)                    do { } while (0)
#define trace_mt_tx(dev, skb, msta, txwi)                       do { } while (0)
#define trace_mt_tx_dma_done(dev, skb)                          do { } while (0)
#define trace_mt_tx_status_cleaned(dev, cleaned)                do { } while (0)

#endif /* __MT7601U_TRACE_H */
