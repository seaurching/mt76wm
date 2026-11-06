/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_HWMON_H
#define MTK_LC_HWMON_H

#include <linux/mtk_core.h>

struct device *devm_hwmon_device_register_with_groups(struct device *dev,
                                                      const char *name,
                                                      void *drvdata,
                                                      const struct attribute_group **groups);

#define HWMON_CHANNEL_INFO(type, args...) do { } while (0)

#endif
