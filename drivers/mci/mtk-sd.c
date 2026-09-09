// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MSDC host for MT6589 secondary bootloader
 * PIO mode with Read and Write support.
 */

#include <common.h>
#include <init.h>
#include <mci.h>
#include <of_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <io.h>
#include <linux/bitops.h>
#include <clock.h>
#include <dma.h>

#define MSDC_CFG		0x00
#define MSDC_CFG_MODE		BIT(0)
#define MSDC_CFG_CKPDN		BIT(1)
#define MSDC_CFG_RST		BIT(2)
#define MSDC_CFG_PIO		BIT(3)
#define MSDC_CFG_CKSTB		BIT(7)
#define MSDC_CFG_CKDIV		GENMASK(15, 8)
#define MSDC_CFG_CKMOD		GENMASK(17, 16)

#define MSDC_IOCON		0x04
#define MSDC_PS			0x08
#define MSDC_INT		0x0c
#define MSDC_INTEN		0x10
#define MSDC_FIFOCS		0x14
#define MSDC_TXDATA		0x18
#define MSDC_RXDATA		0x1c

#define MSDC_FIFOCS_RXCNT	GENMASK(7, 0)
#define MSDC_FIFOCS_TXCNT	GENMASK(23, 16)
#define MSDC_FIFOCS_CLR		BIT(31)
#define MSDC_FIFO_SZ		128

#define SDC_CFG			0x30
#define SDC_CMD			0x34
#define SDC_ARG			0x38
#define SDC_STS			0x3c
#define SDC_STS_SDCBUSY		BIT(0)
#define SDC_STS_CMDBUSY		BIT(1)
#define SDC_RESP0		0x40
#define SDC_RESP1		0x44
#define SDC_RESP2		0x48
#define SDC_RESP3		0x4c
#define SDC_BLK_NUM		0x50

#define MSDC_INT_CMDRDY		BIT(8)
#define MSDC_INT_CMDTMO		BIT(9)
#define MSDC_INT_RSPCRCERR	BIT(10)
#define MSDC_INT_XFER_COMPL	BIT(12)
#define MSDC_INT_DATTMO		BIT(14)
#define MSDC_INT_DATCRCERR	BIT(15)

struct mtk_sd_host {
	struct mci_host mci;
	void __iomem *base;
	struct clk *src_clk;
	struct clk *h_clk;
	unsigned int src_hz;
};

static void msdc_reset_hw(struct mtk_sd_host *host)
{
	u32 val = readl(host->base + MSDC_CFG);
	val |= MSDC_CFG_RST;
	writel(val, host->base + MSDC_CFG);
	while (readl(host->base + MSDC_CFG) & MSDC_CFG_RST)
		;
}

static void msdc_fifo_clr(struct mtk_sd_host *host)
{
	u32 val = readl(host->base + MSDC_FIFOCS);
	val |= MSDC_FIFOCS_CLR;
	writel(val, host->base + MSDC_FIFOCS);
	while (readl(host->base + MSDC_FIFOCS) & MSDC_FIFOCS_CLR)
		;
}

static int mtk_sd_init(struct mci_host *mci, struct device *dev)
{
	struct mtk_sd_host *host = container_of(mci, struct mtk_sd_host, mci);
	u32 val;

	if (!host->base)
		return 0;

	val = readl(host->base + MSDC_CFG);
	val |= MSDC_CFG_MODE | MSDC_CFG_PIO;
	val &= ~MSDC_CFG_CKPDN;
	writel(val, host->base + MSDC_CFG);

	msdc_reset_hw(host);
	msdc_fifo_clr(host);
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

	if (ios->clock >= host->src_hz) {
		div = 0;
		mode = 1;
	} else if (ios->clock) {
		div = (host->src_hz + ios->clock - 1) / (ios->clock * 2);
		if (div > 0xff)
			div = 0xff;
	} else {
		val = readl(host->base + MSDC_CFG);
		val |= MSDC_CFG_CKPDN;
		writel(val, host->base + MSDC_CFG);
		return;
	}

	val = readl(host->base + MSDC_CFG);
	val &= ~(MSDC_CFG_CKDIV | MSDC_CFG_CKMOD | MSDC_CFG_CKPDN);
	val |= (div << 8) | (mode << 16);
	writel(val, host->base + MSDC_CFG);

	while (!(readl(host->base + MSDC_CFG) & MSDC_CFG_CKSTB))
		;
}

