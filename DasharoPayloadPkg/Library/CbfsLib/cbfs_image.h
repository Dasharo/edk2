/* CBFS Image Manipulation */
/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __CBFS_IMAGE_H
#define __CBFS_IMAGE_H

#include "common.h"
#include "cbfs.h"

#define HEADER_OFFSET_UNKNOWN (~0u)

/* CBFS image processing */

struct cbfs_image {
	struct buffer buffer;
	/* An image has a header iff it's a legacy CBFS. */
	bool has_header;
	/* Only meaningful if has_header is selected. */
	struct cbfs_header header;
};

/* Or deserialize into host-native format */
void cbfs_get_header(struct cbfs_header *header, void *src);

/* Constructs a cbfs_image from a buffer. The resulting image contains a shallow
 * copy of the buffer; releasing either one is the legal way to clean up after
 * both of them at once. Always produces a cbfs_image, but...
 * Returns 0 if it contains a valid CBFS, non-zero if it's unrecognized data. */
int cbfs_image_from_buffer(struct cbfs_image *out, struct buffer *in,
			   uint32_t offset);

/* Returns a pointer to entry by name, or NULL if name is not found. */
struct cbfs_file *cbfs_get_entry(struct cbfs_image *image, const char *name);

/* Adds an entry to CBFS image by given name and type. If content_offset is
 * non-zero, try to align "content" (CBFS_SUBHEADER(p)) at content_offset.
 * Never pass this function a top-aligned address: convert it to an offset.
 * Returns 0 on success, otherwise non-zero. */
int cbfs_add_entry(struct cbfs_image *image, struct buffer *buffer,
		   uint32_t content_offset, struct cbfs_file *header,
		   const size_t len_align);

/* Removes an entry from CBFS image. Returns 0 on success, otherwise non-zero. */
int cbfs_remove_entry(struct cbfs_image *image, const char *name);

/* Create a new cbfs file header structure to work with.
   Returns newly allocated memory that the caller needs to free after use. */
struct cbfs_file *cbfs_create_file_header(int type, size_t len,
	const char *name);

/* Initializes a new empty (type = NULL) entry with size and name in CBFS image.
 * Returns 0 on success, otherwise (ex, not found) non-zero. */
int cbfs_create_empty_entry(struct cbfs_file *entry, int type,
			    size_t len, const char *name);

/* Callback function used by cbfs_legacy_walk.
 * Returns 0 on success, or non-zero to stop further iteration. */
typedef int (*cbfs_entry_callback)(struct cbfs_image *image,
				   struct cbfs_file *file,
				   void *arg);

/* Iterates through all entries in CBFS image, and invoke with callback.
 * Stops if callback returns non-zero values. Unlike the commonlib cbfs_walk(),
 * this can deal with different alignments in legacy CBFS (with master header).
 * Returns number of entries invoked. */
int cbfs_legacy_walk(struct cbfs_image *image, cbfs_entry_callback callback,
		     void *arg);

/* Primitive CBFS utilities */

/* Returns a pointer to the only valid CBFS header in give buffer, otherwise
 * NULL (including when multiple headers were found). If there is a X86 ROM
 * style signature (pointer at 0xfffffffc) found in ROM, it will be selected as
 * the only header.*/
struct cbfs_header *cbfs_find_header(char *data, size_t size,
				     uint32_t forced_offset);

/* Returns the first cbfs_file entry in CBFS image by CBFS header (no matter if
 * the entry has valid content or not), otherwise NULL. */
struct cbfs_file *cbfs_find_first_entry(struct cbfs_image *image);

/* Returns next cbfs_file entry (no matter if its content is valid or not), or
 * NULL on failure. */
struct cbfs_file *cbfs_find_next_entry(struct cbfs_image *image,
				       struct cbfs_file *entry);

/* Returns ROM address (offset) of entry.
 * This is different from entry->offset (pointer to content). */
uint32_t cbfs_get_entry_addr(struct cbfs_image *image, struct cbfs_file *entry);

/* Returns 1 if valid new-format CBFS (without a master header), otherwise 0. */
int cbfs_is_valid_cbfs(struct cbfs_image *image);

/* Returns 1 if valid legacy CBFS (with a master header), otherwise 0. */
int cbfs_is_legacy_cbfs(struct cbfs_image *image);

/* Returns 1 if entry has valid data (by checking magic number), otherwise 0. */
int cbfs_is_valid_entry(struct cbfs_image *image, struct cbfs_file *entry);

/* Merge empty entries starting from given entry.
 * Returns 0 on success, otherwise non-zero. */
int cbfs_merge_empty_entry(struct cbfs_image *image, struct cbfs_file *entry,
			   void *arg);

/* Returns the size of a cbfs file header with no extensions */
size_t cbfs_calculate_file_header_size(const char *name);

#endif
