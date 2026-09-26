// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek T-PHY driver for MT6589 secondary bootloader
 * Includes the mandatory MT6589 U2 PHY recover/savecurrent workaround.
 * Without mt6589_u2_phy_recover(), USB does not function on MT6589.
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/phy/phy.h>
#include <of_device.h>
#include <of_address.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <clock.h>

/* U2 PHY registers (relative to instance / port base) */
#define U3P_USBPHYACR0		0x000
#define PA0_RG_USB20_INTR_EN	BIT(5)

#define U3P_USBPHYACR5		0x014
#define PA5_RG_U2_HSTX_SRCAL_EN	BIT(15)
#define PA5_RG_U2_HSTX_SRCTRL	GENMASK(14, 12)
#define PA5_RG_U2_HS_100U_U3_EN	BIT(11)

#define U3P_USBPHYACR3		0x01c
#define PA3_RG_USB20_PUPD_BIST_EN	BIT(12)

#define U3P_USBPHYACR6		0x018
#define PA6_RG_U2_BC11_SW_EN		BIT(23)
#define PA6_RG_U2_OTG_VBUSCMP_EN	BIT(20)

#define U3D_U2PHYDCR0		0x060
#define P2C_RG_USB20_PLL_STABLE		BIT(25)
#define P2C_RG_SIF_U2PLL_FORCE_ON	BIT(24)

#define U3P_U2PHYDTM0		0x068
#define P2C_FORCE_UART_EN		BIT(26)
#define P2C_FORCE_DATAIN		BIT(23)
#define P2C_FORCE_DM_PULLDOWN		BIT(21)
#define P2C_FORCE_DP_PULLDOWN		BIT(20)
#define P2C_FORCE_XCVRSEL		BIT(19)
#define P2C_FORCE_SUSPENDM		BIT(18)
#define P2C_FORCE_TERMSEL		BIT(17)
#define P2C_RG_DATAIN			GENMASK(13, 10)
#define P2C_RG_DMPULLDOWN		BIT(7)
#define P2C_RG_DPPULLDOWN		BIT(6)
#define P2C_RG_XCVRSEL			GENMASK(5, 4)
#define P2C_RG_TERMSEL			BIT(2)
#define P2C_RG_SUSPENDM			BIT(3)
#define P2C_DTM0_PART_MASK \
		(P2C_FORCE_DATAIN | P2C_FORCE_DM_PULLDOWN | \
		 P2C_FORCE_DP_PULLDOWN | P2C_FORCE_XCVRSEL | \
		 P2C_FORCE_TERMSEL | P2C_RG_DMPULLDOWN | \
		 P2C_RG_DPPULLDOWN | P2C_RG_TERMSEL)

#define U3P_U2PHYDTM1		0x06c
#define P2C_RG_UART_EN			BIT(16)
#define P2C_FORCE_IDDIG			BIT(9)
#define P2C_RG_VBUSVALID		BIT(5)
#define P2C_RG_SESSEND			BIT(4)
#define P2C_RG_AVALID			BIT(2)
#define P2C_RG_IDDIG			BIT(1)

#define U3P_U2PHYACR4		0x020
#define P2C_RG_USB20_GPIO_CTL	BIT(9)
#define P2C_USB20_GPIO_MODE	BIT(8)
#define P2C_U2_GPIO_CTR_MSK	(P2C_RG_USB20_GPIO_CTL | P2C_USB20_GPIO_MODE)

#define PA6_RG_U2_SQTH			GENMASK(3, 0)

#define U3P_U2FREQ_FMCR0		0x00
#define P2F_RG_MONCLK_SEL		GENMASK(27, 26)
#define P2F_RG_FREQDET_EN		BIT(24)
#define P2F_RG_CYCLECNT		GENMASK(23, 0)
#define U3P_U2FREQ_VALUE		0x0c
#define U3P_U2FREQ_FMMONR1		0x10
#define P2F_USB_FM_VALID		BIT(0)
#define P2F_RG_FRCK_EN			BIT(8)
#define U3P_FM_DET_CYCLE_CNT	1024
#define U3P_SR_COEF_DIVISOR	1000

