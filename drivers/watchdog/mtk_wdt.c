// SPDX-License-Identifier: GPL-2.0+
/*
 * Mediatek Watchdog Driver
 *
 * Copyright (C) 2014 Matthias Brugger
 *
 * Matthias Brugger <matthias.bgg@gmail.com>
 *
 * Based on sunxi_wdt.c
 */

#include <dt-bindings/reset/mt2712-resets.h>
#include <dt-bindings/reset/mediatek,mt6735-wdt.h>
#include <dt-bindings/reset/mediatek,mt6795-resets.h>
#include <dt-bindings/reset/mt7986-resets.h>
#include <dt-bindings/reset/mt8183-resets.h>
#include <dt-bindings/reset/mt8186-resets.h>
#include <dt-bindings/reset/mt8188-resets.h>
#include <dt-bindings/reset/mt8192-resets.h>
#include <dt-bindings/reset/mt8195-resets.h>
#include <clock.h>
#include <linux/err.h>
#include <init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <of.h>
#include <linux/device.h>
#include <linux/reset-controller.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <watchdog.h>
#include <restart.h>

#define WDT_MAX_TIMEOUT		31
#define WDT_MIN_TIMEOUT		2
#define WDT_LENGTH_TIMEOUT(n)	((n) << 5)

#define WDT_LENGTH		0x04
#define WDT_LENGTH_KEY		0x8

#define WDT_RST			0x08
#define WDT_RST_RELOAD		0x1971

#define WDT_MODE		0x00
#define WDT_MODE_EN		(1 << 0)
#define WDT_MODE_EXT_POL_LOW	(0 << 1)
#define WDT_MODE_EXT_POL_HIGH	(1 << 1)
#define WDT_MODE_EXRST_EN	(1 << 2)
#define WDT_MODE_IRQ_EN		(1 << 3)
#define WDT_MODE_AUTO_START	(1 << 4)
#define WDT_MODE_DUAL_EN	(1 << 6)
#define WDT_MODE_CNT_SEL	(1 << 8)
#define WDT_MODE_KEY		0x22000000

#define WDT_SWRST		0x14
#define WDT_SWRST_KEY		0x1209

#define WDT_SWSYSRST		0x18U
#define WDT_SWSYS_RST_KEY	0x88000000

#define WDT_SWSYSRST_EN		0xfc

#define DRV_NAME		"mtk-wdt"
#define DRV_VERSION		"1.0"

#define MT7988_TOPRGU_SW_RST_NUM	24

struct mtk_wdt_dev {
	struct watchdog wdt_dev;
	void __iomem *wdt_base;
	spinlock_t lock; /* protects WDT_SWSYSRST reg */
	struct reset_controller_dev rcdev;
	bool disable_wdt_extrst;
	bool reset_by_toprgu;
	bool has_swsysrst_en;
	bool started;
	struct restart_handler restart;
};

struct mtk_wdt_data {
	int toprgu_sw_rst_num;
	bool has_swsysrst_en;
};

static inline struct mtk_wdt_dev *to_mtk_wdt_dev(struct watchdog *wdt_dev)
{
	return container_of(wdt_dev, struct mtk_wdt_dev, wdt_dev);
}

static const struct mtk_wdt_data mt2712_data = {
	.toprgu_sw_rst_num = MT2712_TOPRGU_SW_RST_NUM,
};

static const struct mtk_wdt_data mt6735_data = {
	.toprgu_sw_rst_num = MT6735_TOPRGU_RST_NUM,
};

static const struct mtk_wdt_data mt6795_data = {
	.toprgu_sw_rst_num = MT6795_TOPRGU_SW_RST_NUM,
};

static const struct mtk_wdt_data mt7986_data = {
	.toprgu_sw_rst_num = MT7986_TOPRGU_SW_RST_NUM,
};

static const struct mtk_wdt_data mt7988_data = {
	.toprgu_sw_rst_num = MT7988_TOPRGU_SW_RST_NUM,
	.has_swsysrst_en = true,
};

