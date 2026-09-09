// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MSDC SD/eMMC host controller driver for barebox.
 * PIO mode only - sufficient for bootloader use.
 *
 * Copyright (c) 2026 Akari Tsuyukusa <akkun11.open@gmail.com>
 */

#include <clock.h>
#include <common.h>
#include <dma.h>
#include <driver.h>
#include <errno.h>
#include <init.h>
#include <io.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <mci.h>
#include <of.h>

/* Register offsets */
#define MSDC_CFG		0x00
#define MSDC_IOCON		0x04
#define MSDC_PS			0x08
#define MSDC_INT		0x0c
#define MSDC_INTEN		0x10
#define MSDC_FIFOCS		0x14
#define MSDC_TXDATA		0x18
#define MSDC_RXDATA		0x1c
#define SDC_CFG			0x30
#define SDC_CMD			0x34
#define SDC_ARG			0x38
#define SDC_STS			0x3c
#define SDC_RESP0		0x40
#define SDC_RESP1		0x44
#define SDC_RESP2		0x48
#define SDC_RESP3		0x4c
#define SDC_BLK_NUM		0x50
#define MSDC_DMA_SA		0x90
#define MSDC_DMA_CTRL		0x98
#define MSDC_DMA_CFG		0x9c
#define MSDC_PATCH_BIT		0xb0
#define MSDC_PATCH_BIT1		0xb4
#define MSDC_PAD_TUNE		0xec

/* MSDC_CFG */
#define MSDC_CFG_MODE		BIT(0)	/* 1=SD, 0=SPI */
#define MSDC_CFG_CKPDN		BIT(1)
#define MSDC_CFG_RST		BIT(2)
#define MSDC_CFG_PIO		BIT(3)
#define MSDC_CFG_CKDRVEN	BIT(4)
#define MSDC_CFG_CKSTB		BIT(7)
#define MSDC_CFG_CKDIV		GENMASK(15, 8)
#define MSDC_CFG_CKMOD		GENMASK(17, 16)

/* MSDC_INT / MSDC_INTEN */
#define MSDC_INT_CMDTMO		BIT(1)
#define MSDC_INT_CMDRDY		BIT(3)
#define MSDC_INT_RSPCRCERR	BIT(4)
#define MSDC_INT_CSTA		BIT(5)
#define MSDC_INT_XFER_COMPL	BIT(12)
#define MSDC_INT_DATTMO		BIT(14)
#define MSDC_INT_DATCRCERR	BIT(15)
#define MSDC_INT_ACMD19_DONE	BIT(16)
#define MSDC_INT_ACMDRDY	BIT(17)
#define MSDC_INT_ACMDTMO	BIT(18)
#define MSDC_INT_ACMDCRCERR	BIT(19)

/* MSDC_FIFOCS */
#define MSDC_FIFOCS_TXCNT	GENMASK(7, 0)
#define MSDC_FIFOCS_RXCNT	GENMASK(23, 16)
#define MSDC_FIFOCS_CLR		BIT(31)

/* SDC_CFG */
#define SDC_CFG_BUSWIDTH	GENMASK(17, 16)
#define SDC_CFG_BUSWIDTH_1	0
#define SDC_CFG_BUSWIDTH_4	1
#define SDC_CFG_BUSWIDTH_8	2
#define SDC_CFG_BLKLEN		GENMASK(31, 20)

/* SDC_STS */
#define SDC_STS_SDCBUSY		BIT(0)
#define SDC_STS_CMDBUSY		BIT(1)
#define SDC_STS_DLYFIRST	BIT(12)