struct mtk_tphy {
	struct device *dev;
	void __iomem *sif_base;
	void __iomem *legacy_fm_base;
	u32 src_ref_clk;
	u32 src_coef;
	bool need_mt6589_workaround;
};

struct mtk_phy_instance {
	void __iomem *port_base;
	struct phy *phy;
	struct clk *ref_clk;
};

static void mtk_phy_set_bits(void __iomem *reg, u32 bits)
{
	writel(readl(reg) | bits, reg);
}

static void mtk_phy_clear_bits(void __iomem *reg, u32 bits)
{
	writel(readl(reg) & ~bits, reg);
}

static void mtk_phy_update_field(void __iomem *reg, u32 mask, u32 val)
{
	u32 tmp = readl(reg);

	tmp &= ~mask;
	tmp |= FIELD_PREP(mask, val);
	writel(tmp, reg);
}

/*
 * MT6589 specific recover sequence.
 * Must be called on power_on; otherwise USB is completely non-functional.
 */
static void mt6589_u2_phy_recover(void __iomem *com)
{
	/* PUPD_BIST_EN clear (PMIC charger detection) */
	mtk_phy_clear_bits(com + U3P_USBPHYACR3, PA3_RG_USB20_PUPD_BIST_EN);

	/* Clear UART force and normal bits to ensure USB function */
	mtk_phy_clear_bits(com + U3P_U2PHYDTM0, P2C_FORCE_UART_EN);
	mtk_phy_clear_bits(com + U3P_U2PHYDTM1, P2C_RG_UART_EN);

	/* Release force suspendm */
	mtk_phy_clear_bits(com + U3P_U2PHYDTM0, P2C_FORCE_SUSPENDM);

	/* Clear all previously forced pull/termination/xcvr settings */
	mtk_phy_clear_bits(com + U3P_U2PHYDTM0,
			   P2C_RG_DMPULLDOWN | P2C_RG_DPPULLDOWN |
			   P2C_RG_XCVRSEL | P2C_RG_TERMSEL | P2C_RG_DATAIN |
			   P2C_FORCE_DATAIN | P2C_FORCE_DM_PULLDOWN |
			   P2C_FORCE_DP_PULLDOWN | P2C_FORCE_XCVRSEL |
			   P2C_FORCE_TERMSEL | P2C_FORCE_SUSPENDM);

	/* BC1.2 disable, OTG VBUS comparator enable */
	mtk_phy_clear_bits(com + U3P_USBPHYACR6, PA6_RG_U2_BC11_SW_EN);
	mtk_phy_set_bits(com + U3P_USBPHYACR6, PA6_RG_U2_OTG_VBUSCMP_EN);

	udelay(800);
}

static void mt6589_u2_phy_savecurrent(void __iomem *com)
{
	/* Ensure UART off */
	mtk_phy_clear_bits(com + U3P_U2PHYDTM0, P2C_FORCE_UART_EN);
	mtk_phy_clear_bits(com + U3P_U2PHYDTM1, P2C_RG_UART_EN);

	/* Release force suspendm briefly to configure pulldowns */
	mtk_phy_clear_bits(com + U3P_U2PHYDTM0, P2C_FORCE_SUSPENDM);

	/* Set DP/DM pulldowns, XCVRSEL=01, TERMSEL=1, clear DATAIN */
	u32 tm0 = readl(com + U3P_U2PHYDTM0);
	tm0 |= P2C_RG_DMPULLDOWN | P2C_RG_DPPULLDOWN;
	tm0 &= ~P2C_RG_XCVRSEL;
	tm0 |= FIELD_PREP(P2C_RG_XCVRSEL, 1); /* 01 */
	tm0 |= P2C_RG_TERMSEL;
	tm0 &= ~P2C_RG_DATAIN;
	writel(tm0, com + U3P_U2PHYDTM0);

	/* Force DP/DM pulldown, XCVRSEL, TERMSEL, DATAIN */
	mtk_phy_set_bits(com + U3P_U2PHYDTM0,
			 P2C_FORCE_DATAIN | P2C_FORCE_DM_PULLDOWN |
			 P2C_FORCE_DP_PULLDOWN | P2C_FORCE_XCVRSEL |
			 P2C_FORCE_TERMSEL);

	/* Disable BC1.2 and OTG VBUS comparator */
	mtk_phy_clear_bits(com + U3P_USBPHYACR6,
			   PA6_RG_U2_BC11_SW_EN | PA6_RG_U2_OTG_VBUSCMP_EN);

	udelay(800);

	/* Set PLL stable bit */
	mtk_phy_set_bits(com + U3D_U2PHYDCR0, P2C_RG_USB20_PLL_STABLE);
	udelay(1);

	/* Assert force suspendm to enter low power */
	mtk_phy_set_bits(com + U3P_U2PHYDTM0, P2C_FORCE_SUSPENDM);
	udelay(1);
}

