// SPDX-License-Identifier: GPL-2.0+
/*
 * MediaTek Watchdog Driver for barebox secondary bootloader
 *
 * WDT clock is 32768 Hz. LENGTH counts in units of 512 clocks
 * (= 15.625 ms per count). Max count is 1023 ≈ 15.98 s.
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

#define WDT_RST			0x08
#define WDT_RST_RELOAD		0x1971

#define WDT_SWRST		0x14
#define WDT_SWRST_KEY		0x1209

/* 32768 / 512 = 64 counts per second */
#define WDT_COUNTS_PER_SEC	64
#define WDT_MAX_COUNT		1023
#define WDT_MAX_TIMEOUT		15	/* floor(1023/64) */

struct mtk_wdt {
	void __iomem *base;
	struct watchdog wdd;
	struct clk *clk;
};

static int mtk_wdt_set_timeout(struct watchdog *wdd, unsigned int timeout)
{
	struct mtk_wdt *mtk = container_of(wdd, struct mtk_wdt, wdd);
	u32 count, reg;

	if (timeout > WDT_MAX_TIMEOUT)
		timeout = WDT_MAX_TIMEOUT;
	if (timeout < 1)
		timeout = 1;

	count = timeout * WDT_COUNTS_PER_SEC;
	if (count > WDT_MAX_COUNT)
		count = WDT_MAX_COUNT;

	/* LENGTH[15:5] = count, KEY in low bits */
	reg = WDT_LENGTH_KEY | (count << 5);
	writel(reg, mtk->base + WDT_LENGTH);

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
	mtk->wdd.timeout_max = WDT_MAX_TIMEOUT;
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