/* SDC_CMD */
#define SDC_CMD_CMD		GENMASK(5, 0)
#define SDC_CMD_BLK		BIT(6)
#define SDC_CMD_WRITE		BIT(7)
#define SDC_CMD_RSPTYP		GENMASK(10, 8)
#define SDC_CMD_RSPTYP_NONE	0
#define SDC_CMD_RSPTYP_R1	1
#define SDC_CMD_RSPTYP_R2	2
#define SDC_CMD_RSPTYP_R3	3
#define SDC_CMD_RSPTYP_R4	4
#define SDC_CMD_RSPTYP_R5	5
#define SDC_CMD_RSPTYP_R1B	7
#define SDC_CMD_DTYPE		GENMASK(12, 11)
#define SDC_CMD_DTYPE_NONE	0
#define SDC_CMD_DTYPE_SINGLE	1
#define SDC_CMD_DTYPE_MULTI	2
#define SDC_CMD_DTYPE_STREAM	3
#define SDC_CMD_STOP		BIT(14)
#define SDC_CMD_GOIRQ		BIT(15)
#define SDC_CMD_INSWAIT		BIT(16)
#define SDC_CMD_VOLATILE	BIT(18)

#define MSDC_TIMEOUT_MS		1000

struct msdc_host {
	struct mci_host	mci;
	struct device	*dev;
	void __iomem	*base;
	struct clk	*src_clk;
	struct clk	*h_clk;
	u32		src_clk_freq;
	u32		sclk;		/* current bus clock */
};

static inline u32 msdc_readl(struct msdc_host *h, u32 off)
{
	return readl(h->base + off);
}
static inline void msdc_writel(struct msdc_host *h, u32 off, u32 val)
{
	writel(val, h->base + off);
}

static inline struct msdc_host *to_msdc_host(struct mci_host *mci)
{
	return container_of(mci, struct msdc_host, mci);
}

static int msdc_wait_int(struct msdc_host *h, u32 mask, u64 timeout_ns)
{
	u64 start = get_time_ns();

	while (!is_timeout(start, timeout_ns)) {
		u32 val = msdc_readl(h, MSDC_INT);
		if (val & mask) {
			msdc_writel(h, MSDC_INT, val & mask); /* ack */
			if (val & (MSDC_INT_CMDTMO | MSDC_INT_DATTMO))
				return -ETIMEDOUT;
			if (val & (MSDC_INT_RSPCRCERR | MSDC_INT_DATCRCERR |
				   MSDC_INT_ACMDCRCERR))
				return -EILSEQ;
			return 0;
		}
	}
	return -ETIMEDOUT;
}

static int msdc_wait_bus_free(struct msdc_host *h)
{
	u64 start = get_time_ns();

	while (!is_timeout(start, MSDC_TIMEOUT_MS * MSECOND)) {
		u32 sts = msdc_readl(h, SDC_STS);
		if (!(sts & (SDC_STS_SDCBUSY | SDC_STS_CMDBUSY)))
			return 0;
	}
	return -ETIMEDOUT;
}

static int msdc_send_cmd(struct msdc_host *h, struct mci_cmd *cmd,
			 struct mci_data *data)
{
	u32 rawcmd, rawarg;
	u32 rsptyp, dtype;
	int ret;

	ret = msdc_wait_bus_free(h);
	if (ret) {
		dev_err(h->dev, "bus busy before cmd%u\n", cmd->cmdidx);
		return ret;
	}

	/* Response type */
	switch (cmd->resp_type) {
	case MMC_RSP_NONE:	rsptyp = SDC_CMD_RSPTYP_NONE; break;
	case MMC_RSP_R1:	rsptyp = SDC_CMD_RSPTYP_R1;   break;
	case MMC_RSP_R1b:	rsptyp = SDC_CMD_RSPTYP_R1B;  break;
	case MMC_RSP_R2:	rsptyp = SDC_CMD_RSPTYP_R2;   break;
	case MMC_RSP_R3:	rsptyp = SDC_CMD_RSPTYP_R3;   break;
	default:		rsptyp = SDC_CMD_RSPTYP_NONE; break;
	}

