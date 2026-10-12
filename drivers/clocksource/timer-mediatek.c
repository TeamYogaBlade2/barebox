// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MediaTek SoCs General-Purpose Timer (GPT) clocksource for barebox.
 * Uses GPT2 as free-running clocksource at 13 MHz.
 *
 * Copyright (C) 2014 Matthias Brugger
 * Copyright (c) 2026 Akari Tsuyukusa <akkun11.open@gmail.com>
 */

#include <clock.h>
#include <common.h>
#include <init.h>
#include <io.h>
#include <linux/clk.h>
#include <of.h>

/* GPT register offsets (per timer n=1..6) */
#define GPT_IRQ_EN_REG		0x00
#define GPT_IRQ_ACK_REG		0x08

#define GPT_CTRL_REG(n)		(0x10 * (n))
#define GPT_CTRL_OP(v)		(((v) & 0x3) << 4)
#define GPT_CTRL_OP_FREERUN	3
#define GPT_CTRL_CLEAR		2
#define GPT_CTRL_ENABLE		1

#define GPT_CLK_REG(n)		(0x04 + 0x10 * (n))
#define GPT_CLK_SRC_SYS13M	0
#define GPT_CLK_DIV1		0

#define GPT_CNT_REG(n)		(0x08 + 0x10 * (n))
#define GPT_CMP_REG(n)		(0x0C + 0x10 * (n))

/* Use GPT2 as clocksource (freerun at 13 MHz) */
#define CLOCKSOURCE_TIMER	2
#define GPT_CLK_HZ		13000000

static void __iomem *gpt_base;

static uint64_t mtk_gpt_read(void)
{
	return readl(gpt_base + GPT_CNT_REG(CLOCKSOURCE_TIMER));
}

static struct clocksource mtk_gpt_cs = {
	.read     = mtk_gpt_read,
	.mask     = CLOCKSOURCE_MASK(32),
	.shift    = 10,
	.priority = 80,
};

static int mtk_gpt_probe(struct device *dev)
{
	struct resource *iores;
	u32 val;

	iores = dev_request_mem_resource(dev, 0);
	if (IS_ERR(iores))
		return PTR_ERR(iores);
	gpt_base = IOMEM(iores->start);

	/* Disable and clear GPT2 */
	writel(GPT_CTRL_CLEAR | GPT_CTRL_OP(GPT_CTRL_OP_FREERUN),
	       gpt_base + GPT_CTRL_REG(CLOCKSOURCE_TIMER));

	/* Set clock source to 13 MHz system clock, no divider */
	writel(GPT_CLK_SRC_SYS13M | GPT_CLK_DIV1,
	       gpt_base + GPT_CLK_REG(CLOCKSOURCE_TIMER));

	/* Start in free-run mode */
	val = GPT_CTRL_OP(GPT_CTRL_OP_FREERUN) | GPT_CTRL_ENABLE;
	writel(val, gpt_base + GPT_CTRL_REG(CLOCKSOURCE_TIMER));

	mtk_gpt_cs.mult = clocksource_hz2mult(GPT_CLK_HZ, mtk_gpt_cs.shift);

	return init_clock(&mtk_gpt_cs);
}

static const struct of_device_id mtk_gpt_dt_ids[] = {
	{ .compatible = "mediatek,mt6589-timer" },
	{ .compatible = "mediatek,mt6577-timer" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_gpt_dt_ids);

static struct driver mtk_gpt_driver = {
	.name         = "mtk-timer",
	.probe        = mtk_gpt_probe,
	.of_compatible = DRV_OF_COMPAT(mtk_gpt_dt_ids),
};
core_platform_driver(mtk_gpt_driver);
