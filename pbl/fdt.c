// SPDX-License-Identifier: GPL-2.0
#include <compressed-dtb.h>
#include <linux/libfdt.h>
#include <pbl.h>
#include <linux/printk.h>
#include <stdio.h>
#include <uncompress.h>

static const __be32 *fdt_parse_reg(const __be32 *reg, uint32_t n,
				   uint64_t *val)
{
	int i;

	*val = 0;
	for (i = 0; i < n; i++)
		*val = (*val << 32) | fdt32_to_cpu(*reg++);

	return reg;
}

/*
 * Find the next /reserved-memory range which either contains @cursor,
 * or starts after @cursor.
 *
 * fdt_find_mem() can only return one contiguous memory range. Therefore
 * callers use this helper to walk the gaps between reserved regions and
 * select the largest usable range.
 */
static int fdt_find_next_reserved_mem(const void *fdt, int parent,
				       int na, int ns,
				       uint64_t mem_base, uint64_t mem_end,
				       uint64_t cursor,
				       uint64_t *res_start,
				       uint64_t *res_end)
{
	int child;
	bool overlapping = false;
	uint64_t next_start = mem_end;
	uint64_t next_end = mem_end;

	fdt_for_each_subnode(child, fdt, parent) {
		const __be32 *reg;
		const char *status;
		size_t entry_size;
		int size, i, entries;

		status = fdt_getprop(fdt, child, "status", &size);
		if (status &&
		    strcmp(status, "okay") &&
		    strcmp(status, "ok"))
			continue;

		reg = fdt_getprop(fdt, child, "reg", &size);
		if (!reg)
			continue;

		if (!na || !ns) {
			pr_err("Invalid reserved-memory address/size cells\n");
			return -EINVAL;
		}

		entry_size = (na + ns) * sizeof(*reg);
		if (size < entry_size || size % entry_size) {
			pr_err("Invalid reserved-memory reg property\n");
			return -EINVAL;
		}

		entries = size / entry_size;
		for (i = 0; i < entries; i++) {
			uint64_t start, length, end;

			reg = fdt_parse_reg(reg, na, &start);
			reg = fdt_parse_reg(reg, ns, &length);

			if (!length)
				continue;

			if (start > (uint64_t)-1 - length) {
				pr_err("Reserved-memory range overflows\n");
				return -EINVAL;
			}

			end = start + length;

			if (end <= mem_base || start >= mem_end)
				continue;

			if (start < mem_base)
				start = mem_base;
			if (end > mem_end)
				end = mem_end;

			/*
			 * Prefer a reservation which covers @cursor. There may
			 * be several overlapping reserved regions; skip all of
			 * them in one step by taking the furthest end.
			 */
			if (start <= cursor && cursor < end) {
				if (!overlapping || end > next_end) {
					next_start = start;
					next_end = end;
				}
				overlapping = true;
				continue;
			}

			/*
			 * Otherwise remember the nearest reservation after the
			 * current cursor.
			 */
			if (!overlapping && start > cursor && start < next_start)
				next_start = start;
		}
	}

	if (overlapping || next_start < mem_end) {
		*res_start = next_start;
		*res_end = overlapping ? next_end : next_start;
		return 1;
	}

	return 0;
}

