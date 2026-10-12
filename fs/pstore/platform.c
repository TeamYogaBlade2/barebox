/*
 * Persistent Storage - platform driver interface parts.
 *
 * Copyright (C) 2007-2008 Google, Inc.
 * Copyright (C) 2010 Intel Corporation <tony.luck@intel.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) "pstore: " fmt

#include <linux/types.h>
#include <linux/pstore.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/zlib.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <console.h>
#include <malloc.h>
#include <linux/printk.h>
#include <module.h>

#include "internal.h"

struct pstore_info *psinfo;

static char *backend;

/* How much of the console log to snapshot */
static unsigned long kmsg_bytes = 10240;

void pstore_set_kmsg_bytes(int bytes)
{
	kmsg_bytes = bytes;
}

static int pstore_ready;

bool pstore_is_ready(void)
{
	return pstore_ready;
}
EXPORT_SYMBOL(pstore_is_ready);

/*
 * Linux pstore uses raw DEFLATE for compressed dmesg records. The ramoops
 * backend appends its ECC notice after the stored record, so exclude the
 * notice from the compressed input and append it to the expanded output.
 */
static int pstore_decompress_record(struct pstore_record *record)
{
	struct z_stream_s stream = {};
	size_t max_size, ecc_size, input_size, input_left, output_size;
	char *output;
	void *workspace;
	int ret, end_ret;

	if (!record->psi || record->type != PSTORE_TYPE_DMESG ||
	    !record->buf || record->size < 0 ||
	    record->ecc_notice_size < 0 ||
	    record->ecc_notice_size > record->size)
		return -EINVAL;

	ecc_size = record->ecc_notice_size;
	input_size = record->size - ecc_size;

	if (!record->psi->bufsize ||
	    record->psi->bufsize > (size_t)-1 / 3)
		return -EOVERFLOW;

	/*
	 * Match Linux fs/pstore/platform.c, which permits an uncompressed
	 * record up to three times the ramoops payload buffer size.
	 */
	max_size = record->psi->bufsize * 3;
	if (max_size > (size_t)~0U || input_size > (size_t)~0U ||
	    max_size > (size_t)-1 - ecc_size - 1)
		return -EOVERFLOW;

	output = kmalloc(max_size + ecc_size + 1, GFP_KERNEL);
	if (!output)
		return -ENOMEM;

	workspace = kmalloc(zlib_inflate_workspacesize(), GFP_KERNEL);
	if (!workspace) {
		kfree(output);
		return -ENOMEM;
	}

	stream.workspace = workspace;
	ret = zlib_inflateInit2(&stream, -MAX_WBITS);
	if (ret != Z_OK) {
		if (ret >= 0)
			ret = -EINVAL;
		goto err;
	}

	stream.next_in = (const Byte *)record->buf;
	stream.avail_in = input_size;
	stream.next_out = (Byte *)output;
	stream.avail_out = max_size;

	ret = zlib_inflate(&stream, Z_FINISH);
	input_left = stream.avail_in;
	output_size = stream.total_out;
	end_ret = zlib_inflateEnd(&stream);

	if (ret != Z_STREAM_END || input_left ||
	    end_ret != Z_OK || output_size > max_size ||
	    output_size > INT_MAX || ecc_size > INT_MAX - output_size) {
		/* A positive return must not be mistaken for success. */
		if (ret >= 0)
			ret = -EINVAL;
		goto err;
	}

	memcpy(output + output_size, record->buf + input_size, ecc_size);
	output[output_size + ecc_size] = '\0';

	kfree(workspace);
	kfree(record->buf);
	record->buf = output;
	record->size = output_size + ecc_size;
	record->compressed = false;

	return 0;

err:
	kfree(workspace);
	kfree(output);
	return ret;
}

void pstore_log(const char *str)
{
	uint64_t id;
	static int in_pstore;

	if (!IS_ENABLED(CONFIG_FS_PSTORE_CONSOLE))
		return;

	if (!pstore_ready)
		return;

	if (in_pstore)
		return;

	in_pstore = 1;

	psinfo->write_buf(PSTORE_TYPE_CONSOLE, 0, &id, 0,
			  str, 0, strlen(str), psinfo);

	in_pstore = 0;
}

static void pstore_console_capture_log(void)
{
	uint64_t id;
	struct log_entry *log, *tmp;

	if (IS_ENABLED(CONFIG_CONSOLE_NONE))
		return;

	list_for_each_entry_safe(log, tmp, &barebox_logbuf, list) {
		psinfo->write_buf(PSTORE_TYPE_CONSOLE, 0, &id, 0,
				  log->msg, 0, strlen(log->msg), psinfo);
	}
}

/*
 * platform specific persistent storage driver registers with
 * us here. If pstore is already mounted, call the platform
 * read function right away to populate the file system. If not
 * then the pstore mount code will call us later to fill out
 * the file system.
 *
 * Register with kmsg_dump to save last part of console log on panic.
 */
int pstore_register(struct pstore_info *psi)
{
	if (backend && strcmp(backend, psi->name))
		return -EPERM;

	spin_lock(&pstore_lock);
	if (psinfo) {
		spin_unlock(&pstore_lock);
		return -EBUSY;
	}

	psinfo = psi;
	mutex_init(&psinfo->read_mutex);
	spin_unlock(&pstore_lock);

	pstore_get_records(0);

	pstore_console_capture_log();
	pstore_ready = 1;

	pr_info("Registered %s as persistent store backend\n", psi->name);

	return 0;
}
EXPORT_SYMBOL_GPL(pstore_register);

/*
 * Read all the records from the persistent store. Create
 * files in our filesystem.  Don't warn about -EEXIST errors
 * when we are re-scanning the backing store looking to add new
 * error records.
 */
void pstore_get_records(int quiet)
{
	struct pstore_info *psi = psinfo;
	struct pstore_record	record = { .psi = psi, };
	int			failed = 0, rc;

	if (!psi)
		return;

	mutex_lock(&psi->read_mutex);
	if (psi->open && psi->open(psi))
		goto out;

	while ((record.size = psi->read(&record)) > 0) {
		if (record.compressed) {
			rc = pstore_decompress_record(&record);
			if (rc) {
				pr_err("failed to decompress pstore record %llu: %d\n",
				       record.id, rc);
				failed++;
				kfree(record.buf);
				goto next_record;
			}
		}

		rc = pstore_mkfile(&record);
		if (rc && (rc != -EEXIST || !quiet))
			failed++;

		kfree(record.buf);
		record.buf = NULL;

next_record:
		memset(&record, 0, sizeof(record));
		record.psi = psi;
	}
	if (psi->close)
		psi->close(psi);
out:
	mutex_unlock(&psi->read_mutex);

	if (failed)
		pr_warn("failed to load %d record(s) from '%s'\n",
			failed, psi->name);
}
