// SPDX-License-Identifier: GPL-2.0-only
/*
 * Regulator driver for MediaTek MT6320 PMIC
 * Based on Linux drivers/regulator/mt6320-regulator.c (subset of rails)
 */

#include <common.h>
#include <init.h>
#include <of_device.h>
#include <linux/err.h>
#include <linux/regmap.h>
#include <regulator.h>
#include <linux/bitops.h>

struct mt6320_info {
	struct regulator_desc desc;
};

/* Linear ranges (uV) */
static const struct regulator_linear_range buck_range_proc[] = {
	REGULATOR_LINEAR_RANGE(700000, 0, 0x7f, 6250),
};
static const struct regulator_linear_range buck_range_io18[] = {
	REGULATOR_LINEAR_RANGE(1500000, 0, 0x1f, 20000),
};
static const struct regulator_linear_range buck_range_pa[] = {
	REGULATOR_LINEAR_RANGE(500000, 0, 0x3f, 50000),
};

static const unsigned int ldo_1v8_3v3[] = { 1800000, 3300000 };
static const unsigned int ldo_3v0_3v3[] = { 3000000, 3300000 };
static const unsigned int ldo_gp[] = {
	1200000, 1300000, 1500000, 1800000, 2000000, 2800000, 3000000, 3300000,
};

static const struct regulator_ops mt6320_buck_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static const struct regulator_ops mt6320_ldo_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_iterate,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static const struct regulator_ops mt6320_fixed_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

#define BUCK_LINEAR(_name, _match, _en, _vsel, _mask, _ranges) { \
	.desc = { \
		.name = _name, \
		.of_match = _match, \
		.ops = &mt6320_buck_ops, \
		.n_voltages = (_ranges)[0].max_sel - (_ranges)[0].min_sel + 1, \
		.linear_ranges = _ranges, \
		.n_linear_ranges = 1, \
		.vsel_reg = _vsel, \
		.vsel_mask = _mask, \
		.enable_reg = _en, \
		.enable_mask = BIT(0), \
	}, \
}

#define LDO_TABLE(_name, _match, _en, _enbit, _vsel, _mask, _table) { \
	.desc = { \
		.name = _name, \
		.of_match = _match, \
		.ops = &mt6320_ldo_ops, \
		.n_voltages = ARRAY_SIZE(_table), \
		.volt_table = _table, \
		.vsel_reg = _vsel, \
		.vsel_mask = _mask, \
		.enable_reg = _en, \
		.enable_mask = BIT(_enbit), \
	}, \
}

static struct mt6320_info mt6320_regs[] = {
	/* BUCKs */
	BUCK_LINEAR("vproc", "buck_vproc", 0x0214, 0x0218, 0x7f, buck_range_proc),
	BUCK_LINEAR("vsram", "buck_vsram", 0x023A, 0x023E, 0x7f, buck_range_proc),
	BUCK_LINEAR("vcore", "buck_vcore", 0x0266, 0x026A, 0x7f, buck_range_proc),
	BUCK_LINEAR("vm",    "buck_vm",    0x028C, 0x0290, 0x7f, buck_range_proc),
	BUCK_LINEAR("vio18", "buck_vio18", 0x030E, 0x0312, 0x1f, buck_range_io18),
	BUCK_LINEAR("vpa",   "buck_vpa",   0x0330, 0x0334, 0x3f, buck_range_pa),

	/* Key LDOs */
	LDO_TABLE("vmc",      "ldo_vmc1",     0x0424, 12, 0x044A, BIT(4), ldo_1v8_3v3),
	LDO_TABLE("vmch",     "ldo_vmch1",    0x0426, 14, 0x044C, BIT(7), ldo_3v0_3v3),
	LDO_TABLE("vemc_3v3", "ldo_vemc_3v3", 0x0428, 14, 0x044E, BIT(7), ldo_3v0_3v3),
	LDO_TABLE("vemc_1v8", "ldo_vemc_1v8", 0x0462, 14, 0x0464, 0x7 << 5, ldo_gp),
	LDO_TABLE("vgp1",     "ldo_vgp1",     0x042A, 15, 0x0450, 0x7 << 5, ldo_gp),
	LDO_TABLE("vgp2",     "ldo_vgp2",     0x042C, 15, 0x0452, 0x7 << 5, ldo_gp),
	LDO_TABLE("vgp3",     "ldo_vgp3",     0x042E, 15, 0x0454, 0x7 << 5, ldo_gp),
	LDO_TABLE("vsim1",    "ldo_vsim1",    0x0436, 15, 0x045C, 0x7 << 5, ldo_gp),
	LDO_TABLE("vsim2",    "ldo_vsim2",    0x0438, 15, 0x045E, 0x7 << 5, ldo_gp),
	{
		.desc = {
			.name = "vrtc",
			.of_match = "ldo_vrtc",
			.ops = &mt6320_fixed_ops,
			.n_voltages = 1,
			.fixed_uV = 2800000,
			.min_uV = 2800000,
			.uV_step = 1,
			.enable_reg = 0x043A,
			.enable_mask = BIT(8),
		},
	},
};

static struct regmap *mt6320_get_regmap(struct device *dev)
{
	struct regmap *map = dev_get_regmap(dev->parent, NULL);
	if (map)
		return map;
	if (dev->parent && dev->parent->parent)
		return dev_get_regmap(dev->parent->parent, NULL);
	return NULL;
}

static int mt6320_regulator_probe(struct device *dev)
{
	struct regmap *map;
	struct regulator_dev *rdev;
	int i, ret;

	map = mt6320_get_regmap(dev);
	if (!map) {
		dev_err(dev, "no regmap from PWRAP\n");
		return -ENODEV;
	}

	for (i = 0; i < ARRAY_SIZE(mt6320_regs); i++) {
		rdev = xzalloc(sizeof(*rdev));
		rdev->desc = &mt6320_regs[i].desc;
		rdev->regmap = map;
		rdev->dev = dev;

		if (mt6320_regs[i].desc.of_match)
			ret = of_regulator_register(rdev, dev->of_node);
		else
			ret = dev_regulator_register(rdev, mt6320_regs[i].desc.name);
		if (ret)
			dev_warn(dev, "failed to register %s: %d\n",
				 mt6320_regs[i].desc.name, ret);
	}

	dev_info(dev, "MT6320 regulators registered (%d rails)\n",
		 (int)ARRAY_SIZE(mt6320_regs));
	return 0;
}

static const struct of_device_id mt6320_regulator_ids[] = {
	{ .compatible = "mediatek,mt6320-regulator" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mt6320_regulator_ids);

static struct driver mt6320_regulator_driver = {
	.name = "mt6320-regulator",
	.probe = mt6320_regulator_probe,
	.of_compatible = DRV_OF_COMPAT(mt6320_regulator_ids),
};
device_platform_driver(mt6320_regulator_driver);