void fdt_find_mem(const void *fdt, unsigned long *membase, unsigned long *memsize)
{
	const __be32 *reg;
	const __be32 *cells;
	int na, ns;
	uint64_t memsize64, membase64;
	uint64_t mem_end, cursor;
	uint64_t best_base, best_size;
	int node, reserved, size, ret;

	/* Make sure FDT blob is sane */
	if (fdt_check_header(fdt) != 0) {
		pr_err("Invalid device tree blob\n");
		goto err;
	}

	node = fdt_path_offset(fdt, "/");
	if (node < 0) {
		pr_err("Cannot find root node\n");
		goto err;
	}

	na = fdt_address_cells(fdt, node);
	if (na < 0) {
		pr_err("Cannot find #address-cells property");
		goto err;
	}

	ns = fdt_size_cells(fdt, node);
	if (ns < 0) {
		pr_err("Cannot find #size-cells property");
		goto err;
	}

	/* Find the memory range */
	node = fdt_node_offset_by_prop_value(fdt, -1, "device_type",
					     "memory", sizeof("memory"));
	if (node < 0) {
		pr_err("Cannot find memory node\n");
		goto err;
	}

	reg = fdt_getprop(fdt, node, "reg", &size);
	if (size < (na + ns) * sizeof(u32)) {
		pr_err("cannot get memory range\n");
		goto err;
	}

	/* get the memsize and truncate it to under 4G on 32 bit machines */
	reg = fdt_parse_reg(reg, na, &membase64);
	reg = fdt_parse_reg(reg, ns, &memsize64);

	if (!memsize64 || membase64 > (uint64_t)-1 - memsize64) {
		pr_err("Invalid memory range\n");
		goto err;
	}

	/*
	 * /memory describes physical RAM, while /reserved-memory removes
	 * ranges from the memory available for general-purpose allocation.
	 *
	 * fdt_find_mem() historically returned a single base+size range,
	 * so when reservations split RAM into multiple pieces, use the
	 * largest remaining contiguous range.
	 */
	reserved = fdt_path_offset(fdt, "/reserved-memory");
	if (reserved >= 0) {
		int reserved_na, reserved_ns;

		cells = fdt_getprop(fdt, reserved, "#address-cells", &size);
		if (!cells || size != sizeof(*cells)) {
			pr_err("Cannot find reserved-memory #address-cells\n");
			goto err;
		}
		reserved_na = fdt32_to_cpu(*cells);

		cells = fdt_getprop(fdt, reserved, "#size-cells", &size);
		if (!cells || size != sizeof(*cells)) {
			pr_err("Cannot find reserved-memory #size-cells\n");
			goto err;
		}
		reserved_ns = fdt32_to_cpu(*cells);

		mem_end = membase64 + memsize64;
		cursor = membase64;
		best_base = 0;
		best_size = 0;

		while (cursor < mem_end) {
			uint64_t res_start, res_end;
			uint64_t gap_size;

			ret = fdt_find_next_reserved_mem(fdt, reserved,
							 reserved_na,
							 reserved_ns,
							 membase64,
							 mem_end,
							 cursor,
							 &res_start,
							 &res_end);
			if (ret < 0)
				goto err;

			if (!ret) {
				gap_size = mem_end - cursor;
				if (gap_size > best_size) {
					best_base = cursor;
					best_size = gap_size;
				}
				break;
			}

			if (res_start > cursor) {
				gap_size = res_start - cursor;
				if (gap_size > best_size) {
					best_base = cursor;
					best_size = gap_size;
				}
			}

			if (res_end > cursor)
				cursor = res_end;
			else
				cursor = res_start;
		}

		if (!best_size) {
			pr_err("No usable memory outside reserved-memory\n");
			goto err;
		}

		membase64 = best_base;
		memsize64 = best_size;
	}

	*membase = membase64;
	*memsize = memsize64;

	return;
err:
	pr_err("No memory, cannot continue\n");
	while (1);
}

static int fdt_find_or_add_memory(void *fdt, int parentoffset, const char *name)
{
	int err;
	int node;

	node = fdt_subnode_offset(fdt, parentoffset, name);
	if (node != -FDT_ERR_NOTFOUND)
		return node;

	/* Create new memory node */
	node = fdt_add_subnode(fdt, parentoffset, name);
	if (node < 0)
		return node;
	err = fdt_setprop(fdt, node, "device_type", "memory", sizeof("memory"));
	if (err < 0)
		return err;

	return node;
}

