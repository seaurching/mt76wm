/* SPDX-License-Identifier: GPL-2.0 */
/*
 * trace.h - stub tracepoints for the mt76wm XNU port (tracing disabled).
 * Ported for mt76wm (XNU)
 */
#ifndef __MT76_TRACE_H
#define __MT76_TRACE_H

#define trace_reg_rr(dev, offset, val)  do { (void)(dev); (void)(offset); (void)(val); } while (0)
#define trace_reg_wr(dev, offset, val)  do { (void)(dev); (void)(offset); (void)(val); } while (0)
#define trace_reg_rmw(dev, offset, mask, val) do { } while (0)

/* mt7603 (and other families) IRQ/TX-done tracepoints */
#define trace_dev_irq(dev, intr, mask) \
	do { (void)(dev); (void)(intr); (void)(mask); } while (0)
#define trace_mac_txdone(dev, wcid, pktid) \
	do { (void)(dev); (void)(wcid); (void)(pktid); } while (0)

#endif /* __MT76_TRACE_H */
