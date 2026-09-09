// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MT6589 clock driver for barebox secondary bootloader.
 * - APMIXED / TOPCKGEN: fixed rates matching LK handoff
 * - PERI / INFRA: real gate control via SET/CLR/STA registers
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <of_device.h>
#include <of_address.h>
#include <linux/bitops.h>
#include <dt-bindings/clock/mediatek,mt6589-clk.h>

#define MAX_CLKS 256
static struct clk *clks[MAX_CLKS];
static struct clk_onecell_data clk_data = {
	.clks = clks,
	.clk_num = MAX_CLKS,
};

/* ---- simple fixed helpers ---- */
static void reg_fixed(int id, const char *name, unsigned long rate)
{
	struct clk *c = clk_fixed(name, rate);
	if (id >= 0 && id < MAX_CLKS)
		clks[id] = c;
}

/* ---- MediaTek set/clr gate ---- */
struct mtk_gate {
	struct clk_hw hw;
	void __iomem *set_reg;
	void __iomem *clr_reg;
	void __iomem *sta_reg;
	u8 shift;
	bool set_to_disable; /* if true, writing set bit disables */
};

static int mtk_gate_enable(struct clk_hw *hw)
{
	struct mtk_gate *g = container_of(hw, struct mtk_gate, hw);
	if (g->set_to_disable)
		writel(BIT(g->shift), g->clr_reg);
	else
		writel(BIT(g->shift), g->set_reg);
	return 0;
}

static void mtk_gate_disable(struct clk_hw *hw)
{
	struct mtk_gate *g = container_of(hw, struct mtk_gate, hw);
	if (g->set_to_disable)
		writel(BIT(g->shift), g->set_reg);
	else
		writel(BIT(g->shift), g->clr_reg);
}

static int mtk_gate_is_enabled(struct clk_hw *hw)
{
	struct mtk_gate *g = container_of(hw, struct mtk_gate, hw);
	u32 val = readl(g->sta_reg);
	bool bit = !!(val & BIT(g->shift));
	return g->set_to_disable ? !bit : bit;
}

static struct clk_ops mtk_gate_ops = {
	.enable = mtk_gate_enable,
	.disable = mtk_gate_disable,
	.is_enabled = mtk_gate_is_enabled,
};

static struct clk *mtk_clk_register_gate(const char *name, const char *parent,
					 void __iomem *set, void __iomem *clr,
					 void __iomem *sta, u8 shift,
					 bool set_to_disable)
{
	struct mtk_gate *g = xzalloc(sizeof(*g));
	g->set_reg = set;
	g->clr_reg = clr;
	g->sta_reg = sta;
	g->shift = shift;
	g->set_to_disable = set_to_disable;
	g->hw.clk.name = name;
	g->hw.clk.ops = &mtk_gate_ops;
	g->hw.clk.parent_names = &parent;
	g->hw.clk.num_parents = parent ? 1 : 0;
	return &g->hw.clk;
}