	/* Data type */
	if (!data) {
		dtype = SDC_CMD_DTYPE_NONE;
	} else if (data->blocks == 1) {
		dtype = SDC_CMD_DTYPE_SINGLE;
	} else {
		dtype = SDC_CMD_DTYPE_MULTI;
	}

	rawcmd = FIELD_PREP(SDC_CMD_CMD, cmd->cmdidx) |
		 FIELD_PREP(SDC_CMD_RSPTYP, rsptyp) |
		 FIELD_PREP(SDC_CMD_DTYPE, dtype);

	if (data && !(data->flags & MMC_DATA_READ))
		rawcmd |= SDC_CMD_WRITE;

	rawarg = cmd->cmdarg;

	/* Set block count */
	if (data) {
		u32 sdccfg = msdc_readl(h, SDC_CFG);
		sdccfg &= ~SDC_CFG_BLKLEN;
		sdccfg |= FIELD_PREP(SDC_CFG_BLKLEN, data->blocksize);
		msdc_writel(h, SDC_CFG, sdccfg);
		msdc_writel(h, SDC_BLK_NUM, data->blocks);
	}

	msdc_writel(h, SDC_ARG, rawarg);
	msdc_writel(h, SDC_CMD, rawcmd);

	/* Wait for command completion */
	ret = msdc_wait_int(h,
		MSDC_INT_CMDRDY | MSDC_INT_CMDTMO | MSDC_INT_RSPCRCERR,
		MSDC_TIMEOUT_MS * MSECOND);
	if (ret) {
		dev_dbg(h->dev, "cmd%u: int wait failed %d\n",
			cmd->cmdidx, ret);
		return ret;
	}

	/* Read response */
	if (cmd->resp_type & MMC_RSP_136) {
		cmd->response[3] = msdc_readl(h, SDC_RESP0);
		cmd->response[2] = msdc_readl(h, SDC_RESP1);
		cmd->response[1] = msdc_readl(h, SDC_RESP2);
		cmd->response[0] = msdc_readl(h, SDC_RESP3);
	} else if (cmd->resp_type & MMC_RSP_PRESENT) {
		cmd->response[0] = msdc_readl(h, SDC_RESP0);
	}

	return 0;
}

static int msdc_pio_read(struct msdc_host *h, u8 *buf, u32 bytes)
{
	u32 *p = (u32 *)buf;
	u32 left = bytes;
	u64 start = get_time_ns();

	while (left >= 4) {
		u32 cnt = FIELD_GET(MSDC_FIFOCS_RXCNT,
				    msdc_readl(h, MSDC_FIFOCS));
		if (!cnt) {
			if (is_timeout(start, MSDC_TIMEOUT_MS * MSECOND))
				return -ETIMEDOUT;
			continue;
		}
		while (cnt >= 4 && left >= 4) {
			*p++ = msdc_readl(h, MSDC_RXDATA);
			cnt -= 4;
			left -= 4;
		}
	}
	/* Byte-aligned tail */
	while (left) {
		u32 cnt = FIELD_GET(MSDC_FIFOCS_RXCNT,
				    msdc_readl(h, MSDC_FIFOCS));
		if (!cnt) {
			if (is_timeout(start, MSDC_TIMEOUT_MS * MSECOND))
				return -ETIMEDOUT;
			continue;
		}
		u8 *bp = (u8 *)p;
		u32 val = msdc_readl(h, MSDC_RXDATA);
		u32 n = min(left, 4U);
		memcpy(bp, &val, n);
		left -= n;
		p = (u32 *)(bp + n);
	}
	return 0;
}

