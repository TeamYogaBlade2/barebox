// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek keypad (mt6779 compatible) for barebox secondary bootloader
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <of_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <input/input.h>
#include <poller.h>
#include <linux/bitops.h>

#define MTK_KPD_MEM		0x0004
#define MTK_KPD_DEBOUNCE	0x0018
#define MTK_KPD_SEL		0x0020
#define MTK_KPD_NUM_MEMS	5

struct mtk_keypad {
	void __iomem *base;
	struct clk *clk;
	struct input_device input;
	struct poller_struct poller;
	u32 n_rows;
	u32 n_cols;
	u32 last_state[MTK_KPD_NUM_MEMS];
};

static void mtk_keypad_poll(struct poller_struct *poller)
{
	struct mtk_keypad *kp = container_of(poller, struct mtk_keypad, poller);
	u32 state[MTK_KPD_NUM_MEMS];
	int i;

	if (!kp->base)
		return;

	for (i = 0; i < MTK_KPD_NUM_MEMS; i++)
		state[i] = readl(kp->base + MTK_KPD_MEM + i * 4);

	/* Simple change detection - full keycode mapping deferred */
	for (i = 0; i < MTK_KPD_NUM_MEMS; i++) {
		if (state[i] != kp->last_state[i]) {
			/* key event occurred; for now just keep state */
			kp->last_state[i] = state[i];
		}
	}
}

static int mtk_keypad_probe(struct device *dev)
{
	struct mtk_keypad *kp;
	struct resource *res;
	int ret, i;

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

	if (kp->base) {
		writel(0x1fff, kp->base + MTK_KPD_DEBOUNCE);
		for (i = 0; i < MTK_KPD_NUM_MEMS; i++)
			kp->last_state[i] = readl(kp->base + MTK_KPD_MEM + i * 4);
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
