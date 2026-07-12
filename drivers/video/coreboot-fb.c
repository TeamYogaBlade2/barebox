// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026, Brady Norander <brady.norander@mainlining.org>
*/

#include <common.h>
#include <of.h>
#include <linux/coreboot.h>
#include <linux/device.h>
#include <fb.h>

struct corebootfb {
	struct fb_info info;
	struct fb_videomode mode;
	struct fb_ops ops;
};

static void corebootfb_parse_sysinfo(struct corebootfb *cfb,
          								struct coreboot_sysinfo *sysinfo)
{
	struct fb_info *info = &cfb->info;
	struct fb_videomode *mode = &cfb->mode;

	struct fb_bitfield red = {
		.offset = sysinfo->fb->red_mask_pos,
		.length = sysinfo->fb->red_mask_size,
	};
	struct fb_bitfield green = {
		.offset = sysinfo->fb->green_mask_pos,
		.length = sysinfo->fb->green_mask_size,
	};
	struct fb_bitfield blue = {
		.offset = sysinfo->fb->blue_mask_pos,
		.length = sysinfo->fb->blue_mask_size,
	};
	struct fb_bitfield transp = {
		.offset = 0,
		.length = 0,
	};

	mode->name = "coreboot"; // uhh is this allowed?
	mode->xres = sysinfo->fb->x_resolution;
	mode->yres = sysinfo->fb->y_resolution;
	info->mode = mode;
	info->bits_per_pixel = sysinfo->fb->bits_per_pixel;
	info->red = red;
	info->green = green;
	info->blue = blue;
	info->transp = transp;
}

static int corebootfb_probe(struct device *dev)
{
	struct device *coreboot_table;
	struct coreboot_sysinfo *sysinfo;
	struct corebootfb *cfb;
	int ret;

	coreboot_table = of_find_device_by_node_path("/firmware/coreboot");
	if (!coreboot_table) {
		dev_err(dev, "Failed to get coreboot table dev\n");
		return -ENODEV;
	};

	if (!dev_is_probed(coreboot_table))
		return -EPROBE_DEFER;

	sysinfo = dev_get_drvdata(coreboot_table);

	cfb = xzalloc(sizeof(struct corebootfb));
	if (!cfb)
		return -ENOMEM;

	corebootfb_parse_sysinfo(cfb, sysinfo);

	if (!sysinfo->fb->physical_address) {
		cfb->info.screen_base = dma_alloc_writecombine(DMA_DEVICE_BROKEN,
		              sysinfo->fb->bytes_per_line * sysinfo->fb->y_resolution,
		              DMA_ADDRESS_BROKEN);
		sysinfo->fb->physical_address = (uintptr_t)cfb->info.screen_base;
	} else {
		cfb->info.screen_base = (void *)sysinfo->fb->physical_address;
	}
	cfb->info.screen_size = sysinfo->fb->bytes_per_line * sysinfo->fb->y_resolution;
	cfb->info.dev.parent = dev;
	cfb->info.fbops = &cfb->ops;

	ret = register_framebuffer(&cfb->info);
	if (ret < 0) {
		dev_err(dev, "Failed to register coreboot framebuffer: %d\n", ret);
		return ret;
	}

	dev_info(dev, "Registered coreboot framebuffer @0x%pd\n", cfb->info.screen_base);

  return 0;
}

static const struct of_device_id corebootfb_of_match[] = {
	{ .compatible = "coreboot-framebuffer", },
	{ },
};
MODULE_DEVICE_TABLE(of, corebootfb_of_match);

static struct driver corebootfb_driver = {
	.name = "coreboot_framebuffer",
	.of_compatible = corebootfb_of_match,
	.probe = corebootfb_probe,
};
device_platform_driver(corebootfb_driver);

MODULE_AUTHOR("Stephen Warren <swarren@wwwdotorg.org>");
MODULE_DESCRIPTION("coreboot framebuffer driver");
MODULE_LICENSE("GPL v2");
