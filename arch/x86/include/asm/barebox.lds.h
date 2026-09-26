/* SPDX-License-Identifier: GPL-2.0-only */

#ifdef CONFIG_X86_32
#define BAREBOX_OUTPUT_FORMAT	"elf32-i386", "elf32-i386", "elf32-i386"
#define BAREBOX_OUTPUT_ARCH	"i386"
#else
#define BAREBOX_OUTPUT_FORMAT	"elf64-x86-64", "elf64-x86-64", "elf64-x86-64"
#define BAREBOX_OUTPUT_ARCH	"i386:x86-64"
#endif

/*
 * Alignment of the PE/COFF sections of barebox.efi, both in memory
 * (SectionAlignment) and in the file (FileAlignment). Shared between the
 * linker scripts, which lay out the image accordingly, and efi-header.S,
 * which describes that layout to the EFI firmware.
 */
#define PECOFF_SECTION_ALIGNMENT	0x1000
#define PECOFF_FILE_ALIGNMENT		0x200

#include <asm-generic/barebox.lds.h>
