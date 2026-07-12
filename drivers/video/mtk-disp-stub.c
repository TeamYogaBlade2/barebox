// SPDX-License-Identifier: GPL-2.0-only
#include <of.h>
#include <linux/coreboot.h>
#include <linux/device.h>
#include <video/backlight.h>

enum {
	DISP_REG_OVL_STA = 0x0000,
	DISP_REG_OVL_EN = 0x000C,
	DISP_REG_OVL_L0_ADDR = 0x0f40,
	DISP_REG_OVL0_2L_EN = 0x100C,
	DISP_REG_EXDMA_EN = 0x0020,
	DISP_REG_EXDMA_L_EN = 0x0040,
};

struct mtk_disp_ovl {
	void __iomem *regs;
};

static struct coreboot_sysinfo *mtk_disp_ovl_get_corebootfb(struct device *dev)
{
	struct device *coreboot_fb;
	struct device *coreboot_table;

	/* Don't attempt to program the fb addr unless the fb driver has probed */
	coreboot_fb = of_find_device_by_node_path("/coreboot-fb");
	if (!coreboot_fb) {
		dev_err(dev, "Failed to get coreboot framebuffer\n");
		return NULL;
	};

	coreboot_table = of_find_device_by_node_path("/firmware/coreboot");
	if (!coreboot_table) {
		dev_err(dev, "Failed to get coreboot table dev\n");
		return NULL;
	};

	if (!dev_is_probed(coreboot_fb))
		return NULL;

	return dev_get_drvdata(coreboot_table);
}

static int mtk_disp_ovl_probe(struct device *dev)
{
	struct mtk_disp_ovl *priv;
	struct coreboot_sysinfo *sysinfo;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regs = dev_platform_ioremap_resource(dev, 0);
	if (IS_ERR(priv->regs))
		return dev_err_probe(dev, PTR_ERR(priv->regs),
				     "failed to ioremap ovl\n");

	dev_set_drvdata(dev, priv);

	sysinfo = mtk_disp_ovl_get_corebootfb(dev);
	if (!sysinfo)
		return dev_err_probe(dev, PTR_ERR(priv->regs),
				     "failed to get coreboot_sysinfo\n");

	/* framebuffer address */
	writel((u32)sysinfo->fb->physical_address,
	       priv->regs + DISP_REG_OVL_L0_ADDR);
	/* Enable output */
	writel(1, priv->regs + DISP_REG_OVL_EN);

	return 0;
}

static const struct of_device_id mtk_disp_ovl_driver_dt_match[] = {
	{ .compatible = "mediatek,mt2701-disp-ovl" },
	{ .compatible = "mediatek,mt8167-disp-ovl" },
	{ .compatible = "mediatek,mt8173-disp-ovl" },
	{ .compatible = "mediatek,mt8183-disp-ovl" },
	{ .compatible = "mediatek,mt8183-disp-ovl-2l" },
	{ .compatible = "mediatek,mt8192-disp-ovl" },
	{ .compatible = "mediatek,mt8192-disp-ovl-2l" },
	{ .compatible = "mediatek,mt8195-disp-ovl" },
	{},
};

struct driver mtk_disp_ovl_driver = {
	.probe = mtk_disp_ovl_probe,
	.name = "mediatek-disp-ovl",
	.of_match_table = mtk_disp_ovl_driver_dt_match,
};
late_platform_driver(mtk_disp_ovl_driver);
