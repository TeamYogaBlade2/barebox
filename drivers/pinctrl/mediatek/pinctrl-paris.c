/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Ingo Reitz <9l@9lo.re>
 *
 * Based on Linux pinctrl-paris:
 *   Copyright (C) 2018 MediaTek Inc.
 *   Author: Sean Wang <sean.wang@mediatek.com>
 *     Zhiyong Tao <zhiyong.tao@mediatek.com>
 *     Hongzhou.Yang <hongzhou.yang@mediatek.com>
 */

#include <gpio.h>
#include <pinctrl.h>
#include <of.h>
#include <linux/types.h>
#include <linux/device.h>
#include <dt-bindings/pinctrl/mt65xx.h>

#include "pinctrl-paris.h"
#include "pinctrl-mtk-common-v2.h"

#define GPIO_LINE_DIRECTION_IN 1
#define GPIO_LINE_DIRECTION_OUT 0

#define MTK_PIN_CONFIG_TDSEL (PIN_CONFIG_END + 1)
#define MTK_PIN_CONFIG_RDSEL (PIN_CONFIG_END + 2)
#define MTK_PIN_CONFIG_PU_ADV (PIN_CONFIG_END + 3)
#define MTK_PIN_CONFIG_PD_ADV (PIN_CONFIG_END + 4)
#define MTK_PIN_CONFIG_DRV_ADV (PIN_CONFIG_END + 5)

static const struct pinconf_generic_params mtk_custom_bindings[] = {
	{ "mediatek,tdsel", MTK_PIN_CONFIG_TDSEL, 0 },
	{ "mediatek,rdsel", MTK_PIN_CONFIG_RDSEL, 0 },
	{ "mediatek,pull-up-adv", MTK_PIN_CONFIG_PU_ADV, 1 },
	{ "mediatek,pull-down-adv", MTK_PIN_CONFIG_PD_ADV, 1 },
	{ "mediatek,drive-strength-adv", MTK_PIN_CONFIG_DRV_ADV, 2 },
};

static const char *const mtk_gpio_functions[] = {
	"func0",  "func1",  "func2",  "func3",	"func4",  "func5",
	"func6",  "func7",  "func8",  "func9",	"func10", "func11",
	"func12", "func13", "func14", "func15",
};

/*
 * This section supports converting to/from custom MTK_PIN_CONFIG_DRV_ADV
 * and standard PIN_CONFIG_DRIVE_STRENGTH_UA pin configs.
 *
 * The custom value encodes three hardware bits as follows:
 *
 *   |           Bits           |
 *   | 2 (E1) | 1 (E0) | 0 (EN) | drive strength (uA)
 *   ------------------------------------------------
 *   |    x   |    x   |    0   | disabled, use standard drive strength
 *   -------------------------------------
 *   |    0   |    0   |    1   |  125 uA
 *   |    0   |    1   |    1   |  250 uA
 *   |    1   |    0   |    1   |  500 uA
 *   |    1   |    1   |    1   | 1000 uA
 */
static int mtk_drv_uA_to_adv(int val)
{
	switch (val) {
	case 125:
		return 0x1;
	case 250:
		return 0x3;
	case 500:
		return 0x5;
	case 1000:
		return 0x7;
	}

	return -EINVAL;
}

static const struct mtk_func_desc *
mtk_pctrl_find_function_by_pin(struct mtk_pinctrl *hw, u32 pin_num, u32 fnum)
{
	const struct mtk_pin_desc *pin = hw->soc->pins + pin_num;
	const struct mtk_func_desc *func = pin->funcs;

	while (func && func->name) {
		if (func->muxval == fnum)
			return func;
		func++;
	}

	return NULL;
}

static bool mtk_pctrl_is_function_valid(struct mtk_pinctrl *hw, u32 pin_num,
					u32 fnum)
{
	int i;

	for (i = 0; i < hw->soc->npins; i++) {
		const struct mtk_pin_desc *pin = hw->soc->pins + i;

		if (pin->number == pin_num) {
			const struct mtk_func_desc *func = pin->funcs;

			while (func && func->name) {
				if (func->muxval == fnum)
					return true;
				func++;
			}

			break;
		}
	}

	return false;
}

/* pinctrl_ops */

static int mtk_pctrl_get_groups_count(struct pinctrl_device *pdev)
{
	struct mtk_pinctrl *hw = to_mtk_pinctrl(pdev);

	return hw->soc->ngrps;
}

static const char *mtk_pctrl_get_group_name(struct pinctrl_device *pdev,
					    unsigned group)
{
	struct mtk_pinctrl *hw = to_mtk_pinctrl(pdev);

	return hw->groups[group].name;
}