static u32 msdc_cmd_find_resp(struct mci_cmd *cmd)
{
	switch (cmd->resp_type) {
	case MMC_RSP_R1:
	case MMC_RSP_R1b:
		return 0x1;
	case MMC_RSP_R2:
		return 0x2;
	case MMC_RSP_R3:
		return 0x3;
	default:
		return 0x0;
	}
}

static int msdc_pio_read(struct mtk_sd_host *host, struct mci_data *data)
{
	u8 *buf = data->dest;
	unsigned int left = data->blocks * data->blocksize;
	u64 start;
	u32 count, val;

	while (left) {
		start = get_time_ns();
		do {
			count = readl(host->base + MSDC_FIFOCS) & MSDC_FIFOCS_RXCNT;
			if (count)
				break;
			val = readl(host->base + MSDC_INT);
			if (val & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) {
				writel(val, host->base + MSDC_INT);
				return (val & MSDC_INT_DATTMO) ? -ETIMEDOUT : -EILSEQ;
			}
		} while (!is_timeout(start, 500 * MSECOND));

		if (!count)
			return -ETIMEDOUT;

		if (count > left)
			count = left;
		/* prefer 4-byte access */
		while (count >= 4) {
			*(u32 *)buf = readl(host->base + MSDC_RXDATA);
			buf += 4;
			count -= 4;
			left -= 4;
		}
		while (count) {
			*buf++ = readb(host->base + MSDC_RXDATA);
			count--;
			left--;
		}
	}

	/* wait transfer complete */
	start = get_time_ns();
	do {
		val = readl(host->base + MSDC_INT);
		if (val & MSDC_INT_XFER_COMPL)
			break;
		if (val & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) {
			writel(val, host->base + MSDC_INT);
			return (val & MSDC_INT_DATTMO) ? -ETIMEDOUT : -EILSEQ;
		}
	} while (!is_timeout(start, 500 * MSECOND));

	writel(val, host->base + MSDC_INT);
	return (val & MSDC_INT_XFER_COMPL) ? 0 : -ETIMEDOUT;
}

static int msdc_pio_write(struct mtk_sd_host *host, struct mci_data *data)
{
	const u8 *buf = data->src;
	unsigned int left = data->blocks * data->blocksize;
	u64 start;
	u32 count, val, space;

	while (left) {
		start = get_time_ns();
		do {
			count = (readl(host->base + MSDC_FIFOCS) & MSDC_FIFOCS_TXCNT) >> 16;
			space = MSDC_FIFO_SZ - count;
			if (space)
				break;
			val = readl(host->base + MSDC_INT);
			if (val & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) {
				writel(val, host->base + MSDC_INT);
				return (val & MSDC_INT_DATTMO) ? -ETIMEDOUT : -EILSEQ;
			}
		} while (!is_timeout(start, 500 * MSECOND));

		if (!space)
			return -ETIMEDOUT;

		if (space > left)
			space = left;
		while (space >= 4) {
			writel(*(const u32 *)buf, host->base + MSDC_TXDATA);
			buf += 4;
			space -= 4;
			left -= 4;
		}
		while (space) {
			writeb(*buf++, host->base + MSDC_TXDATA);
			space--;
			left--;
		}
	}

	start = get_time_ns();
	do {
		val = readl(host->base + MSDC_INT);
		if (val & MSDC_INT_XFER_COMPL)
			break;
		if (val & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) {
			writel(val, host->base + MSDC_INT);
			return (val & MSDC_INT_DATTMO) ? -ETIMEDOUT : -EILSEQ;
		}
	} while (!is_timeout(start, 1000 * MSECOND));

	writel(val, host->base + MSDC_INT);
	return (val & MSDC_INT_XFER_COMPL) ? 0 : -ETIMEDOUT;
}

