// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MT6589 clock driver for barebox secondary bootloader.
 * Registers the full set of fixed-rate PLLs and fixed-factor dividers
 * from the Linux clk-mt6589-* drivers, using rates left by preloader/LK.
 * Muxes are approximated as fixed selection of the common parent.
 */

#include <common.h>
#include <init.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <of_device.h>
#include <dt-bindings/clock/mediatek,mt6589-clk.h>

#define MAX_CLKS 256
static struct clk *clks[MAX_CLKS];
static struct clk_onecell_data clk_data = {
	.clks = clks,
	.clk_num = MAX_CLKS,
};

static void reg_fixed(int id, const char *name, unsigned long rate)
{
	if (id >= 0 && id < MAX_CLKS)
		clks[id] = clk_fixed(name, rate);
	else
		clk_fixed(name, rate);
}

static void reg_factor(int id, const char *name, const char *parent,
		     unsigned long parent_rate, int mult, int div)
{
	unsigned long rate = 0;
	if (parent_rate && div)
		rate = (parent_rate * mult) / div;
	if (id >= 0 && id < MAX_CLKS)
		clks[id] = clk_fixed(name, rate ? rate : parent_rate);
	else
		clk_fixed(name, rate ? rate : parent_rate);
}

static int mt6589_apmixed_probe(struct device *dev)
{
	/* PLLs and their derived fixed outputs (rates from typical LK) */
	reg_fixed(CLK_APMIXED_ARMPLL, "armpll", 1300000000);
	reg_fixed(CLK_APMIXED_MAINPLL, "mainpll", 806000000);
	reg_fixed(CLK_APMIXED_UNIVPLL, "univpll", 1248000000);
	reg_fixed(CLK_APMIXED_MMPLL, "mmpll", 900000000);
	reg_fixed(CLK_APMIXED_ISPPLL, "isppll", 208000000);
	reg_fixed(CLK_APMIXED_MSDCPLL, "msdcpll", 208000000);
	reg_fixed(CLK_APMIXED_TVDPLL, "tvdpll", 445500000);
	reg_fixed(CLK_APMIXED_LVDSPLL, "lvdspll", 180000000);

	reg_fixed(CLK_APMIXED_ARMPLL_1300M, "armpll_1300m", 1300000000);
	reg_fixed(CLK_APMIXED_MAINPLL_806M, "mainpll_806m", 806000000);
	reg_fixed(CLK_APMIXED_MAINPLL_537P3M, "mainpll_537p3m", 537300000);
	reg_fixed(CLK_APMIXED_MAINPLL_322P4M, "mainpll_322p4m", 322400000);
	reg_fixed(CLK_APMIXED_MAINPLL_230P3M, "mainpll_230p3m", 230300000);

	reg_fixed(CLK_APMIXED_UNIVPLL_624M, "univpll_624m", 624000000);
	reg_fixed(CLK_APMIXED_UNIVPLL_416M, "univpll_416m", 416000000);
	reg_fixed(CLK_APMIXED_UNIVPLL_249P6M, "univpll_249p6m", 249600000);
	reg_fixed(CLK_APMIXED_UNIVPLL_178P3M, "univpll_178p3m", 178300000);
	reg_fixed(CLK_APMIXED_UNIVPLL_48M, "univpll_48m", 48000000);
	reg_fixed(CLK_APMIXED_UNIVPLL_USB_48M, "univpll_usb_48m", 48000000);

	reg_fixed(CLK_APMIXED_MMPLL_D2, "mmpll_d2", 450000000);
	reg_fixed(CLK_APMIXED_MMPLL_D3, "mmpll_d3", 300000000);
	reg_fixed(CLK_APMIXED_MMPLL_D5, "mmpll_d5", 180000000);
	reg_fixed(CLK_APMIXED_MMPLL_D7, "mmpll_d7", 128500000);
	reg_fixed(CLK_APMIXED_ISPPLL_208M, "isppll_208m", 208000000);
	reg_fixed(CLK_APMIXED_MSDCPLL_208M, "msdcpll_208m", 208000000);
	reg_fixed(CLK_APMIXED_TVDPLL_148P5M, "tvdpll_148p5m", 148500000);
	reg_fixed(CLK_APMIXED_LVDSPLL_180M, "lvdspll_180m", 180000000);

	of_clk_add_provider(dev->of_node, of_clk_src_onecell_get, &clk_data);
	dev_info(dev, "MT6589 apmixedsys clocks registered\n");
	return 0;
}

