/* CBFS Image Manipulation */
/* SPDX-License-Identifier: GPL-2.0-only */

#include "common.h"
#include "cbfs_image.h"

/* Even though the file-adding functions---cbfs_add_entry() and
 * cbfs_add_entry_at()---perform their sizing checks against the beginning of
 * the subsequent section rather than a stable recorded value such as an empty
 * file header's len field, it's possible to prove two interesting properties
 * about their behavior:
 *  - Placing a new file within an empty entry located below an existing file
 *    entry will never leave an aligned flash address containing neither the
 *    beginning of a file header nor part of a file.
 *  - Placing a new file in an empty entry at the very end of the image such
 *    that it fits, but leaves no room for a final header, is guaranteed not to
 *    change the total amount of space for entries, even if that new file is
 *    later removed from the CBFS.
 * These properties are somewhat nonobvious from the implementation, so the
 * reader is encouraged to blame this comment and examine the full proofs
 * in the commit message before making significant changes that would risk
 * removing said guarantees.
 */

/* CBFS image */

size_t cbfs_calculate_file_header_size(const char *name)
{
	return (sizeof(struct cbfs_file) +
		align_up(strlen(name) + 1, CBFS_ATTRIBUTE_ALIGN));
}

/* Only call on legacy CBFSes possessing a master header. */
static int cbfs_fix_legacy_size(struct cbfs_image *image, char *hdr_loc)
{
	assert(image);
	assert(cbfs_is_legacy_cbfs(image));
	// A bug in old cbfstool may produce extra few bytes (by alignment) and
	// cause cbfstool to overwrite things after free space -- which is
	// usually CBFS header on x86. We need to workaround that.
	// Except when we run across a file that contains the actual header,
	// in which case this image is a safe, new-style
	// `cbfstool add-master-header` based image.

	struct cbfs_file *entry, *first = NULL, *last = NULL;
	for (first = entry = cbfs_find_first_entry(image);
	     entry && cbfs_is_valid_entry(image, entry);
	     entry = cbfs_find_next_entry(image, entry)) {
		/* Is the header guarded by a CBFS file entry? Then exit */
		if (((char *)entry) + ntohl(entry->offset) == hdr_loc) {
			return 0;
		}
		last = entry;
	}
	if ((char *)first < (char *)hdr_loc &&
	    (char *)entry > (char *)hdr_loc) {
		WARN("CBFS image was created with old cbfstool with size bug. "
		     "Fixing size in last entry...\n");
		last->len = htonl(ntohl(last->len) - image->header.align);
	}
	return 0;
}

void cbfs_get_header(struct cbfs_header *header, void *src)
{
	struct buffer outheader;

	outheader.data = src;	/* We're not modifying the data */
	outheader.size = 0;

	header->magic = xdr_be.get32(&outheader);
	header->version = xdr_be.get32(&outheader);
	header->romsize = xdr_be.get32(&outheader);
	header->bootblocksize = xdr_be.get32(&outheader);
	header->align = xdr_be.get32(&outheader);
	header->offset = xdr_be.get32(&outheader);
	header->architecture = xdr_be.get32(&outheader);
}

int cbfs_image_from_buffer(struct cbfs_image *out, struct buffer *in,
			   uint32_t offset)
{
	assert(out);
	assert(in);
	assert(in->data);

	buffer_clone(&out->buffer, in);
	out->has_header = false;

	if (cbfs_is_valid_cbfs(out)) {
		return 0;
	}

	void *header_loc = cbfs_find_header(in->data, in->size, offset);
	if (header_loc) {
		cbfs_get_header(&out->header, header_loc);
		out->has_header = true;
		cbfs_fix_legacy_size(out, header_loc);
		return 0;
	} else if (offset != HEADER_OFFSET_UNKNOWN) {
		ERROR("The -H switch is only valid on legacy images having CBFS master headers.\n");
	}
	ERROR("Selected image region is not a valid CBFS.\n");
	return 1;
}

