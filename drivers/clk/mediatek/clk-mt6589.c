// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MT6589 clock driver for barebox secondary bootloader.
 * Registers fixed-rate and fixed-factor clocks matching rates left by
 * preloader/LK. This is sufficient for most peripheral probes.
 */

#include <common.h>
#include <init.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <of_device.h>
#include <dt-bindings/clock/mediatek,mt6589-clk.h>

#define MAX_CLKS 128
static struct clk *clks[MAX_CLKS];
static struct clk_onecell_data clk_data = {
	.clks = clks,
	.clk_num = MAX_CLKS,
};

static void register_fixed(int id, const char *name, unsigned long rate)
{
	if (id >= MAX_CLKS)
		return;
	clks[id] = clk_fixed(name, rate);
}

static void register_factor(int id, const char *name, const char *parent,
			  int mult, int div)
{
	struct clk *p;
	if (id >= MAX_CLKS)
		return;
	/* find parent by name if already registered, else create fixed */
	p = clk_lookup(parent);
	if (!p)
		p = clk_fixed(parent, 0); /* placeholder */
	clks[id] = clk_fixed_factor(name, parent, mult, div, 0);
}

static int mt6589_apmixed_probe(struct device *dev)
{
	register_fixed(CLK_APMIXED_ARMPLL, "armpll", 1300000000);
	register_fixed(CLK_APMIXED_MAINPLL, "mainpll", 806000000);
	register_fixed(CLK_APMIXED_UNIVPLL, "univpll", 1248000000);
	register_fixed(CLK_APMIXED_MMPLL, "mmpll", 900000000);
	register_fixed(CLK_APMIXED_ISPPLL, "isppll", 208000000);
	register_fixed(CLK_APMIXED_MSDCPLL, "msdcpll", 208000000);
	register_fixed(CLK_APMIXED_TVDPLL, "tvdpll", 445500000);
	register_fixed(CLK_APMIXED_LVDSPLL, "lvdspll", 180000000);

	register_fixed(CLK_APMIXED_ARMPLL_1300M, "armpll_1300m", 1300000000);
	register_fixed(CLK_APMIXED_MAINPLL_806M, "mainpll_806m", 806000000);
	register_fixed(CLK_APMIXED_MAINPLL_537P3M, "mainpll_537p3m", 537300000);
	register_fixed(CLK_APMIXED_MAINPLL_322P4M, "mainpll_322p4m", 322400000);
	register_fixed(CLK_APMIXED_MAINPLL_230P3M, "mainpll_230p3m", 230300000);

	register_fixed(CLK_APMIXED_UNIVPLL_624M, "univpll_624m", 624000000);
	register_fixed(CLK_APMIXED_UNIVPLL_416M, "univpll_416m", 416000000);
	register_fixed(CLK_APMIXED_UNIVPLL_249P6M, "univpll_249p6m", 249600000);
	register_fixed(CLK_APMIXED_UNIVPLL_178P3M, "univpll_178p3m", 178300000);
	register_fixed(CLK_APMIXED_UNIVPLL_48M, "univpll_48m", 48000000);
	register_fixed(CLK_APMIXED_UNIVPLL_USB_48M, "univpll_usb_48m", 48000000);

	register_fixed(CLK_APMIXED_MMPLL_D2, "mmpll_d2", 450000000);
	register_fixed(CLK_APMIXED_MMPLL_D3, "mmpll_d3", 300000000);
	register_fixed(CLK_APMIXED_MMPLL_D5, "mmpll_d5", 180000000);
	register_fixed(CLK_APMIXED_MMPLL_D7, "mmpll_d7", 128500000);
	register_fixed(CLK_APMIXED_ISPPLL_208M, "isppll_208m", 208000000);
	register_fixed(CLK_APMIXED_MSDCPLL_208M, "msdcpll_208m", 208000000);
	register_fixed(CLK_APMIXED_TVDPLL_148P5M, "tvdpll_148p5m", 148500000);
	register_fixed(CLK_APMIXED_LVDSPLL_180M, "lvdspll_180m", 180000000);

	of_clk_add_provider(dev->of_node, of_clk_src_onecell_get, &clk_data);
	dev_info(dev, "MT6589 apmixedsys clocks registered\n");
	return 0;
}

