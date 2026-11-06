/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt7615_trace.h - stub tracepoints for the mt76wm XNU port (tracing
 * disabled; the Linux mt7615_trace.h TRACE_EVENT declarations are not
 * ported).
 *
 * Ported for mt76wm (XNU)
 */
#ifndef __MT7615_TRACE_H
#define __MT7615_TRACE_H

#include "../mt76core/trace.h"

#define trace_mac_tx_free(dev, token) \
	do { (void)(dev); (void)(token); } while (0)

#endif /* __MT7615_TRACE_H */
