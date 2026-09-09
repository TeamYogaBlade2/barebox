// SPDX-License-Identifier: GPL-2.0-only
/* Minimal MediaTek keypad for MT6589 secondary bootloader */
#include <common.h>
#include <init.h>
#include <of_device.h>

static int mtk_keypad_probe(struct device *dev)
{
	dev_info(dev, "MTK keypad (minimal) probed\n");
	return 0;
}

static const struct of_device_id mtk_keypad_ids[] = {
	{ .compatible = "mediatek,mt6779-keypad" },
	{ .compatible = "mediatek,kp" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_keypad_ids);

static struct driver mtk_keypad_driver = {
	.name = "mtk-keypad",
	.probe = mtk_keypad_probe,
	.of_compatible = DRV_OF_COMPAT(mtk_keypad_ids),
};
device_platform_driver(mtk_keypad_driver);
