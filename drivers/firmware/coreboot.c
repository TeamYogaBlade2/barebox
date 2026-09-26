// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026 Brady Norander <brady.norander@mainlining.org>
 */

#include "linux/device.h"
#include "xfuncs.h"
#include <device.h>
#include <of_device.h>
#include <linux/coreboot.h>

/* Coreboot table header structure */
struct coreboot_table_header {
	char signature[4];
	u32 header_bytes;
	u32 header_checksum;
	u32 table_bytes;
	u32 table_checksum;
	u32 table_entries;
};

static void coreboot_table_populate_sysinfo(struct device *dev, void *ptr)
{
 int i;
 void *ptr_entry;
 struct coreboot_table_entry *entry;
 struct coreboot_table_header *header = ptr;
 struct cb_mainboard *mb;
 struct cb_framebuffer *fb;
 char *string_ptr;

 struct coreboot_sysinfo *sysinfo = dev_get_drvdata(dev);

 ptr_entry = ptr + header->header_bytes;
 for (i = 0; i < header->table_entries; i++) {
  entry = ptr_entry;

  if (entry->size < sizeof(*entry)) {
   dev_err(dev, "Coreboot table entry too small, size: %d\n", entry->size);
   continue;
  }

  dev_dbg(dev, "Coreboot table entry tag: %d, size: %d\n", entry->tag, entry->size);

  switch (entry->tag) {
   case CB_TAG_MAINBOARD:
    mb = ptr_entry;
    string_ptr = mb->strings;
    dev_dbg(dev, "mainboard vendor: %s\n", string_ptr);
    sysinfo->mainboard_vendor = string_ptr;
    string_ptr += mb->part_number_idx;
    dev_dbg(dev, "mainboard model: %s\n", string_ptr);
    sysinfo->mainboard_model = string_ptr;
    break;
   case CB_TAG_FRAMEBUFFER:
    fb = ptr_entry;
    dev_dbg(dev, "fb phys_addr: 0x%llx\n", fb->physical_address);
    dev_dbg(dev, "fb res: %dx%d @%dbpp\n", fb->x_resolution, fb->y_resolution, fb->bits_per_pixel);
    sysinfo->fb = fb;
    break;
  };

  ptr_entry += entry->size;
 }
}

static int coreboot_table_probe(struct device *dev)
{
 struct resource *res;
 struct coreboot_table_header *header;
 struct coreboot_sysinfo *sysinfo;
 int ret;

 res = dev_get_resource(dev, IORESOURCE_MEM, 0);
 if (IS_ERR(res))
  return PTR_ERR(res);

 header = (struct coreboot_table_header *)res->start;

 /* Check header signature */
 ret = strncmp(header->signature, "LBIO", sizeof(header->signature));
 if (ret) {
  dev_err(dev, "coreboot table invalid\n");
  return -ENODEV;
 }
 
 sysinfo = xzalloc(sizeof(struct coreboot_sysinfo));
 if (!sysinfo)
  return -ENOMEM;

 dev_set_drvdata(dev, sysinfo);

 coreboot_table_populate_sysinfo(dev, header);

 return 0;
}

static struct of_device_id coreboot_table_id[] = {
 { .compatible = "coreboot" },
 { }
};
MODULE_DEVICE_TABLE(of, coreboot_table_id);

static struct driver coreboot_table_driver = {
 .name = "coreboot_table",
 .of_compatible = of_match_ptr(coreboot_table_id),
 .probe = coreboot_table_probe,
};
core_platform_driver(coreboot_table_driver);