static const struct mtk_wdt_data mt8183_data = {
	.toprgu_sw_rst_num = MT8183_TOPRGU_SW_RST_NUM,
};

static const struct mtk_wdt_data mt8186_data = {
	.toprgu_sw_rst_num = MT8186_TOPRGU_SW_RST_NUM,
};

static const struct mtk_wdt_data mt8188_data = {
	.toprgu_sw_rst_num = MT8188_TOPRGU_SW_RST_NUM,
};

static const struct mtk_wdt_data mt8192_data = {
	.toprgu_sw_rst_num = MT8192_TOPRGU_SW_RST_NUM,
};

static const struct mtk_wdt_data mt8195_data = {
	.toprgu_sw_rst_num = MT8195_TOPRGU_SW_RST_NUM,
};

/**
 * toprgu_reset_sw_en_unlocked() - enable/disable software control for reset bit
 * @data: Pointer to instance of driver data.
 * @id: Bit number identifying the reset to be enabled or disabled.
 * @enable: If true, enable software control for that bit, disable otherwise.
 *
 * Context: The caller must hold lock of struct mtk_wdt_dev.
 */
static void toprgu_reset_sw_en_unlocked(struct mtk_wdt_dev *data,
					unsigned long id, bool enable)
{
	u32 tmp;

	tmp = readl(data->wdt_base + WDT_SWSYSRST_EN);
	if (enable)
		tmp |= BIT(id);
	else
		tmp &= ~BIT(id);

	writel(tmp, data->wdt_base + WDT_SWSYSRST_EN);
}

static int toprgu_reset_update(struct reset_controller_dev *rcdev,
			       unsigned long id, bool assert)
{
	unsigned int tmp;
	unsigned long flags;
	struct mtk_wdt_dev *data =
		 container_of(rcdev, struct mtk_wdt_dev, rcdev);

	spin_lock_irqsave(&data->lock, flags);

	if (assert && data->has_swsysrst_en)
		toprgu_reset_sw_en_unlocked(data, id, true);

	tmp = readl(data->wdt_base + WDT_SWSYSRST);
	if (assert)
		tmp |= BIT(id);
	else
		tmp &= ~BIT(id);
	tmp |= WDT_SWSYS_RST_KEY;
	writel(tmp, data->wdt_base + WDT_SWSYSRST);

	if (!assert && data->has_swsysrst_en)
		toprgu_reset_sw_en_unlocked(data, id, false);

	spin_unlock_irqrestore(&data->lock, flags);

	return 0;
}

static int toprgu_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	return toprgu_reset_update(rcdev, id, true);
}

static int toprgu_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	return toprgu_reset_update(rcdev, id, false);
}

static int toprgu_reset(struct reset_controller_dev *rcdev,
			unsigned long id)
{
	int ret;

	ret = toprgu_reset_assert(rcdev, id);
	if (ret)
		return ret;

	return toprgu_reset_deassert(rcdev, id);
}

static const struct reset_control_ops toprgu_reset_ops = {
	.assert = toprgu_reset_assert,
	.deassert = toprgu_reset_deassert,
	.reset = toprgu_reset,
};

static int toprgu_register_reset_controller(struct device *dev,
					    int rst_num)
{
	int ret;
	struct mtk_wdt_dev *mtk_wdt = dev_get_drvdata(dev);

	spin_lock_init(&mtk_wdt->lock);

	mtk_wdt->rcdev.nr_resets = rst_num;
	mtk_wdt->rcdev.ops = &toprgu_reset_ops;
	mtk_wdt->rcdev.of_node = dev->of_node;
	ret = reset_controller_register(&mtk_wdt->rcdev);
	if (ret != 0)
		dev_err(dev,
			"couldn't register wdt reset controller: %d\n", ret);
	return ret;
}

