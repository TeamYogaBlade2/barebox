/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * MediaTek MT6589 register definitions
 *
 * Copyright (c) 2026 Akari Tsuyukusa <akkun11.open@gmail.com>
 */

#ifndef __MACH_MT6589_REGS_H
#define __MACH_MT6589_REGS_H

/* Watchdog (TOPRGU) - physical 0x10000000 */
#define MT6589_WDT_BASE			IOMEM(0x10000000)
#define MT6589_WDT_MODE			0x00
#define MT6589_WDT_LENGTH		0x04
#define MT6589_WDT_RESTART		0x08
#define MT6589_WDT_STATUS		0x0c
#define MT6589_WDT_INTERVAL		0x10
#define MT6589_WDT_SWRST		0x14
#define MT6589_WDT_NONRST_REG		0x20

#define MT6589_WDT_MODE_KEY		0x22000000
#define MT6589_WDT_MODE_EN		BIT(0)
#define MT6589_WDT_MODE_EXTEN		BIT(2)
#define MT6589_WDT_MODE_IRQ		BIT(3)
#define MT6589_WDT_MODE_DUAL		BIT(4)
#define MT6589_WDT_MODE_DDR_RESERVE	BIT(7)

#define MT6589_WDT_LENGTH_KEY		0x8
#define MT6589_WDT_LENGTH_TIMEOUT(n)	((n) << 5)

#define MT6589_WDT_RESTART_KEY		0x1971
#define MT6589_WDT_SWRST_KEY		0x1209

/* infracfg */
#define MT6589_INFRACFG_BASE		IOMEM(0x10001000)

/* Topckgen */
#define MT6589_TOPCKGEN_BASE		IOMEM(0x10000100)

/* GIC */
#define MT6589_GIC_CPU_BASE		IOMEM(0x10212000)
#define MT6589_GIC_DIST_BASE		IOMEM(0x10211000)

#endif /* __MACH_MT6589_REGS_H */
