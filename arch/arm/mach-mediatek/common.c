// SPDX-License-Identifier: GPL-2.0+
#include <init.h>
#include <envfs.h>

static int mach_init(void) {
	defaultenv_append_directory(defaultenv_mt81xx);

	return 0;
}
late_initcall(mach_init);
