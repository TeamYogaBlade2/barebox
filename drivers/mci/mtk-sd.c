// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MSDC host for MT6589 secondary bootloader
 * Basic init / clock / bus-width support. Full CMD/data path still limited.
 */

#include <common.h>
#include <init.h>
#include <mci.h>
#include <of_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <io.h>
#include <linux/bitops.h>

#define MSDC_CFG		0x00
#define MSDC_CFG_MODE		BIT(0)
#define MSDC_CFG_CKPDN		BIT(1)
#define MSDC_CFG_RST		BIT(2)
#define MSDC_CFG_PIO		BIT(3)
#define MSDC_CFG_CKSTB		BIT(7)
#define MSDC_CFG_CKDIV		GENMASK(15, 8)
#define MSDC_CFG_CKMOD		GENMASK(17, 16)

#define MSDC_IOCON		0x04
#define MSDC_PS			0x24
#define MSDC_INT		0x0c
#define MSDC_INTEN		0x10
#define SDC_CFG			0x30
#define SDC_CMD			0x34
#define SDC_ARG			0x38
#define SDC_STS			0x3c
#define SDC_STS_SDCBUSY		BIT(0)
#define SDC_STS_CMDBUSY		BIT(1)

struct mtk_sd_host {
	struct mci_host mci;
	void __iomem *base;
	struct clk *src_clk;
	struct clk *h_clk;
	unsigned int src_hz;
};

static void msdc_reset_hw(struct mtk_sd_host *host)
{
	u32 val;

	val = readl(host->base + MSDC_CFG);
	val |= MSDC_CFG_RST;
	writel(val, host->base + MSDC_CFG);
	while (readl(host->base + MSDC_CFG) & MSDC_CFG_RST)
		;
}

static int mtk_sd_init(struct mci_host *mci, struct device *dev)
{
	struct mtk_sd_host *host = container_of(mci, struct mtk_sd_host, mci);
	u32 val;

	if (!host->base)
		return 0;

	/* Basic controller enable, PIO mode */
	val = readl(host->base + MSDC_CFG);
	val |= MSDC_CFG_MODE | MSDC_CFG_PIO;
	val &= ~MSDC_CFG_CKPDN;
	writel(val, host->base + MSDC_CFG);

	msdc_reset_hw(host);

	/* Clear interrupts */
	writel(0xffffffff, host->base + MSDC_INT);
	writel(0, host->base + MSDC_INTEN);

	return 0;
}

static void mtk_sd_set_ios(struct mci_host *mci, struct mci_ios *ios)
{
	struct mtk_sd_host *host = container_of(mci, struct mtk_sd_host, mci);
	u32 val, div = 0, mode = 0;

	if (!host->base || !host->src_hz)
		return;

	/* Simple divider calculation */
	if (ios->clock >= host->src_hz) {
		div = 0;
		mode = 1; /* no div */
	} else if (ios->clock) {
		div = (host->src_hz + ios->clock - 1) / (ios->clock * 2);
		if (div > 0xff)
			div = 0xff;
	} else {
		/* stop clock */
		val = readl(host->base + MSDC_CFG);
		val |= MSDC_CFG_CKPDN;
		writel(val, host->base + MSDC_CFG);
		return;
	}

	val = readl(host->base + MSDC_CFG);
	val &= ~(MSDC_CFG_CKDIV | MSDC_CFG_CKMOD | MSDC_CFG_CKPDN);
	val |= (div << 8) | (mode << 16);
	writel(val, host->base + MSDC_CFG);

	/* wait stable */
	while (!(readl(host->base + MSDC_CFG) & MSDC_CFG_CKSTB))
		;
}

static int mtk_sd_send_cmd(struct mci_host *mci, struct mci_cmd *cmd)
{
	/* Full command engine (SDC_CMD/ARG/STS + response) still TODO.
	 * Return -ENOSYS so upper layers know the path is incomplete.
	 */
	return -ENOSYS;
}

static const struct mci_ops mtk_sd_ops = {
	.init = mtk_sd_init,
	.set_ios = mtk_sd_set_ios,
	.send_cmd = mtk_sd_send_cmd,
};

static int mtk_sd_probe(struct device *dev)
{
	struct mtk_sd_host *host;
	struct resource *res;

	host = xzalloc(sizeof(*host));
	host->mci.hw_dev = dev;
	host->mci.ops = mtk_sd_ops;
	host->mci.voltages = MMC_VDD_32_33 | MMC_VDD_33_34;
	host->mci.host_caps = MMC_CAP_4_BIT_DATA | MMC_CAP_MMC_HIGHSPEED;

	res = dev_get_resource(dev, IORESOURCE_MEM, 0);
	if (!IS_ERR(res))
		host->base = IOMEM(res->start);

	host->src_clk = clk_get(dev, "source");
	if (IS_ERR_OR_NULL(host->src_clk))
		host->src_clk = clk_get(dev, NULL);
	if (!IS_ERR_OR_NULL(host->src_clk)) {
		clk_enable(host->src_clk);
		host->src_hz = clk_get_rate(host->src_clk);
	}
	if (!host->src_hz)
		host->src_hz = 48000000; /* fallback */

	host->h_clk = clk_get(dev, "hclk");
	if (!IS_ERR_OR_NULL(host->h_clk))
		clk_enable(host->h_clk);

	dev_info(dev, "MTK MSDC host registered (src %u Hz)\n", host->src_hz);
	return mci_register(&host->mci);
}

static const struct of_device_id mtk_sd_ids[] = {
	{ .compatible = "mediatek,mt6589-mmc" },
	{ .compatible = "mediatek,mtk-sd" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_sd_ids);

static struct driver mtk_sd_driver = {
	.name = "mtk-sd",
	.probe = mtk_sd_probe,
	.of_compatible = DRV_OF_COMPAT(mtk_sd_ids),
};
device_platform_driver(mtk_sd_driver);
