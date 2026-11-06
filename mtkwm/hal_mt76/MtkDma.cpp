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

#include "MtkDma.hpp"
#include <compat.h>
#include <linux/kernel.h>

#define DMA_BIT_MASK(n) (((n) == 64) ? ~0ULL : ((1ULL << (n)) - 1))

void *mtk_dma_alloc_coherent(uint64_t size, uint64_t alignment,
                             uint64_t *dma_addr, struct mtk_dma_handle **out_handle)
{
    IOBufferMemoryDescriptor *bmd;
    IODMACommand::Segment64 seg;
    UInt64 ofs = 0;
    UInt32 numSegs = 1;

    if (alignment == 0)
        alignment = 1;

    bmd = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
            kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMapInhibitCache,
            size, DMA_BIT_MASK(32));  /* MT7921 WPDMA truncates descriptor addresses to 32 bits */
    if (bmd == NULL) {
        XYLog("%s: alloc DMA buffer failed (size %llu)\n", __FUNCTION__, size);
        return NULL;
    }
    if (bmd->prepare() != kIOReturnSuccess) {
        XYLog("%s: prepare DMA buffer failed\n", __FUNCTION__);
        bmd->release();
        return NULL;
    }
    IODMACommand *cmd = IODMACommand::withSpecification(kIODMACommandOutputHost64,
            64, 0, IODMACommand::kMapped, 0, alignment);
    if (cmd == NULL) {
        XYLog("%s: alloc IODMACommand failed\n", __FUNCTION__);
        bmd->complete();
        bmd->release();
        return NULL;
    }
    if (cmd->setMemoryDescriptor(bmd) != kIOReturnSuccess ||
        cmd->gen64IOVMSegments(&ofs, &seg, &numSegs) != kIOReturnSuccess) {
        XYLog("%s: map DMA segments failed\n", __FUNCTION__);
        cmd->release();
        bmd->complete();
        bmd->release();
        return NULL;
    }

    struct mtk_dma_handle *h = (struct mtk_dma_handle *)IOMalloc(sizeof(*h));
    if (h == NULL) {
        cmd->clearMemoryDescriptor();
        cmd->release();
        bmd->complete();
        bmd->release();
        return NULL;
    }
    h->bmd = bmd;
    h->cmd = cmd;

    void *vaddr = bmd->getBytesNoCopy();
    memset(vaddr, 0, size);
    *dma_addr = seg.fIOVMAddr;
    *out_handle = h;
    return vaddr;
}

void mtk_dma_free_coherent(struct mtk_dma_handle *handle, void *vaddr, uint64_t size)
{
    if (handle == NULL)
        return;
    if (handle->cmd) {
        handle->cmd->clearMemoryDescriptor();
        handle->cmd->release();
        handle->cmd = NULL;
    }
    if (handle->bmd) {
        handle->bmd->complete();
        handle->bmd->release();
        handle->bmd = NULL;
    }
    IOFree(handle, sizeof(*handle));
}