/* Tries to add an entry with its data (CBFS_SUBHEADER) at given offset. */
static int cbfs_add_entry_at(struct cbfs_image *image,
			     struct cbfs_file *entry,
			     const void *data,
			     uint32_t content_offset,
			     const struct cbfs_file *header,
			     const size_t len_align)
{
	struct cbfs_file *next = cbfs_find_next_entry(image, entry);
	uint32_t addr = cbfs_get_entry_addr(image, entry),
		 addr_next = cbfs_get_entry_addr(image, next);
	uint32_t min_entry_size = cbfs_calculate_file_header_size("");
	uint32_t len, header_offset;
	uint32_t align = image->has_header ? image->header.align :
							CBFS_ALIGNMENT;
	uint32_t header_size = ntohl(header->offset);

	header_offset = content_offset - header_size;
	if (header_offset % align)
		header_offset -= header_offset % align;
	if (header_offset < addr) {
		ERROR("No space to hold cbfs_file header.");
		return -1;
	}

	// Process buffer BEFORE content_offset.
	if (header_offset - addr > min_entry_size) {
		len = header_offset - addr - min_entry_size;
		if (cbfs_create_empty_entry(entry, CBFS_TYPE_NULL, len, ""))
			return -1;
		entry = cbfs_find_next_entry(image, entry);
		addr = cbfs_get_entry_addr(image, entry);
	}

	len = content_offset - addr - header_size;
	memcpy(entry, header, header_size);
	if (len != 0) {
		/*
		 * The header moved backwards a bit to accommodate cbfs_file
		 * alignment requirements, so patch up ->offset to still point
		 * to file data. Move attributes forward so the end of the
		 * attribute list still matches the end of the metadata.
		 */
		uint32_t offset = ntohl(entry->offset);
		uint32_t attrs = ntohl(entry->attributes_offset);
		if (attrs == 0) {
			memset((uint8_t *)entry + offset, 0, len);
		} else {
			uint8_t *p = (uint8_t *)entry + attrs;
			memmove(p + len, p, offset - attrs);
			memset(p, 0, len);
			attrs += len;
			entry->attributes_offset = htonl(attrs);
		}
		offset += len;
		entry->offset = htonl(offset);
	}

	// Ready to fill data into entry.
	assert((char*)CBFS_SUBHEADER(entry) - image->buffer.data ==
	       (ptrdiff_t)content_offset);
	memcpy(CBFS_SUBHEADER(entry), data, ntohl(entry->len));

	// Align the length to a multiple of len_align
	if (len_align &&
	    ((ntohl(entry->offset) + ntohl(entry->len)) % len_align)) {
		size_t off = (ntohl(entry->offset) + ntohl(entry->len)) % len_align;
		entry->len = htonl(ntohl(entry->len) + len_align - off);
	}

	// Process buffer AFTER entry.
	entry = cbfs_find_next_entry(image, entry);
	addr = cbfs_get_entry_addr(image, entry);
	if (addr == addr_next)
		return 0;

	assert(addr < addr_next);
	if (addr_next - addr < min_entry_size) {
		/* No need to increase the size of the just
		 * stored file to extend to next file. Alignment
		 * of next file takes care of this.
		 */
		return 0;
	}

	len = addr_next - addr - min_entry_size;
	/* keep space for master header pointer */
	if ((uint8_t *)entry + min_entry_size + len >
			(uint8_t *)buffer_get(&image->buffer) +
			buffer_size(&image->buffer) - sizeof(int32_t)) {
		len -= sizeof(int32_t);
	}
	if (cbfs_create_empty_entry(entry, CBFS_TYPE_NULL, len, ""))
		return -1;
	return 0;
}

int cbfs_add_entry(struct cbfs_image *image, struct buffer *buffer,
		   uint32_t content_offset,
		   struct cbfs_file *header,
		   const size_t len_align)
{
	assert(image);
	assert(buffer);
	assert(buffer->data);
	assert(!IS_HOST_SPACE_ADDRESS(content_offset));

	const char *name = header->filename;

	/* This is so special rows in cbfstool print -k -v output stay unambiguous. */
	if (name[0] == '[') {
		ERROR("CBFS file name `%s` must not start with `[`\n", name);
		return -1;
	}

	uint32_t entry_type;
	uint32_t addr, addr_next;
	uint32_t entry_size;
	uint32_t max_null_entry_size = 0;
	struct cbfs_file *entry, *next;
	uint32_t need_size;
	uint32_t header_size = ntohl(header->offset);

	need_size = header_size + buffer->size;

	// Merge empty entries.
	cbfs_legacy_walk(image, cbfs_merge_empty_entry, NULL);

	for (entry = cbfs_find_first_entry(image);
	     entry && cbfs_is_valid_entry(image, entry);
	     entry = cbfs_find_next_entry(image, entry)) {

		entry_type = ntohl(entry->type);
		if (entry_type != CBFS_TYPE_NULL)
			continue;

		addr = cbfs_get_entry_addr(image, entry);
		next = cbfs_find_next_entry(image, entry);
		addr_next = cbfs_get_entry_addr(image, next);
		entry_size = addr_next - addr;
		max_null_entry_size = MAX(max_null_entry_size, entry_size);

		/* Will the file fit? Don't yet worry if we have space for a new
		 * "empty" entry. We take care of that later.
		 */
		if (addr + need_size > addr_next)
			continue;

		// Test for complicated cases
		if (content_offset > 0) {
			if (addr_next < content_offset) {
				continue;
			} else if (addr > content_offset) {
				break;
			} else if (addr + header_size > content_offset) {
				ERROR("Not enough space for header.\n");
				break;
			} else if (content_offset + buffer->size > addr_next) {
				ERROR("Not enough space for content.\n");
				break;
			}
		}

		// TODO there are more few tricky cases that we may
		// want to fit by altering offset.

		if (content_offset == 0) {
			// we tested every condition earlier under which
			// placing the file there might fail
			content_offset = addr + header_size;
		}

		if (cbfs_add_entry_at(image, entry, buffer->data,
				      content_offset, header, len_align) == 0) {
			return 0;
		}
		break;
	}

	ERROR("Could not add %s [header %d + content %zd bytes (%zd KB)] @0x%x; "
	      "Largest empty slot: %d bytes\n",
	      buffer->name, header_size, buffer->size, buffer->size / 1024, content_offset,
	      max_null_entry_size);
	return -1;
}

