// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MUSB glue for barebox (MT6589 and compatible)
 * Based on Linux drivers/usb/musb/mediatek.c and barebox musb_dsps.c
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <malloc.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <of_device.h>
#include <linux/phy/phy.h>
#include <linux/usb/musb.h>
#include <linux/usb/usb.h>
#include <linux/barebox-wrapper.h>

#include "musb_core.h"

#define USB_L1INTS		0x00a0
#define USB_L1INTM		0x00a4
#define MUSB_RXTOG		0x80
#define MUSB_RXTOGEN		0x82
#define MUSB_TXTOG		0x84
#define MUSB_TXTOGEN		0x86
#define MTK_TOGGLE_EN		GENMASK(15, 0)

#define TX_INT_STATUS		BIT(0)
#define RX_INT_STATUS		BIT(1)
#define USBCOM_INT_STATUS	BIT(2)
#define DMA_INT_STATUS		BIT(3)

#define MTK_MUSB_CLKS_NUM	3

struct mtk_glue {
	struct device *dev;
	struct musb musb;
	struct musb_hdrc_platform_data pdata;
	struct musb_hdrc_config config;
	struct clk_bulk_data clks[MTK_MUSB_CLKS_NUM];
	struct phy *phy;
	enum phy_mode phy_mode;
};

static int mtk_musb_interrupt(struct musb *musb)
{
	u32 l1_ints;

	l1_ints = musb_readl(musb->mregs, USB_L1INTS) &
		  musb_readl(musb->mregs, USB_L1INTM);

	if (l1_ints & (TX_INT_STATUS | RX_INT_STATUS | USBCOM_INT_STATUS))
		return musb_interrupt(musb);

	return 0;
}

static int mtk_musb_init(struct musb *musb)
{
	struct mtk_glue *glue = container_of(musb, struct mtk_glue, musb);
	int ret;

	musb->isr = mtk_musb_interrupt;

	/* Enable TX/RX toggle */
	musb_writew(musb->mregs, MUSB_TXTOGEN, MTK_TOGGLE_EN);
	musb_writew(musb->mregs, MUSB_RXTOGEN, MTK_TOGGLE_EN);

	if (glue->phy) {
		ret = phy_init(glue->phy);
		if (ret)
			return ret;
		ret = phy_power_on(glue->phy);
		if (ret) {
			phy_exit(glue->phy);
			return ret;
		}
		if (glue->phy_mode)
			phy_set_mode(glue->phy, glue->phy_mode);
	}

	/* Unmask L1 interrupts */
	musb_writel(musb->mregs, USB_L1INTM,
		    TX_INT_STATUS | RX_INT_STATUS |
		    USBCOM_INT_STATUS | DMA_INT_STATUS);

	return 0;
}

static int mtk_musb_exit(struct musb *musb)
{
	struct mtk_glue *glue = container_of(musb, struct mtk_glue, musb);

	musb_writel(musb->mregs, USB_L1INTM, 0);
	if (glue->phy) {
		phy_power_off(glue->phy);
		phy_exit(glue->phy);
	}
	return 0;
}

static void mtk_musb_enable(struct musb *musb)
{
	musb_writel(musb->mregs, USB_L1INTM,
		    TX_INT_STATUS | RX_INT_STATUS |
		    USBCOM_INT_STATUS | DMA_INT_STATUS);
}

static void mtk_musb_disable(struct musb *musb)
{
	musb_writel(musb->mregs, USB_L1INTM, 0);
}

static struct musb_platform_ops mtk_ops = {
	.init		= mtk_musb_init,
	.exit		= mtk_musb_exit,
	.enable		= mtk_musb_enable,
	.disable	= mtk_musb_disable,
};

static int get_musb_port_mode(struct device *dev)
{
	enum usb_dr_mode mode = usb_get_dr_mode(dev);

	switch (mode) {
	case USB_DR_MODE_HOST:
		return MUSB_HOST;
	case USB_DR_MODE_PERIPHERAL:
		return MUSB_PERIPHERAL;
	default:
		if (!IS_ENABLED(CONFIG_USB_MUSB_HOST))
			return MUSB_PERIPHERAL;
		if (!IS_ENABLED(CONFIG_USB_MUSB_GADGET))
			return MUSB_HOST;
		return MUSB_OTG;
	}
}

static int mtk_musb_probe(struct device *dev)
{
	struct mtk_glue *glue;
	struct resource *res;
	struct musb_hdrc_platform_data *pdata;
	struct musb_hdrc_config *config;
	int ret;

	if (!IS_ENABLED(CONFIG_USB_MUSB_HOST) &&
	    !IS_ENABLED(CONFIG_USB_MUSB_GADGET)) {
		dev_err(dev, "Both host and gadget disabled\n");
		return -ENODEV;
	}

	glue = xzalloc(sizeof(*glue));
	glue->dev = dev;
	dev->priv = glue;

	/* clocks */
	glue->clks[0].id = "main";
	glue->clks[1].id = "mcu";
	glue->clks[2].id = "univpll";
	ret = clk_bulk_get(dev, MTK_MUSB_CLKS_NUM, glue->clks);
	if (!ret)
		clk_bulk_enable(MTK_MUSB_CLKS_NUM, glue->clks);
	else
		dev_dbg(dev, "clocks not available yet: %d\n", ret);

	/* PHY */
	glue->phy = of_phy_get_by_phandle(dev, "phys", 0);
	if (IS_ERR(glue->phy)) {
		dev_dbg(dev, "phy not ready: %pe\n", glue->phy);
		glue->phy = NULL;
	}

	res = dev_request_mem_resource(dev, 0);
	if (IS_ERR(res)) {
		ret = PTR_ERR(res);
		goto err;
	}
	glue->musb.mregs = IOMEM(res->start);
	glue->musb.controller = dev;

	pdata = &glue->pdata;
	config = &glue->config;

	pdata->config = config;
	pdata->platform_ops = &mtk_ops;
	pdata->mode = get_musb_port_mode(dev);

	/* Defaults suitable for MT6589 */
	config->num_eps = 8;
	config->ram_bits = 12;
	config->multipoint = 1;
	{
	u32 tmp;
	if (!of_property_read_u32(dev->of_node, "mentor,num-eps", &tmp))
		config->num_eps = tmp;
	}
	of_property_read_u32(dev->of_node, "mentor,ram-bits", &config->ram_bits);
	config->multipoint = of_property_read_bool(dev->of_node, "mentor,multipoint");

	switch (pdata->mode) {
	case MUSB_HOST:
		glue->phy_mode = PHY_MODE_USB_HOST;
		break;
	case MUSB_PERIPHERAL:
		glue->phy_mode = PHY_MODE_USB_DEVICE;
		break;
	default:
		glue->phy_mode = PHY_MODE_USB_OTG;
		break;
	}

	ret = musb_init_controller(&glue->musb, pdata);
	if (ret) {
		dev_err(dev, "musb_init_controller failed: %d\n", ret);
		goto err_mem;
	}

	dev_info(dev, "MediaTek MUSB glue registered (mode %d)\n", pdata->mode);
	return 0;

err_mem:
	release_region(res);
err:
	free(glue);
	return ret;
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