static int mt6589_topckgen_probe(struct device *dev)
{
	/* base fixed */
	clk_fixed("clk26m", 26000000);
	clk_fixed("clk32k", 32000);
	clk_fixed("clk13m", 13000000);
	clk_fixed("clk_null", 0);

	register_fixed(CLK_TOP_CLK_NULL, "clk_null", 0);
	register_fixed(CLK_TOP_CLKPH_MCK, "clkph_mck", 0);
	register_fixed(CLK_TOP_CPUM_TCK_IN, "cpum_tck_in", 0);

	/* factors derived from mainpll/univpll (rates approximate post-LK) */
	register_fixed(CLK_TOP_SYSPLL_D2, "syspll_d2", 403000000);
	register_fixed(CLK_TOP_SYSPLL_D3, "syspll_d3", 268666666);
	register_fixed(CLK_TOP_SYSPLL_D4, "syspll_d4", 201500000);
	register_fixed(CLK_TOP_SYSPLL_D5, "syspll_d5", 161200000);
	register_fixed(CLK_TOP_SYSPLL_D6, "syspll_d6", 134333333);
	register_fixed(CLK_TOP_SYSPLL_D8, "syspll_d8", 100750000);
	register_fixed(CLK_TOP_SYSPLL_D10, "syspll_d10", 80600000);
	register_fixed(CLK_TOP_SYSPLL_D16, "syspll_d16", 50375000);
	register_fixed(CLK_TOP_SYSPLL_D24, "syspll_d24", 33583333);

	register_fixed(CLK_TOP_UNIVPLL_D3, "univpll_d3", 416000000);
	register_fixed(CLK_TOP_UNIVPLL_D5, "univpll_d5", 249600000);
	register_fixed(CLK_TOP_UNIVPLL_D7, "univpll_d7", 178285714);
	register_fixed(CLK_TOP_UNIVPLL_D10, "univpll_d10", 124800000);
	register_fixed(CLK_TOP_UNIVPLL_D26, "univpll_d26", 48000000);

	register_fixed(CLK_TOP_UNIVPLL1_D2, "univpll1_d2", 312000000);
	register_fixed(CLK_TOP_UNIVPLL1_D4, "univpll1_d4", 156000000);
	register_fixed(CLK_TOP_UNIVPLL1_D6, "univpll1_d6", 104000000);
	register_fixed(CLK_TOP_UNIVPLL1_D8, "univpll1_d8", 78000000);
	register_fixed(CLK_TOP_UNIVPLL1_D10, "univpll1_d10", 62400000);

	register_fixed(CLK_TOP_UNIVPLL2_D2, "univpll2_d2", 208000000);
	register_fixed(CLK_TOP_UNIVPLL2_D4, "univpll2_d4", 104000000);
	register_fixed(CLK_TOP_UNIVPLL2_D6, "univpll2_d6", 69333333);
	register_fixed(CLK_TOP_UNIVPLL2_D8, "univpll2_d8", 52000000);

	of_clk_add_provider(dev->of_node, of_clk_src_onecell_get, &clk_data);
	dev_info(dev, "MT6589 topckgen clocks registered\n");
	return 0;
}

static int mt6589_peri_infra_probe(struct device *dev)
{
	/* Many peri/infra clocks are gates; for secondary we just provide
	 * a few commonly referenced ones as fixed so clk_get succeeds.
	 */
	clk_fixed("peri_usb0", 48000000);
	clk_fixed("peri_usb1", 48000000);
	clk_fixed("infra_m4u", 0);
	of_clk_add_provider(dev->of_node, of_clk_src_onecell_get, &clk_data);
	dev_info(dev, "MT6589 peri/infra clocks (minimal) registered\n");
	return 0;
}

static int mt6589_clk_probe(struct device *dev)
{
	const char *compat = of_get_property(dev->of_node, "compatible", NULL);

	if (!compat)
		return -EINVAL;

	if (strstr(compat, "apmixedsys"))
		return mt6589_apmixed_probe(dev);
	if (strstr(compat, "topckgen"))
		return mt6589_topckgen_probe(dev);
	if (strstr(compat, "pericfg") || strstr(compat, "infracfg"))
		return mt6589_peri_infra_probe(dev);

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
