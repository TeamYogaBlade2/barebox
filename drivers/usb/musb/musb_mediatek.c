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
#include <linux/reset.h>
#include <of_device.h>
#include <linux/phy/phy.h>
#include <linux/usb/musb.h>
#include <linux/usb/usb.h>
#include <linux/barebox-wrapper.h>

#include "musb_core.h"

#define USB_L1INTS		0x00a0
#define USB_L1INTM		0x00a4
#define MTK_MUSB_TXFUNCADDR	0x0480
#define MUSB_RXTOG		0x80
#define MUSB_RXTOGEN		0x82
#define MUSB_TXTOG		0x84
#define MUSB_TXTOGEN		0x86
#define MTK_TOGGLE_EN		GENMASK(15, 0)

#define TX_INT_STATUS		BIT(0)
#define RX_INT_STATUS		BIT(1)
#define USBCOM_INT_STATUS	BIT(2)

#define MTK_MUSB_L1INT_MASK	(TX_INT_STATUS | RX_INT_STATUS | \
				 USBCOM_INT_STATUS)

#define MTK_MUSB_CLKS_NUM	3

struct mtk_glue {
	struct device *dev;
	struct musb musb;
	struct musb_hdrc_platform_data pdata;
	struct musb_hdrc_config config;
	struct clk_bulk_data clks[MTK_MUSB_CLKS_NUM];
	struct reset_control *rstc;
	struct phy *phy;
	enum phy_mode phy_mode;
};

static int mtk_musb_set_mode(void *ctx, enum usb_dr_mode mode)
{
	struct mtk_glue *glue = ctx;
	struct musb *musb = &glue->musb;
	u8 devctl;
	int ret;

	if (musb->port_mode != MUSB_OTG)
		return -EINVAL;

	devctl = musb_readb(musb->mregs, MUSB_DEVCTL);

	switch (mode) {
	case USB_DR_MODE_HOST:
		ret = phy_set_mode(glue->phy, PHY_MODE_USB_HOST);
		if (ret)
			return ret;

		devctl |= MUSB_DEVCTL_SESSION;
		musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);
		MUSB_HST_MODE(musb);
		glue->phy_mode = PHY_MODE_USB_HOST;
		break;

	case USB_DR_MODE_PERIPHERAL:
		ret = phy_set_mode(glue->phy, PHY_MODE_USB_DEVICE);
		if (ret)
			return ret;

		devctl &= ~MUSB_DEVCTL_SESSION;
		musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);
		MUSB_DEV_MODE(musb);
		glue->phy_mode = PHY_MODE_USB_DEVICE;
		break;

	case USB_DR_MODE_OTG:
		ret = phy_set_mode(glue->phy, PHY_MODE_USB_OTG);
		if (ret)
			return ret;

		devctl &= ~MUSB_DEVCTL_SESSION;
		musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);
		glue->phy_mode = PHY_MODE_USB_OTG;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/* W1C helpers for interrupt status registers */
static u8 mtk_musb_clearb(void __iomem *addr, unsigned int offset)
{
	u8 data = musb_readb(addr, offset);

	musb_writeb(addr, offset, data);
	return data;
}

static u16 mtk_musb_clearw(void __iomem *addr, unsigned int offset)
{
	u16 data = musb_readw(addr, offset);

	musb_writew(addr, offset, data);
	return data;
}

static int mtk_musb_interrupt(struct musb *musb)
{
	u32 l1_ints;

	l1_ints = musb_readl(musb->mregs, USB_L1INTS) &
		  musb_readl(musb->mregs, USB_L1INTM);

	if (l1_ints & (TX_INT_STATUS | RX_INT_STATUS | USBCOM_INT_STATUS)) {
		/* Latch and clear the lower-level MUSB interrupt status */
		musb->int_usb = mtk_musb_clearb(musb->mregs, MUSB_INTRUSB);
		musb->int_rx = mtk_musb_clearw(musb->mregs, MUSB_INTRRX);
		musb->int_tx = mtk_musb_clearw(musb->mregs, MUSB_INTRTX);

		if ((musb->int_usb & MUSB_INTR_RESET) &&
		    !is_host_active(musb)) {
			musb_ep_select(musb->mregs, 0);
			musb_writeb(musb->mregs, MUSB_FADDR, 0);
		}

		if (musb->int_usb || musb->int_tx || musb->int_rx)
			return musb_interrupt(musb);
	}

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
		ret = phy_set_mode(glue->phy, glue->phy_mode);
		if (ret) {
			phy_power_off(glue->phy);
			phy_exit(glue->phy);
			return ret;
		}
	}

	/* DMA has no barebox backend, so don't unmask the DMA L1 source. */
	musb_writel(musb->mregs, USB_L1INTM, MTK_MUSB_L1INT_MASK);

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
	if (glue->rstc)
		reset_control_assert(glue->rstc);
	return 0;
}