static int mtk_pctrl_get_group_pins(struct pinctrl_device *pdev, unsigned group,
				    const unsigned **pins, unsigned *num_pins)
{
	struct mtk_pinctrl *hw = to_mtk_pinctrl(pdev);

	*pins = (unsigned *)&hw->groups[group].pin;
	*num_pins = 1;

	return 0;
}

static const struct pinctrl_ops mtk_pinctrl_ops = {
	.set_state = pinctrl_generic_set_state,
	.get_groups_count = mtk_pctrl_get_groups_count,
	.get_group_name = mtk_pctrl_get_group_name,
	.get_group_pins = mtk_pctrl_get_group_pins,
};

/* pinmux_ops */

static int mtk_pmx_get_funcs_cnt(struct pinctrl_device *pdev)
{
	return ARRAY_SIZE(mtk_gpio_functions);
}

static const char *mtk_pmx_get_func_name(struct pinctrl_device *pdev,
					 unsigned selector)
{
	return mtk_gpio_functions[selector];
}

static int mtk_pmx_get_func_groups(struct pinctrl_device *pdev,
				   unsigned function,
				   const char *const **groups,
				   unsigned *const num_groups)
{
	struct mtk_pinctrl *hw = to_mtk_pinctrl(pdev);

	*groups = hw->grp_names;
	*num_groups = hw->soc->ngrps;

	return 0;
}

static int mtk_pmx_set_mux(struct pinctrl_device *pdev, unsigned function,
			   unsigned group)
{
	struct mtk_pinctrl *hw = to_mtk_pinctrl(pdev);
	struct mtk_pinctrl_group *grp = hw->groups + group;
	const struct mtk_func_desc *desc_func;
	const struct mtk_pin_desc *desc;
	bool ret;

	ret = mtk_pctrl_is_function_valid(hw, grp->pin, function);
	if (!ret) {
		dev_err(hw->dev, "invalid function %d on group %d .\n",
			function, group);
		return -EINVAL;
	}

	desc_func = mtk_pctrl_find_function_by_pin(hw, grp->pin, function);
	if (!desc_func)
		return -EINVAL;

	desc = (const struct mtk_pin_desc *)&hw->soc->pins[grp->pin];
	return mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_MODE,
				desc_func->muxval);
}

static int mtk_dt_pinmux_to_group(struct pinctrl_device *pdev, u32 pinmux,
				  unsigned int *func_selector,
				  unsigned int *group_selector)
{
	struct mtk_pinctrl *hw = to_mtk_pinctrl(pdev);
	unsigned pin, func;

	pin = MTK_GET_PIN_NO(pinmux);
	func = MTK_GET_PIN_FUNC(pinmux);

	if (pin >= hw->soc->npins || func >= ARRAY_SIZE(mtk_gpio_functions)) {
		dev_err(hw->dev, "invalid pins value.\n");
		return -EINVAL;
	}

	for (unsigned int group = 0; group < hw->soc->ngrps; group++) {
		struct mtk_pinctrl_group *grp = hw->groups + group;

		if (grp->pin == pin) {
			*group_selector = group;
			*func_selector = func;
			return 0;
		}
	}

	dev_err(hw->dev, "unable to match pin %d to group\n", pin);
	return -EINVAL;
}

static const struct pinmux_ops mtk_pmxops = {
	.get_functions_count = mtk_pmx_get_funcs_cnt,
	.get_function_name = mtk_pmx_get_func_name,
	.get_function_groups = mtk_pmx_get_func_groups,
	.set_mux = mtk_pmx_set_mux,
	.dt_pinmux_to_group = mtk_dt_pinmux_to_group
};

/* pinconf_ops */

static int mtk_pinconf_set(struct pinctrl_device *pdev, unsigned int pin,
			   enum pin_config_param param, u32 arg)
{
	struct mtk_pinctrl *hw = to_mtk_pinctrl(pdev);
	const struct mtk_pin_desc *desc;
	int err = -ENOTSUPP;
	u32 reg;

	if (pin >= hw->soc->npins)
		return -EINVAL;

	desc = (const struct mtk_pin_desc *)&hw->soc->pins[pin];

