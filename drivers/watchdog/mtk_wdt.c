// SPDX-License-Identifier: GPL-2.0+
/*
 * MediaTek Watchdog Driver (simplified for barebox secondary bootloader)
 * Kick-only support.
 *
 * Based on Linux drivers/watchdog/mtk_wdt.c
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <of_device.h>
#include <watchdog.h>

#define WDT_MODE		0x00
#define WDT_MODE_KEY		0x22000000
#define WDT_MODE_EN		BIT(0)

#define WDT_LENGTH		0x04
#define WDT_LENGTH_KEY		0x8
#define WDT_LENGTH_TIMEOUT(n)	((n) << 5)

#define WDT_RST			0x08
#define WDT_RST_RELOAD		0x1971

struct mtk_wdt {
	void __iomem *base;
	struct watchdog wdd;
	struct clk *clk;
};

static int mtk_wdt_set_timeout(struct watchdog *wdd, unsigned int timeout)
{
	struct mtk_wdt *mtk = container_of(wdd, struct mtk_wdt, wdd);
	u32 reg;

	if (timeout > 31)
		timeout = 31;
	if (timeout < 1)
		timeout = 1;

	reg = WDT_LENGTH_KEY | WDT_LENGTH_TIMEOUT(timeout);
	writel(reg, mtk->base + WDT_LENGTH);

	/* enable and reload */
	reg = readl(mtk->base + WDT_MODE);
	reg |= WDT_MODE_EN | WDT_MODE_KEY;
	writel(reg, mtk->base + WDT_MODE);

	writel(WDT_RST_RELOAD, mtk->base + WDT_RST);

	wdd->timeout_cur = timeout;
	return 0;
}

static int mtk_wdt_ping(struct watchdog *wdd)
{
	struct mtk_wdt *mtk = container_of(wdd, struct mtk_wdt, wdd);

	writel(WDT_RST_RELOAD, mtk->base + WDT_RST);
	return 0;
}

static int mtk_wdt_probe(struct device *dev)
{
	struct mtk_wdt *mtk;
	struct resource *res;

	mtk = xzalloc(sizeof(*mtk));

	res = dev_get_resource(dev, IORESOURCE_MEM, 0);
	if (IS_ERR(res))
		return PTR_ERR(res);

	mtk->base = IOMEM(res->start);

	mtk->clk = clk_get(dev, NULL);
	if (!IS_ERR_OR_NULL(mtk->clk))
		clk_enable(mtk->clk);

	mtk->wdd.name = "mtk-wdt";
	mtk->wdd.hwdev = dev;
	mtk->wdd.timeout_max = 31;
	mtk->wdd.set_timeout = mtk_wdt_set_timeout;
	mtk->wdd.ping = mtk_wdt_ping;
	mtk->wdd.priority = 100;
	mtk->wdd.running = WDOG_HW_RUNNING_UNSUPPORTED;

	return watchdog_register(&mtk->wdd);
}

static const struct of_device_id mtk_wdt_dt_ids[] = {
	{ .compatible = "mediatek,mt6589-wdt" },
	{ .compatible = "mediatek,mt6577-wdt" },
	{ .compatible = "mediatek,wdt" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_wdt_dt_ids);

static struct driver mtk_wdt_driver = {
	.name = "mtk-wdt",
	.probe = mtk_wdt_probe,
	.of_compatible = DRV_OF_COMPAT(mtk_wdt_dt_ids),
};
device_platform_driver(mtk_wdt_driver);
