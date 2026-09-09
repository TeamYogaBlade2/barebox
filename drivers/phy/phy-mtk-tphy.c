// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek T-PHY driver for MT6589 secondary bootloader
 * Implements basic U2 power-on sequence (OTG/VBUS bits).
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

/* U2 PHY common registers (relative to instance base) */
#define U3P_USBPHYACR6		0x018
#define PA6_RG_U2_OTG_VBUSCMP_EN	BIT(20)

#define U3P_U2PHYDTM0		0x068
#define P2C_FORCE_UART_EN		BIT(26)
#define P2C_FORCE_SUSPENDM		BIT(14)
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
};

struct mtk_phy_instance {
	void __iomem *port_base;
	struct phy *phy;
};

static void mtk_phy_set_bits(void __iomem *reg, u32 bits)
{
	u32 tmp = readl(reg);
	tmp |= bits;
	writel(tmp, reg);
}

static void mtk_phy_clear_bits(void __iomem *reg, u32 bits)
{
	u32 tmp = readl(reg);
	tmp &= ~bits;
	writel(tmp, reg);
}

static int mtk_phy_init(struct phy *phy)
{
	return 0;
}

static int mtk_phy_power_on(struct phy *phy)
{
	struct mtk_phy_instance *inst = phy_get_drvdata(phy);
	void __iomem *com;

	if (!inst || !inst->port_base)
		return 0;

	com = inst->port_base;

	/* Basic U2 power-on for OTG/host use after LK */
	mtk_phy_set_bits(com + U3P_USBPHYACR6, PA6_RG_U2_OTG_VBUSCMP_EN);
	mtk_phy_set_bits(com + U3P_U2PHYDTM1, P2C_RG_VBUSVALID | P2C_RG_AVALID);
	mtk_phy_clear_bits(com + U3P_U2PHYDTM1, P2C_RG_SESSEND);

	/* Ensure UART path off */
	mtk_phy_clear_bits(com + U3P_U2PHYDTM0, P2C_FORCE_UART_EN);
	mtk_phy_clear_bits(com + U3P_U2PHYDTM1, P2C_RG_UART_EN);

	return 0;
}

static int mtk_phy_power_off(struct phy *phy)
{
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

	/* The PHY port node is the args->np or we use the parent ranges */
	ret = of_address_to_resource(args->np, 0, &res);
	if (!ret)
		inst->port_base = IOMEM(res.start);
	else if (tphy->sif_base)
		/* fallback offset for first U2 port */
		inst->port_base = tphy->sif_base + 0x800;

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

	tphy = xzalloc(sizeof(*tphy));
	tphy->dev = dev;
	dev->priv = tphy;

	res = dev_get_resource(dev, IORESOURCE_MEM, 0);
	if (!IS_ERR(res))
		tphy->sif_base = IOMEM(res->start);

	tphy->ref_clk = clk_get(dev, "ref");
	if (!IS_ERR_OR_NULL(tphy->ref_clk))
		clk_enable(tphy->ref_clk);

	provider = of_phy_provider_register(dev, mtk_phy_xlate);
	if (IS_ERR(provider))
		return PTR_ERR(provider);

	dev_info(dev, "MediaTek T-PHY registered\n");
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
