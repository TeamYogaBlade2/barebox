// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek PWRAP (minimal) for MT6589 secondary bootloader
 * Populates child devices (PMIC) so MT6320 nodes can appear.
 */

#include <common.h>
#include <init.h>
#include <of_device.h>
#include <linux/clk.h>
#include <io.h>

struct mtk_pwrap {
	void __iomem *base;
	struct clk *clk_spi;
	struct clk *clk_sys;
};

static int mtk_pwrap_probe(struct device *dev)
{
	struct mtk_pwrap *pwrap;
	struct resource *res;

	pwrap = xzalloc(sizeof(*pwrap));

	res = dev_get_resource(dev, IORESOURCE_MEM, 0);
	if (!IS_ERR(res))
		pwrap->base = IOMEM(res->start);

	pwrap->clk_spi = clk_get(dev, "spi");
	if (!IS_ERR_OR_NULL(pwrap->clk_spi))
		clk_enable(pwrap->clk_spi);
	pwrap->clk_sys = clk_get(dev, "wrap");
	if (!IS_ERR_OR_NULL(pwrap->clk_sys))
		clk_enable(pwrap->clk_sys);

	/* Populate PMIC and other children */
	of_platform_populate(dev->of_node, NULL, dev);

	dev_info(dev, "MTK PWRAP registered (minimal)\n");
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
