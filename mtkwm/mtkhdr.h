/*
 * mt76wm - MediaTek Wi-Fi driver for macOS
 * Copyright (C) 2026 mt76wm contributors
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

/*
 * mtkhdr.h - common BSD/XNU kernel shims aggregated for the mtkwm kext.
 * Adapted from itlwm's itlhdr.h (Copyright (C) 2020 钟先耀, GPL-2.0),
 * with the Intel iwm-specific bits removed.
 */

#ifndef mtkhdr_h
#define mtkhdr_h

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/kpi_mbuf.h>
#include <sys/pcireg.h>

#define DEVNAME(_s)    ("mtkwm")

#define M_DEVBUF 2
#define M_WAIT 3
#define DELAY IODelay

#define    INFSLP    UINT64_MAX

#define IC2IFP(_ic_) (&(_ic_)->ic_if)
#define LLADDR(s) ((caddr_t)((s)->sdl_data + (s)->sdl_nlen))

/*
 * RSSI floor used by the ClientKit/HeliPort reporting convention
 * (kept the itlwm name so the copied UserClient code stays unchanged).
 */
#define IWM_MIN_DBM    -100

#endif /* mtkhdr_h */
