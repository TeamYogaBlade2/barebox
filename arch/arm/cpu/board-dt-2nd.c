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

/*
 * When CONFIG_ARM_APPENDED_DTB is enabled the build appends a DTB to
 * barebox-dt-2nd.img. If the primary bootloader loaded the whole file
 * into memory, the FDT sits right after the linked image (__image_end).
 */
static void *dt_2nd_find_fdt(void *r2_fdt)
{
	void *appended;

	if (r2_fdt && blob_is_fdt(r2_fdt))
		return r2_fdt;

	if (!IS_ENABLED(CONFIG_ARM_APPENDED_DTB))
		return r2_fdt;

	/*
	 * After relocate_to_current_adr(), linker symbols are valid at the
	 * runtime address. The appended DTB is concatenated after the
	 * binary that the linker produced.
	 */
	appended = (void *)__image_end;
	if (blob_is_fdt(appended))
		return appended;

	/* Also accept a few bytes of padding (alignment) */
	appended = (void *)ALIGN((unsigned long)__image_end, 4);
	if (blob_is_fdt(appended))
		return appended;

	return r2_fdt;
}

static noinline void dt_2nd_continue(void *fdt)
{
	unsigned long membase, memsize;

	fdt = dt_2nd_find_fdt(fdt);
	if (!fdt || !blob_is_fdt(fdt))
		hang();

	fdt_find_mem(fdt, &membase, &memsize);

	barebox_arm_entry(membase, memsize, fdt);
}

ENTRY_FUNCTION(start_dt_2nd, r0, r1, r2)
{
	unsigned long image_start = (unsigned long)_text + global_variable_offset();

	arm_cpu_lowlevel_init();

	arm_setup_stack(image_start);

	relocate_to_current_adr();
	setup_c();
	barrier();

	dt_2nd_continue((void *)r2);
}
#endif
