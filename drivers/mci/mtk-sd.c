// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MSDC host (minimal) for MT6589 secondary bootloader
 * Full command/data path deferred; registers the host so DT probe succeeds.
 */

#include <common.h>
#include <init.h>
#include <mci.h>
#include <of_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <io.h>

struct mtk_sd_host {
	struct mci_host mci;
	void __iomem *base;
	struct clk *src_clk;
	struct clk *h_clk;
};

static int mtk_sd_init(struct mci_host *mci, struct device *dev)
{
	return 0;
}

static void mtk_sd_set_ios(struct mci_host *mci, struct mci_ios *ios)
{
	/* TODO: set clock / bus width when full driver is ready */
}

static int mtk_sd_send_cmd(struct mci_host *mci, struct mci_cmd *cmd)
{
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
	if (!IS_ERR_OR_NULL(host->src_clk))
		clk_enable(host->src_clk);
	host->h_clk = clk_get(dev, "hclk");
	if (!IS_ERR_OR_NULL(host->h_clk))
		clk_enable(host->h_clk);

	dev_info(dev, "MTK MSDC host registered (minimal)\n");
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
