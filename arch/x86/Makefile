# SPDX-License-Identifier: GPL-2.0-only

KBUILD_DEFCONFIG := efi_defconfig

KBUILD_CPPFLAGS += -D__X86__

TEXT_BASE = $(CONFIG_TEXT_BASE)

machine-y := efi
KBUILD_CFLAGS += -fpic -mno-sse -mno-mmx
# .eh_frame would end up in barebox.efi, which is a plain objcopy -O binary
KBUILD_CFLAGS += -fno-asynchronous-unwind-tables -fno-unwind-tables

ifeq ($(CONFIG_X86_32),y)
	UTS_MACHINE := i386
	biarch := $(call cc-option,-m32)
	KBUILD_AFLAGS += $(biarch)
	KBUILD_CFLAGS += $(biarch)
	KBUILD_CPPFLAGS += $(biarch)
else
	UTS_MACHINE := x86_64
	KBUILD_AFLAGS += -m64
	KBUILD_CFLAGS += -m64 -mno-red-zone
	KBUILD_CPPFLAGS += -m64
endif


ifeq ($(CONFIG_X86_EFI),y)
# Link as PIE like the other architectures, not as a shared object: the
# latter exports every global symbol, which makes each of them a root for
# --gc-sections and thereby defeats LD_DEAD_CODE_DATA_ELIMINATION.
LDFLAGS_barebox += -pie -znocombreloc
else
LDFLAGS_barebox += -static
endif

machdirs := $(patsubst %,arch/x86/mach-%/,$(machine-y))

KBUILD_CPPFLAGS += $(patsubst %,-I$(srctree)/%include,$(machdirs))

ifneq ($(machine-y),)
core-y += arch/x86/mach-$(machine-y)/
endif

lds-$(CONFIG_X86_32)   := arch/x86/mach-efi/elf_ia32_efi.lds
lds-$(CONFIG_X86_64)   := arch/x86/mach-efi/elf_x86_64_efi.lds

KBUILD_BINARY := barebox

KBUILD_LDFLAGS := --no-undefined

# The PE/COFF header is part of the image (arch/x86/mach-efi/efi-header.S)
# and the linker script takes care of the layout, so no efi-app-* objcopy
# output target, which llvm-objcopy lacks, is needed.
OBJCOPYFLAGS_barebox.efi = -O binary

barebox.efi: $(KBUILD_BINARY) FORCE
	$(call if_changed,objcopy)

KBUILD_IMAGE := barebox.efi

KBUILD_LDFLAGS += -m elf_$(UTS_MACHINE)

CLEAN_FILES += $(lds-y)
