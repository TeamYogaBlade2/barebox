/* SPDX-License-Identifier: GPL-2.0-only */

#include <asm/barebox.lds.h>

/*
 * barebox.efi is objcopy -O binary of this image, so file offsets equal
 * virtual addresses and the layout below is the PE/COFF layout described
 * by efi-header.S: the header, a PE .text section covering the ELF .text
 * and a PE .data section covering everything else. Both PE sections
 * start PECOFF_SECTION_ALIGNMENT aligned and their file sizes are
 * multiples of PECOFF_FILE_ALIGNMENT.
 *
 * The __pecoff_*_size symbols below are not absolute, but addresses
 * relative to image_base, which is 0: efi-header.S can then reference
 * them with PC-relative relocations, the only kind of relocation that
 * both ld.bfd and ld.lld resolve to a link-time constant in a
 * position-independent link.
 */

OUTPUT_FORMAT(BAREBOX_OUTPUT_FORMAT)
OUTPUT_ARCH(BAREBOX_OUTPUT_ARCH)
ENTRY(_start)
SECTIONS
{
	. = 0;

	.efi.header : {
		image_base = .;
		KEEP(*(.efi.header))
	}

	. = ALIGN(PECOFF_SECTION_ALIGNMENT);

	.text : {
		_stext = .;
		_text = .;
		*(.text)
		*(.text.*)
		*(.gnu.linkonce.t.*)
	}

	. = ALIGN(PECOFF_SECTION_ALIGNMENT);
	_etext = .;
	__pecoff_text_size = image_base + (_etext - _stext);

	_sdata = .;

	.data : {
		__start_rodata = .;
		*(.rodata*)
		*(.bbenv.rodata.*)
		*(.bblogo.rodata.*)
		RO_DATA_SECTION
		__end_rodata = .;
		*(.got.plt)
		*(.got)
		*(.data*)
		__bss_start = .;
		*(.sdata)
		/* the EFI loader doesn't seem to like a .bss section, so we stick
		 * it all into .data: */
		*(.sbss)
		*(.scommon)
		*(.dynbss)
		*(.bss .bss.*)
		*(COMMON)
		*(.rel.local)
		__bss_stop = .;
	}
	_edata = .;

	.barebox_imd : { BAREBOX_IMD }

	. = ALIGN(8);
	.dynamic : { *(.dynamic) }
	. = ALIGN(8);
	.rela : {
		*(.rela*)
	}
	. = ALIGN(8);
	.dynsym : { *(.dynsym) }
	.dynstr : { *(.dynstr) }
	. = ALIGN(8);
	.hash : { *(.hash) }
	. = ALIGN(8);
	.gnu.hash : { *(.gnu.hash) }

	/*
	 * objcopy -O binary stops at the last byte of the last section, so
	 * padding out the PE .data section to the file alignment has to be
	 * actual section contents.
	 */
	.pecoff_edata_padding : {
		BYTE(0);
		. = ALIGN(PECOFF_FILE_ALIGNMENT);
	}
	__pecoff_data_rawsize = image_base + (. - _sdata);

	. = ALIGN(PECOFF_SECTION_ALIGNMENT);
	__pecoff_data_size = image_base + (. - _sdata);
	__image_end = .;

	/DISCARD/ : {
		*(.eh_frame)
		*(.note.GNU-stack)
	}

	.comment 0 : { *(.comment) }
}
