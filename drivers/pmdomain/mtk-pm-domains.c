// SPDX-License-Identifier: GPL-2.0-only
/* Minimal MediaTek power domains for MT6589 */
#include <common.h>
#include <init.h>
#include <of_device.h>

static int mtk_pmdomain_probe(struct device *dev)
{
	dev_info(dev, "MTK power domains (minimal) probed\n");
	return 0;
}

static const struct of_device_id mtk_pmdomain_ids[] = {
	{ .compatible = "mediatek,mt6589-scpsys" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_pmdomain_ids);

static struct driver mtk_pmdomain_driver = {
	.name = "mtk-pm-domains",
	.probe = mtk_pmdomain_probe,
	.of_compatible = DRV_OF_COMPAT(mtk_pmdomain_ids),
};
device_platform_driver(mtk_pmdomain_driver);
