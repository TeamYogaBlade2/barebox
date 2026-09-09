// SPDX-License-Identifier: GPL-2.0-only
/* Minimal MediaTek MSDC (mtk-sd) for MT6589 */
#include <common.h>
#include <init.h>
#include <of_device.h>
#include <mci.h>

static int mtk_sd_probe(struct device *dev)
{
	dev_info(dev, "MTK MSDC (minimal) probed\n");
	return 0;
}

static const struct of_device_id mtk_sd_ids[] = {
	{ .compatible = "mediatek,mt6589-mmc" },
	{ .compatible = "mediatek,mtk-sd" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_sd_ids);

static struct driver mtk_sd_driver = {
	.name = "mtk-sd",
	.probe = mtk_sd_probe,
	.of_compatible = DRV_OF_COMPAT(mtk_sd_ids),
};
device_platform_driver(mtk_sd_driver);
