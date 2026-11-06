/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Ported for mt76wm (XNU) */
/*
 * mt76x02_trace.h - stub tracepoints for the mt76wm XNU port
 * (tracing disabled).  The Linux original wires TRACE_EVENT classes into
 * ftrace; the kext build has no tracepoint machinery, so every trace_*
 * call site compiles to a no-op.  mt76x02_trace.c is not ported.
 */
#ifndef __MT76x02_TRACE_H
#define __MT76x02_TRACE_H

#define trace_mac_txstat_poll(dev)		do { (void)(dev); } while (0)
#define trace_mac_txstat_fetch(dev, stat)	do { (void)(dev); (void)(stat); } while (0)

#endif /* __MT76x02_TRACE_H */
