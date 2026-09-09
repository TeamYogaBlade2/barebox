// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek keypad (mt6779 compatible) for barebox secondary bootloader
 * Minimal: registers input device and enables the controller.
 * Full matrix scanning / IRQ can be added later.
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <of_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <input/input.h>
#include <input/matrix_keypad.h>
#include <poller.h>

#define MTK_KPD_SEL		0x0020
#define MTK_KPD_DEBOUNCE	0x0018

struct mtk_keypad {
	void __iomem *base;
	struct clk *clk;
	struct input_device input;
	struct poller_struct poller;
	u32 n_rows;
	u32 n_cols;
};

static void mtk_keypad_poll(struct poller_struct *poller)
{
	/* Placeholder: full scan would read MEM registers and report keys */
}

static int mtk_keypad_probe(struct device *dev)
{
	struct mtk_keypad *kp;
	struct resource *res;
	int ret;

	kp = xzalloc(sizeof(*kp));

	res = dev_get_resource(dev, IORESOURCE_MEM, 0);
	if (IS_ERR(res))
		return PTR_ERR(res);
	kp->base = IOMEM(res->start);

	kp->clk = clk_get(dev, NULL);
	if (!IS_ERR_OR_NULL(kp->clk))
		clk_enable(kp->clk);

	of_property_read_u32(dev->of_node, "keypad,num-rows", &kp->n_rows);
	of_property_read_u32(dev->of_node, "keypad,num-columns", &kp->n_cols);
	if (!kp->n_rows)
		kp->n_rows = 3;
	if (!kp->n_cols)
		kp->n_cols = 3;

	/* Basic enable */
	if (kp->base) {
		writel(0x1fff, kp->base + MTK_KPD_DEBOUNCE); /* max debounce */
		/* leave SEL as default / already set by LK */
	}

	kp->input.parent = dev;
	ret = input_device_register(&kp->input);
	if (ret)
		return ret;

	kp->poller.func = mtk_keypad_poll;
	poller_register(&kp->poller, "mtk-keypad");

	dev_info(dev, "MTK keypad registered (%ux%u)\n", kp->n_rows, kp->n_cols);
	return 0;
}

static const struct of_device_id mtk_keypad_ids[] = {
	{ .compatible = "mediatek,mt6779-keypad" },
	{ .compatible = "mediatek,kp" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_keypad_ids);

static struct driver mtk_keypad_driver = {
	.name = "mtk-keypad",
	.probe = mtk_keypad_probe,
	.of_compatible = DRV_OF_COMPAT(mtk_keypad_ids),
};
device_platform_driver(mtk_keypad_driver);
