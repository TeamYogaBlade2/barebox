// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <errno.h>
#include <malloc.h>
#include <of.h>
#include <pinctrl.h>

enum pinctrl_generic_target {
	PINCTRL_GENERIC_TARGET_NONE,
	PINCTRL_GENERIC_TARGET_PINS,
	PINCTRL_GENERIC_TARGET_GROUPS,
	PINCTRL_GENERIC_TARGET_PINMUX,
};

static const struct pinconf_generic_params dt_params[] = {
	{ "bias-bus-hold", PIN_CONFIG_BIAS_BUS_HOLD, 0 },
	{ "bias-disable", PIN_CONFIG_BIAS_DISABLE, 0 },
	{ "bias-high-impedance", PIN_CONFIG_BIAS_HIGH_IMPEDANCE, 0 },
	{ "bias-pull-up", PIN_CONFIG_BIAS_PULL_UP, 1 },
	{ "bias-pull-pin-default", PIN_CONFIG_BIAS_PULL_PIN_DEFAULT, 1 },
	{ "bias-pull-down", PIN_CONFIG_BIAS_PULL_DOWN, 1 },
	{ "drive-open-drain", PIN_CONFIG_DRIVE_OPEN_DRAIN, 0 },
	{ "drive-open-source", PIN_CONFIG_DRIVE_OPEN_SOURCE, 0 },
	{ "drive-push-pull", PIN_CONFIG_DRIVE_PUSH_PULL, 0 },
	{ "drive-strength", PIN_CONFIG_DRIVE_STRENGTH, 0 },
	{ "drive-strength-microamp", PIN_CONFIG_DRIVE_STRENGTH_UA, 0 },
	{ "input-debounce", PIN_CONFIG_INPUT_DEBOUNCE, 0 },
	{ "input-disable", PIN_CONFIG_INPUT_ENABLE, 0 },
	{ "input-enable", PIN_CONFIG_INPUT_ENABLE, 1 },
	{ "input-schmitt", PIN_CONFIG_INPUT_SCHMITT, 0 },
	{ "input-schmitt-disable", PIN_CONFIG_INPUT_SCHMITT_ENABLE, 0 },
	{ "input-schmitt-enable", PIN_CONFIG_INPUT_SCHMITT_ENABLE, 1 },
	{ "input-schmitt-microvolts", PIN_CONFIG_INPUT_SCHMITT_UV, 0 },
	{ "low-power-disable", PIN_CONFIG_MODE_LOW_POWER, 0 },
	{ "low-power-enable", PIN_CONFIG_MODE_LOW_POWER, 1 },
	{ "output-disable", PIN_CONFIG_OUTPUT_ENABLE, 0 },
	{ "output-enable", PIN_CONFIG_OUTPUT_ENABLE, 1 },
	{ "output-high", PIN_CONFIG_LEVEL, 1, },
	{ "output-impedance-ohms", PIN_CONFIG_OUTPUT_IMPEDANCE_OHMS, 0 },
	{ "output-low", PIN_CONFIG_LEVEL, 0, },
	{ "power-source", PIN_CONFIG_POWER_SOURCE, 0 },
	{ "sleep-hardware-state", PIN_CONFIG_SLEEP_HARDWARE_STATE, 0 },
	{ "slew-rate", PIN_CONFIG_SLEW_RATE, 0 },
	{ "skew-delay", PIN_CONFIG_SKEW_DELAY, 0 },
};

static const struct pinconf_generic_params *
pinconf_find_param(const struct pinconf_generic_params *params,
		   unsigned int num_params,
		   const char *property)
{
	unsigned int i;

	for (i = 0; i < num_params; i++)
		if (!strcmp(params[i].property, property))
			return &params[i];

	return NULL;
}

static bool pinconf_is_builtin_param(const char *property)
{
	return pinconf_find_param(dt_params,
				  ARRAY_SIZE(dt_params),
				  property);
}

/**
 * parse_dt_cfg() - Parse DT pinconf parameters
 * @np:	DT node
 * @params:	Array of describing generic parameters
 * @count:	Number of entries in @params
 * @cfg:	Array of parsed config options
 * @ncfg:	Number of entries in @cfg
 *
 * Parse the config options described in @params from @np and puts the result
 * in @cfg. @cfg does not need to be empty, entries are added beginning at
 * @ncfg. @ncfg is updated to reflect the number of entries after parsing. @cfg
 * needs to have enough memory allocated to hold all possible entries.
 */
static void parse_dt_cfg(struct device_node *np,
			 const struct pinconf_generic_params *params,
			 unsigned int count, unsigned long *cfg,
			 unsigned int *ncfg)
{
	int i;

	for (i = 0; i < count; i++) {
		u32 val;
		int ret;
		const struct pinconf_generic_params *par = &params[i];

		ret = of_property_read_u32(np, par->property, &val);

		/* property not found */
		if (ret == -EINVAL)
			continue;

		/* use default value, when no value is specified */
		if (ret)
			val = par->default_value;

		pr_debug("found %s with value %u\n", par->property, val);
		cfg[*ncfg] = pinconf_to_config_packed(par->param, val);
		(*ncfg)++;
	}
}