struct cbfs_file *cbfs_get_entry(struct cbfs_image *image, const char *name)
{
	struct cbfs_file *entry;
	for (entry = cbfs_find_first_entry(image);
	     entry && cbfs_is_valid_entry(image, entry);
	     entry = cbfs_find_next_entry(image, entry)) {
		if (strcasecmp(entry->filename, name) == 0) {
			return entry;
		}
	}
	return NULL;
}

int cbfs_remove_entry(struct cbfs_image *image, const char *name)
{
	struct cbfs_file *entry;
	entry = cbfs_get_entry(image, name);
	if (!entry) {
		ERROR("CBFS file %s not found.\n", name);
		return -1;
	}
	entry->type = htonl(CBFS_TYPE_DELETED);
	cbfs_legacy_walk(image, cbfs_merge_empty_entry, NULL);
	return 0;
}

int cbfs_merge_empty_entry(struct cbfs_image *image, struct cbfs_file *entry,
			   unused void *arg)
{
	struct cbfs_file *next;
	uint32_t next_addr = 0;

	/* We don't return here even if this entry is already empty because we
	   want to merge the empty entries following after it. */

	/* Loop until non-empty entry is found, starting from the current entry.
	   After the loop, next_addr points to the next non-empty entry. */
	next = entry;
	while (ntohl(next->type) == CBFS_TYPE_DELETED ||
			ntohl(next->type) == CBFS_TYPE_NULL) {
		next = cbfs_find_next_entry(image, next);
		if (!next)
			break;
		next_addr = cbfs_get_entry_addr(image, next);
		if (!cbfs_is_valid_entry(image, next))
			/* 'next' could be the end of cbfs */
			break;
	}

	if (!next_addr)
		/* Nothing to empty */
		return 0;

	/* We can return here if we find only a single empty entry.
	   For simplicity, we just proceed (and make it empty again). */

	/* We're creating one empty entry for combined empty spaces */
	uint32_t addr = cbfs_get_entry_addr(image, entry);
	size_t len = next_addr - addr - cbfs_calculate_file_header_size("");
	return cbfs_create_empty_entry(entry, CBFS_TYPE_NULL, len, "");
}

int cbfs_legacy_walk(struct cbfs_image *image, cbfs_entry_callback callback,
	      void *arg)
{
	int count = 0;
	struct cbfs_file *entry;
	for (entry = cbfs_find_first_entry(image);
	     entry && cbfs_is_valid_entry(image, entry);
	     entry = cbfs_find_next_entry(image, entry)) {
		count ++;
		if (callback(image, entry, arg) != 0)
			break;
	}
	return count;
}

static int cbfs_header_valid(struct cbfs_header *header)
{
	if ((ntohl(header->magic) == CBFS_HEADER_MAGIC) &&
	    ((ntohl(header->version) == CBFS_HEADER_VERSION1) ||
	     (ntohl(header->version) == CBFS_HEADER_VERSION2)) &&
	    (ntohl(header->offset) < ntohl(header->romsize)))
		return 1;
	return 0;
}

