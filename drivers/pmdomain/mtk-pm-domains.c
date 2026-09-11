// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek SCPSYS power domains (minimal) for MT6589
 */

#include <common.h>
#include <init.h>
#include <of_device.h>
#include <io.h>

static int mtk_pmdomain_probe(struct device *dev)
{
	struct resource *res;
	void __iomem *base = NULL;

	res = dev_get_resource(dev, IORESOURCE_MEM, 0);
	if (!IS_ERR(res))
		base = IOMEM(res->start);

	/* For secondary bootloader most domains are already on.
	 * Just acknowledge the node.
	 */
	dev_info(dev, "MTK power domains registered (base %p)\n", base);
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
