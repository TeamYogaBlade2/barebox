// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek Watchdog driver for barebox (kick-only, no reset).
 *
 * Copyright (c) 2026 Akari Tsuyukusa <akkun11.open@gmail.com>
 */

#include <common.h>
#include <driver.h>
#include <init.h>
#include <io.h>
#include <of.h>
#include <watchdog.h>

#define MTK_WDT_MODE		0x00
#define MTK_WDT_LENGTH		0x04
#define MTK_WDT_RESTART		0x08
#define MTK_WDT_STATUS		0x0c

#define MTK_WDT_MODE_KEY	0x22000000
#define MTK_WDT_MODE_EN		BIT(0)
#define MTK_WDT_MODE_EXTEN	BIT(2)
#define MTK_WDT_MODE_IRQ	BIT(3)

#define MTK_WDT_LENGTH_KEY	0x8
#define MTK_WDT_RESTART_KEY	0x1971

struct mtk_wdt {
	struct watchdog	wdd;
	void __iomem	*base;
};

static inline struct mtk_wdt *to_mtk_wdt(struct watchdog *wdd)
{
	return container_of(wdd, struct mtk_wdt, wdd);
}

static int mtk_wdt_set_timeout(struct watchdog *wdd, unsigned int timeout)
{
	struct mtk_wdt *w = to_mtk_wdt(wdd);

	if (timeout == 0) {
		/* Disable WDT */
		writel(MTK_WDT_MODE_KEY, w->base + MTK_WDT_MODE);
		return 0;
	}

	/*
	 * WDT_LENGTH = timeout in seconds encoded as:
	 * bits[15:5] = timeout * (32768 / 512) = timeout * 64
	 * but hardware expects the value in the KEY format.
	 * Use the downstream formula: len = timeout * 512 / 16 << 5
	 */
	u32 len = ((timeout * 32) & 0x7ff) << 5;
	writel(MTK_WDT_LENGTH_KEY | len, w->base + MTK_WDT_LENGTH);

	/* Enable, no IRQ, external reset */
	writel(MTK_WDT_MODE_KEY | MTK_WDT_MODE_EN | MTK_WDT_MODE_EXTEN,
	       w->base + MTK_WDT_MODE);

	/* Initial kick */
	writel(MTK_WDT_RESTART_KEY, w->base + MTK_WDT_RESTART);

	return 0;
}

static int mtk_wdt_probe(struct device *dev)
{
	struct mtk_wdt *w;
	struct resource *iores;

	w = xzalloc(sizeof(*w));
	iores = dev_request_mem_resource(dev, 0);
	if (IS_ERR(iores))
		return PTR_ERR(iores);
	w->base = IOMEM(iores->start);

	/* Keep WDT running but only expose kick interface */
	w->wdd.hwdev       = dev;
	w->wdd.set_timeout = mtk_wdt_set_timeout;
	w->wdd.timeout_max = 31;

	dev->priv = w;
	return watchdog_register(&w->wdd);
}

static const struct of_device_id mtk_wdt_dt_ids[] = {
	{ .compatible = "mediatek,mt6589-wdt" },
	{ .compatible = "mediatek,mt8183-wdt" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_wdt_dt_ids);

static struct driver mtk_wdt_driver = {
	.name         = "mtk-wdt",
	.probe        = mtk_wdt_probe,
	.of_compatible = DRV_OF_COMPAT(mtk_wdt_dt_ids),
};
device_platform_driver(mtk_wdt_driver);