static void mtk_musb_enable(struct musb *musb)
{
	musb_writel(musb->mregs, USB_L1INTM, MTK_MUSB_L1INT_MASK);
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

#define MTK_MUSB_MAX_EP_NUM	8
#define MTK_MUSB_RAM_BITS	11

static struct musb_fifo_cfg mtk_musb_mode_cfg[] = {
	{ .hw_ep_num = 1, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 1, .style = FIFO_RX, .maxpacket = 512, },
	{ .hw_ep_num = 2, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 2, .style = FIFO_RX, .maxpacket = 512, },
	{ .hw_ep_num = 3, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 3, .style = FIFO_RX, .maxpacket = 512, },
	{ .hw_ep_num = 4, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 4, .style = FIFO_RX, .maxpacket = 512, },
	{ .hw_ep_num = 5, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 5, .style = FIFO_RX, .maxpacket = 512, },
	{ .hw_ep_num = 6, .style = FIFO_TX, .maxpacket = 1024, },
	{ .hw_ep_num = 6, .style = FIFO_RX, .maxpacket = 1024, },
	{ .hw_ep_num = 7, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 7, .style = FIFO_RX, .maxpacket = 64, },
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
	if (ret)
		goto err;

	ret = clk_bulk_enable(MTK_MUSB_CLKS_NUM, glue->clks);
	if (ret)
		goto err_clk_put;

	glue->rstc = reset_control_get_optional(dev, "hrst");
	if (IS_ERR(glue->rstc)) {
		ret = PTR_ERR(glue->rstc);
		goto err_clk_disable;
	}

	if (glue->rstc) {
		ret = reset_control_reset(glue->rstc);
		if (ret)
			goto err_reset_put;
	}

	/* DT uses <&usb_port0 PHY_TYPE_USB2>, so preserve the phandle arg. */
	glue->phy = phy_get_by_index(dev, 0);
	if (IS_ERR(glue->phy)) {
		ret = PTR_ERR(glue->phy);
		goto err_reset_put;
	}

	res = dev_request_mem_resource(dev, 0);
	if (IS_ERR(res)) {
		ret = PTR_ERR(res);
		goto err_reset_put;
	}
	glue->musb.mregs = IOMEM(res->start);
	glue->musb.controller = dev;

	pdata = &glue->pdata;
	config = &glue->config;

	pdata->config = config;
	pdata->platform_ops = &mtk_ops;
	pdata->mode = get_musb_port_mode(dev);

	/* Match Linux MT6589 MUSB FIFO layout and RAM size. */
	config->fifo_cfg = mtk_musb_mode_cfg;
	config->fifo_cfg_size = ARRAY_SIZE(mtk_musb_mode_cfg);
	config->num_eps = MTK_MUSB_MAX_EP_NUM;
	config->ram_bits = MTK_MUSB_RAM_BITS;
	config->multipoint = 1;

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

	if (pdata->mode == MUSB_OTG) {
		ret = usb_register_otg_device(dev, mtk_musb_set_mode, glue);
		if (ret)
			goto err_mem;
	}

	dev_info(dev, "MediaTek MUSB glue registered (mode %d)\n", pdata->mode);
	return 0;

err_mem:
	release_region(res);
err_reset_put:
	if (glue->rstc)
		reset_control_put(glue->rstc);
err_clk_disable:
	clk_bulk_disable(MTK_MUSB_CLKS_NUM, glue->clks);
err_clk_put:
	clk_bulk_put(MTK_MUSB_CLKS_NUM, glue->clks);
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
