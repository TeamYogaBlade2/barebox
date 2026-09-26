/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-Comment: Origin-URL: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/linux/pinctrl/pinctrl.h?id=11aa02d6a9c222260490f952d041dec6d7f16a92 */
/*
 * Interface the pinctrl subsystem
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * This interface is used in the core to keep track of pins.
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 */
#ifndef __LINUX_PINCTRL_PINCTRL_H
#define __LINUX_PINCTRL_PINCTRL_H

#include <linux/bits.h>
#include <linux/types.h>
#include <pinctrl.h>

/**
 * struct pingroup - provides information on pingroup
 * @name: a name for pingroup
 * @pins: an array of pins in the pingroup
 * @npins: number of pins in the pingroup
 */
struct pingroup {
	const char *name;
	const unsigned int *pins;
	size_t npins;
};

/* Convenience macro to define a single named or anonymous pingroup */
#define PINCTRL_PINGROUP(_name, _pins, _npins)	\
(struct pingroup) {				\
	.name = _name,				\
	.pins = _pins,				\
	.npins = _npins,			\
}

/**
 * struct group_desc - generic pin group descriptor
 * @grp: generic pin group data
 * @data: driver-defined per-group data
 */
struct group_desc {
	struct pingroup grp;
	void *data;
};

#define PINCTRL_GROUP_DESC(_name, _pins, _npins, _data)	\
(struct group_desc) {					\
	.grp = PINCTRL_PINGROUP(_name, _pins, _npins),	\
	.data = _data,					\
}

/**
 * struct pinctrl_pin_desc - boards/machines provide information on their
 * pins, pads or other muxable units in this struct
 * @number: unique pin number from the global pin number space
 * @name: a name for this pin
 * @drv_data: driver-defined per-pin data. pinctrl core does not touch this
 */
struct pinctrl_pin_desc {
	unsigned int number;
	const char *name;
	void *drv_data;
};

/* Convenience macro to define a single named or anonymous pin descriptor */
#define PINCTRL_PIN(a, b) { .number = a, .name = b }
#define PINCTRL_PIN_ANON(a) { .number = a }

#define PINFUNCTION_FLAG_GPIO	BIT(0)

/**
 * struct pinfunction - Description about a function
 * @name: Name of the function
 * @groups: An array of groups for this function
 * @ngroups: Number of groups in @groups
 * @flags: Additional pin function flags
 */
struct pinfunction {
	const char *name;
	const char * const *groups;
	size_t ngroups;
	unsigned long flags;
};

/* Convenience macro to define a single named pinfunction */
#define PINCTRL_PINFUNCTION(_name, _groups, _ngroups)	\
(struct pinfunction) {					\
		.name = (_name),			\
		.groups = (_groups),			\
		.ngroups = (_ngroups),			\
	}

/* Same as PINCTRL_PINFUNCTION() but for the GPIO category of functions */
#define PINCTRL_GPIO_PINFUNCTION(_name, _groups, _ngroups)	\
(struct pinfunction) {						\
		.name = (_name),				\
		.groups = (_groups),				\
		.ngroups = (_ngroups),				\
		.flags = PINFUNCTION_FLAG_GPIO,			\
	}

#endif /* __LINUX_PINCTRL_PINCTRL_H */