static int msdc_pio_write(struct msdc_host *h, const u8 *buf, u32 bytes)
{
	const u32 *p = (const u32 *)buf;
	u32 left = bytes;
	u64 start = get_time_ns();

	while (left >= 4) {
		u32 cnt = FIELD_GET(MSDC_FIFOCS_TXCNT,
				    msdc_readl(h, MSDC_FIFOCS));
		/* FIFO size is 128 bytes; wait if full */
		if (cnt >= 128) {
			if (is_timeout(start, MSDC_TIMEOUT_MS * MSECOND))
				return -ETIMEDOUT;
			continue;
		}
		msdc_writel(h, MSDC_TXDATA, *p++);
		left -= 4;
	}
	if (left) {
		u32 val = 0;
		memcpy(&val, p, left);
		msdc_writel(h, MSDC_TXDATA, val);
	}
	return 0;
}

static int msdc_send_request(struct mci_host *mci, struct mci_cmd *cmd,
			     struct mci_data *data)
{
	struct msdc_host *h = to_msdc_host(mci);
	int ret;

	ret = msdc_send_cmd(h, cmd, data);
	if (ret || !data)
		return ret;

	if (data->flags & MMC_DATA_READ) {
		u32 buf_left = (u32)data->blocks * data->blocksize;
		u8 *buf = data->dest;

		while (buf_left) {
			u32 chunk = min(buf_left, (u32)data->blocksize);
			ret = msdc_pio_read(h, buf, chunk);
			if (ret)
				return ret;
			buf += chunk;
			buf_left -= chunk;
		}
	} else {
		u32 buf_left = (u32)data->blocks * data->blocksize;
		const u8 *buf = data->src;

		while (buf_left) {
			u32 chunk = min(buf_left, (u32)data->blocksize);
			ret = msdc_pio_write(h, buf, chunk);
			if (ret)
				return ret;
			buf += chunk;
			buf_left -= chunk;
		}
	}

	/* Wait for transfer complete */
	return msdc_wait_int(h,
		MSDC_INT_XFER_COMPL | MSDC_INT_DATTMO | MSDC_INT_DATCRCERR,
		MSDC_TIMEOUT_MS * MSECOND * 10);
}

static void msdc_set_ios(struct mci_host *mci, struct mci_ios *ios)
{
	struct msdc_host *h = to_msdc_host(mci);
	u32 cfg, sdc;

	/* Bus width */
	sdc = msdc_readl(h, SDC_CFG);
	sdc &= ~SDC_CFG_BUSWIDTH;
	if (ios->bus_width == MMC_BUS_WIDTH_4)
		sdc |= FIELD_PREP(SDC_CFG_BUSWIDTH, SDC_CFG_BUSWIDTH_4);
	else if (ios->bus_width == MMC_BUS_WIDTH_8)
		sdc |= FIELD_PREP(SDC_CFG_BUSWIDTH, SDC_CFG_BUSWIDTH_8);
	msdc_writel(h, SDC_CFG, sdc);

	/* Clock */
	if (!ios->clock) {
		/* Gate clock */
		cfg = msdc_readl(h, MSDC_CFG);
		cfg &= ~MSDC_CFG_CKDRVEN;
		msdc_writel(h, MSDC_CFG, cfg);
		return;
	}

	if (ios->clock != h->sclk) {
		u32 src = h->src_clk_freq;
		u32 div;

		if (ios->clock >= src) {
			div = 0; /* bypass divider */
		} else {
			div = DIV_ROUND_UP(src, 4 * ios->clock) - 1;
			if (div > 0xff)
				div = 0xff;
		}

		cfg = msdc_readl(h, MSDC_CFG);
		cfg &= ~(MSDC_CFG_CKDIV | MSDC_CFG_CKMOD);
		cfg |= FIELD_PREP(MSDC_CFG_CKDIV, div);
		msdc_writel(h, MSDC_CFG, cfg);

		/* Wait for clock stable */
		u64 start = get_time_ns();
		while (!(msdc_readl(h, MSDC_CFG) & MSDC_CFG_CKSTB)) {
			if (is_timeout(start, 50 * MSECOND)) {
				dev_err(h->dev, "clock not stable\n");
				break;
			}
		}
		h->sclk = div ? (src / (4 * (div + 1))) : src;
	}
}

