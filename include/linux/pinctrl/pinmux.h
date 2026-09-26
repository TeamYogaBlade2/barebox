/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-Comment: Origin-URL: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/linux/pinctrl/pinmux.h?id=7958b4bb806c1af800ca23c8333a98231b3ab0b1 */
/*
 * Interface the pinmux subsystem
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * Based on bits of regulator core, gpio core and clk core
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 */
#ifndef __LINUX_PINCTRL_PINMUX_H
#define __LINUX_PINCTRL_PINMUX_H

#include <linux/types.h>

struct pinctrl_device;

/**
 * struct pinmux_ops - pinmux operations, to be implemented by pin controller
 * drivers that support pinmuxing
 * @get_functions_count: returns number of selectable named functions available
 *	in this pinmux driver
 * @get_function_name: return the function name of the muxing selector,
 *	called by the core to figure out which mux setting it shall map a
 *	certain device to
 * @get_function_groups: return an array of groups names (in turn
 *	referencing pins) connected to a certain function selector. The group
 *	name can be used with the generic @pinctrl_ops to retrieve the
 *	actual pins affected. The applicable groups will be returned in
 *	@groups and the number of groups in @num_groups
 * @set_mux: enable a certain muxing function with a certain pin group. The
 *	driver does not need to figure out whether enabling this function
 *	conflicts some other use of the pins in that group, such collisions
 *	are handled by the pinmux subsystem. The @func_selector selects a
 *	certain function whereas @group_selector selects a certain set of pins
 *	to be used. On simple controllers the latter argument may be ignored
 * @dt_pinmux_to_group: barebox-specific helper to decode one packed vendor
 *	pinmux property cell into function and group selectors for @set_mux.
 *	Linux drivers normally do this in @dt_node_to_map; barebox applies
 *	pinctrl DT nodes directly at set_state time and does not keep maps.
 */
struct pinmux_ops {
	int (*get_functions_count)(struct pinctrl_device *pctldev);
	const char *(*get_function_name)(struct pinctrl_device *pctldev,
					  unsigned int selector);
	int (*get_function_groups)(struct pinctrl_device *pctldev,
				    unsigned int selector,
				    const char * const **groups,
				    unsigned int *num_groups);
	int (*set_mux)(struct pinctrl_device *pctldev,
		       unsigned int func_selector,
		       unsigned int group_selector);
	int (*dt_pinmux_to_group)(struct pinctrl_device *pctldev,
				  u32 pinmux,
				  unsigned int *func_selector,
				  unsigned int *group_selector);
};

#endif /* __LINUX_PINCTRL_PINMUX_H */