static void mtk_wdt_restart(struct restart_handler *rst,
			   unsigned long flags)
{
	struct mtk_wdt_dev *mtk_wdt
		= container_of(rst, struct mtk_wdt_dev, restart);
	void __iomem *wdt_base;
	u32 reg;

	wdt_base = mtk_wdt->wdt_base;

	/* Enable reset in order to issue a system reset instead of an IRQ */
	reg = readl(wdt_base + WDT_MODE);
	reg &= ~WDT_MODE_IRQ_EN;
	writel(reg | WDT_MODE_KEY, wdt_base + WDT_MODE);

	while (1) {
		writel(WDT_SWRST_KEY, wdt_base + WDT_SWRST);
		mdelay(5);
	}
}

static int mtk_wdt_ping(struct watchdog *wdt_dev)
{
	struct mtk_wdt_dev *mtk_wdt = to_mtk_wdt_dev(wdt_dev);
	void __iomem *wdt_base = mtk_wdt->wdt_base;

	iowrite32(WDT_RST_RELOAD, wdt_base + WDT_RST);

	return 0;
}

static int mtk_wdt_set_timeout(struct watchdog *wdt_dev,
				unsigned int timeout)
{
	struct mtk_wdt_dev *mtk_wdt = to_mtk_wdt_dev(wdt_dev);
	void __iomem *wdt_base = mtk_wdt->wdt_base;
	u32 reg;

	/*
	 * One bit is the value of 512 ticks
	 * The clock has 32 KHz
	 */
	reg = WDT_LENGTH_TIMEOUT(timeout << 6) | WDT_LENGTH_KEY;
	iowrite32(reg, wdt_base + WDT_LENGTH);

	mtk_wdt_ping(wdt_dev);

	return 0;
}

static int mtk_wdt_stop(struct watchdog *wdt_dev)
{
	struct mtk_wdt_dev *mtk_wdt = to_mtk_wdt_dev(wdt_dev);
	void __iomem *wdt_base = mtk_wdt->wdt_base;
	u32 reg;

	reg = readl(wdt_base + WDT_MODE);
	reg &= ~WDT_MODE_EN;
	reg |= WDT_MODE_KEY;
	iowrite32(reg, wdt_base + WDT_MODE);

	mtk_wdt->started = false;
	return 0;
}

static int mtk_wdt_start(struct watchdog *wdt_dev,
			 unsigned int timeout)
{
	u32 reg;
	struct mtk_wdt_dev *mtk_wdt = to_mtk_wdt_dev(wdt_dev);
	void __iomem *wdt_base = mtk_wdt->wdt_base;
	int ret;

	ret = mtk_wdt_set_timeout(wdt_dev, timeout);
	if (ret < 0)
		return ret;

	if (mtk_wdt->started)
		return 0;

	reg = ioread32(wdt_base + WDT_MODE);
	reg &= ~(WDT_MODE_IRQ_EN | WDT_MODE_DUAL_EN);
	if (mtk_wdt->disable_wdt_extrst)
		reg &= ~WDT_MODE_EXRST_EN;
	if (mtk_wdt->reset_by_toprgu)
		reg |= WDT_MODE_CNT_SEL;
	reg |= (WDT_MODE_EN | WDT_MODE_KEY);
	iowrite32(reg, wdt_base + WDT_MODE);

	mtk_wdt->started = true;
	return 0;
}

static int mtk_wdt_configure(struct watchdog *wdt_dev,
			     unsigned int timeout)
{
	struct mtk_wdt_dev *mtk_wdt = to_mtk_wdt_dev(wdt_dev);
	void __iomem *wdt_base = mtk_wdt->wdt_base;
	u32 reg;

	if (timeout)
		mtk_wdt_start(wdt_dev, timeout);
	else
		mtk_wdt_stop(wdt_dev);

	/*
	 * One bit is the value of 512 ticks
	 * The clock has 32 KHz
	 */
	reg = WDT_LENGTH_TIMEOUT(timeout << 6) | WDT_LENGTH_KEY;
	iowrite32(reg, wdt_base + WDT_LENGTH);

	mtk_wdt_ping(wdt_dev);

	return 0;
}