	switch ((u32)param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (!hw->soc->bias_set_combo)
			break;
		err = hw->soc->bias_set_combo(hw, desc, 0, MTK_DISABLE);
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (!hw->soc->bias_set_combo)
			break;
		err = hw->soc->bias_set_combo(hw, desc, 1, arg);
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (!hw->soc->bias_set_combo)
			break;
		err = hw->soc->bias_set_combo(hw, desc, 0, arg);
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		/* regard all non-zero value as enable */
		err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_IES, !!arg);
		break;
	case PIN_CONFIG_SLEW_RATE:
		/* regard all non-zero value as enable */
		err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_SR, !!arg);
		break;
	case PIN_CONFIG_LEVEL:
		err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_DO, arg);
		if (err)
			break;

		err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_DIR,
				       MTK_OUTPUT);
		break;
	case PIN_CONFIG_INPUT_SCHMITT:
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		/* arg = 1: Input mode & SMT enable ;
		 * arg = 0: Output mode & SMT disable
		 */
		err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_DIR, !arg);
		if (err)
			break;

		err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_SMT, !!arg);
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		if (!hw->soc->drive_set)
			break;
		err = hw->soc->drive_set(hw, desc, arg);
		break;
	case PIN_CONFIG_DRIVE_STRENGTH_UA:
		if (!hw->soc->adv_drive_set)
			break;

		err = mtk_drv_uA_to_adv(arg);
		if (err < 0)
			break;
		err = hw->soc->adv_drive_set(hw, desc, err);
		break;
	case MTK_PIN_CONFIG_TDSEL:
	case MTK_PIN_CONFIG_RDSEL:
		reg = (param == MTK_PIN_CONFIG_TDSEL) ? PINCTRL_PIN_REG_TDSEL :
							PINCTRL_PIN_REG_RDSEL;
		err = mtk_hw_set_value(hw, desc, reg, arg);
		break;
	case MTK_PIN_CONFIG_PU_ADV:
	case MTK_PIN_CONFIG_PD_ADV:
		if (!hw->soc->adv_pull_set)
			break;
		err = hw->soc->adv_pull_set(
			hw, desc, (param == MTK_PIN_CONFIG_PU_ADV), arg);
		break;
	case MTK_PIN_CONFIG_DRV_ADV:
		if (!hw->soc->adv_drive_set)
			break;
		err = hw->soc->adv_drive_set(hw, desc, arg);
		break;
	}

	return err;
}

static int mtk_pconf_group_set(struct pinctrl_device *pdev, unsigned group,
			       unsigned long *configs, unsigned num_configs)
{
	struct mtk_pinctrl *hw = to_mtk_pinctrl(pdev);
	struct mtk_pinctrl_group *grp = &hw->groups[group];
	bool drive_strength_uA_found = false;
	bool adv_drve_strength_found = false;
	int i, ret;

	for (i = 0; i < num_configs; i++) {
		ret = mtk_pinconf_set(pdev, grp->pin,
				      pinconf_to_config_param(configs[i]),
				      pinconf_to_config_argument(configs[i]));
		if (ret < 0)
			return ret;

		if (pinconf_to_config_param(configs[i]) ==
		    PIN_CONFIG_DRIVE_STRENGTH_UA)
			drive_strength_uA_found = true;
		if (pinconf_to_config_param(configs[i]) ==
		    MTK_PIN_CONFIG_DRV_ADV)
			adv_drve_strength_found = true;
	}

	/*
	 * Disable advanced drive strength mode if drive-strength-microamp
	 * is not set. However, mediatek,drive-strength-adv takes precedence
	 * as its value can explicitly request the mode be enabled or not.
	 */
	if (hw->soc->adv_drive_set && !drive_strength_uA_found &&
	    !adv_drve_strength_found)
		hw->soc->adv_drive_set(hw, &hw->soc->pins[grp->pin], 0);

	return 0;
}

static const struct pinconf_ops mtk_confops = {
	.custom_params = mtk_custom_bindings,
	.num_custom_params = ARRAY_SIZE(mtk_custom_bindings),
	.pin_config_group_set = mtk_pconf_group_set,
};

/* gpio */

static int mtk_gpio_request(struct gpio_chip *chip, unsigned int gpio)
{
	struct mtk_pinctrl *hw = chip->dev->priv;
	const struct mtk_pin_desc *desc;

	desc = (const struct mtk_pin_desc *)&hw->soc->pins[gpio];

	return mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_MODE,
				hw->soc->gpio_m);
}

static int mtk_gpio_get_direction(struct gpio_chip *chip, unsigned int gpio)
{
	struct mtk_pinctrl *hw = chip->dev->priv;
	const struct mtk_pin_desc *desc;
	int value, err;

	desc = (const struct mtk_pin_desc *)&hw->soc->pins[gpio];

	if (mtk_is_virt_gpio(hw, gpio))
		return 1;

	err = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_DIR, &value);
	if (err)
		return err;

	if (value)
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int mtk_gpio_get(struct gpio_chip *chip, unsigned int gpio)
{
	struct mtk_pinctrl *hw = chip->dev->priv;
	const struct mtk_pin_desc *desc;
	int value, err;

	desc = (const struct mtk_pin_desc *)&hw->soc->pins[gpio];

	err = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_DI, &value);
	if (err)
		return err;

	return !!value;
}

static int mtk_gpio_set(struct gpio_chip *chip, unsigned int gpio, int value)
{
	struct mtk_pinctrl *hw = chip->dev->priv;
	const struct mtk_pin_desc *desc;

	desc = (const struct mtk_pin_desc *)&hw->soc->pins[gpio];

	return mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_DO, !!value);
}

