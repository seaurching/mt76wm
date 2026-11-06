/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_NVMEM_CONSUMER_H
#define MTK_LC_NVMEM_CONSUMER_H

#include <linux/mtk_core.h>
#include <linux/of.h>

struct nvmem_cell;

static inline struct nvmem_cell *of_nvmem_cell_get(struct device_node *np,
                                                   const char *name)
{
    (void)np; (void)name;
    return (struct nvmem_cell *)ERR_PTR(-ENODEV);
}

static inline void *nvmem_cell_read(struct nvmem_cell *cell, size_t *len)
{
    (void)cell; (void)len;
    return NULL;
}

static inline void nvmem_cell_put(struct nvmem_cell *cell)
{
    (void)cell;
}

#endif
