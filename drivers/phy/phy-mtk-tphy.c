// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek T-PHY driver for MT6589 secondary bootloader
 * Includes the mandatory MT6589 U2 PHY recover/savecurrent workaround.
 * Without mt6589_u2_phy_recover(), USB does not function on MT6589.
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/phy/phy.h>
#include <of_device.h>
#include <of_address.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <clock.h>

/* U2 PHY registers (relative to instance / port base) */
#define U3P_USBPHYACR3		0x01c
#define PA3_RG_USB20_PUPD_BIST_EN	BIT(12)

#define U3P_USBPHYACR6		0x018
#define PA6_RG_U2_BC11_SW_EN		BIT(23)
#define PA6_RG_U2_OTG_VBUSCMP_EN	BIT(20)

#define U3D_U2PHYDCR0		0x060
#define P2C_RG_USB20_PLL_STABLE		BIT(25)
#define P2C_RG_SIF_U2PLL_FORCE_ON	BIT(24)

#define U3P_U2PHYDTM0		0x068
#define P2C_FORCE_UART_EN		BIT(26)
#define P2C_FORCE_DATAIN		BIT(23)
#define P2C_FORCE_DM_PULLDOWN		BIT(21)
#define P2C_FORCE_DP_PULLDOWN		BIT(20)
#define P2C_FORCE_XCVRSEL		BIT(19)
#define P2C_FORCE_SUSPENDM		BIT(18)
#define P2C_FORCE_TERMSEL		BIT(17)
#define P2C_RG_DATAIN			GENMASK(13, 10)
#define P2C_RG_DMPULLDOWN		BIT(7)
#define P2C_RG_DPPULLDOWN		BIT(6)
#define P2C_RG_XCVRSEL			GENMASK(5, 4)
#define P2C_RG_TERMSEL			BIT(2)
#define P2C_RG_SUSPENDM			BIT(3)

#define U3P_U2PHYDTM1		0x06c
#define P2C_RG_VBUSVALID		BIT(23)
#define P2C_RG_AVALID			BIT(20)
#define P2C_RG_SESSEND			BIT(16)
#define P2C_RG_UART_EN			BIT(16)

struct mtk_tphy {
	struct device *dev;
	void __iomem *sif_base;
	struct clk *ref_clk;
	bool need_mt6589_workaround;
};

struct mtk_phy_instance {
	void __iomem *port_base;
	struct phy *phy;
};

static void mtk_phy_set_bits(void __iomem *reg, u32 bits)
{
	writel(readl(reg) | bits, reg);
}

static void mtk_phy_clear_bits(void __iomem *reg, u32 bits)
{
	writel(readl(reg) & ~bits, reg);
}

/*
 * MT6589 specific recover sequence.
 * Must be called on power_on; otherwise USB is completely non-functional.
 */
static void mt6589_u2_phy_recover(void __iomem *com)
{
	/* PUPD_BIST_EN clear (PMIC charger detection) */
	mtk_phy_clear_bits(com + U3P_USBPHYACR3, PA3_RG_USB20_PUPD_BIST_EN);

	/* Clear UART force and normal bits to ensure USB function */
	mtk_phy_clear_bits(com + U3P_U2PHYDTM0, P2C_FORCE_UART_EN);
	mtk_phy_clear_bits(com + U3P_U2PHYDTM1, P2C_RG_UART_EN);

	/* Release force suspendm */
	mtk_phy_clear_bits(com + U3P_U2PHYDTM0, P2C_FORCE_SUSPENDM);

	/* Clear all previously forced pull/termination/xcvr settings */
	mtk_phy_clear_bits(com + U3P_U2PHYDTM0,
			   P2C_RG_DMPULLDOWN | P2C_RG_DPPULLDOWN |
			   P2C_RG_XCVRSEL | P2C_RG_TERMSEL | P2C_RG_DATAIN |
			   P2C_FORCE_DATAIN | P2C_FORCE_DM_PULLDOWN |
			   P2C_FORCE_DP_PULLDOWN | P2C_FORCE_XCVRSEL |
			   P2C_FORCE_TERMSEL | P2C_FORCE_SUSPENDM);

	/* BC1.2 disable, OTG VBUS comparator enable */
	mtk_phy_clear_bits(com + U3P_USBPHYACR6, PA6_RG_U2_BC11_SW_EN);
	mtk_phy_set_bits(com + U3P_USBPHYACR6, PA6_RG_U2_OTG_VBUSCMP_EN);

	udelay(800);
}

static void mt6589_u2_phy_savecurrent(void __iomem *com)
{
	/* Ensure UART off */
	mtk_phy_clear_bits(com + U3P_U2PHYDTM0, P2C_FORCE_UART_EN);
	mtk_phy_clear_bits(com + U3P_U2PHYDTM1, P2C_RG_UART_EN);

	/* Release force suspendm briefly to configure pulldowns */
	mtk_phy_clear_bits(com + U3P_U2PHYDTM0, P2C_FORCE_SUSPENDM);

	/* Set DP/DM pulldowns, XCVRSEL=01, TERMSEL=1, clear DATAIN */
	u32 tm0 = readl(com + U3P_U2PHYDTM0);
	tm0 |= P2C_RG_DMPULLDOWN | P2C_RG_DPPULLDOWN;
	tm0 &= ~P2C_RG_XCVRSEL;
	tm0 |= FIELD_PREP(P2C_RG_XCVRSEL, 1); /* 01 */
	tm0 |= P2C_RG_TERMSEL;
	tm0 &= ~P2C_RG_DATAIN;
	writel(tm0, com + U3P_U2PHYDTM0);

	/* Force DP/DM pulldown, XCVRSEL, TERMSEL, DATAIN */
	mtk_phy_set_bits(com + U3P_U2PHYDTM0,
			 P2C_FORCE_DATAIN | P2C_FORCE_DM_PULLDOWN |
			 P2C_FORCE_DP_PULLDOWN | P2C_FORCE_XCVRSEL |
			 P2C_FORCE_TERMSEL);

	/* Disable BC1.2 and OTG VBUS comparator */
	mtk_phy_clear_bits(com + U3P_USBPHYACR6,
			   PA6_RG_U2_BC11_SW_EN | PA6_RG_U2_OTG_VBUSCMP_EN);

	udelay(800);

	/* Set PLL stable bit */
	mtk_phy_set_bits(com + U3D_U2PHYDCR0, P2C_RG_USB20_PLL_STABLE);
	udelay(1);

	/* Assert force suspendm to enter low power */
	mtk_phy_set_bits(com + U3P_U2PHYDTM0, P2C_FORCE_SUSPENDM);
	udelay(1);
}

