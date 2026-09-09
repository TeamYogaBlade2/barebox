// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MT6589 pinctrl driver for barebox (secondary bootloader)
 *
 * Supports basic pinmux setting from DT "pinmux" properties.
 * Drive strength / pull / eint full support deferred.
 * Based on Linux pinctrl-mt6589 / pinctrl-paris.
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <of_device.h>
#include <pinctrl.h>
#include <malloc.h>
#include <linux/err.h>

#define MT6589_PIN_REG_BASE	0x1000b000  /* typical, overridden by DT */
#define MTK_RANGE		0x10
#define MTK_MODE_BITS		3
#define MTK_MODE_MASK		0x7

struct mtk_pinctrl {
	void __iomem *base;
	struct pinctrl_device pctl;
};

/*
 * MediaTek pinmux value in DT is usually (pin << 8) | mode
 * or the raw value from pinfunc header.
 * We extract pin and mode and write to the mode register.
 */
static int mtk_pinctrl_set_state(struct pinctrl_device *pdev,
				 struct device_node *np)
{
	struct mtk_pinctrl *mtk = container_of(pdev, struct mtk_pinctrl, pctl);
	struct property *prop;
	const __be32 *list;
	int size, i;

	prop = of_find_property(np, "pinmux", &size);
	if (!prop)
		prop = of_find_property(np, "pins", &size);
	if (!prop)
		return 0; /* nothing to do */

	list = prop->value;
	size /= sizeof(*list);

	for (i = 0; i < size; i++) {
		u32 val = be32_to_cpu(list[i]);
		u32 pin = (val >> 8) & 0xff;
		u32 mode = val & 0x7;
		u32 reg, shift, mask, tmp;

		/* Mode registers are typically at base + 0x0C0 + (pin/5)*0x10
		 * for older MTK; exact layout varies. For secondary we do a
		 * best-effort write if base is valid.
		 */
		if (!mtk->base)
			continue;

		reg = 0x0C0 + (pin / 5) * 0x10;
		shift = (pin % 5) * 3;
		mask = MTK_MODE_MASK << shift;

		tmp = readl(mtk->base + reg);
		tmp = (tmp & ~mask) | ((mode << shift) & mask);
		writel(tmp, mtk->base + reg);
	}

	return 0;
}

static struct pinctrl_ops mtk_pinctrl_ops = {
	.set_state = mtk_pinctrl_set_state,
};

static int mtk_pinctrl_probe(struct device *dev)
{
	struct mtk_pinctrl *mtk;
	struct resource *res;

	mtk = xzalloc(sizeof(*mtk));

	res = dev_get_resource(dev, IORESOURCE_MEM, 0);
	if (!IS_ERR(res))
		mtk->base = IOMEM(res->start);
	else
		mtk->base = NULL;

	mtk->pctl.dev = dev;
	mtk->pctl.ops = &mtk_pinctrl_ops;
	pinctrl_register(&mtk->pctl);

	dev_info(dev, "MT6589 pinctrl registered (base %p)\n", mtk->base);
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
