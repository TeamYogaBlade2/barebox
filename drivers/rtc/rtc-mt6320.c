// SPDX-License-Identifier: GPL-2.0-only
/*
 * RTC driver for MediaTek MT6320 (same IP as MT6397)
 * Based on Linux drivers/rtc/rtc-mt6397.c
 */

#include <common.h>
#include <init.h>
#include <of_device.h>
#include <linux/err.h>
#include <linux/regmap.h>
#include <linux/rtc.h>
#include <linux/bitops.h>
#include <clock.h>

#define RTC_BBPU		0x0000
#define RTC_BBPU_CBUSY		BIT(6)
#define RTC_TC_SEC		0x000a
#define RTC_TC_MTH_MASK		0x000f
#define RTC_OFFSET_COUNT	7
#define RTC_WRTGR		0x003c
#define MT6320_RTC_BASE		0xe000
#define MTK_RTC_POLL_TIMEOUT_US	1000000

struct mt6320_rtc {
	struct rtc_device rtc;
	struct regmap *regmap;
	u32 addr_base;
};

static int mtk_rtc_write_trigger(struct mt6320_rtc *rtc)
{
	u32 data;
	u64 start;
	int ret;

	ret = regmap_write(rtc->regmap, rtc->addr_base + RTC_WRTGR, 1);
	if (ret)
		return ret;

	start = get_time_ns();
	do {
		ret = regmap_read(rtc->regmap, rtc->addr_base + RTC_BBPU, &data);
		if (ret)
			return ret;
		if (!(data & RTC_BBPU_CBUSY))
			return 0;
	} while (!is_timeout(start, MTK_RTC_POLL_TIMEOUT_US * 1000ULL));

	return -ETIMEDOUT;
}

static int mt6320_rtc_read_time(struct rtc_device *rtcdev, struct rtc_time *tm)
{
	struct mt6320_rtc *rtc = container_of(rtcdev, struct mt6320_rtc, rtc);
	u16 data[RTC_OFFSET_COUNT];
	u32 sec;
	int ret;

	do {
		ret = regmap_bulk_read(rtc->regmap, rtc->addr_base + RTC_TC_SEC,
				       data, RTC_OFFSET_COUNT);
		if (ret)
			return ret;

		tm->tm_sec = data[0];
		tm->tm_min = data[1];
		tm->tm_hour = data[2];
		tm->tm_mday = data[3];
		tm->tm_wday = data[4];
		tm->tm_mon = data[5] & RTC_TC_MTH_MASK;
		tm->tm_year = data[6];

		ret = regmap_read(rtc->regmap, rtc->addr_base + RTC_TC_SEC, &sec);
		if (ret)
			return ret;
	} while ((sec & 0xffff) < tm->tm_sec);

	/* HW starts mon/wday from 1; rtc_time uses 0-based */
	tm->tm_mon--;
	tm->tm_wday--;
	return 0;
}

static int mt6320_rtc_set_time(struct rtc_device *rtcdev, struct rtc_time *tm)
{
	struct mt6320_rtc *rtc = container_of(rtcdev, struct mt6320_rtc, rtc);
	u16 data[RTC_OFFSET_COUNT];
	int ret;

	data[0] = tm->tm_sec;
	data[1] = tm->tm_min;
	data[2] = tm->tm_hour;
	data[3] = tm->tm_mday;
	data[4] = tm->tm_wday + 1;
	data[5] = tm->tm_mon + 1;
	data[6] = tm->tm_year;

	ret = regmap_bulk_write(rtc->regmap, rtc->addr_base + RTC_TC_SEC,
				data, RTC_OFFSET_COUNT);
	if (ret)
		return ret;

	return mtk_rtc_write_trigger(rtc);
}

static const struct rtc_class_ops mt6320_rtc_ops = {
	.read_time = mt6320_rtc_read_time,
	.set_time = mt6320_rtc_set_time,
};

static struct regmap *mt6320_get_regmap(struct device *dev)
{
	struct regmap *map;

	map = dev_get_regmap(dev->parent, NULL);
	if (map)
		return map;
	if (dev->parent && dev->parent->parent)
		return dev_get_regmap(dev->parent->parent, NULL);
	return NULL;
}

static int mt6320_rtc_probe(struct device *dev)
{
	struct mt6320_rtc *rtc;
	struct resource *res;

	rtc = xzalloc(sizeof(*rtc));
	rtc->regmap = mt6320_get_regmap(dev);
	if (!rtc->regmap) {
		dev_err(dev, "no regmap from PWRAP\n");
		return -ENODEV;
	}

	res = dev_get_resource(dev, IORESOURCE_MEM, 0);
	if (!IS_ERR(res))
		rtc->addr_base = res->start;
	else
		rtc->addr_base = MT6320_RTC_BASE;

	rtc->rtc.dev = dev;
	rtc->rtc.ops = &mt6320_rtc_ops;
	return rtc_register(&rtc->rtc);
}

static const struct of_device_id mt6320_rtc_ids[] = {
	{ .compatible = "mediatek,mt6320-rtc" },
	{ .compatible = "mediatek,mt6397-rtc" },
	{ .compatible = "mediatek,mt6323-rtc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mt6320_rtc_ids);

static struct driver mt6320_rtc_driver = {
	.name = "mt6320-rtc",
	.probe = mt6320_rtc_probe,
	.of_compatible = DRV_OF_COMPAT(mt6320_rtc_ids),
};
device_platform_driver(mt6320_rtc_driver);