static int mtk_phy_init(struct phy *phy)
{
	struct mtk_phy_instance *inst = phy_get_drvdata(phy);
	int ret;

	if (!inst || !inst->port_base)
		return -EINVAL;

	if (inst->ref_clk) {
		ret = clk_enable(inst->ref_clk);
		if (ret)
			return ret;
	}

	/* Linux u2_phy_instance_init(), MTK T-PHY V1 subset. */
	mtk_phy_clear_bits(inst->port_base + U3P_U2PHYDTM0,
			   P2C_FORCE_UART_EN | P2C_FORCE_SUSPENDM);
	mtk_phy_clear_bits(inst->port_base + U3P_U2PHYDTM0,
			   P2C_RG_XCVRSEL | P2C_RG_DATAIN | P2C_DTM0_PART_MASK);
	mtk_phy_clear_bits(inst->port_base + U3P_U2PHYDTM1,
			   P2C_RG_UART_EN);
	mtk_phy_set_bits(inst->port_base + U3P_USBPHYACR0,
			       PA0_RG_USB20_INTR_EN);
	mtk_phy_clear_bits(inst->port_base + U3P_USBPHYACR5,
			   PA5_RG_U2_HS_100U_U3_EN);
	mtk_phy_clear_bits(inst->port_base + U3P_U2PHYACR4,
			   P2C_U2_GPIO_CTR_MSK);
	mtk_phy_clear_bits(inst->port_base + U3P_USBPHYACR6,
			   PA6_RG_U2_BC11_SW_EN);
	mtk_phy_update_field(inst->port_base + U3P_USBPHYACR6,
			     PA6_RG_U2_SQTH, 2);

	return 0;
}

static int mtk_phy_exit(struct phy *phy)
{
	struct mtk_phy_instance *inst = phy_get_drvdata(phy);

	if (inst && inst->ref_clk)
		clk_disable(inst->ref_clk);

	return 0;
}

static int mtk_phy_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	struct mtk_phy_instance *inst = phy_get_drvdata(phy);
	u32 tmp;

	if (!inst || !inst->port_base)
		return -EINVAL;

	tmp = readl(inst->port_base + U3P_U2PHYDTM1);

	switch (mode) {
	case PHY_MODE_USB_DEVICE:
		tmp |= P2C_FORCE_IDDIG | P2C_RG_IDDIG;
		break;
	case PHY_MODE_USB_HOST:
		tmp |= P2C_FORCE_IDDIG;
		tmp &= ~P2C_RG_IDDIG;
		break;
	case PHY_MODE_USB_OTG:
		tmp &= ~(P2C_FORCE_IDDIG | P2C_RG_IDDIG);
		break;
	default:
		return -EINVAL;
	}

	writel(tmp, inst->port_base + U3P_U2PHYDTM1);
	return 0;
}

