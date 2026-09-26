// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MT6320 PMIC MFD core for barebox
 *
 * Uses the parent PWRAP regmap. Populates DT children (regulator, rtc, keys…).
 */

#include <common.h>
#include <init.h>
#include <of_device.h>
#include <linux/err.h>
#include <linux/regmap.h>

#define MT6320_CID		0x0100

struct mt6320_chip {
	struct device *dev;
	struct regmap *regmap;
	unsigned int chip_id;
};

static int mt6320_probe(struct device *dev)
{
	struct mt6320_chip *chip;
	unsigned int id = 0;
	int ret;

	chip = xzalloc(sizeof(*chip));
	chip->dev = dev;

	/* Regmap is provided by the parent PWRAP driver */
	chip->regmap = dev_get_regmap(dev->parent, NULL);
	if (!chip->regmap) {
		dev_err(dev, "parent regmap not found\n");
		return -ENODEV;
	}

	ret = regmap_read(chip->regmap, MT6320_CID, &id);
	if (ret) {
		dev_err(dev, "failed to read CID: %d\n", ret);
		return ret;
	}

	chip->chip_id = (id >> 8) & 0xff; /* same shift as Linux mt6320_core */
	dev->priv = chip;

	dev_info(dev, "MT6320 PMIC detected, CID=0x%04x (chip_id=0x%02x)\n",
		 id, chip->chip_id);

	of_platform_populate(dev->of_node, NULL, dev);
	return 0;
}

static const struct of_device_id mt6320_ids[] = {
	{ .compatible = "mediatek,mt6320" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mt6320_ids);

static struct driver mt6320_driver = {
	.name = "mt6320",
	.probe = mt6320_probe,
	.of_compatible = DRV_OF_COMPAT(mt6320_ids),
};
device_platform_driver(mt6320_driver);