static int mt6589_topckgen_probe(struct device *dev)
{
	/* base oscillators */
	clk_fixed("clk26m", 26000000);
	clk_fixed("clk32k", 32000);
	clk_fixed("clk13m", 13000000);
	clk_fixed("clk_null", 0);

	reg_fixed(CLK_TOP_CLK_NULL, "clk_null", 0);
	reg_fixed(CLK_TOP_CLKPH_MCK, "clkph_mck", 0);
	reg_fixed(CLK_TOP_CPUM_TCK_IN, "cpum_tck_in", 0);

	/* syspll factors from mainpll_806m */
	reg_factor(CLK_TOP_SYSPLL_D2, "syspll_d2", "mainpll_806m", 806000000, 1, 2);
	reg_factor(CLK_TOP_SYSPLL_D3, "syspll_d3", "mainpll_806m", 806000000, 1, 3);
	reg_factor(CLK_TOP_SYSPLL_D3P5, "syspll_d3p5", "mainpll_806m", 806000000, 2, 7);
	reg_factor(CLK_TOP_SYSPLL_D4, "syspll_d4", "mainpll_806m", 806000000, 1, 4);
	reg_factor(CLK_TOP_SYSPLL_D5, "syspll_d5", "mainpll_806m", 806000000, 1, 5);
	reg_factor(CLK_TOP_SYSPLL_D6, "syspll_d6", "mainpll_806m", 806000000, 1, 6);
	reg_factor(CLK_TOP_SYSPLL_D8, "syspll_d8", "mainpll_806m", 806000000, 1, 8);
	reg_factor(CLK_TOP_SYSPLL_D10, "syspll_d10", "mainpll_806m", 806000000, 1, 10);
	reg_factor(CLK_TOP_SYSPLL_D16, "syspll_d16", "mainpll_806m", 806000000, 1, 16);
	reg_factor(CLK_TOP_SYSPLL_D24, "syspll_d24", "mainpll_806m", 806000000, 1, 24);

	/* univpll factors */
	reg_factor(CLK_TOP_UNIVPLL_D3, "univpll_d3", "univpll_416m", 416000000, 1, 1);
	reg_factor(CLK_TOP_UNIVPLL_D5, "univpll_d5", "univpll_249p6m", 249600000, 1, 1);
	reg_factor(CLK_TOP_UNIVPLL_D7, "univpll_d7", "univpll_178p3m", 178300000, 1, 1);
	reg_factor(CLK_TOP_UNIVPLL_D10, "univpll_d10", "univpll_249p6m", 249600000, 1, 2);
	reg_factor(CLK_TOP_UNIVPLL_D26, "univpll_d26", "univpll_48m", 48000000, 1, 1);

	reg_factor(CLK_TOP_UNIVPLL1_D2, "univpll1_d2", "univpll_624m", 624000000, 1, 2);
	reg_factor(CLK_TOP_UNIVPLL1_D4, "univpll1_d4", "univpll_624m", 624000000, 1, 4);
	reg_factor(CLK_TOP_UNIVPLL1_D6, "univpll1_d6", "univpll_624m", 624000000, 1, 6);
	reg_factor(CLK_TOP_UNIVPLL1_D8, "univpll1_d8", "univpll_624m", 624000000, 1, 8);
	reg_factor(CLK_TOP_UNIVPLL1_D10, "univpll1_d10", "univpll_624m", 624000000, 1, 10);

	reg_factor(CLK_TOP_UNIVPLL2_D2, "univpll2_d2", "univpll_416m", 416000000, 1, 2);
	reg_factor(CLK_TOP_UNIVPLL2_D4, "univpll2_d4", "univpll_416m", 416000000, 1, 4);
	reg_factor(CLK_TOP_UNIVPLL2_D6, "univpll2_d6", "univpll_416m", 416000000, 1, 6);
	reg_factor(CLK_TOP_UNIVPLL2_D8, "univpll2_d8", "univpll_416m", 416000000, 1, 8);

	reg_factor(CLK_TOP_MMPLL_D4, "mmpll_d4", "mmpll_d2", 450000000, 1, 2);
	reg_factor(CLK_TOP_MMPLL_D6, "mmpll_d6", "mmpll_d3", 300000000, 1, 2);

	reg_fixed(CLK_TOP_LVDSPLL, "lvdspll_ck", 180000000);
	reg_factor(CLK_TOP_LVDSPLL_D2, "lvdspll_d2", "lvdspll", 180000000, 1, 2);
	reg_factor(CLK_TOP_LVDSPLL_D4, "lvdspll_d4", "lvdspll", 180000000, 1, 4);
	reg_factor(CLK_TOP_LVDSPLL_D8, "lvdspll_d8", "lvdspll", 180000000, 1, 8);

	/* Common mux outputs approximated to the usual parent after LK */
	clk_fixed("axi_sel", 268666666);		/* syspll_d3 */
	clk_fixed("smi_sel", 268666666);
	clk_fixed("mfg_sel", 312000000);		/* univpll1_d2 */
	clk_fixed("irda_sel", 26000000);
	clk_fixed("cam_sel", 268666666);
	clk_fixed("aud_intbus_sel", 134333333);	/* syspll_d6 */
	clk_fixed("jpg_sel", 161200000);		/* syspll_d5 */
	clk_fixed("disp_sel", 268666666);
	clk_fixed("msdc30_0_sel", 208000000);	/* msdcpll */
	clk_fixed("msdc30_1_sel", 208000000);
	clk_fixed("msdc30_2_sel", 208000000);
	clk_fixed("msdc50_3_sel", 208000000);
	clk_fixed("usb20_sel", 48000000);
	clk_fixed("pwm_sel", 26000000);
	clk_fixed("spi_sel", 104000000);
	clk_fixed("uart_sel", 26000000);
	clk_fixed("mem_sel", 0);
	clk_fixed("camtg_sel", 26000000);
	clk_fixed("msdc50_0_sel", 208000000);
	clk_fixed("msdc30_3_sel", 208000000);
	clk_fixed("msdc30_4_sel", 208000000);

	of_clk_add_provider(dev->of_node, of_clk_src_onecell_get, &clk_data);
	dev_info(dev, "MT6589 topckgen clocks registered (full fixed set)\n");
	return 0;
}

