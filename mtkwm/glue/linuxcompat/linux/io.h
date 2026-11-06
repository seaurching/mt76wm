/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_IO_H
#define MTK_LC_IO_H

#include <linux/mtk_core.h>
#include <libkern/OSByteOrder.h>

/*
 * MMIO access on x86: the BAR mapping is little-endian; use the
 * OSRead/WriteLittleIntXX helpers.  mt76 goes through mt76_bus_ops
 * (mmio.c) so these are the only register primitives needed.
 */
static inline u32 readl(const volatile void *addr)
{
    return (u32)OSReadLittleInt32((void *)addr, 0);
}

static inline void writel(u32 val, volatile void *addr)
{
    OSWriteLittleInt32((void *)addr, 0, val);
}

static inline u16 readw(const volatile void *addr)
{
    return (u16)OSReadLittleInt16((void *)addr, 0);
}

static inline void writew(u16 val, volatile void *addr)
{
    OSWriteLittleInt16((void *)addr, 0, val);
}

static inline u8 readb(const volatile void *addr)
{
    return *(const volatile u8 *)addr;
}

static inline void writeb(u8 val, volatile void *addr)
{
    *(volatile u8 *)addr = val;
}

#define ioread8(a)      readb(a)
#define ioread16(a)     readw(a)
#define ioread32(a)     readl(a)
#define iowrite8(v, a)  writeb((v), (a))
#define iowrite16(v, a) writew((v), (a))
#define iowrite32(v, a) writel((v), (a))

#define readl_relaxed(a)    readl(a)
#define writel_relaxed(v, a) writel((v), (a))
#define readw_relaxed(a)    readw(a)
#define writew_relaxed(v, a) writew((v), (a))

static inline void *ioremap(unsigned long offset, unsigned long size)
{
    (void)size;
    return (void *)offset;
}
#define ioremap_wc(o, s) ioremap((o), (s))
#define iounmap(addr) do { (void)(addr); } while (0)

#define memset_io(d, c, n)  memset((d), (c), (n))
#define memcpy_fromio(d, s, n) memcpy((d), (s), (n))
#define memcpy_toio(d, s, n)   memcpy((d), (s), (n))

#endif /* MTK_LC_IO_H */
