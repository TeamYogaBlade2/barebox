// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MUSB glue for barebox (minimal secondary bootloader support)
 * Full OTG/role-switch/PHY power management deferred until clocks/pinctrl ready.
 */

#include <common.h>
#include <init.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <of_device.h>
#include <linux/phy/phy.h>
#include <linux/usb/musb.h>

struct mtk_musb {
	struct device *dev;
	struct clk_bulk_data clks[3];
	struct phy *phy;
};

static int mtk_musb_probe(struct device *dev)
{
	struct mtk_musb *mtk;
	int ret;

	mtk = xzalloc(sizeof(*mtk));
	mtk->dev = dev;

	/* clocks: main, mcu, univpll - optional until clk driver lands */
	mtk->clks[0].id = "main";
	mtk->clks[1].id = "mcu";
	mtk->clks[2].id = "univpll";
	ret = clk_bulk_get(dev, 3, mtk->clks); if (ret) dev_dbg(dev, "clocks not ready yet\n");
	if (!ret)
		clk_bulk_enable(3, mtk->clks);

	mtk->phy = of_phy_get_by_phandle(dev, "phys", 0);
	if (!IS_ERR_OR_NULL(mtk->phy)) {
		phy_init(mtk->phy);
		phy_power_on(mtk->phy);
	}

	dev_info(dev, "MediaTek MUSB glue probed (minimal)\n");
	return 0;
}

static const struct of_device_id mtk_musb_ids[] = {
	{ .compatible = "mediatek,mt6589-musb" },
	{ .compatible = "mediatek,mtk-musb" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_musb_ids);

static struct driver mtk_musb_driver = {
	.name = "musb-mediatek",
	.probe = mtk_musb_probe,
	.of_compatible = DRV_OF_COMPAT(mtk_musb_ids),
};
device_platform_driver(mtk_musb_driver);
