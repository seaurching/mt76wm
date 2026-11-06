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

#ifndef MtkDma_hpp
#define MtkDma_hpp

#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODMACommand.h>

/*
 * DMA coherent allocation helpers for the ported mt76 code, following the
 * allocDmaMemory2() pattern from itlwm's ItlIwx.cpp (Copyright (C) 2020
 * 钟先耀, GPL-2.0).
 */

#ifdef __cplusplus
extern "C" {
#endif

struct mtk_dma_handle {
    IOBufferMemoryDescriptor *bmd;
    IODMACommand             *cmd;
};

/* Allocate size bytes of cache-inhibited, physically contiguous DMA memory.
 * On success returns the virtual address and fills *dma_addr (IOVA) and
 * *out_handle (for mtk_dma_free_coherent). Returns NULL on failure. */
void *mtk_dma_alloc_coherent(uint64_t size, uint64_t alignment,
                             uint64_t *dma_addr, struct mtk_dma_handle **out_handle);

/* Free a block returned by mtk_dma_alloc_coherent. */
void mtk_dma_free_coherent(struct mtk_dma_handle *handle, void *vaddr, uint64_t size);

#ifdef __cplusplus
}
#endif

#endif /* MtkDma_hpp */
