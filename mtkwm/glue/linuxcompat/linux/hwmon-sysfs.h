/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_HWMON_SYSFS_H
#define MTK_LC_HWMON_SYSFS_H

#include <linux/mtk_core.h>

struct attribute_group {
    const char *name;
    struct attribute **attrs;
};

struct attribute {
    const char *name;
    unsigned short mode;
};

struct device_attribute {
    struct attribute attr;
    ssize_t (*show)(struct device *dev, struct device_attribute *attr,
                    char *buf);
    ssize_t (*store)(struct device *dev, struct device_attribute *attr,
                     const char *buf, size_t count);
};

struct sensor_device_attribute {
    struct device_attribute dev_attr;
    int index;
};

#define to_sensor_dev_attr(_at) \
    container_of(_at, struct sensor_device_attribute, dev_attr)

/* Linux SENSOR_DEVICE_ATTR(): explicit mode + full show/store function
 * names (mt7615 thermal zone, init.c). */
#define SENSOR_DEVICE_ATTR(_name, _mode, _show, _store, _index)        \
    struct sensor_device_attribute sensor_dev_attr_##_name = {    \
        .dev_attr = {                            \
            .attr = { .name = __stringify(_name), .mode = _mode },    \
            .show = _show,                        \
            .store = _store,                    \
        },                                \
        .index = _index,                            \
    }

#define SENSOR_DEVICE_ATTR_RO(_name, _func, _index)            \
    struct sensor_device_attribute sensor_dev_attr_##_name = {    \
        .dev_attr = {                            \
            .attr = { .name = __stringify(_name), .mode = 0444 },    \
            .show = _func##_show,                    \
        },                                \
        .index = _index,                            \
    }

#define SENSOR_DEVICE_ATTR_RW(_name, _func, _index)            \
    struct sensor_device_attribute sensor_dev_attr_##_name = {    \
        .dev_attr = {                            \
            .attr = { .name = __stringify(_name), .mode = 0644 },    \
            .show = _func##_show,                    \
            .store = _func##_store,                    \
        },                                \
        .index = _index,                            \
    }

#define DEVICE_ATTR_RO(_name)                        \
    struct device_attribute dev_attr_##_name = {            \
        .attr = { .name = __stringify(_name), .mode = 0444 },    \
        .show = _name##_show,                        \
    }

#define DEVICE_ATTR_RW(_name)                        \
    struct device_attribute dev_attr_##_name = {            \
        .attr = { .name = __stringify(_name), .mode = 0644 },    \
        .show = _name##_show,                        \
        .store = _name##_store,                        \
    }

#define ATTRIBUTE_GROUPS(_name)                        \
    static const struct attribute_group _name##_group = {        \
        .attrs = _name##_attrs,                        \
    };                                \
    static const struct attribute_group *_name##_groups[] = {    \
        &_name##_group,                            \
        NULL,                                \
    }

#define __ATTR(_name, _mode, _show, _store)                \
    { .name = __stringify(_name), .mode = _mode, }

#endif
