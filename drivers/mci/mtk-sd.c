// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MSDC host for MT6589 secondary bootloader
 * PIO mode with Read and Write support.
 *
 * Protocol details aligned with U-Boot drivers/mmc/mtk-sd.c
 * (resp types, bus width, clock divider, STOP, DTOC).
 */

#include <common.h>
#include <init.h>
#include <mci.h>
#include <of_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <io.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>
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
#define MSDC_PS_DAT0		BIT(16)
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
#define SDC_CFG_DTOC		GENMASK(31, 24)
#define SDC_CFG_BUSWIDTH	GENMASK(17, 16)
#define SDC_CMD			0x34
#define SDC_CMD_BLK_LEN		GENMASK(27, 16)
#define SDC_CMD_STOP		BIT(14)
#define SDC_CMD_WR		BIT(13)
#define SDC_CMD_DTYPE		GENMASK(12, 11)
#define SDC_CMD_RSPTYP		GENMASK(9, 7)	/* bits 9..7, not 10..8 */
#define SDC_CMD_CMD		GENMASK(5, 0)
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

#define MSDC_BUS_1BITS		0x0
#define MSDC_BUS_4BITS		0x1
#define MSDC_BUS_8BITS		0x2

/* Data timeout unit = 2^20 sclk cycles (U-Boot SCLK_CYCLES_SHIFT) */
#define SCLK_CYCLES_SHIFT	20

struct mtk_sd_host {
	struct mci_host mci;
	void __iomem *base;
	struct clk *src_clk;
	struct clk *h_clk;
	unsigned int src_hz;
	unsigned int sclk;	/* actual SDCLK after divider */
	u32 timeout_ns;
	u32 timeout_clks;
	u32 last_resp_type;
	bool last_data_write;
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

static void msdc_set_timeout(struct mtk_sd_host *host, u32 ns, u32 clks)
{
	u32 timeout, clk_ns, mode;

	host->timeout_ns = ns;
	host->timeout_clks = clks;

	if (!host->sclk) {
		timeout = 0;
	} else {
		clk_ns = 1000000000UL / host->sclk;
		timeout = (ns + clk_ns - 1) / clk_ns + clks;
		/* unit is 2^20 sclk cycles */
		timeout = (timeout + (1U << SCLK_CYCLES_SHIFT) - 1)
			  >> SCLK_CYCLES_SHIFT;
		mode = FIELD_GET(MSDC_CFG_CKMOD, readl(host->base + MSDC_CFG));
		/* DDR mode (mode >= 2) doubles the cycle count */
		if (mode >= 2)
			timeout *= 2;
		timeout = timeout > 1 ? timeout - 1 : 0;
		if (timeout > 255)
			timeout = 255;
	}

	clrsetbits_le32(host->base + SDC_CFG, SDC_CFG_DTOC,
			FIELD_PREP(SDC_CFG_DTOC, timeout));
}

static void msdc_set_buswidth(struct mtk_sd_host *host, enum mci_bus_width width)
{
	u32 val = readl(host->base + SDC_CFG);
	u32 bw;

	val &= ~SDC_CFG_BUSWIDTH;

	switch (width) {
	case MMC_BUS_WIDTH_8:
		bw = MSDC_BUS_8BITS;
		break;
	case MMC_BUS_WIDTH_4:
		bw = MSDC_BUS_4BITS;
		break;
	case MMC_BUS_WIDTH_1:
	default:
		bw = MSDC_BUS_1BITS;
		break;
	}

	val |= FIELD_PREP(SDC_CFG_BUSWIDTH, bw);
	writel(val, host->base + SDC_CFG);
}

/*
 * Clock programming from U-Boot msdc_set_mclk() (divisor modes only;
 * DDR/HS400 paths omitted — not needed for secondary bootloader).
 *
 * CKMOD:
 *   0 = divisor mode: sclk = src / (4 * div)  [div=0 means /2]
 *   1 = no divisor:   sclk = src
 *   2 = DDR divisor (unused here)
 *   3 = HS400 (unused here)
 */
static void msdc_set_mclk(struct mtk_sd_host *host, unsigned int hz)
{
	u32 mode, div, sclk, val;

	if (!hz) {
		val = readl(host->base + MSDC_CFG);
		val &= ~MSDC_CFG_CKPDN;
		writel(val, host->base + MSDC_CFG);
		host->sclk = 0;
		return;
	}

	if (hz >= host->src_hz) {
		mode = 0x1; /* no divisor */
		div = 0;
		sclk = host->src_hz;
	} else {
		mode = 0x0; /* use divisor */
		if (hz >= (host->src_hz >> 1)) {
			div = 0; /* means div = 1/2 */
			sclk = host->src_hz >> 1;
		} else {
			div = (host->src_hz + ((hz << 2) - 1)) / (hz << 2);
			sclk = (host->src_hz >> 2) / div;
		}
	}

	if (div > 0xff)
		div = 0xff;

	val = readl(host->base + MSDC_CFG);
	val &= ~(MSDC_CFG_CKPDN | MSDC_CFG_CKDIV | MSDC_CFG_CKMOD);
	val |= FIELD_PREP(MSDC_CFG_CKMOD, mode) |
	       FIELD_PREP(MSDC_CFG_CKDIV, div);
	writel(val, host->base + MSDC_CFG);

	while (!(readl(host->base + MSDC_CFG) & MSDC_CFG_CKSTB))
		;

	val = readl(host->base + MSDC_CFG);
	val |= MSDC_CFG_CKPDN;
	writel(val, host->base + MSDC_CFG);

	host->sclk = sclk;
	msdc_set_timeout(host, host->timeout_ns, host->timeout_clks);
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

	/* Default DTOC = 3 (same as U-Boot probe default) */
	host->timeout_ns = 1000000000U / 2; /* 0.5s in ns scale used below */
	host->timeout_clks = 0;
	host->sclk = host->src_hz ? host->src_hz / 2 : 0;
	clrsetbits_le32(host->base + SDC_CFG, SDC_CFG_DTOC,
			FIELD_PREP(SDC_CFG_DTOC, 3));

	msdc_set_buswidth(host, MMC_BUS_WIDTH_1);
	return 0;
}

static void mtk_sd_set_ios(struct mci_host *mci, struct mci_ios *ios)
{
	struct mtk_sd_host *host = container_of(mci, struct mtk_sd_host, mci);

	if (!host->base)
		return;

	msdc_set_buswidth(host, ios->bus_width);
	msdc_set_mclk(host, ios->clock);
}

static u32 msdc_cmd_find_resp(struct mci_cmd *cmd)
{
	switch (cmd->resp_type) {
	case MMC_RSP_R1:
		return 0x1;
	case MMC_RSP_R1b:
		return 0x7;	/* busy-aware response */
	case MMC_RSP_R2:
		return 0x2;
	case MMC_RSP_R3:
		return 0x3;
	default:
		return 0x0;
	}
}

static int msdc_card_busy(struct mtk_sd_host *host)
{
	u64 start = get_time_ns();

	/* Wait programming busy on DAT0 after R1b / write */
	do {
		if (readl(host->base + MSDC_PS) & MSDC_PS_DAT0)
			return 0;
	} while (!is_timeout(start, 1000 * MSECOND));

	return -ETIMEDOUT;
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
			if (val & MSDC_INT_XFER_COMPL) {
				/* transfer done; drain remaining FIFO */
				count = readl(host->base + MSDC_FIFOCS) &
					MSDC_FIFOCS_RXCNT;
				if (count)
					break;
			}
		} while (!is_timeout(start, 500 * MSECOND));

