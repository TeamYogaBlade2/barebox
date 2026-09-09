// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal MediaTek MT6589 clock driver for barebox secondary bootloader.
 * Provides fixed-rate clocks that match the rates set by preloader/LK.
 * Full mux/divider/gate support can be added later.
 */

#include <common.h>
#include <init.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <of_device.h>
#include <dt-bindings/clock/mediatek,mt6589-clk.h>

static struct clk *clks[128];

static int mt6589_clk_probe(struct device *dev)
{
	struct device_node *np = dev->of_node;
	const char *compat = of_get_property(np, "compatible", NULL);

	/* Provide the most commonly used fixed clocks at the rates
	 * expected after LK handoff.
	 */
	clks[CLK_APMIXED_UNIVPLL_48M] = clk_fixed("univpll_48m", 48000000);
	clks[CLK_APMIXED_MAINPLL_806M] = clk_fixed("mainpll_806m", 806000000);
	clks[CLK_APMIXED_UNIVPLL_624M] = clk_fixed("univpll_624m", 624000000);
	clks[CLK_APMIXED_UNIVPLL_416M] = clk_fixed("univpll_416m", 416000000);

	/* simple fixed for peri etc */
	clk_fixed("clk26m", 26000000);
	clk_fixed("clk32k", 32000);
	clk_fixed("clk13m", 13000000);

	of_clk_add_provider(np, of_clk_src_onecell_get, clks);

	dev_info(dev, "MT6589 clocks (minimal fixed) registered for %s\n",
		 compat ? compat : "unknown");
	return 0;
}

static const struct of_device_id mt6589_clk_ids[] = {
	{ .compatible = "mediatek,mt6589-topckgen" },
	{ .compatible = "mediatek,mt6589-apmixedsys" },
	{ .compatible = "mediatek,mt6589-pericfg" },
	{ .compatible = "mediatek,mt6589-infracfg" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mt6589_clk_ids);

static struct driver mt6589_clk_driver = {
	.name = "clk-mt6589",
	.probe = mt6589_clk_probe,
	.of_compatible = DRV_OF_COMPAT(mt6589_clk_ids),
};
core_platform_driver(mt6589_clk_driver);
