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
 * When CONFIG_ARM_APPENDED_DTB is enabled, look for an appended DTB if
 * the primary bootloader did not pass one in r2. If a DTB was appended
 * to the image at build time, it sits right after the linked image
 * (__image_end).
 */
static void *dt_2nd_find_fdt(void *r2_fdt)
{
	void *appended;

	if (dt_2nd_valid_fdt(r2_fdt))
		return r2_fdt;

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
	if (dt_2nd_valid_fdt(appended))
		return appended;

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
	if (!dt_2nd_valid_fdt(fdt))
		hang();

	fdt_find_mem(fdt, &membase, &memsize);

	barebox_arm_entry(membase, memsize, fdt);
}

ENTRY_FUNCTION(start_dt_2nd, r0, r1, r2)
{
	unsigned long image_start = (unsigned long)_text + global_variable_offset();
	void *fdt;

	arm_cpu_lowlevel_init();

	arm_setup_stack(image_start);

	/*
	 * Locate the appended DTB while the image is still at its load
	 * address. runtime_address(__image_end) is only meaningful here.
	 */
	fdt = dt_2nd_find_fdt((void *)r2);
	if (!fdt)
		hang();

	relocate_to_current_adr();
	setup_c();
	barrier();

	dt_2nd_continue(fdt);
}
#endif
