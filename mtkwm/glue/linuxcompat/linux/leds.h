/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_LEDS_H
#define MTK_LC_LEDS_H

#include <linux/mtk_core.h>

struct led_classdev {
    const char *name;
    unsigned int brightness;
    unsigned int max_brightness;
    void (*brightness_set)(struct led_classdev *led, unsigned int value);
    int (*brightness_set_blocking)(struct led_classdev *led, unsigned int value);
    unsigned int (*brightness_get)(struct led_classdev *led);
};

static inline int led_classdev_register(struct device *dev,
                                        struct led_classdev *cdev)
{
    (void)dev; (void)cdev;
    return 0;
}
static inline void led_classdev_unregister(struct led_classdev *cdev)
{
    (void)cdev;
}
static inline void led_classdev_resume(struct led_classdev *cdev)
{
    (void)cdev;
}
static inline void led_classdev_suspend(struct led_classdev *cdev)
{
    (void)cdev;
}

#endif