static int mt6589_peri_infra_probe(struct device *dev)
{
	const char *compat = of_get_property(dev->of_node, "compatible", NULL);

	/* Peri / infra gates are left enabled by LK; expose commonly used names */
	clk_fixed("peri_usb0", 48000000);
	clk_fixed("peri_usb1", 48000000);
	clk_fixed("peri_msdc0", 208000000);
	clk_fixed("peri_msdc1", 208000000);
	clk_fixed("peri_msdc2", 208000000);
	clk_fixed("peri_msdc3", 208000000);
	clk_fixed("peri_uart0", 26000000);
	clk_fixed("peri_uart1", 26000000);
	clk_fixed("peri_uart2", 26000000);
	clk_fixed("peri_uart3", 26000000);
	clk_fixed("peri_i2c0", 26000000);
	clk_fixed("peri_i2c1", 26000000);
	clk_fixed("peri_i2c2", 26000000);
	clk_fixed("peri_i2c3", 26000000);
	clk_fixed("peri_i2c4", 26000000);
	clk_fixed("peri_i2c5", 26000000);
	clk_fixed("peri_i2c6", 26000000);
	clk_fixed("peri_spi0", 104000000);
	clk_fixed("infra_m4u", 0);
	clk_fixed("infra_smi", 0);
	clk_fixed("infra_audio", 0);

	of_clk_add_provider(dev->of_node, of_clk_src_onecell_get, &clk_data);
	dev_info(dev, "MT6589 %s clocks registered\n",
		 compat ? compat : "peri/infra");
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
