/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_OF_H
#define MTK_LC_OF_H

#include <linux/mtk_core.h>
#include <linux/slab.h>

/* No device tree on x86 macOS: all OF lookups fail. */

struct property {
    const char *name;
    int length;
    const void *value;
};

static inline struct device_node *of_node_get(struct device_node *node)
{
    return node;
}
static inline struct device_node *of_node_put(struct device_node *node)
{
    return node;
}

static inline const void *of_get_property(const struct device_node *node,
                                          const char *name, int *lenp)
{
    (void)node; (void)name; (void)lenp;
    return NULL;
}

static inline struct property *of_find_property(const struct device_node *node,
                                                const char *name, int *lenp)
{
    (void)node; (void)name; (void)lenp;
    return NULL;
}

static inline bool of_property_read_bool(const struct device_node *node,
                                         const char *name)
{
    (void)node; (void)name;
    return false;
}

static inline int of_property_read_u32(const struct device_node *node,
                                       const char *name, u32 *out)
{
    (void)node; (void)name; (void)out;
    return -EINVAL;
}

static inline int of_property_read_u32_array(const struct device_node *node,
                                             const char *name, u32 *out,
                                             size_t sz)
{
    (void)node; (void)name; (void)out; (void)sz;
    return -EINVAL;
}

static inline struct device_node *of_get_child_by_name(
    const struct device_node *node, const char *name)
{
    (void)node; (void)name;
    return NULL;
}

static inline struct device_node *of_find_node_by_phandle(uint32_t ph)
{
    (void)ph;
    return NULL;
}

static inline const char *of_prop_next_string(const void *prop,
                                              const char *s)
{
    (void)prop; (void)s;
    return NULL;
}

static inline int of_property_count_strings(const struct device_node *node,
                                            const char *name)
{
    (void)node; (void)name;
    return -EINVAL;
}

#define for_each_child_of_node(parent, child) \
    for ((child) = NULL; (child); (child) = NULL)

#define for_each_available_child_of_node(parent, child) \
    for ((child) = NULL; (child); (child) = NULL)

#endif