struct cbfs_header *cbfs_find_header(char *data, size_t size,
				     uint32_t forced_offset)
{
	size_t offset;
	int found = 0;
	int32_t rel_offset;
	struct cbfs_header *header, *result = NULL;

	if (forced_offset < (size - sizeof(struct cbfs_header))) {
		/* Check if the forced header is valid. */
		header = (struct cbfs_header *)(data + forced_offset);
		if (cbfs_header_valid(header))
			return header;
		return NULL;
	}

	// Try finding relative offset of master header at end of file first.
	rel_offset = *(int32_t *)(data + size - sizeof(int32_t));
	offset = size + rel_offset;

	if (offset >= size - sizeof(*header) ||
	    !cbfs_header_valid((struct cbfs_header *)(data + offset))) {
		// Some use cases append non-CBFS data to the end of the ROM.
		offset = 0;
	}

	for (; offset + sizeof(*header) < size; offset++) {
		header = (struct cbfs_header *)(data + offset);
		if (!cbfs_header_valid(header))
			continue;
		if (!found++)
			result = header;
	}
	if (found > 1)
		// Top-aligned images usually have a working relative offset
		// field, so this is more likely to happen on bottom-aligned
		// ones (where the first header is the "outermost" one)
		WARN("Multiple (%d) CBFS headers found, using the first one.\n",
		       found);
	return result;
}


struct cbfs_file *cbfs_find_first_entry(struct cbfs_image *image)
{
	assert(image);
	if (image->has_header)
		/* header.offset is relative to start of flash, not
		 * start of region, so use it with the full image.
		 */
		return (struct cbfs_file *)
			(buffer_get_original_backing(&image->buffer) +
			image->header.offset);
	else
		return (struct cbfs_file *)buffer_get(&image->buffer);
}

struct cbfs_file *cbfs_find_next_entry(struct cbfs_image *image,
				       struct cbfs_file *entry)
{
	uint32_t addr = cbfs_get_entry_addr(image, entry);
	int align = image->has_header ? image->header.align : CBFS_ALIGNMENT;
	assert(entry && cbfs_is_valid_entry(image, entry));
	addr += ntohl(entry->offset) + ntohl(entry->len);
	addr = align_up(addr, align);
	return (struct cbfs_file *)(image->buffer.data + addr);
}

uint32_t cbfs_get_entry_addr(struct cbfs_image *image, struct cbfs_file *entry)
{
	assert(image && image->buffer.data && entry);
	return (int32_t)((char *)entry - image->buffer.data);
}

int cbfs_is_valid_cbfs(struct cbfs_image *image)
{
	return buffer_check_magic(&image->buffer, CBFS_FILE_MAGIC,
						strlen(CBFS_FILE_MAGIC));
}

int cbfs_is_legacy_cbfs(struct cbfs_image *image)
{
	return image->has_header;
}

int cbfs_is_valid_entry(struct cbfs_image *image, struct cbfs_file *entry)
{
	uint32_t offset = cbfs_get_entry_addr(image, entry);

	if (offset >= image->buffer.size)
		return 0;

	struct buffer entry_data;
	buffer_clone(&entry_data, &image->buffer);
	buffer_seek(&entry_data, offset);
	return buffer_check_magic(&entry_data, CBFS_FILE_MAGIC,
						strlen(CBFS_FILE_MAGIC));
}

struct cbfs_file *cbfs_create_file_header(int type,
			    size_t len, const char *name)
{
	size_t header_size = cbfs_calculate_file_header_size(name);
	if (header_size > CBFS_METADATA_MAX_SIZE) {
		ERROR("'%s' name too long to fit in CBFS header\n", name);
		return NULL;
	}

	struct cbfs_file *entry = malloc(CBFS_METADATA_MAX_SIZE);
	memset(entry, CBFS_CONTENT_DEFAULT_VALUE, CBFS_METADATA_MAX_SIZE);
	memcpy(entry->magic, CBFS_FILE_MAGIC, sizeof(entry->magic));
	entry->type = htonl(type);
	entry->len = htonl(len);
	entry->attributes_offset = 0;
	entry->offset = htonl(header_size);
	memset(entry->filename, 0, ntohl(entry->offset) - sizeof(*entry));
	strcpy(entry->filename, name);
	return entry;
}

int cbfs_create_empty_entry(struct cbfs_file *entry, int type,
			    size_t len, const char *name)
{
	struct cbfs_file *tmp = cbfs_create_file_header(type, len, name);
	if (!tmp)
		return -1;

	memcpy(entry, tmp, ntohl(tmp->offset));
	free(tmp);
	memset(CBFS_SUBHEADER(entry), CBFS_CONTENT_DEFAULT_VALUE, len);
	return 0;
}
