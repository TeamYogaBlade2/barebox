// SPDX-License-Identifier: GPL-2.0-only
/*
 * Regulator driver for MediaTek MT6320 PMIC
 *
 * Based on Linux drivers/regulator/mt6320-regulator.c.
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
	u32 vselon_reg;
	u32 vselctrl_reg;
	u32 vselctrl_mask;
};

/* BUCK voltage ranges (uV). */
static const struct regulator_linear_range buck_range_proc[] = {
	REGULATOR_LINEAR_RANGE(700000, 0, 0x7f, 6250),
};

static const struct regulator_linear_range buck_range_io18[] = {
	REGULATOR_LINEAR_RANGE(1500000, 0, 0x1f, 20000),
};

static const struct regulator_linear_range buck_range_pa[] = {
	REGULATOR_LINEAR_RANGE(500000, 0, 0x3f, 50000),
};

static const struct regulator_linear_range buck_range_vrf18[] = {
	REGULATOR_LINEAR_RANGE(1050000, 0, 0x1f, 25000),
};

/* LDO voltage tables. */
static const unsigned int ldo_1v8_3v3[] = {
	1800000, 3300000,
};

static const unsigned int ldo_3v0_3v3[] = {
	3000000, 3300000,
};

static const unsigned int ldo_gp[] = {
	1200000, 1300000, 1500000, 1800000,
	2500000, 2800000, 3000000, 3300000,
};

static const unsigned int ldo_1v8_2v85[] = {
	1800000, 2850000,
};

static const unsigned int ldo_va[] = {
	1800000, 2500000,
};

static const unsigned int ldo_vcama[] = {
	1500000, 1800000, 2500000, 2800000,
};

static const unsigned int ldo_vast[] = {
	1200000, 1100000, 1000000, 900000,
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

#define BUCK_LINEAR(_name, _match, _en, _vsel, _mask, _vselon, _vselctrl, _ranges) { \
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
	.vselon_reg = _vselon, \
	.vselctrl_reg = _vselctrl, \
	.vselctrl_mask = BIT(1), \
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

#define FIXED(_name, _match, _en, _enbit, _uv) { \
	.desc = { \
		.name = _name, \
		.of_match = _match, \
		.ops = &mt6320_fixed_ops, \
		.n_voltages = 1, \
		.min_uV = _uv, \
		.uV_step = 1, \
		.fixed_uV = _uv, \
		.enable_reg = _en, \
		.enable_mask = BIT(_enbit), \
	}, \
}

static struct mt6320_info mt6320_regs[] = {
	/* BUCKs */
	BUCK_LINEAR("vproc", "buck_vproc",
		0x0214, 0x0218, 0x7f, 0x021A, 0x0210,
		buck_range_proc),
	BUCK_LINEAR("vsram", "buck_vsram",
		0x023A, 0x023E, 0x7f, 0x0240, 0x0236,
		buck_range_proc),
	BUCK_LINEAR("vcore", "buck_vcore",
		0x0266, 0x026A, 0x7f, 0x026C, 0x0262,
		buck_range_proc),
	BUCK_LINEAR("vm", "buck_vm",
		0x028C, 0x0290, 0x7f, 0x0292, 0x0288,
		buck_range_proc),
	BUCK_LINEAR("vio18", "buck_vio18",
		0x030E, 0x0312, 0x1f, 0x0314, 0x030A,
		buck_range_io18),
	BUCK_LINEAR("vpa", "buck_vpa",
		0x0334, 0x0338, 0x3f, 0x033A, 0x0330,
		buck_range_pa),
	BUCK_LINEAR("vrf18", "buck_vrf18",
		0x035E, 0x0362, 0x1f, 0x0364, 0x035A,
		buck_range_vrf18),
	BUCK_LINEAR("vrf18_2", "buck_vrf18_2",
		0x0388, 0x038C, 0x1f, 0x038E, 0x0384,
		buck_range_vrf18),

	/* Analog LDOs */
	LDO_TABLE("vrf28", "ldo_vrf28",
		0x0400, 12, 0x0412, BIT(3), ldo_1v8_2v85),
	FIXED("vtcxo", "ldo_vtcxo", 0x0402, 10, 2800000),
	LDO_TABLE("va", "ldo_va",
		0x0404, 14, 0x0410, BIT(6), ldo_va),
	FIXED("va28", "ldo_va28", 0x0406, 14, 2800000),
	LDO_TABLE("vcama", "ldo_vcama",
		0x0408, 15, 0x0414, 0x3 << 6, ldo_vcama),
	LDO_TABLE("vrf28_2", "ldo_vrf28_2",
		0x041A, 12, 0x0418, BIT(3), ldo_1v8_2v85),
	LDO_TABLE("vtcxo_2", "ldo_vtcxo_2",
		0x041C, 10, 0x0416, BIT(3), ldo_1v8_2v85),