static int mtk_sd_send_cmd(struct mci_host *mci, struct mci_cmd *cmd)
{
	struct mtk_sd_host *host = container_of(mci, struct mtk_sd_host, mci);
	u32 rawcmd, val, resp;
	u64 start;
	int ret = 0;

	if (!host->base)
		return -ENODEV;

	start = get_time_ns();
	while (readl(host->base + SDC_STS) & SDC_STS_CMDBUSY) {
		if (is_timeout(start, 20 * MSECOND))
			return -ETIMEDOUT;
	}

	if (cmd->data || cmd->resp_type == MMC_RSP_R1b) {
		start = get_time_ns();
		while (readl(host->base + SDC_STS) & SDC_STS_SDCBUSY) {
			if (is_timeout(start, 20 * MSECOND))
				return -ETIMEDOUT;
		}
	}

	writel(0xffffffff, host->base + MSDC_INT);
	msdc_fifo_clr(host);

	resp = msdc_cmd_find_resp(cmd);
	rawcmd = (cmd->cmdidx & 0x3f) | ((resp & 0x7) << 7);

	if (cmd->data) {
		struct mci_data *data = cmd->data;
		rawcmd |= ((data->blocksize & 0xfff) << 16);
		if (data->flags & MMC_DATA_WRITE)
			rawcmd |= BIT(13);
		if (data->blocks > 1)
			rawcmd |= BIT(12); /* multi */
		else
			rawcmd |= BIT(11); /* single */
		/* keep PIO mode */
		val = readl(host->base + MSDC_CFG);
		val |= MSDC_CFG_PIO;
		writel(val, host->base + MSDC_CFG);
		writel(data->blocks, host->base + SDC_BLK_NUM);
	}

	writel(cmd->cmdarg, host->base + SDC_ARG);
	writel(rawcmd, host->base + SDC_CMD);

	/* Wait command done */
	start = get_time_ns();
	do {
		val = readl(host->base + MSDC_INT);
		if (val & (MSDC_INT_CMDRDY | MSDC_INT_CMDTMO | MSDC_INT_RSPCRCERR))
			break;
	} while (!is_timeout(start, 100 * MSECOND));

	writel(val & (MSDC_INT_CMDRDY | MSDC_INT_CMDTMO | MSDC_INT_RSPCRCERR),
	       host->base + MSDC_INT);

	if (val & MSDC_INT_CMDTMO)
		return -ETIMEDOUT;
	if (val & MSDC_INT_RSPCRCERR)
		return -EILSEQ;
	if (!(val & MSDC_INT_CMDRDY))
		return -ETIMEDOUT;

	if (cmd->resp_type == MMC_RSP_R2) {
		cmd->response[0] = readl(host->base + SDC_RESP3);
		cmd->response[1] = readl(host->base + SDC_RESP2);
		cmd->response[2] = readl(host->base + SDC_RESP1);
		cmd->response[3] = readl(host->base + SDC_RESP0);
	} else if (cmd->resp_type != MMC_RSP_NONE) {
		cmd->response[0] = readl(host->base + SDC_RESP0);
	}

	/* Data phase in PIO */
	if (cmd->data) {
		if (cmd->data->flags & MMC_DATA_WRITE)
			ret = msdc_pio_write(host, cmd->data);
		else
			ret = msdc_pio_read(host, cmd->data);
	}

	return ret;
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
		host->src_hz = 48000000;

	host->h_clk = clk_get(dev, "hclk");
	if (!IS_ERR_OR_NULL(host->h_clk))
		clk_enable(host->h_clk);

	dev_info(dev, "MTK MSDC PIO host registered (src %u Hz)\n", host->src_hz);
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
