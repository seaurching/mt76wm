/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat debugfs for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_DEBUGFS_H
#define MTK_LC_DEBUGFS_H

#include <linux/mtk_core.h>

#ifndef MTK_LC_UMODE_T_DEFINED
#define MTK_LC_UMODE_T_DEFINED
typedef unsigned short umode_t;
#endif

/*
 * debugfs is not available inside the kext.  The per-family debugfs
 * drivers (Linux mt76x02_debugfs.c, mt7603/debugfs.c, ...) are not
 * ported; the handful of helpers below exist so code that references
 * them links to a no-op.
 */
struct dentry;

static inline struct dentry *debugfs_create_dir(const char *name,
                                                struct dentry *parent)
{
    (void)name;
    (void)parent;
    return NULL;
}

static inline struct dentry *debugfs_create_file(const char *name, umode_t mode,
                                                 struct dentry *parent,
                                                 void *data,
                                                 const void *fops)
{
    (void)name;
    (void)mode;
    (void)parent;
    (void)data;
    (void)fops;
    return NULL;
}

static inline struct dentry *debugfs_create_u8(const char *name, umode_t mode,
                                               struct dentry *parent, u8 *val)
{
    (void)name;
    (void)mode;
    (void)parent;
    (void)val;
    return NULL;
}

static inline struct dentry *debugfs_create_u16(const char *name, umode_t mode,
                                                struct dentry *parent, u16 *val)
{
    (void)name;
    (void)mode;
    (void)parent;
    (void)val;
    return NULL;
}

static inline struct dentry *debugfs_create_u32(const char *name, umode_t mode,
                                                struct dentry *parent, u32 *val)
{
    (void)name;
    (void)mode;
    (void)parent;
    (void)val;
    return NULL;
}

static inline struct dentry *debugfs_create_bool(const char *name, umode_t mode,
                                                 struct dentry *parent,
                                                 bool *val)
{
    (void)name;
    (void)mode;
    (void)parent;
    (void)val;
    return NULL;
}

static inline void debugfs_remove(struct dentry *d)
{
    (void)d;
}

static inline void debugfs_remove_recursive(struct dentry *d)
{
    (void)d;
}

static inline struct dentry *debugfs_lookup(const char *name,
                                            struct dentry *parent)
{
    (void)name;
    (void)parent;
    return NULL;
}

#endif /* MTK_LC_DEBUGFS_H */