static int mtk_wdt_probe(struct device *dev)
{
	struct mtk_wdt_dev *mtk_wdt;
	const struct mtk_wdt_data *wdt_data;
	void __iomem *wdt_base;
	int err;

	mtk_wdt = devm_kzalloc(dev, sizeof(*mtk_wdt), GFP_KERNEL);
	if (!mtk_wdt)
		return -ENOMEM;

	dev_set_drvdata(dev, mtk_wdt);

	wdt_base = dev_platform_ioremap_resource(dev, 0);
	if (IS_ERR(wdt_base))
		return PTR_ERR(wdt_base);

	mtk_wdt->wdt_base = wdt_base;
	mtk_wdt->wdt_dev.ping = mtk_wdt_ping;
	mtk_wdt->wdt_dev.set_timeout = mtk_wdt_configure;
	mtk_wdt->wdt_dev.timeout_max = WDT_MAX_TIMEOUT;
	mtk_wdt->wdt_dev.hwdev = dev;

	if (readl(wdt_base + WDT_MODE) & WDT_MODE_EN)
		mtk_wdt->wdt_dev.running = WDOG_HW_RUNNING;
	else
		mtk_wdt->wdt_dev.running = WDOG_HW_NOT_RUNNING;

	err = watchdog_register(&mtk_wdt->wdt_dev);
	if (err)
		return err;

	dev_info(dev, "Watchdog enabled\n");

	wdt_data = device_get_match_data(dev);
	if (wdt_data) {
		err = toprgu_register_reset_controller(dev,
						       wdt_data->toprgu_sw_rst_num);
		if (err)
			return err;

		mtk_wdt->has_swsysrst_en = wdt_data->has_swsysrst_en;
	}

	mtk_wdt->disable_wdt_extrst =
		of_property_read_bool(dev->of_node, "mediatek,disable-extrst");

	mtk_wdt->reset_by_toprgu =
		of_property_read_bool(dev->of_node, "mediatek,reset-by-toprgu");

	mtk_wdt->restart.name = "imxwd";
	mtk_wdt->restart.restart = mtk_wdt_restart;
	mtk_wdt->restart.priority = RESTART_DEFAULT_PRIORITY;
	mtk_wdt->restart.dev = &mtk_wdt->wdt_dev.dev;

	restart_handler_register(&mtk_wdt->restart);

	return 0;
}

static const struct of_device_id mtk_wdt_dt_ids[] = {
	{ .compatible = "mediatek,mt2712-wdt", .data = &mt2712_data },
	{ .compatible = "mediatek,mt6589-wdt" },
	{ .compatible = "mediatek,mt6735-wdt", .data = &mt6735_data },
	{ .compatible = "mediatek,mt6795-wdt", .data = &mt6795_data },
	{ .compatible = "mediatek,mt7986-wdt", .data = &mt7986_data },
	{ .compatible = "mediatek,mt7988-wdt", .data = &mt7988_data },
	{ .compatible = "mediatek,mt8183-wdt", .data = &mt8183_data },
	{ .compatible = "mediatek,mt8186-wdt", .data = &mt8186_data },
	{ .compatible = "mediatek,mt8188-wdt", .data = &mt8188_data },
	{ .compatible = "mediatek,mt8192-wdt", .data = &mt8192_data },
	{ .compatible = "mediatek,mt8195-wdt", .data = &mt8195_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_wdt_dt_ids);

static struct driver mtk_wdt_driver = {
	.probe		= mtk_wdt_probe,
	.name		= DRV_NAME,
	.of_match_table	= mtk_wdt_dt_ids,
};
device_platform_driver(mtk_wdt_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Matthias Brugger <matthias.bgg@gmail.com>");
MODULE_DESCRIPTION("Mediatek WatchDog Timer Driver");
MODULE_VERSION(DRV_VERSION);