static int mtk_phy_init(struct phy *phy)
{
	return 0;
}

static int mtk_phy_power_on(struct phy *phy)
{
	struct mtk_phy_instance *inst = phy_get_drvdata(phy);
	struct mtk_tphy *tphy = phy->dev.parent ? phy->dev.parent->priv : NULL;
	void __iomem *com;

	if (!inst || !inst->port_base)
		return 0;

	com = inst->port_base;

	/* Common U2 power-on */
	mtk_phy_set_bits(com + U3P_USBPHYACR6, PA6_RG_U2_OTG_VBUSCMP_EN);
	mtk_phy_set_bits(com + U3P_U2PHYDTM1, P2C_RG_VBUSVALID | P2C_RG_AVALID);
	mtk_phy_clear_bits(com + U3P_U2PHYDTM1, P2C_RG_SESSEND);

	/* Mandatory for MT6589 */
	if (tphy && tphy->need_mt6589_workaround)
		mt6589_u2_phy_recover(com);

	return 0;
}

static int mtk_phy_power_off(struct phy *phy)
{
	struct mtk_phy_instance *inst = phy_get_drvdata(phy);
	struct mtk_tphy *tphy = phy->dev.parent ? phy->dev.parent->priv : NULL;
	void __iomem *com;

	if (!inst || !inst->port_base)
		return 0;

	com = inst->port_base;

	mtk_phy_clear_bits(com + U3P_USBPHYACR6, PA6_RG_U2_OTG_VBUSCMP_EN);
	mtk_phy_clear_bits(com + U3P_U2PHYDTM1, P2C_RG_VBUSVALID | P2C_RG_AVALID);
	mtk_phy_set_bits(com + U3P_U2PHYDTM1, P2C_RG_SESSEND);

	if (tphy && tphy->need_mt6589_workaround)
		mt6589_u2_phy_savecurrent(com);

	return 0;
}

static const struct phy_ops mtk_tphy_ops = {
	.init		= mtk_phy_init,
	.power_on	= mtk_phy_power_on,
	.power_off	= mtk_phy_power_off,
};

static struct phy *mtk_phy_xlate(struct device *dev,
				 const struct of_phandle_args *args)
{
	struct mtk_tphy *tphy = dev->priv;
	struct mtk_phy_instance *inst;
	struct phy *phy;
	struct resource res;
	int ret;

	if (args->args_count < 1)
		return ERR_PTR(-EINVAL);

	inst = xzalloc(sizeof(*inst));

	ret = of_address_to_resource(args->np, 0, &res);
	if (!ret)
		inst->port_base = IOMEM(res.start);
	else if (tphy->sif_base)
		inst->port_base = tphy->sif_base + 0x800; /* first U2 port */

	phy = phy_create(dev, args->np, &mtk_tphy_ops);
	if (IS_ERR(phy)) {
		free(inst);
		return phy;
	}

	phy_set_drvdata(phy, inst);
	inst->phy = phy;
	return phy;
}

static int mtk_tphy_probe(struct device *dev)
{
	struct mtk_tphy *tphy;
	struct resource *res;
	struct phy_provider *provider;
	const struct of_device_id *match;

	tphy = xzalloc(sizeof(*tphy));
	tphy->dev = dev;
	dev->priv = tphy;

	/* MT6589 always needs the workaround */
	tphy->need_mt6589_workaround = true;
	if (of_device_is_compatible(dev->of_node, "mediatek,generic-tphy-v1"))
		tphy->need_mt6589_workaround =
			of_device_is_compatible(dev->of_node, "mediatek,mt6589-tphy");

	res = dev_get_resource(dev, IORESOURCE_MEM, 0);
	if (!IS_ERR(res))
		tphy->sif_base = IOMEM(res->start);

	tphy->ref_clk = clk_get(dev, "ref");
	if (!IS_ERR_OR_NULL(tphy->ref_clk))
		clk_enable(tphy->ref_clk);

	provider = of_phy_provider_register(dev, mtk_phy_xlate);
	if (IS_ERR(provider))
		return PTR_ERR(provider);

	dev_info(dev, "MediaTek T-PHY registered (MT6589 workaround %s)\n",
		 tphy->need_mt6589_workaround ? "enabled" : "disabled");
	return 0;
}

static const struct of_device_id mtk_tphy_ids[] = {
	{ .compatible = "mediatek,mt6589-tphy" },
	{ .compatible = "mediatek,generic-tphy-v1" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_tphy_ids);

static struct driver mtk_tphy_driver = {
	.name = "mtk-tphy",
	.probe = mtk_tphy_probe,
	.of_compatible = DRV_OF_COMPAT(mtk_tphy_ids),
};
device_platform_driver(mtk_tphy_driver);