static void hs_slew_rate_calibrate(struct mtk_tphy *tphy,
				   struct mtk_phy_instance *inst)
{
	void __iomem *com = inst->port_base;
	void __iomem *fmreg = tphy->legacy_fm_base;
	u32 tmp;
	u32 fm_out;
	u32 calibration_val;
	unsigned int i;

	if (!fmreg || !tphy->src_ref_clk || !tphy->src_coef)
		return;

	/* Linux: enable USB ring oscillator. */
	mtk_phy_set_bits(com + U3P_USBPHYACR5, PA5_RG_U2_HSTX_SRCAL_EN);
	udelay(1);
	mtk_phy_set_bits(fmreg + U3P_U2FREQ_FMMONR1, P2F_RG_FRCK_EN);

	tmp = readl(fmreg + U3P_U2FREQ_FMCR0);
	tmp &= ~(P2F_RG_CYCLECNT | P2F_RG_MONCLK_SEL);
	tmp |= FIELD_PREP(P2F_RG_CYCLECNT, U3P_FM_DET_CYCLE_CNT);
	writel(tmp, fmreg + U3P_U2FREQ_FMCR0);

	mtk_phy_set_bits(fmreg + U3P_U2FREQ_FMCR0, P2F_RG_FREQDET_EN);

	/* Equivalent upper bound to Linux readl_poll_timeout(..., 10, 200). */
	for (i = 0; i < 20; i++) {
		if (readl(fmreg + U3P_U2FREQ_FMMONR1) & P2F_USB_FM_VALID)
			break;
		udelay(10);
	}

	fm_out = readl(fmreg + U3P_U2FREQ_VALUE);
	mtk_phy_clear_bits(fmreg + U3P_U2FREQ_FMCR0, P2F_RG_FREQDET_EN);
	mtk_phy_clear_bits(fmreg + U3P_U2FREQ_FMMONR1, P2F_RG_FRCK_EN);

	if (fm_out) {
		tmp = tphy->src_ref_clk * tphy->src_coef;
		tmp = (tmp * U3P_FM_DET_CYCLE_CNT) / fm_out;
		calibration_val = (tmp + U3P_SR_COEF_DIVISOR / 2) /
			U3P_SR_COEF_DIVISOR;
	} else {
		calibration_val = 4;
	}

	mtk_phy_update_field(com + U3P_USBPHYACR5,
			     PA5_RG_U2_HSTX_SRCTRL, calibration_val);
	mtk_phy_clear_bits(com + U3P_USBPHYACR5,
			   PA5_RG_U2_HSTX_SRCAL_EN);
}

static int mtk_phy_power_on(struct phy *phy)
{
	struct mtk_phy_instance *inst = phy_get_drvdata(phy);
	struct mtk_tphy *tphy = phy->dev.parent ? phy->dev.parent->priv : NULL;
	void __iomem *com;

	if (!inst || !inst->port_base)
		return 0;

	com = inst->port_base;

	/* Common U2 power-on */
	mtk_phy_set_bits(com + U3P_USBPHYACR6, PA6_RG_U2_OTG_VBUSCMP_EN);
	mtk_phy_set_bits(com + U3P_U2PHYDTM1, P2C_RG_VBUSVALID | P2C_RG_AVALID);
	mtk_phy_clear_bits(com + U3P_U2PHYDTM1, P2C_RG_SESSEND);

	/* Mandatory for MT6589 */
	if (tphy && tphy->need_mt6589_workaround)
		mt6589_u2_phy_recover(com);

	if (tphy)
		hs_slew_rate_calibrate(tphy, inst);

	return 0;
}