static int mtk_gpio_direction_input(struct gpio_chip *chip, unsigned int gpio)
{
	struct mtk_pinctrl *hw = chip->dev->priv;
	const struct mtk_pin_desc *desc;

	desc = (const struct mtk_pin_desc *)&hw->soc->pins[gpio];

	return mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_DIR, 0);
}

static int mtk_gpio_direction_output(struct gpio_chip *chip, unsigned int gpio,
				     int value)
{
	struct mtk_pinctrl *hw = chip->dev->priv;
	const struct mtk_pin_desc *desc;
	int ret;

	desc = (const struct mtk_pin_desc *)&hw->soc->pins[gpio];

	ret = mtk_gpio_set(chip, gpio, value);
	if (ret)
		return ret;

	return mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_DIR, 1);
}

static struct gpio_ops mtk_gpio_ops = {
	.request = mtk_gpio_request,
	.get_direction = mtk_gpio_get_direction,
	.direction_input = mtk_gpio_direction_input,
	.direction_output = mtk_gpio_direction_output,
	.get = mtk_gpio_get,
	.set = mtk_gpio_set,
};

/* probe */

static int mtk_build_gpiochip(struct mtk_pinctrl *hw)
{
	struct gpio_chip *chip = &hw->chip;

	chip->base = -1;
	chip->ops = &mtk_gpio_ops;
	chip->ngpio = hw->soc->npins;
	chip->dev = hw->dev;

	return gpiochip_add(chip);
}

static int paris_pinctrl_build_state(struct device *dev)
{
	struct mtk_pinctrl *hw = dev_get_drvdata(dev);
	// struct mtk_pinctrl *hw = to_mtk_pinctrl(pdev);
	int i;

	/* Allocate groups */
	hw->groups = devm_kmalloc_array(dev, hw->soc->ngrps,
					sizeof(*hw->groups), GFP_KERNEL);
	if (!hw->groups)
		return -ENOMEM;

	/* We assume that one pin is one group, use pin name as group name. */
	hw->grp_names = devm_kmalloc_array(dev, hw->soc->ngrps,
					   sizeof(*hw->grp_names), GFP_KERNEL);
	if (!hw->grp_names)
		return -ENOMEM;

	for (i = 0; i < hw->soc->npins; i++) {
		const struct mtk_pin_desc *pin = hw->soc->pins + i;
		struct mtk_pinctrl_group *group = hw->groups + i;

		group->name = pin->name;
		group->pin = pin->number;

		hw->grp_names[i] = pin->name;
	}

	return 0;
}

int mtk_paris_pinctrl_probe(struct device *dev)
{
	struct mtk_pin_desc *pins;
	struct mtk_pinctrl *hw;
	int err;

	hw = devm_kzalloc(dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	hw->soc = device_get_match_data(dev);
	if (!hw->soc)
		return -ENOENT;

	dev->priv = hw;
	hw->dev = dev;

	if (!hw->soc->nbase_names)
		return dev_err_probe(
			dev, -EINVAL,
			"SoC should be assigned at least one register base\n");

	hw->base = devm_kmalloc_array(dev, hw->soc->nbase_names,
				      sizeof(*hw->base), GFP_KERNEL);
	if (!hw->base)
		return -ENOMEM;

	for (int i = 0; i < hw->soc->nbase_names; i++) {
		hw->base[i] = dev_request_mem_region_by_name(
			dev, hw->soc->base_names[i]);
		if (IS_ERR(hw->base[i]))
			return PTR_ERR(hw->base[i]);
	}

	hw->nbase = hw->soc->nbase_names;

	hw->rsel_si_unit = of_property_read_bool(
		hw->dev->of_node, "mediatek,rsel-resistance-in-si-unit");

	spin_lock_init(&hw->lock);

	err = paris_pinctrl_build_state(dev);
	if (err)
		return dev_err_probe(dev, err, "build state failed\n");

	/* Copy from internal struct mtk_pin_desc to register to the core */
	pins = devm_kmalloc_array(dev, hw->soc->npins, sizeof(*pins),
				  GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	for (int i = 0; i < hw->soc->npins; i++) {
		pins[i].number = hw->soc->pins[i].number;
		pins[i].name = hw->soc->pins[i].name;
	}

	hw->pctl_dev.dev = dev;
	hw->pctl_dev.ops = &mtk_pinctrl_ops;
	hw->pctl_dev.pmxops = &mtk_pmxops;
	hw->pctl_dev.confops = &mtk_confops;
	hw->pctl_dev.npins = hw->soc->npins;

	err = pinctrl_register(&hw->pctl_dev);
	if (err)
		return err;

	err = mtk_build_gpiochip(hw);
	if (err)
		return err;

	return 0;
}
