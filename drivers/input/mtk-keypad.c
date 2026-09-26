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
#include <input/matrix_keypad.h>
#include <poller.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>


#define MTK_KPD_MEM			0x0004
#define MTK_KPD_DEBOUNCE		0x0018
#define MTK_KPD_SEL			0x0020
#define MTK_KPD_NUM_MEMS		5

#define MTK_KPD_SEL_COL			GENMASK(15, 10)
#define MTK_KPD_SEL_ROW			GENMASK(9, 4)
#define MTK_KPD_SEL_DOUBLE_KP_MODE	BIT(0)

struct mtk_keypad {
	void __iomem *base;
	struct clk *clk;
	struct input_device input;
	struct poller_struct poller;
	u32 n_rows;
	u32 n_cols;
	u32 row_shift;
	u32 last_state[MTK_KPD_NUM_MEMS];
};

static void mtk_keypad_poll(struct poller_struct *poller)
{
	struct mtk_keypad *kp = container_of(poller, struct mtk_keypad, poller);
	u32 state[MTK_KPD_NUM_MEMS];
	int i, bit;

	if (!kp->base)
		return;

	for (i = 0; i < MTK_KPD_NUM_MEMS; i++)
		state[i] = readl(kp->base + MTK_KPD_MEM + i * 4);

	for (i = 0; i < MTK_KPD_NUM_MEMS; i++) {
		u32 change = (state[i] ^ kp->last_state[i]) & 0xffff;

		kp->last_state[i] = state[i];
		if (!change)
			continue;

		for_each_set_bit(bit, (unsigned long *)&change, 16) {
			unsigned int key = i * 16 + bit;
			unsigned int row = key / 9;
			unsigned int col = key % 9;
			unsigned int scancode;
			bool pressed;

			if (row >= kp->n_rows || col >= kp->n_cols)
				continue;

			scancode = MATRIX_SCAN_CODE(row, col, kp->row_shift);
			/* MEM bit 0 = pressed */
			pressed = !(state[i] & BIT(bit));
			input_report_key_event(&kp->input, scancode, pressed);
		}
	}
}

static int mtk_keypad_probe(struct device *dev)
{
	struct mtk_keypad *kp;
	struct resource *res;
	u32 debounce_ms = 16;
	u32 sel = 0;
	int ret, i;

	kp = xzalloc(sizeof(*kp));

	res = dev_get_resource(dev, IORESOURCE_MEM, 0);
	if (IS_ERR(res))
		return PTR_ERR(res);
	kp->base = IOMEM(res->start);

	kp->clk = clk_get(dev, "kpd");
	if (IS_ERR_OR_NULL(kp->clk))
		kp->clk = clk_get(dev, NULL);
	if (!IS_ERR_OR_NULL(kp->clk))
		clk_enable(kp->clk);

	of_property_read_u32(dev->of_node, "keypad,num-rows", &kp->n_rows);
	of_property_read_u32(dev->of_node, "keypad,num-columns", &kp->n_cols);
	if (!kp->n_rows)
		kp->n_rows = 3;
	if (!kp->n_cols)
		kp->n_cols = 3;
	kp->row_shift = get_count_order(kp->n_cols);

	of_property_read_u32(dev->of_node, "debounce-delay-ms", &debounce_ms);
	if (debounce_ms > 256)
		debounce_ms = 256;

	if (kp->base) {
		/* debounce unit is ~32kHz ticks; approx ms * 32 */
		writel((debounce_ms * 32) & GENMASK(13, 0),
		       kp->base + MTK_KPD_DEBOUNCE);

		sel = FIELD_PREP(MTK_KPD_SEL_ROW, kp->n_rows) |
		      FIELD_PREP(MTK_KPD_SEL_COL, kp->n_cols);
		writel(sel, kp->base + MTK_KPD_SEL);

		for (i = 0; i < MTK_KPD_NUM_MEMS; i++)
			kp->last_state[i] = readl(kp->base + MTK_KPD_MEM + i * 4);
	}

	kp->input.parent = dev;
	ret = input_device_register(&kp->input);
	if (ret)
		return ret;

	kp->poller.func = mtk_keypad_poll;
	poller_register(&kp->poller, "mtk-keypad");

	dev_info(dev, "MTK keypad registered (%ux%u, debounce %ums)\n",
		 kp->n_rows, kp->n_cols, debounce_ms);
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
