// SPDX-License-Identifier: GPL-2.0-only
/* Minimal MediaTek PWRAP for MT6589 */
#include <common.h>
#include <init.h>
#include <of_device.h>

static int mtk_pwrap_probe(struct device *dev)
{
	dev_info(dev, "MTK PWRAP (minimal) probed\n");
	of_platform_populate(dev->of_node, NULL, dev);
	return 0;
}

static const struct of_device_id mtk_pwrap_ids[] = {
	{ .compatible = "mediatek,mt6589-pwrap" },
	{ .compatible = "mediatek,mt8135-pwrap" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_pwrap_ids);

static struct driver mtk_pwrap_driver = {
	.name = "mtk-pwrap",
	.probe = mtk_pwrap_probe,
	.of_compatible = DRV_OF_COMPAT(mtk_pwrap_ids),
};
device_platform_driver(mtk_pwrap_driver);