		if (!count)
			return -ETIMEDOUT;

		if (count > left)
			count = left;
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
	u32 rawcmd, val, resp, dtype = 0;
	u64 start;
	int ret = 0;

	if (!host->base)
		return -ENODEV;

	/* Wait CMD bus idle */
	start = get_time_ns();
	while (readl(host->base + SDC_STS) & SDC_STS_CMDBUSY) {
		if (is_timeout(start, 20 * MSECOND)) {
			msdc_reset_hw(host);
			return -ETIMEDOUT;
		}
	}

	if (cmd->data || cmd->resp_type == MMC_RSP_R1b) {
		start = get_time_ns();
		while (readl(host->base + SDC_STS) & SDC_STS_SDCBUSY) {
			if (is_timeout(start, 20 * MSECOND)) {
				msdc_reset_hw(host);
				return -ETIMEDOUT;
			}
		}
	}

	/* After previous R1b / write, ensure DAT0 released */
	if (host->last_resp_type == MMC_RSP_R1b || host->last_data_write) {
		ret = msdc_card_busy(host);
		if (ret) {
			msdc_reset_hw(host);
			return ret;
		}
	}

	writel(0xffffffff, host->base + MSDC_INT);
	msdc_fifo_clr(host);

	resp = msdc_cmd_find_resp(cmd);

	if (cmd->data) {
		struct mci_data *data = cmd->data;

		if (data->blocks > 1)
			dtype = 2;
		else
			dtype = 1;

		val = readl(host->base + MSDC_CFG);
		val |= MSDC_CFG_PIO;
		writel(val, host->base + MSDC_CFG);
		writel(data->blocks, host->base + SDC_BLK_NUM);
	} else if (cmd->cmdidx == MMC_CMD_STOP_TRANSMISSION) {
		/* STOP has no data phase but still needs STOP bit */
	}

	rawcmd = FIELD_PREP(SDC_CMD_CMD, cmd->cmdidx) |
		 FIELD_PREP(SDC_CMD_RSPTYP, resp) |
		 FIELD_PREP(SDC_CMD_DTYPE, dtype);

	if (cmd->data) {
		rawcmd |= FIELD_PREP(SDC_CMD_BLK_LEN, cmd->data->blocksize);
		if (cmd->data->flags & MMC_DATA_WRITE)
			rawcmd |= SDC_CMD_WR;
	}

	if (cmd->cmdidx == MMC_CMD_STOP_TRANSMISSION)
		rawcmd |= SDC_CMD_STOP;

	writel(cmd->cmdarg, host->base + SDC_ARG);
	writel(rawcmd, host->base + SDC_CMD);

	start = get_time_ns();
	do {
		val = readl(host->base + MSDC_INT);
		if (val & (MSDC_INT_CMDRDY | MSDC_INT_CMDTMO | MSDC_INT_RSPCRCERR))
			break;
	} while (!is_timeout(start, 100 * MSECOND));

	writel(val & (MSDC_INT_CMDRDY | MSDC_INT_CMDTMO | MSDC_INT_RSPCRCERR),
	       host->base + MSDC_INT);

	host->last_resp_type = cmd->resp_type;
	host->last_data_write = false;

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

	if (cmd->data) {
		if (cmd->data->flags & MMC_DATA_WRITE) {
			host->last_data_write = true;
			ret = msdc_pio_write(host, cmd->data);
		} else {
			ret = msdc_pio_read(host, cmd->data);
		}
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
