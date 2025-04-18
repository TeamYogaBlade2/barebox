/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __MACH_MEDIATEK_DEBUG_LL_H__
#define __MACH_MEDIATEK_DEBUG_LL_H__

#include <linux/types.h>
#include <io.h>

#ifdef CONFIG_DEBUG_MEDIATEK_UART

static inline uint8_t debug_ll_read_reg(void __iomem *base, int reg)
{
	return readb(base + (reg << 2));
}

static inline void debug_ll_write_reg(void __iomem *base, int reg, uint8_t val)
{
	writeb(val, base + (reg << 2));
}

#include <debug_ll/ns16550.h>

static inline void mediatek_debug_ll_init(void)
{
	void __iomem *base = IOMEM(CONFIG_DEBUG_MEDIATEK_UART_BASE);
	unsigned int divisor;

	divisor = debug_ll_ns16550_calc_divisor(26000000);
	debug_ll_ns16550_init(base, divisor);
	debug_ll_write_reg(base, NS16550_MDR, 0);
}

static inline void PUTC_LL(int c)
{
	void __iomem *base = IOMEM(CONFIG_DEBUG_MEDIATEK_UART_BASE);

	debug_ll_ns16550_putc(base, c);
}

#else
static inline void mediatek_debug_ll_init(void)
{

}
#endif

#endif /* __MACH_MEDIATEK_DEBUG_LL_H__ */
