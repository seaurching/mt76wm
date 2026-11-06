// Ported for mt76wm (XNU).
// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */

#include "mt76.h"
#include "dma.h"
#include "trace.h"

/*
 * Register access routing.
 *
 * The MT7921 register file spans ~2 GB (TOP 0x18060000, WPDMA0 0x54000000,
 * PCIe MAC 0x74030000, CONN_ON 0x7c060000, ...) but the host PCIe BAR
 * window on macOS is only 1 MB -- direct accesses past it read 0xffffffff
 * (or page-fault on a truncated mapping).  The chip provides a standard
 * escape hatch: the HIF L1 remap window.  Writing the target's 64K page
 * number into MT_HIF_REMAP_L1 maps that page at MT_HIF_REMAP_BASE_L1
 * (0xe0000), inside the low 1 MB window.  This is the same mechanism
 * mt7921_reg_map_l1() uses for the chip-id.
 */
#define MTK_MMIO_LOW_WINDOW	0x100000
#define MTK_HIF_REMAP_L1	0xf11ac
#define MTK_HIF_REMAP_BASE_L1	0xe0000

static spinlock_t mtk_remap_lock;

static void mtk_l1_remap(struct mt76_dev *dev, u32 offset)
{
	u8 *regs = (u8 *)dev->mmio.regs;
	u32 base = offset >> 16;

	/* direct low-segment accesses -- must not recurse through bus ops */
	writel((readl(regs + MTK_HIF_REMAP_L1) & 0xffff0000) | base,
	       regs + MTK_HIF_REMAP_L1);
	readl(regs + MTK_HIF_REMAP_L1);	/* push the write */
}

static u32 mt76_mmio_rr(struct mt76_dev *dev, u32 offset)
{
	u32 val;

	/* EXPERIMENT (sem-bisection): plain direct read like build 11, with
	 * a fail-safe above the 1 MB BAR window instead of L1 remapping. */
	if (offset < MTK_MMIO_LOW_WINDOW) {
		val = readl((u8 *)dev->mmio.regs + offset);
	} else {
		static int warned;
		if (warned++ < 8)
			pr_err("mtk-dbg: mmio_rr 0x%x above window, fail-safe 0xffffffff\n", offset);
		val = 0xffffffff;
	}
	trace_reg_rr(dev, offset, val);

	return val;
}

static void mt76_mmio_wr(struct mt76_dev *dev, u32 offset, u32 val)
{
	trace_reg_wr(dev, offset, val);
	if (offset < MTK_MMIO_LOW_WINDOW) {
		writel(val, (u8 *)dev->mmio.regs + offset);
	} else {
		static int warned;
		if (warned++ < 8)
			pr_err("mtk-dbg: mmio_wr 0x%x above window, dropped\n", offset);
	}
}

static u32 mt76_mmio_rmw(struct mt76_dev *dev, u32 offset, u32 mask, u32 val)
{
	val |= mt76_mmio_rr(dev, offset) & ~mask;
	mt76_mmio_wr(dev, offset, val);
	return val;
}

static void mt76_mmio_write_copy(struct mt76_dev *dev, u32 offset,
				 const void *data, int len)
{
	int i;

	for (i = 0; i + 4 <= len; i += 4)
		mt76_mmio_wr(dev, offset + i, get_unaligned_le32((u8 *)data + i));

	if (i < len) {
		u8 tmp[4] = {};

		memcpy(tmp, (u8 *)data + i, len - i);
		mt76_mmio_wr(dev, offset + i, get_unaligned_le32(tmp));
	}
}

static void mt76_mmio_read_copy(struct mt76_dev *dev, u32 offset,
				void *data, int len)
{
	int i;

	for (i = 0; i + 4 <= len; i += 4)
		put_unaligned_le32(mt76_mmio_rr(dev, offset + i),
				   (u8 *)data + i);

	if (i < len) {
		u8 tmp[4];

		put_unaligned_le32(mt76_mmio_rr(dev, offset + i), tmp);
		memcpy((u8 *)data + i, tmp, len - i);
	}
}

static int mt76_mmio_wr_rp(struct mt76_dev *dev, u32 base,
			   const struct mt76_reg_pair *data, int len)
{
	while (len > 0) {
		mt76_mmio_wr(dev, data->reg, data->value);
		data++;
		len--;
	}

	return 0;
}

static int mt76_mmio_rd_rp(struct mt76_dev *dev, u32 base,
			   struct mt76_reg_pair *data, int len)
{
	while (len > 0) {
		data->value = mt76_mmio_rr(dev, data->reg);
		data++;
		len--;
	}

	return 0;
}

void mt76_set_irq_mask(struct mt76_dev *dev, u32 addr,
		       u32 clear, u32 set)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->mmio.irq_lock, flags);
	dev->mmio.irqmask &= ~clear;
	dev->mmio.irqmask |= set;
	if (addr) {
		if (mtk_wed_device_active(&dev->mmio.wed))
			mtk_wed_device_irq_set_mask(&dev->mmio.wed,
						    dev->mmio.irqmask);
		else
			mt76_mmio_wr(dev, addr, dev->mmio.irqmask);
	}
	spin_unlock_irqrestore(&dev->mmio.irq_lock, flags);
}
EXPORT_SYMBOL_GPL(mt76_set_irq_mask);

/*
 * Register-window registry.  The HAL builds the windows before probing
 * (mt76_mmio_init imports them into every mt76_dev).  See mtk_regwin in
 * mt76.h for the why.
 */
static struct mtk_regwin mtk_regwins[6];
static int mtk_nregwins;
static u32 mtk_main_len;

void mtk_mmio_register_windows(const void *wins, int n, u32 main_len)
{
	const struct mtk_regwin *w = (const struct mtk_regwin *)wins;
	int i;

	mtk_nregwins = 0;
	mtk_main_len = main_len;
	for (i = 0; i < n && i < ARRAY_SIZE(mtk_regwins); i++) {
		mtk_regwins[i] = w[i];
		mtk_nregwins++;
	}
}

void mt76_mmio_init(struct mt76_dev *dev, void __iomem *regs)
{
	static const struct mt76_bus_ops mt76_mmio_ops = {
		.rr = mt76_mmio_rr,
		.rmw = mt76_mmio_rmw,
		.wr = mt76_mmio_wr,
		.write_copy = mt76_mmio_write_copy,
		.read_copy = mt76_mmio_read_copy,
		.wr_rp = mt76_mmio_wr_rp,
		.rd_rp = mt76_mmio_rd_rp,
		.type = MT76_BUS_MMIO,
	};

	dev->mmio.regs = regs;
	dev->mmio.main_len = mtk_main_len;
	dev->mmio.n_wins = mtk_nregwins;
	memcpy(dev->mmio.wins, mtk_regwins, sizeof(mtk_regwins[0]) * mtk_nregwins);

	dev->bus = &mt76_mmio_ops;

	spin_lock_init(&dev->mmio.irq_lock);
	spin_lock_init(&mtk_remap_lock);
}
EXPORT_SYMBOL_GPL(mt76_mmio_init);
