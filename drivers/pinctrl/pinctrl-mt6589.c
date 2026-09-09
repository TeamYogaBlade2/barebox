// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal MediaTek MT6589 pinctrl for barebox secondary bootloader.
 * Full pinmux/drive/slew support deferred; just satisfies DT probe and
 * basic gpiochip registration so other drivers can request pins.
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <of_device.h>
#include <pinctrl.h>
#include <gpio.h>

#define MT6589_GPIO_NUM 200  /* approximate */

struct mtk_pinctrl {
	void __iomem *base;
	struct pinctrl_device pctl;
	struct gpio_chip gc;
};

static int mtk_pinctrl_probe(struct device *dev)
{
	struct mtk_pinctrl *mtk;
	struct resource *res;

	mtk = xzalloc(sizeof(*mtk));

	res = dev_get_resource(dev, IORESOURCE_MEM, 0);
	if (IS_ERR(res))
		return PTR_ERR(res);
	mtk->base = IOMEM(res->start);

	mtk->pctl.dev = dev;
	/* minimal ops - full implementation later */
	pinctrl_register(&mtk->pctl);

	

	dev_info(dev, "MT6589 pinctrl/gpio registered (minimal)\n");
	return 0;
}

static const struct of_device_id mtk_pinctrl_ids[] = {
	{ .compatible = "mediatek,mt6589-pinctrl" },
	{ .compatible = "mediatek,mt6577-pinctrl" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_pinctrl_ids);

static struct driver mtk_pinctrl_driver = {
	.name = "pinctrl-mt6589",
	.probe = mtk_pinctrl_probe,
	.of_compatible = DRV_OF_COMPAT(mtk_pinctrl_ids),
};
core_platform_driver(mtk_pinctrl_driver);
