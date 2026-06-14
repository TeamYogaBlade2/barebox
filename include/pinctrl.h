/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PINCTRL_H
#define PINCTRL_H

#include <linux/types.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>

struct pinctrl_device;
struct pinconf_param;
struct pinconf_ops;
struct device_node;
struct group_desc;
struct pinfunction;

/**
 * struct pinctrl_ops - pin control operations, to be implemented by pin
 * controller drivers.
 * @set_state: apply a device tree pinctrl state node
 * @set_direction: set GPIO direction for a pin
 * @get_direction: get GPIO direction for a pin
 * @get_pins_count: return the number of named pins accepted by the pins property
 * @get_pin_name: return the pin name for a dense enumeration index
 * @get_pin_selector: translate a dense pin enumeration index to the pin selector
 *	passed to pinconf/pinmux callbacks
 * @get_groups_count: returns the count of total number of groups registered
 * @get_group_name: return the group name of the pin group
 * @get_group_pins: return the pins belonging to a pin group
 */
struct pinctrl_ops {
	/* barebox-specific */
	int (*set_state)(struct pinctrl_device *, struct device_node *);
	int (*set_direction)(struct pinctrl_device *, unsigned int, bool);
	int (*get_direction)(struct pinctrl_device *, unsigned int);

	int (*get_pins_count)(struct pinctrl_device *pdev);
	const char *(*get_pin_name)(struct pinctrl_device *pdev,
				    unsigned int index);
	/*
	 * U-Boot-style barebox extension. Linux exposes named pins through
	 * struct pinctrl_desc::pins instead, but barebox keeps callbacks here
	 * so drivers can opt into generic set_state-time DT parsing without
	 * allocating a Linux-style pin descriptor table.
	 */
	int (*get_pin_selector)(struct pinctrl_device *pdev,
				unsigned int index, unsigned int *selector);
	/* Linux ops */
	int (*get_groups_count)(struct pinctrl_device *pdev);
	const char *(*get_group_name)(struct pinctrl_device *pdev,
				      unsigned int selector);
	int (*get_group_pins)(struct pinctrl_device *pdev,
			      unsigned int selector,
			      const unsigned int **pins,
			      unsigned int *npins);
};

struct pinctrl_device {
	struct device *dev;
	const struct pinctrl_ops *ops;
	const struct pinmux_ops *pmxops;
	const struct pinconf_ops *confops;
	struct list_head list;
	struct device_node *node;
	unsigned int base, npins;
	struct group_desc *groups;
	unsigned int ngroups;
	const struct pinfunction **functions;
	unsigned int nfunctions;
};

int pinctrl_register(struct pinctrl_device *pdev);
void pinctrl_unregister(struct pinctrl_device *pdev);
int pinctrl_generic_set_state(struct pinctrl_device *pdev,
			      struct device_node *np);
int pinctrl_generic_add_group(struct pinctrl_device *pdev, const char *name,
			      const unsigned int *pins, unsigned int npins,
			      void *data);
int pinctrl_generic_get_group_count(struct pinctrl_device *pdev);
const char *pinctrl_generic_get_group_name(struct pinctrl_device *pdev,
					   unsigned int selector);
int pinctrl_generic_get_group_pins(struct pinctrl_device *pdev,
				   unsigned int selector,
				   const unsigned int **pins,
				   unsigned int *npins);
struct group_desc *pinctrl_generic_get_group(struct pinctrl_device *pdev,
					     unsigned int selector);
int pinmux_generic_add_pinfunction(struct pinctrl_device *pdev,
				   const struct pinfunction *func);
int pinmux_generic_get_function_count(struct pinctrl_device *pdev);
const char *pinmux_generic_get_function_name(struct pinctrl_device *pdev,
					     unsigned int selector);
int pinmux_generic_get_function_groups(struct pinctrl_device *pdev,
				       unsigned int selector,
				       const char * const **groups,
				       unsigned int *num_groups);

#ifdef CONFIG_PINCTRL_STATE_PARAM
void of_pinctrl_register_consumer(struct device *dev, struct device_node *np);
void of_pinctrl_unregister_consumer(struct device *dev);
#else
static inline void of_pinctrl_register_consumer(struct device *dev, struct device_node *np)
{
}
static inline void of_pinctrl_unregister_consumer(struct device *dev) {}
#endif

#endif /* PINCTRL_H */
