// SPDX-License-Identifier: GPL-2.0-only

#include <types.h>

#include <asm/cache.h>
#include <asm/barebox-arm.h>
#include <asm-generic/memory_layout.h>

#include "entry.h"

/*
 * Main ARM entry point. Call this with the memory region you can
 * spare for barebox. This doesn't necessarily have to be the full
 * SDRAM. The currently running binary can be inside or outside of
 * this region. TEXT_BASE can be inside or outside of this
 * region. boarddata will be preserved and can be accessed later with
 * barebox_arm_boarddata().
 *
 * -> membase + memsize
 *   STACK_SIZE              - stack
 *   16KiB, aligned to 16KiB - First level page table if early MMU support
 *                             is enabled
 *   128KiB                  - early memory space
 * -> maximum end of barebox binary
 *
 * Usually a TEXT_BASE of 1MiB below your lowest possible end of memory should
 * be fine.
 */

/*
 * It can be hard to convince GCC to not use old stack pointer after
 * we modify it with arm_setup_stack() on ARM64, so we implement the
 * low level details in assembly
 */
void __noreturn __barebox_arm_entry(unsigned long membase,
				    unsigned long memsize,
				    void *boarddata,
				    unsigned long sp);

/*
 * arm_mem_stack_top(membase + memsize) == endmem - OPTEE_SIZE - SCRATCH_SIZE
 * (see arm_mem_stack / arm_mem_scratch helpers).
 *
 * Clang forbids non-ASM statements in __naked functions, so under Clang we
 * keep __naked (required when SP is invalid at entry — BootROM paths) and
 * compute the stack pointer in pure assembly before branching.
 *
 * Simply dropping __naked is incorrect; see:
 * https://github.com/barebox/barebox/issues/45#issuecomment-5469055707
 */
void NAKED __noreturn barebox_arm_entry(unsigned long membase,
					unsigned long memsize, void *boarddata)
{
#if defined(__clang__)
	/*
	 * Incoming: r0=membase, r1=memsize, r2=boarddata (AAPCS).
	 * Outgoing to __barebox_arm_entry: r0,r1,r2 unchanged, r3=sp.
	 * Large immediates are materialised via literal pool (not sub #imm).
	 */
	asm volatile(
		"add	r3, r0, r1\n\t"
		"ldr	r12, 2f\n\t"
		"sub	r3, r3, r12\n\t"
		"b	__barebox_arm_entry\n\t"
		"2:	.word	%c0\n\t"
		:
		: "i"(OPTEE_SIZE + SCRATCH_SIZE)
		: "r3", "r12", "memory"
	);
#else
	__barebox_arm_entry(membase, memsize, boarddata,
			    arm_mem_stack_top(membase + memsize));
#endif
}

void __noreturn barebox_pbl_entry(ulong, ulong, void *)
	__alias(barebox_arm_entry);