/* ---- APMIXED ---- */
static int mt6589_apmixed_probe(struct device *dev)
{
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

/* ---- TOPCKGEN ---- */
static int mt6589_topckgen_probe(struct device *dev)
{
	clk_fixed("clk26m", 26000000);
	clk_fixed("clk32k", 32000);
	clk_fixed("clk13m", 13000000);
	clk_fixed("clk_null", 0);

	reg_fixed(CLK_TOP_CLK_NULL, "clk_null", 0);
	reg_fixed(CLK_TOP_SYSPLL_D2, "syspll_d2", 403000000);
	reg_fixed(CLK_TOP_SYSPLL_D3, "syspll_d3", 268666666);
	reg_fixed(CLK_TOP_SYSPLL_D3P5, "syspll_d3p5", 230285714);
	reg_fixed(CLK_TOP_SYSPLL_D4, "syspll_d4", 201500000);
	reg_fixed(CLK_TOP_SYSPLL_D5, "syspll_d5", 161200000);
	reg_fixed(CLK_TOP_SYSPLL_D6, "syspll_d6", 134333333);
	reg_fixed(CLK_TOP_SYSPLL_D8, "syspll_d8", 100750000);
	reg_fixed(CLK_TOP_SYSPLL_D10, "syspll_d10", 80600000);
	reg_fixed(CLK_TOP_SYSPLL_D16, "syspll_d16", 50375000);
	reg_fixed(CLK_TOP_SYSPLL_D24, "syspll_d24", 33583333);

	reg_fixed(CLK_TOP_UNIVPLL_D3, "univpll_d3", 416000000);
	reg_fixed(CLK_TOP_UNIVPLL_D5, "univpll_d5", 249600000);
	reg_fixed(CLK_TOP_UNIVPLL_D7, "univpll_d7", 178285714);
	reg_fixed(CLK_TOP_UNIVPLL_D10, "univpll_d10", 124800000);
	reg_fixed(CLK_TOP_UNIVPLL_D26, "univpll_d26", 48000000);

	reg_fixed(CLK_TOP_UNIVPLL1_D2, "univpll1_d2", 312000000);
	reg_fixed(CLK_TOP_UNIVPLL1_D4, "univpll1_d4", 156000000);
	reg_fixed(CLK_TOP_UNIVPLL1_D6, "univpll1_d6", 104000000);
	reg_fixed(CLK_TOP_UNIVPLL1_D8, "univpll1_d8", 78000000);
	reg_fixed(CLK_TOP_UNIVPLL1_D10, "univpll1_d10", 62400000);

	reg_fixed(CLK_TOP_UNIVPLL2_D2, "univpll2_d2", 208000000);
	reg_fixed(CLK_TOP_UNIVPLL2_D4, "univpll2_d4", 104000000);
	reg_fixed(CLK_TOP_UNIVPLL2_D6, "univpll2_d6", 69333333);
	reg_fixed(CLK_TOP_UNIVPLL2_D8, "univpll2_d8", 52000000);

	reg_fixed(CLK_TOP_MMPLL_D4, "mmpll_d4", 225000000);
	reg_fixed(CLK_TOP_MMPLL_D6, "mmpll_d6", 150000000);

	/* approximated mux outputs */
	clk_fixed("axi_sel", 268666666);
	clk_fixed("usb20_sel", 48000000);
	clk_fixed("msdc0_sel", 208000000);
	clk_fixed("msdc1_sel", 208000000);
	clk_fixed("msdc2_sel", 208000000);
	clk_fixed("msdc3_sel", 208000000);
	clk_fixed("msdc4_sel", 208000000);
	clk_fixed("uart_sel", 26000000);
	clk_fixed("spi_sel", 104000000);

	of_clk_add_provider(dev->of_node, of_clk_src_onecell_get, &clk_data);
	dev_info(dev, "MT6589 topckgen clocks registered\n");
	return 0;
}

/* ---- PERI gates (real SET/CLR) ---- */
#define PERI_PDN0_SET	0x0008
#define PERI_PDN0_CLR	0x0010
#define PERI_PDN0_STA	0x0018

static int mt6589_pericfg_probe(struct device *dev)
{
	void __iomem *base;
	struct resource *res;

	res = dev_get_resource(dev, IORESOURCE_MEM, 0);
	if (IS_ERR(res))
		return PTR_ERR(res);
	base = IOMEM(res->start);

	/* Important peri gates – shift numbers from Linux clk-mt6589-pericfg */
	clks[0]  = mtk_clk_register_gate("peri_usb0", "usb20_sel",
			base + PERI_PDN0_SET, base + PERI_PDN0_CLR,
			base + PERI_PDN0_STA, 10, false);
	clks[1]  = mtk_clk_register_gate("peri_usb1", "usb20_sel",
			base + PERI_PDN0_SET, base + PERI_PDN0_CLR,
			base + PERI_PDN0_STA, 11, false);
	clks[2]  = mtk_clk_register_gate("peri_msdc0", "msdc0_sel",
			base + PERI_PDN0_SET, base + PERI_PDN0_CLR,
			base + PERI_PDN0_STA, 13, false);
	clks[3]  = mtk_clk_register_gate("peri_msdc1", "msdc1_sel",
			base + PERI_PDN0_SET, base + PERI_PDN0_CLR,
			base + PERI_PDN0_STA, 14, false);
	clks[4]  = mtk_clk_register_gate("peri_msdc2", "msdc2_sel",
			base + PERI_PDN0_SET, base + PERI_PDN0_CLR,
			base + PERI_PDN0_STA, 15, false);
	clks[5]  = mtk_clk_register_gate("peri_msdc3", "msdc3_sel",
			base + PERI_PDN0_SET, base + PERI_PDN0_CLR,
			base + PERI_PDN0_STA, 16, false);
	clks[6]  = mtk_clk_register_gate("peri_msdc4", "msdc4_sel",
			base + PERI_PDN0_SET, base + PERI_PDN0_CLR,
			base + PERI_PDN0_STA, 17, false);

	/* also keep plain fixed names for compatibility */
	clk_fixed("peri_uart0", 26000000);
	clk_fixed("peri_uart1", 26000000);
	clk_fixed("peri_spi0", 10400000);

	of_clk_add_provider(dev->of_node, of_clk_src_onecell_get, &clk_data);
	dev_info(dev, "MT6589 pericfg gates registered (real SET/CLR)\n");
	return 0;
}

/* ---- INFRA gates ---- */
#define INFRA_PDN_SET	0x0040
#define INFRA_PDN_CLR	0x0044
#define INFRA_PDN_STA	0x0048

static int mt6589_infracfg_probe(struct device *dev)
{
	void __iomem *base;
	struct resource *res;

	res = dev_get_resource(dev, IORESOURCE_MEM, 0);
	if (IS_ERR(res))
		return PTR_ERR(res);
	base = IOMEM(res->start);

	/* a few commonly used infra gates; shifts from Linux */
	clks[0] = mtk_clk_register_gate("infra_smi", "axi_sel",
			base + INFRA_PDN_SET, base + INFRA_PDN_CLR,
			base + INFRA_PDN_STA, 1, false);
	clks[1] = mtk_clk_register_gate("infra_m4u", "axi_sel",
			base + INFRA_PDN_SET, base + INFRA_PDN_CLR,
			base + INFRA_PDN_STA, 8, false);

	of_clk_add_provider(dev->of_node, of_clk_src_onecell_get, &clk_data);
	dev_info(dev, "MT6589 infracfg gates registered (real SET/CLR)\n");
	return 0;
}

enum mt6589_clk_type {
	MT6589_CLK_APMIXED,
	MT6589_CLK_TOPCKGEN,
	MT6589_CLK_PERICFG,
	MT6589_CLK_INFRACFG,
};

static int mt6589_clk_probe(struct device *dev)
{
	const void *data = device_get_match_data(dev);

	if (!data)
		return -ENODEV;

	switch ((uintptr_t)data) {
	case MT6589_CLK_APMIXED:
		return mt6589_apmixed_probe(dev);
	case MT6589_CLK_TOPCKGEN:
		return mt6589_topckgen_probe(dev);
	case MT6589_CLK_PERICFG:
		return mt6589_pericfg_probe(dev);
	case MT6589_CLK_INFRACFG:
		return mt6589_infracfg_probe(dev);
	default:
		return -EINVAL;
	}
}

static const struct of_device_id mt6589_clk_ids[] = {
	{ .compatible = "mediatek,mt6589-apmixedsys",
	  .data = (void *)MT6589_CLK_APMIXED },
	{ .compatible = "mediatek,mt6589-topckgen",
	  .data = (void *)MT6589_CLK_TOPCKGEN },
	{ .compatible = "mediatek,mt6589-pericfg",
	  .data = (void *)MT6589_CLK_PERICFG },
	{ .compatible = "mediatek,mt6589-infracfg",
	  .data = (void *)MT6589_CLK_INFRACFG },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mt6589_clk_ids);

static struct driver mt6589_clk_driver = {
	.name = "clk-mt6589",
	.probe = mt6589_clk_probe,
	.of_compatible = DRV_OF_COMPAT(mt6589_clk_ids),
};
core_platform_driver(mt6589_clk_driver);