static void parse_dt_cfg_custom(struct device_node *np,
				const struct pinconf_generic_params *params,
				unsigned int count, unsigned long *cfg,
				unsigned int *ncfg)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		if (pinconf_is_builtin_param(params[i].property))
			continue;

		parse_dt_cfg(np, &params[i], 1, cfg, ncfg);
	}
}

static int pinconf_apply(struct pinctrl_device *pdev, bool group,
			 unsigned int selector, unsigned long *configs,
			 unsigned int num_configs)
{
	const struct pinconf_ops *confops = pdev->confops;
	const unsigned int *pins;
	unsigned int npins;
	int i, ret;

	if (!num_configs)
		return 0;

	if (group) {
		if (!confops || !confops->pin_config_group_set)
			goto apply_pins;

		return confops->pin_config_group_set(pdev, selector, configs,
						     num_configs);
	}

	if (!confops || !confops->pin_config_set)
		return -ENOTSUPP;

	return confops->pin_config_set(pdev, selector, configs, num_configs);

apply_pins:
	if (!pdev->ops->get_group_pins || !confops ||
	    !confops->pin_config_set)
		return -ENOTSUPP;

	ret = pdev->ops->get_group_pins(pdev, selector, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		ret = confops->pin_config_set(pdev, pins[i], configs,
					      num_configs);
		if (ret)
			return ret;
	}

	return 0;
}

static int pinconf_apply_all(struct pinctrl_device *pdev,
			     struct device_node *np,
			     bool group, unsigned int selector)
{
	unsigned long *configs;
	const struct pinconf_ops *confops = pdev->confops;
	const struct pinconf_generic_params *custom_params = NULL;
	unsigned int num_custom_params = 0;
	unsigned int max_configs, num_configs = 0;
	int ret;

	if (confops) {
		custom_params = confops->custom_params;
		num_custom_params = confops->num_custom_params;
	}

	max_configs = ARRAY_SIZE(dt_params) + num_custom_params;
	if (!max_configs)
		return 0;

	configs = calloc(max_configs, sizeof(*configs));
	if (!configs)
		return -ENOMEM;

	parse_dt_cfg(np, dt_params, ARRAY_SIZE(dt_params), configs,
		     &num_configs);
	parse_dt_cfg_custom(np, custom_params, num_custom_params, configs,
			    &num_configs);

	ret = pinconf_apply(pdev, group, selector, configs, num_configs);
	free(configs);

	return ret;
}

static int pinctrl_name_to_selector(struct pinctrl_device *pdev,
				    const char *name,
				    int (*get_count)(struct pinctrl_device *),
				    const char *(*get_name)(struct pinctrl_device *,
							    unsigned int))
{
	int i, count;

	if (!get_count || !get_name)
		return -ENOTSUPP;

	count = get_count(pdev);
	if (count < 0)
		return count;

	for (i = 0; i < count; i++) {
		const char *selector_name = get_name(pdev, i);

		if (selector_name && !strcmp(selector_name, name))
			return i;
	}

	return -ENOENT;
}

static int pinctrl_pin_name_to_selector(struct pinctrl_device *pdev,
					const char *name,
					unsigned int *selector)
{
	const struct pinctrl_ops *ops = pdev->ops;
	int i, count, ret;

	if (!ops || !ops->get_pins_count || !ops->get_pin_name)
		return -ENOTSUPP;

	count = ops->get_pins_count(pdev);
	if (count < 0)
		return count;

	for (i = 0; i < count; i++) {
		const char *pin_name = ops->get_pin_name(pdev, i);

		if (!pin_name || strcmp(pin_name, name))
			continue;

		if (!ops->get_pin_selector) {
			*selector = i;
			return 0;
		}

		ret = ops->get_pin_selector(pdev, i, selector);
		if (ret)
			return ret;

		return 0;
	}

	return -ENOENT;
}

static int pinctrl_function_selector(struct pinctrl_device *pdev,
				     struct device_node *np,
				     bool *has_function)
{
	const struct pinmux_ops *pmxops = pdev->pmxops;
	const char *function;
	int ret;

	*has_function = false;

	ret = of_property_read_string(np, "function", &function);
	if (ret == -EINVAL)
		return 0;
	if (ret)
		return ret;

	*has_function = true;

	if (!pmxops)
		return -ENOTSUPP;

	return pinctrl_name_to_selector(pdev, function,
					pmxops->get_functions_count,
					pmxops->get_function_name);
}

