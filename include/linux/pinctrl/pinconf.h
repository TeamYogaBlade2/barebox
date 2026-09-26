/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Interface the pinconfig portions of the pinctrl subsystem
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * This interface is used in the core to keep track of pins.
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 */
#ifndef __LINUX_PINCTRL_PINCONF_H
#define __LINUX_PINCTRL_PINCONF_H

#include <linux/types.h>

struct pinctrl_device;
struct pinconf_param;

/**
 * struct pinconf_ops - pin configuration operations.
 * @custom_params: driver-specific pin configuration properties
 * @num_custom_params: number of entries in @custom_params
 * @pin_config_set: configure an individual pin
 * @pin_config_group_set: configure all pins in a group
 */
struct pinconf_ops {
	const struct pinconf_generic_params *custom_params;
	unsigned int num_custom_params;
	int (*pin_config_set)(struct pinctrl_device *pdev,
			      unsigned int pin_selector,
			      unsigned long *configs,
			      unsigned int num_configs);
	int (*pin_config_group_set)(struct pinctrl_device *pdev,
				    unsigned int group_selector,
				    unsigned long *configs,
				    unsigned int num_configs);
};

#endif /* __LINUX_PINCTRL_PINCONF_H */
