// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MT6589 pinctrl for barebox
 *
 * MT6589 has two GPIO register blocks:
 *   gpio  @ 0x10005000  (base index 0) – mode/dir/di/do and most pins
 *   gpio1 @ 0x1020c000  (base index 1) – some drive/slew/pull extras
 * plus eint @ 0x1000b000 (not handled here).
 *
 * Mode/dir register layout taken from Linux pinctrl-mt6589.c.
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <of_device.h>
#include <pinctrl.h>
#include <malloc.h>
#include <linux/err.h>
#include <linux/bitops.h>

#define MT6589_NPINS		232

struct mtk_pinctrl {
	void __iomem *base[2];	/* gpio, gpio1 */
	struct pinctrl_device pctl;
};

/* ---- mode register lookup (from Linux mt6589_pin_mode_range) ---- */
struct mtk_mode_range {
	u16 s_pin, e_pin;
	u8  base_idx;
	u16 offset;
	u8  bits;	/* bits per pin (3 or 4) */
	u8  pins_per_reg; /* how many pins packed in lower half of reg */
};

static const struct mtk_mode_range mode_ranges[] = {
	/* pin 0-43:  0x0c00, 3-bit, 5 pins / 16-bit half */
	{  0,  43, 0, 0x0c00, 3, 5 },
	/* pin 44-46: 0x0980, 4-bit */
	{ 44,  46, 0, 0x0980, 4, 4 },
	/* pin 47-49: 0x09a0, 4-bit */
	{ 47,  49, 0, 0x09a0, 4, 4 },
	/* pin 50-231: 0x0ca0, 3-bit */
	{ 50, 231, 0, 0x0ca0, 3, 5 },
};

static int mtk_pin_mode_addr(u32 pin, u32 *reg_off, u32 *shift, u32 *mask,
			     u8 *base_idx)
{
	const struct mtk_mode_range *r;
	u32 idx, pin_in_range;
	int i;

	for (i = 0; i < ARRAY_SIZE(mode_ranges); i++) {
		r = &mode_ranges[i];
		if (pin >= r->s_pin && pin <= r->e_pin) {
			pin_in_range = pin - r->s_pin;
			idx = pin_in_range / r->pins_per_reg;
			*reg_off = r->offset + idx * 0x10;
			*shift = (pin_in_range % r->pins_per_reg) * r->bits;
			*mask = (1u << r->bits) - 1;
			*base_idx = r->base_idx;
			return 0;
		}
	}
	return -EINVAL;
}

/* dir / di / do: all on base 0, 1 bit per pin, 16 pins per 0x10 stride */
static void __iomem *mtk_bit_reg(struct mtk_pinctrl *mtk, u32 pin,
				 u32 start_off, u32 *bit)
{
	u32 idx = pin / 16;
	*bit = pin % 16;
	return mtk->base[0] + start_off + idx * 0x10;
}

static int mtk_set_mode(struct mtk_pinctrl *mtk, u32 pin, u32 mode)
{
	u32 reg_off, shift, mask, tmp;
	u8 base_idx;
	void __iomem *addr;

	if (mtk_pin_mode_addr(pin, &reg_off, &shift, &mask, &base_idx))
		return -EINVAL;
	if (!mtk->base[base_idx])
		return -ENODEV;

	addr = mtk->base[base_idx] + reg_off;
	tmp = readl(addr);
	tmp = (tmp & ~(mask << shift)) | ((mode & mask) << shift);
	writel(tmp, addr);
	return 0;
}

static int mtk_pinctrl_set_state(struct pinctrl_device *pdev,
				 struct device_node *np)
{
	struct mtk_pinctrl *mtk = container_of(pdev, struct mtk_pinctrl, pctl);
	struct property *prop;
	const __be32 *list;
	int size, i;

	prop = of_find_property(np, "pinmux", &size);
	if (!prop)
		prop = of_find_property(np, "pins", &size);
	if (!prop)
		return 0;

	list = prop->value;
	size /= sizeof(*list);

	for (i = 0; i < size; i++) {
		u32 val = be32_to_cpu(list[i]);
		/* Linux MTK pinmux binding: (pin << 8) | function */
		u32 pin = (val >> 8) & 0xff;
		u32 mode = val & 0xf; /* up to 4-bit mode on some pins */

		if (pin >= MT6589_NPINS)
			continue;
		mtk_set_mode(mtk, pin, mode);
	}
	return 0;
}

static int mtk_pinctrl_set_direction(struct pinctrl_device *pdev,
				     unsigned int pin, bool input)
{
	struct mtk_pinctrl *mtk = container_of(pdev, struct mtk_pinctrl, pctl);
	void __iomem *addr;
	u32 bit, tmp;

	if (pin >= MT6589_NPINS || !mtk->base[0])
		return -EINVAL;

	/* DIR at 0x0000: 1 = output, 0 = input */
	addr = mtk_bit_reg(mtk, pin, 0x0000, &bit);
	tmp = readl(addr);
	if (input)
		tmp &= ~BIT(bit);
	else
		tmp |= BIT(bit);
	writel(tmp, addr);
	return 0;
}

static int mtk_pinctrl_get_direction(struct pinctrl_device *pdev,
				     unsigned int pin)
{
	struct mtk_pinctrl *mtk = container_of(pdev, struct mtk_pinctrl, pctl);
	void __iomem *addr;
	u32 bit, tmp;

	if (pin >= MT6589_NPINS || !mtk->base[0])
		return -EINVAL;

	addr = mtk_bit_reg(mtk, pin, 0x0000, &bit);
	tmp = readl(addr);
	return (tmp & BIT(bit)) ? 0 : 1; /* 0=out, 1=in like gpiolib */
}

static struct pinctrl_ops mtk_pinctrl_ops = {
	.set_state = mtk_pinctrl_set_state,
	.set_direction = mtk_pinctrl_set_direction,
	.get_direction = mtk_pinctrl_get_direction,
};

static int mtk_pinctrl_probe(struct device *dev)
{
	struct mtk_pinctrl *mtk;
	struct resource *res;
	int i;

	mtk = xzalloc(sizeof(*mtk));

	/*
	 * DT reg-names = "gpio", "gpio1", "eint"
	 * index 0 = gpio (primary), index 1 = gpio1 (secondary block)
	 */
	for (i = 0; i < 2; i++) {
		res = dev_get_resource(dev, IORESOURCE_MEM, i);
		if (!IS_ERR(res))
			mtk->base[i] = IOMEM(res->start);
		else
			mtk->base[i] = NULL;
	}

	if (!mtk->base[0]) {
		dev_err(dev, "missing primary gpio register range\n");
		return -EINVAL;
	}

	mtk->pctl.dev = dev;
	mtk->pctl.ops = &mtk_pinctrl_ops;
	mtk->pctl.base = 0;
	mtk->pctl.npins = MT6589_NPINS;
	pinctrl_register(&mtk->pctl);

	dev_info(dev, "MT6589 pinctrl: gpio=%p gpio1=%p (%u pins)\n",
		 mtk->base[0], mtk->base[1], MT6589_NPINS);
	return 0;
}

static const struct of_device_id mtk_pinctrl_ids[] = {
	{ .compatible = "mediatek,mt6589-pinctrl" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_pinctrl_ids);

static struct driver mtk_pinctrl_driver = {
	.name = "pinctrl-mt6589",
	.probe = mtk_pinctrl_probe,
	.of_compatible = DRV_OF_COMPAT(mtk_pinctrl_ids),
};
core_platform_driver(mtk_pinctrl_driver);