int fdt_fixup_mem(void *fdt, unsigned long membase[], unsigned long memsize[],
		  size_t num)
{
	int node, root;
	int err;
	int i;

	err = fdt_check_header(fdt);
	if (err != 0) {
		pr_err("Invalid device tree blob: %s\n", fdt_strerror(err));
		return err;
	}

	root = fdt_path_offset(fdt, "/");
	if (root < 0) {
		pr_err("Cannot find root node: %s\n", fdt_strerror(root));
		return root;
	}

	/* Delete memory node without @address postfix */
	node = fdt_subnode_offset(fdt, root, "memory");
	if (node >= 0)
		fdt_del_node(fdt, node);

	for (i = 0; i < num; i++) {
		unsigned long base = membase[i];
		unsigned long size = memsize[i];
		char name[32];

		if (size == 0)
			continue;

		snprintf(name, sizeof(name), "memory@%lx", base);
		node = fdt_find_or_add_memory(fdt, root, name);
		if (node < 0) {
			pr_warn("%s: Failed to get node: %s\n",
				name, fdt_strerror(node));
			continue;
		}

		/* Add or rewrite the reg property */
		fdt_delprop(fdt, node, "reg");
		err = fdt_appendprop_addrrange(fdt, root, node, "reg",
					       base, size);
		if (err < 0) {
			pr_warn("%s: Failed to set reg property %lx %lx: %s\n",
				name, base, size, fdt_strerror(err));
			continue;
		}

		/* Remove status property to ensure the node is enabled */
		fdt_delprop(fdt, node, "status");
	}

	return err;
}

const void *fdt_device_get_match_data(const void *fdt, const char *nodepath,
				      const struct fdt_device_id ids[])
{
	int node, length;
	const char *list, *end;
	const struct fdt_device_id *id;

	node = fdt_path_offset(fdt, nodepath);
	if (node < 0)
		return NULL;

	list = fdt_getprop(fdt, node, "compatible", &length);
	if (!list)
		return NULL;

	end = list + length;

	while (list < end) {
		length = strnlen(list, end - list) + 1;

		/* Abort if the last string isn't properly NUL-terminated. */
		if (list + length > end)
			return NULL;

		for (id = ids; id->compatible; id++) {
			if (!strcasecmp(list, id->compatible))
				return id->data;
		}

		list += length;
	}

	return NULL;
}

static int pbl_open_dtbz_into(const void *fdt, void *buf, int bufsize)
{
	const struct barebox_boarddata_compressed_dtb *compressed_dtb;
	int error;

	if (!fdt_blob_can_be_decompressed(fdt)) {
		pr_warn("DTB can't be decompressed\n");
		return -EINVAL;
	}

	compressed_dtb = fdt;

	if (IS_ENABLED(CONFIG_IMAGE_COMPRESSION_NONE)) {
		error = fdt_open_into(compressed_dtb->data, buf, bufsize);
		if (error) {
			pr_warn("Failed to open uncompressed DTB with %s\n",
				fdt_strerror(error));
			return -EINVAL;
		}
		return 0;
	}

	if (bufsize < compressed_dtb->datalen_uncompressed) {
		pr_warn("FDT buffer to small, min. %u bytes required\n",
			compressed_dtb->datalen_uncompressed);
		return -EINVAL;
	}

	/*
	 * No error handling required, since this will hang() if uncompress
	 * fails.
	 */
	pbl_dtbz_uncompress(buf, (void *)compressed_dtb->data,
			    compressed_dtb->datalen);

	if (!blob_is_fdt(buf)) {
		pr_warn("Failed to determine FDT type for uncompressed DTB\n");
		return -EINVAL;
	}

	/* Required to setup size data structures to allow runtime adaptions */
	error = fdt_open_into(buf, buf, bufsize);
	if (error) {
		pr_warn("Failed to open decompressed DTB with %s\n",
			fdt_strerror(error));
		return -EINVAL;
	}

	return 0;
}

int pbl_load_fdt(const void *fdt, void *dest, int destsize)
{
	if (destsize == 0 || !dest || !fdt) {
		pr_warn("Skip early FDT load, invalid input\n");
		return -EINVAL;
	}

	if (blob_is_fdt(fdt)) {
		int error;

		error = fdt_open_into(fdt, dest, destsize);
		if (error) {
			pr_warn("Failed to uncompressed DTB with %s\n",
				fdt_strerror(error));
			return -EINVAL;
		}
		return 0;
	} else if (blob_is_compressed_fdt(fdt)) {
		return pbl_open_dtbz_into(fdt, dest, destsize);
	}

	pr_warn("FDT detection failed\n");

	return -EINVAL;
}
