// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MT6589 SoC support
 *
 * Copyright (c) 2026 Akari Tsuyukusa <akkun11.open@gmail.com>
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <restart.h>
#include <mach/mediatek/mt6589-regs.h>

static void __noreturn mt6589_restart_soc(struct restart_handler *rst,
					   unsigned long flags)
{
	/* Trigger WDT-based system reset via TOPRGU */
	writel(MT6589_WDT_MODE_KEY | MT6589_WDT_MODE_EN,
	       MT6589_WDT_BASE + MT6589_WDT_MODE);
	writel(MT6589_WDT_RESTART_KEY,
	       MT6589_WDT_BASE + MT6589_WDT_RESTART);
	/* Trigger immediate reset by setting timeout to 1 */
	writel(MT6589_WDT_LENGTH_KEY | MT6589_WDT_LENGTH_TIMEOUT(1),
	       MT6589_WDT_BASE + MT6589_WDT_LENGTH);
	writel(MT6589_WDT_SWRST_KEY,
	       MT6589_WDT_BASE + MT6589_WDT_SWRST);

	hang();
}

static int mt6589_init(void)
{
	restart_handler_register_fn("soc", mt6589_restart_soc);
	return 0;
}
postcore_initcall(mt6589_init);