static int mtk_phy_power_off(struct phy *phy)
{
	struct mtk_phy_instance *inst = phy_get_drvdata(phy);
	struct mtk_tphy *tphy = phy->dev.parent ? phy->dev.parent->priv : NULL;
	void __iomem *com;

	if (!inst || !inst->port_base)
		return 0;

	com = inst->port_base;

	mtk_phy_clear_bits(com + U3P_USBPHYACR6, PA6_RG_U2_OTG_VBUSCMP_EN);
	mtk_phy_clear_bits(com + U3P_U2PHYDTM1, P2C_RG_VBUSVALID | P2C_RG_AVALID);
	mtk_phy_set_bits(com + U3P_U2PHYDTM1, P2C_RG_SESSEND);

	if (tphy && tphy->need_mt6589_workaround)
		mt6589_u2_phy_savecurrent(com);

	return 0;
}

static const struct phy_ops mtk_tphy_ops = {
	.init		= mtk_phy_init,
	.exit		= mtk_phy_exit,
	.power_on	= mtk_phy_power_on,
	.power_off	= mtk_phy_power_off,
	.set_mode	= mtk_phy_set_mode,
};

static struct phy *mtk_phy_xlate(struct device *dev,
				 const struct of_phandle_args *args)
{
	struct mtk_tphy *tphy = dev->priv;
	struct mtk_phy_instance *inst;
	struct phy *phy;
	struct resource res;
	int ret;

	if (!args || args->args_count != 1)
		return ERR_PTR(-EINVAL);

	inst = xzalloc(sizeof(*inst));

	ret = of_address_to_resource(args->np, 0, &res);
	if (!ret)
		inst->port_base = IOMEM(res.start);
	else if (tphy->sif_base)
		inst->port_base = tphy->sif_base + 0x800; /* first U2 port */

	phy = phy_create(dev, args->np, &mtk_tphy_ops);
	if (IS_ERR(phy)) {
		free(inst);
		return phy;
	}

	phy_set_drvdata(phy, inst);
	inst->phy = phy;
	inst->ref_clk = clk_get_optional(&phy->dev, "ref");
	if (IS_ERR(inst->ref_clk)) {
		ret = PTR_ERR(inst->ref_clk);
		phy_destroy(phy);
		free(inst);
		return ERR_PTR(ret);
	}

	return phy;
}

static int mtk_tphy_probe(struct device *dev)
{
	struct mtk_tphy *tphy;
	struct resource *res;
	struct phy_provider *provider;

	tphy = xzalloc(sizeof(*tphy));
	tphy->dev = dev;
	dev->priv = tphy;

	/* .data is non-NULL for MT6589 which needs the USB recover sequence */
	tphy->need_mt6589_workaround = !!device_get_match_data(dev);

	res = dev_get_resource(dev, IORESOURCE_MEM, 0);
	if (!IS_ERR(res))
		tphy->sif_base = IOMEM(res->start);

	res = dev_get_resource(dev, IORESOURCE_MEM, 1);
	if (!IS_ERR(res))
		tphy->legacy_fm_base = IOMEM(res->start);

	tphy->src_ref_clk = tphy->need_mt6589_workaround ? 48 : 26;
	tphy->src_coef = tphy->need_mt6589_workaround ? 22 : 28;
	if (dev->of_node) {
		of_property_read_u32(dev->of_node, "mediatek,src-ref-clk-mhz",
				     &tphy->src_ref_clk);
		of_property_read_u32(dev->of_node, "mediatek,src-coef",
				     &tphy->src_coef);
	}

	provider = of_phy_provider_register(dev, mtk_phy_xlate);
	if (IS_ERR(provider))
		return PTR_ERR(provider);

	dev_info(dev, "MediaTek T-PHY registered (MT6589 workaround %s)\n",
		 tphy->need_mt6589_workaround ? "enabled" : "disabled");
	return 0;
}

static const struct of_device_id mtk_tphy_ids[] = {
	{ .compatible = "mediatek,mt6589-tphy", .data = (void *)1 },
	{ .compatible = "mediatek,generic-tphy-v1" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_tphy_ids);

static struct driver mtk_tphy_driver = {
	.name = "mtk-tphy",
	.probe = mtk_tphy_probe,
	.of_compatible = DRV_OF_COMPAT(mtk_tphy_ids),
};
device_platform_driver(mtk_tphy_driver);