static int msdc_init(struct mci_host *mci, struct device *dev)
{
	struct msdc_host *h = to_msdc_host(mci);
	u32 val;

	/* Software reset */
	val = msdc_readl(h, MSDC_CFG);
	val |= MSDC_CFG_RST;
	msdc_writel(h, MSDC_CFG, val);

	u64 start = get_time_ns();
	while (msdc_readl(h, MSDC_CFG) & MSDC_CFG_RST) {
		if (is_timeout(start, 100 * MSECOND))
			return -ETIMEDOUT;
	}

	/* Enable SD mode, PIO, clock gating disabled */
	val = MSDC_CFG_MODE | MSDC_CFG_PIO | MSDC_CFG_CKDRVEN;
	msdc_writel(h, MSDC_CFG, val);

	/* Clear and disable all interrupts */
	msdc_writel(h, MSDC_INT, 0xffffffff);
	msdc_writel(h, MSDC_INTEN, 0);

	/* Clear FIFO */
	msdc_writel(h, MSDC_FIFOCS, MSDC_FIFOCS_CLR);

	return 0;
}


/* barebox MCI API: send_cmd receives cmd with cmd->data set */
static int msdc_send_cmd_barebox(struct mci_host *mci, struct mci_cmd *cmd)
{
	return msdc_send_request(mci, cmd, cmd->data);
}

static int msdc_probe(struct device *dev)
{
	struct msdc_host *h;
	struct resource *iores;
	int ret;

	h = xzalloc(sizeof(*h));
	h->dev = dev;

	iores = dev_request_mem_resource(dev, 0);
	if (IS_ERR(iores))
		return PTR_ERR(iores);
	h->base = IOMEM(iores->start);

	h->src_clk = clk_get(dev, "source");
	if (IS_ERR(h->src_clk)) {
		dev_err(dev, "failed to get source clock\n");
		return PTR_ERR(h->src_clk);
	}

	h->h_clk = clk_get(dev, "hclk");
	if (IS_ERR(h->h_clk))
		h->h_clk = NULL; /* optional */

	if (h->h_clk)
		clk_enable(h->h_clk);
	clk_enable(h->src_clk);

	h->src_clk_freq = clk_get_rate(h->src_clk);
	if (!h->src_clk_freq)
		h->src_clk_freq = 192000000; /* default 192 MHz */

	h->mci.hw_dev = dev;
	h->mci.voltages = MMC_VDD_32_33 | MMC_VDD_33_34;
	h->mci.host_caps = MMC_CAP_4_BIT_DATA | MMC_CAP_MMC_HIGHSPEED |
			   MMC_CAP_SD_HIGHSPEED;
	h->mci.f_min = 400000;
	h->mci.f_max = min_t(u32, h->src_clk_freq, 50000000);
	h->mci.ops.send_cmd = msdc_send_cmd_barebox;
	h->mci.ops.set_ios  = msdc_set_ios;
	h->mci.ops.init     = msdc_init;

	of_property_read_u32(dev->device_node, "max-frequency",
			     &h->mci.f_max);

	dev->priv = h;

	ret = mci_register(&h->mci);
	if (ret) {
		dev_err(dev, "mci_register failed: %d\n", ret);
		return ret;
	}

	dev_dbg(dev, "MSDC probed (src_clk=%u Hz)\n", h->src_clk_freq);
	return 0;
}

static const struct of_device_id msdc_dt_ids[] = {
	{ .compatible = "mediatek,mt6589-mmc" },
	{ .compatible = "mediatek,mt8135-mmc" },
	{ .compatible = "mediatek,mt8173-mmc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, msdc_dt_ids);

static struct driver msdc_driver = {
	.name         = "mtk-msdc",
	.probe        = msdc_probe,
	.of_compatible = DRV_OF_COMPAT(msdc_dt_ids),
};
device_platform_driver(msdc_driver);