	/* Digital LDOs */
	FIXED("vio28", "ldo_vio28", 0x0420, 14, 2800000),
	FIXED("vusb", "ldo_vusb", 0x0422, 14, 3300000),
	LDO_TABLE("vmc1", "ldo_vmc1",
		0x0424, 12, 0x044A, BIT(4), ldo_1v8_3v3),
	LDO_TABLE("vmch1", "ldo_vmch1",
		0x0426, 14, 0x044C, BIT(7), ldo_3v0_3v3),
	LDO_TABLE("vemc_3v3", "ldo_vemc_3v3",
		0x0428, 14, 0x044E, BIT(7), ldo_3v0_3v3),
	LDO_TABLE("vgp1", "ldo_vgp1",
		0x042A, 15, 0x0450, 0x7 << 5, ldo_gp),
	LDO_TABLE("vgp2", "ldo_vgp2",
		0x042C, 15, 0x0452, 0x7 << 5, ldo_gp),
	LDO_TABLE("vgp3", "ldo_vgp3",
		0x042E, 15, 0x0454, 0x7 << 5, ldo_gp),
	LDO_TABLE("vgp4", "ldo_vgp4",
		0x0430, 15, 0x0456, 0x7 << 5, ldo_gp),
	LDO_TABLE("vgp5", "ldo_vgp5",
		0x0432, 15, 0x0458, 0x7 << 5, ldo_gp),
	LDO_TABLE("vgp6", "ldo_vgp6",
		0x0434, 15, 0x045A, 0x7 << 5, ldo_gp),
	LDO_TABLE("vsim1", "ldo_vsim1",
		0x0436, 15, 0x045C, 0x7 << 5, ldo_gp),
	LDO_TABLE("vsim2", "ldo_vsim2",
		0x0438, 15, 0x045E, 0x7 << 5, ldo_gp),
	FIXED("vrtc", "ldo_vrtc", 0x043A, 8, 2800000),
	LDO_TABLE("vast", "ldo_vast",
		0x0444, 12, 0x0444, 0x3 << 13, ldo_vast),
	LDO_TABLE("vemc_1v8", "ldo_vemc_1v8",
		0x0462, 14, 0x0464, 0x7 << 5, ldo_gp),
	LDO_TABLE("vibr", "ldo_vibr",
		0x0466, 15, 0x0468, 0x7 << 5, ldo_gp),
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

static int mt6320_set_buck_vosel_reg(struct regmap *map)
{
	unsigned int i;
	unsigned int regval;
	int ret;

	for (i = 0; i < ARRAY_SIZE(mt6320_regs); i++) {
		if (!mt6320_regs[i].vselctrl_reg)
			continue;

		ret = regmap_read(map, mt6320_regs[i].vselctrl_reg, &regval);
		if (ret)
			return ret;

		if (regval & mt6320_regs[i].vselctrl_mask)
			mt6320_regs[i].desc.vsel_reg = mt6320_regs[i].vselon_reg;
	}

	return 0;
}

static int mt6320_regulator_probe(struct device *dev)
{
	struct regmap *map;
	struct regulator_config config = {};
	struct regulator_dev *rdev;
	unsigned int i;
	int ret;

	map = mt6320_get_regmap(dev);
	if (!map) {
		dev_err(dev, "no regmap from PWRAP\n");
		return -ENODEV;
	}

	ret = mt6320_set_buck_vosel_reg(map);
	if (ret) {
		dev_err(dev, "failed to select buck voltage register: %d\n", ret);
		return ret;
	}

	config.dev = dev;
	config.regmap = map;

	for (i = 0; i < ARRAY_SIZE(mt6320_regs); i++) {
		rdev = regulator_register(dev, &mt6320_regs[i].desc, &config);
		if (!rdev) {
			dev_err(dev, "failed to register %s\n",
				mt6320_regs[i].desc.name);
			return -ENOMEM;
		}
	}

	dev_info(dev, "MT6320 regulators registered (%u rails)\n",
		 ARRAY_SIZE(mt6320_regs));
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
