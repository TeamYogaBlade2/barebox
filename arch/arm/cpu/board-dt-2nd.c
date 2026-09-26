// SPDX-License-Identifier: GPL-2.0

#include <common.h>
#include <linux/sizes.h>
#include <asm/barebox-arm-head.h>
#include <asm/barebox-arm.h>
#include <io.h>
#include <debug_ll.h>
#include <asm/cache.h>
#include <asm/sections.h>
#include <pbl.h>
#include <linux/libfdt.h>
#include <compressed-dtb.h>

#ifdef CONFIG_ARCH_MT6589
/*
 * Temporary MT6589 boot-flow marker.
 *
 * LK leaves the framebuffer at 0xbf600000, 1280x800 RGB565.
 * arm_cpu_lowlevel_init() has already disabled the MMU and caches when
 * these early markers are used.
 */
#define MT6589_EARLY_FB_ADDR	0xbf600000UL
#define MT6589_EARLY_FB_WIDTH	1280
#define MT6589_EARLY_FB_HEIGHT	800
#define MT6589_EARLY_FB_STRIDE	(MT6589_EARLY_FB_WIDTH * 2)

static void mt6589_early_fb_fill(u16 color)
{
	volatile u32 *fb = (volatile u32 *)MT6589_EARLY_FB_ADDR;
	u32 packed = (u32)color | ((u32)color << 16);
	unsigned int y, x;

	for (y = 0; y < MT6589_EARLY_FB_HEIGHT; y++) {
		volatile u32 *row = (volatile u32 *)(
			MT6589_EARLY_FB_ADDR + y * MT6589_EARLY_FB_STRIDE);

		for (x = 0; x < MT6589_EARLY_FB_WIDTH / 2; x++)
			row[x] = packed;
	}

	asm volatile("dsb sy" : : : "memory");
}
#else
static inline void mt6589_early_fb_fill(u16 color)
{
}
#endif

#ifdef CONFIG_CPU_V8

/* called from assembly */
void dt_2nd_aarch64(void *fdt);

void dt_2nd_aarch64(void *fdt)
{
	unsigned long membase, memsize;

	putc_ll('>');

	/* entry point already set up stack */

	arm_cpu_lowlevel_init();

	relocate_to_current_adr();
	setup_c();

	if (!fdt)
		hang();

	fdt_find_mem(fdt, &membase, &memsize);

	barebox_arm_entry(membase, memsize, fdt);
}

#else

static bool dt_2nd_valid_fdt(const void *fdt)
{
	return fdt &&
		IS_ALIGNED((unsigned long)fdt, 8) &&
		blob_is_fdt(fdt);
}

/*
 * When CONFIG_ARM_APPENDED_DTB is enabled the build appends a DTB to
 * barebox-dt-2nd.img. If the primary bootloader loaded the whole file
 * into memory, the FDT sits right after the linked image (__image_end).
 */
static void *dt_2nd_find_fdt(void *r2_fdt)
{
	void *appended;

	if (dt_2nd_valid_fdt(r2_fdt)) {
		mt6589_early_fb_fill(0x07ff); /* cyan: FDT from r2 */
		return r2_fdt;
	}

	if (!IS_ENABLED(CONFIG_ARM_APPENDED_DTB))
		return NULL;

	/*
	 * This function must run before relocate_to_current_adr().
	 *
	 * __image_end is a link-time address, so convert it to the address
	 * where the currently executing PBL image resides first. The
	 * generated appended image pads the PBL to the next 8-byte boundary.
	 */
	appended = (void *)ALIGN(
		(unsigned long)runtime_address(__image_end), 8);
	if (dt_2nd_valid_fdt(appended)) {
		mt6589_early_fb_fill(0xffe0); /* yellow: appended FDT */
		return appended;
	}

	return NULL;
}

static noinline void dt_2nd_continue(void *fdt)
{
	unsigned long membase, memsize;

	/*
	 * The FDT was resolved before relocation. In particular, don't call
	 * dt_2nd_find_fdt() here: runtime_address() must not be evaluated
	 * against the already-relocated image.
	 */
	if (!dt_2nd_valid_fdt(fdt)) {
		mt6589_early_fb_fill(0xf81f); /* magenta: FDT became invalid */
		hang();
	}

	fdt_find_mem(fdt, &membase, &memsize);
	mt6589_early_fb_fill(0xffff); /* white: fdt_find_mem() returned */

	barebox_arm_entry(membase, memsize, fdt);
}

ENTRY_FUNCTION(start_dt_2nd, r0, r1, r2)
{
	unsigned long image_start = (unsigned long)_text + global_variable_offset();
	void *fdt;

	arm_cpu_lowlevel_init();

	arm_setup_stack(image_start);

	mt6589_early_fb_fill(0xf800); /* red: entered start_dt_2nd */

	/*
	 * Locate the appended DTB while the image is still at its load
	 * address. runtime_address(__image_end) is only meaningful here.
	 */
	fdt = dt_2nd_find_fdt((void *)r2);
	if (!fdt) {
		mt6589_early_fb_fill(0xf81f); /* magenta: no valid FDT */
		hang();
	}

	mt6589_early_fb_fill(0x8410); /* gray: valid FDT selected */

	relocate_to_current_adr();
	mt6589_early_fb_fill(0x07e0); /* green: relocation returned */

	setup_c();
	mt6589_early_fb_fill(0x001f); /* blue: setup_c() returned */

	barrier();

	dt_2nd_continue(fdt);
}
#endif
