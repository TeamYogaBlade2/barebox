// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek PMIC Wrapper (PWRAP) for MT6589
 *
 * Preloader/LK already initialises the bridge; we only implement the
 * WACS2 software access path so child devices (MT6320) can talk to the
 * PMIC via regmap.
 *
 * Register offsets and protocol from Linux drivers/soc/mediatek/mtk-pmic-wrap.c
 * (mt6589_regs + pwrap_read16 / pwrap_write16).
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <of_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/regmap.h>
#include <linux/bitops.h>
#include <clock.h>

#define PWRAP_POLL_DELAY_US	10
#define PWRAP_POLL_TIMEOUT_US	10000

/* MT6589 WACS2 offsets */
#define PWRAP_WACS2_EN		0x9c
#define PWRAP_INIT_DONE2	0xa0
#define PWRAP_WACS2_CMD		0xa4
#define PWRAP_WACS2_RDATA	0xa8
#define PWRAP_WACS2_VLDCLR	0xac

#define PWRAP_GET_WACS_RDATA(x)	(((x) >> 0) & 0xffff)
#define PWRAP_GET_WACS_FSM(x)	(((x) >> 16) & 0x7)

#define PWRAP_WACS_FSM_IDLE	0x00
#define PWRAP_WACS_FSM_WFVLDCLR	0x06
#define PWRAP_STATE_INIT_DONE0	BIT(21)

struct mtk_pwrap {
	struct device *dev;
	void __iomem *base;
	struct clk *clk_spi;
	struct clk *clk_sys;
	struct regmap *regmap;
};

static u32 pwrap_readl(struct mtk_pwrap *wrp, u32 off)
{
	return readl(wrp->base + off);
}

static void pwrap_writel(struct mtk_pwrap *wrp, u32 val, u32 off)
{
	writel(val, wrp->base + off);
}

static int pwrap_wait_fsm(struct mtk_pwrap *wrp, u32 want)
{
	u64 start = get_time_ns();
	u32 val, fsm;

	do {
		val = pwrap_readl(wrp, PWRAP_WACS2_RDATA);
		fsm = PWRAP_GET_WACS_FSM(val);
		if (fsm == want)
			return 0;
		/* recover stuck VLDCLR state */
		if (fsm == PWRAP_WACS_FSM_WFVLDCLR)
			pwrap_writel(wrp, 1, PWRAP_WACS2_VLDCLR);
	} while (!is_timeout(start, PWRAP_POLL_TIMEOUT_US * 1000ULL));

	return -ETIMEDOUT;
}

static int pwrap_read16(void *context, unsigned int reg, unsigned int *val)
{
	struct mtk_pwrap *wrp = context;
	u32 rdata;
	int ret;

	ret = pwrap_wait_fsm(wrp, PWRAP_WACS_FSM_IDLE);
	if (ret)
		return ret;

	/* 16-bit protocol: address is half-word indexed */
	pwrap_writel(wrp, (reg >> 1) << 16, PWRAP_WACS2_CMD);

	ret = pwrap_wait_fsm(wrp, PWRAP_WACS_FSM_WFVLDCLR);
	if (ret)
		return ret;

	rdata = pwrap_readl(wrp, PWRAP_WACS2_RDATA);
	*val = PWRAP_GET_WACS_RDATA(rdata);
	pwrap_writel(wrp, 1, PWRAP_WACS2_VLDCLR);

	return 0;
}

static int pwrap_write16(void *context, unsigned int reg, unsigned int val)
{
	struct mtk_pwrap *wrp = context;
	int ret;

	ret = pwrap_wait_fsm(wrp, PWRAP_WACS_FSM_IDLE);
	if (ret)
		return ret;

	pwrap_writel(wrp, BIT(31) | ((reg >> 1) << 16) | (val & 0xffff),
		     PWRAP_WACS2_CMD);

	return 0;
}

static const struct regmap_bus pwrap_regmap_bus = {
	.reg_write = pwrap_write16,
	.reg_read = pwrap_read16,
};

static const struct regmap_config pwrap_regmap_config = {
	.reg_bits = 16,
	.val_bits = 16,
	.reg_stride = 2,
	.max_register = 0xffff,
};

static int mtk_pwrap_probe(struct device *dev)
{
	struct mtk_pwrap *wrp;
	struct resource *res;
	u32 rdata;
	int ret;

	wrp = xzalloc(sizeof(*wrp));
	wrp->dev = dev;
	dev->priv = wrp;

	res = dev_get_resource(dev, IORESOURCE_MEM, 0);
	if (IS_ERR(res))
		return PTR_ERR(res);
	wrp->base = IOMEM(res->start);

	wrp->clk_spi = clk_get(dev, "spi");
	if (!IS_ERR_OR_NULL(wrp->clk_spi))
		clk_enable(wrp->clk_spi);
	wrp->clk_sys = clk_get(dev, "wrap");
	if (!IS_ERR_OR_NULL(wrp->clk_sys))
		clk_enable(wrp->clk_sys);

	/* Ensure WACS2 is enabled (should already be from LK) */
	if (!(pwrap_readl(wrp, PWRAP_WACS2_EN) & 1))
		pwrap_writel(wrp, 1, PWRAP_WACS2_EN);

	rdata = pwrap_readl(wrp, PWRAP_WACS2_RDATA);
	if (!(rdata & PWRAP_STATE_INIT_DONE0))
		dev_warn(dev, "WACS2 INIT_DONE not set (0x%08x) – PMIC access may fail\n",
			 rdata);

	wrp->regmap = regmap_init(dev, &pwrap_regmap_bus, wrp,
				  &pwrap_regmap_config);
	if (IS_ERR(wrp->regmap)) {
		ret = PTR_ERR(wrp->regmap);
		dev_err(dev, "regmap_init failed: %d\n", ret);
		return ret;
	}

	/* Sanity: try to read MT6320 CID (0x0100 / 0x0102 typically) */
	ret = pwrap_read16(wrp, 0x0100, &rdata);
	if (!ret)
		dev_info(dev, "PMIC CID@0x100 = 0x%04x\n", rdata);
	else
		dev_warn(dev, "PMIC probe read failed: %d\n", ret);

	of_platform_populate(dev->of_node, NULL, dev);

	dev_info(dev, "MTK PWRAP WACS2 ready\n");
	return 0;
}

static const struct of_device_id mtk_pwrap_ids[] = {
	{ .compatible = "mediatek,mt6589-pwrap" },
	{ .compatible = "mediatek,mt8135-pwrap" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_pwrap_ids);

static struct driver mtk_pwrap_driver = {
	.name = "mtk-pwrap",
	.probe = mtk_pwrap_probe,
	.of_compatible = DRV_OF_COMPAT(mtk_pwrap_ids),
};
device_platform_driver(mtk_pwrap_driver);