static int pinctrl_generic_apply_pins(struct pinctrl_device *pdev,
				      struct device_node *np)
{
	unsigned int selector;
	bool has_function;
	const char *name;
	int count, func, i, ret;

	count = of_property_count_strings(np, "pins");
	if (count < 0)
		return count;

	func = pinctrl_function_selector(pdev, np, &has_function);
	if (func < 0)
		return func;
	if (has_function)
		return -ENOTSUPP;

	for (i = 0; i < count; i++) {
		ret = of_property_read_string_index(np, "pins", i, &name);
		if (ret)
			return ret;

		ret = pinctrl_pin_name_to_selector(pdev, name, &selector);
		if (ret)
			return ret;

		ret = pinconf_apply_all(pdev, np, false, selector);
		if (ret)
			return ret;
	}

	return 0;
}

static int pinctrl_generic_apply_groups(struct pinctrl_device *pdev,
					struct device_node *np)
{
	unsigned int selector;
	const struct pinmux_ops *pmxops = pdev->pmxops;
	bool has_function;
	const char *name;
	int count, func, i, ret;

	count = of_property_count_strings(np, "groups");
	if (count < 0)
		return count;

	func = pinctrl_function_selector(pdev, np, &has_function);
	if (func < 0)
		return func;

	for (i = 0; i < count; i++) {
		ret = of_property_read_string_index(np, "groups", i, &name);
		if (ret)
			return ret;

		ret = pinctrl_name_to_selector(pdev, name,
					       pdev->ops->get_groups_count,
					       pdev->ops->get_group_name);
		if (ret < 0)
			return ret;
		selector = ret;

		if (has_function) {
			if (!pmxops || !pmxops->set_mux)
				return -ENOTSUPP;

			ret = pmxops->set_mux(pdev, func, selector);
			if (ret)
				return ret;
		}

		ret = pinconf_apply_all(pdev, np, true, selector);
		if (ret)
			return ret;
	}

	return 0;
}

static int pinctrl_generic_apply_pinmux(struct pinctrl_device *pdev,
					struct device_node *np)
{
	const struct pinmux_ops *pmxops = pdev->pmxops;
	int count, i, ret;
	unsigned int group, func;
	u32 pinmux;

	if (!pmxops || !pmxops->dt_pinmux_to_group || !pmxops->set_mux)
		return -ENOTSUPP;

	count = of_property_count_elems_of_size(np, "pinmux", sizeof(u32));
	if (count < 0)
		return count;

	for (i = 0; i < count; i++) {
		ret = of_property_read_u32_index(np, "pinmux", i, &pinmux);
		if (ret)
			return ret;

		ret = pmxops->dt_pinmux_to_group(pdev, pinmux, &func,
						 &group);
		if (ret)
			return ret;

		ret = pmxops->set_mux(pdev, func, group);
		if (ret)
			return ret;

		ret = pinconf_apply_all(pdev, np, true, group);
		if (ret)
			return ret;
	}

	return 0;
}

static int pinctrl_generic_get_target(struct device_node *np,
				      enum pinctrl_generic_target *target)
{
	bool has_pins = of_find_property(np, "pins", NULL);
	bool has_groups = of_find_property(np, "groups", NULL);
	bool has_pinmux = of_find_property(np, "pinmux", NULL);
	int targets = has_pins + has_groups + has_pinmux;

	if (targets > 1)
		return -EINVAL;

	if (has_pins)
		*target = PINCTRL_GENERIC_TARGET_PINS;
	else if (has_groups)
		*target = PINCTRL_GENERIC_TARGET_GROUPS;
	else if (has_pinmux)
		*target = PINCTRL_GENERIC_TARGET_PINMUX;
	else
		*target = PINCTRL_GENERIC_TARGET_NONE;

	return 0;
}

static int pinctrl_generic_set_one(struct pinctrl_device *pdev,
				   struct device_node *np)
{
	enum pinctrl_generic_target target;
	int ret;

	ret = pinctrl_generic_get_target(np, &target);
	if (ret)
		return ret;

	switch (target) {
	case PINCTRL_GENERIC_TARGET_NONE:
		return 0;
	case PINCTRL_GENERIC_TARGET_PINS:
		return pinctrl_generic_apply_pins(pdev, np);
	case PINCTRL_GENERIC_TARGET_GROUPS:
		return pinctrl_generic_apply_groups(pdev, np);
	case PINCTRL_GENERIC_TARGET_PINMUX:
		return pinctrl_generic_apply_pinmux(pdev, np);
	}

	return -EINVAL;
}

int pinctrl_generic_set_state(struct pinctrl_device *pdev,
			      struct device_node *np)
{
	struct device_node *child;
	int ret;

	ret = pinctrl_generic_set_one(pdev, np);
	if (ret)
		return ret;

	for_each_child_of_node(np, child) {
		ret = pinctrl_generic_set_one(pdev, child);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(pinctrl_generic_set_state);
