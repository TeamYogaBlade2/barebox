// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek T-PHY driver (minimal for MT6589 secondary bootloader)
 * Full bank programming deferred; just registers the PHY provider.
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/phy/phy.h>
#include <of_device.h>
#include <of_address.h>

struct mtk_tphy {
	struct device *dev;
	void __iomem *sif_base;
	struct clk *ref_clk;
};

static int mtk_phy_init(struct phy *phy)
{
	return 0;
}

static int mtk_phy_power_on(struct phy *phy)
{
	/* TODO: full V1 bank init when clocks available */
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
	struct phy *phy;

	if (args->args_count != 1)
		return ERR_PTR(-EINVAL);

	phy = phy_create(dev, NULL, &mtk_tphy_ops);
	if (IS_ERR(phy))
		return phy;

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

	dev_info(dev, "MediaTek T-PHY registered (minimal)\n");
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
