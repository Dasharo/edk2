/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __CBFSTOOL_COMMON_H
#define __CBFSTOOL_COMMON_H

#include "adapt.h"

#include "cbfs_serialized.h"
#include "swab.h"

/*
 * There are two address spaces that this tool deals with - SPI flash address space and host
 * address space. This macros checks if the address is greater than 2GiB under the assumption
 * that the low MMIO lives in the top half of the 4G address space of the host.
 */
#define IS_HOST_SPACE_ADDRESS(addr)	((uint32_t)(addr) > 0x80000000)

// #define ERROR(...) do { fprintf(stderr, __VA_ARGS__); } while(0)
// #define INFO(...) do { fprintf(stderr, __VA_ARGS__); } while(0)
// #define WARN(...) do { fprintf(stderr, __VA_ARGS__); } while(0)
// #define DEBUG(...) do { fprintf(stderr, __VA_ARGS__); } while(0)
#define LOG(...) do { fprintf(stderr, __VA_ARGS__); } while(0)

#define unused __attribute__((unused))

static inline uint32_t align_up(uint32_t value, uint32_t align)
{
	if (value % align)
		value += align - (value % align);
	return value;
}

/* Buffer and file I/O */
struct buffer {
	char *name;
	char *data;
	size_t offset;
	size_t size;
};

static inline void *buffer_get(const struct buffer *b)
{
	return b->data;
}

static inline size_t buffer_size(const struct buffer *b)
{
	return b->size;
}

static inline size_t buffer_offset(const struct buffer *b)
{
	return b->offset;
}

/*
 * Shrink a buffer toward the beginning of its previous space.
 * Afterward, buffer_delete() remains the means of cleaning it up. */
static inline void buffer_set_size(struct buffer *b, size_t size)
{
	b->size = size;
}

/* Initialize a buffer with the given constraints. */
static inline void buffer_init(struct buffer *b, char *name, void *data,
                               size_t size)
{
	b->name = name;
	b->data = data;
	b->size = size;
	b->offset = 0;
}

/* Splice a buffer into another buffer. Note that it's up to the caller to
 * bounds check the offset and size. The resulting buffer is backed by the same
 * storage as the original, so although it is valid to buffer_delete() either
 * one of them, doing so releases both simultaneously. */
static inline void buffer_splice(struct buffer *dest, const struct buffer *src,
                                 size_t offset, size_t size)
{
	dest->name = src->name;
	dest->data = src->data + offset;
	dest->offset = src->offset + offset;
	dest->size = size;
}

/*
 * Shallow copy a buffer. To clean up the resources, buffer_delete()
 * either one, but not both. */
static inline void buffer_clone(struct buffer *dest, const struct buffer *src)
{
	buffer_splice(dest, src, 0, src->size);
}

/*
 * Shrink a buffer toward the end of its previous space.
 * Afterward, buffer_delete() remains the means of cleaning it up. */
static inline void buffer_seek(struct buffer *b, size_t size)
{
	b->offset += size;
	b->size -= size;
	b->data += size;
}

/* Returns whether the buffer begins with the specified magic bytes. */
static inline bool buffer_check_magic(const struct buffer *b, const char *magic,
							size_t magic_len)
{
	assert(magic);
	return b && b->size >= magic_len &&
					memcmp(b->data, magic, magic_len) == 0;
}

/* Returns the start of the underlying buffer, with the offset undone */
static inline void *buffer_get_original_backing(const struct buffer *b)
{
	if (!b)
		return NULL;
	return buffer_get(b) - buffer_offset(b);
}

/* Loads a file into memory buffer. Returns 0 on success, otherwise non-zero. */
int buffer_from_file(struct buffer *buffer, const char *filename);

/* Destroys a memory buffer. */
void buffer_delete(struct buffer *buffer);

/* xdr.c */
struct xdr {
	uint8_t (*get8)(struct buffer *input);
	uint16_t (*get16)(struct buffer *input);
	uint32_t (*get32)(struct buffer *input);
	uint64_t (*get64)(struct buffer *input);
	void (*put8)(struct buffer *input, uint8_t val);
	void (*put16)(struct buffer *input, uint16_t val);
	void (*put32)(struct buffer *input, uint32_t val);
	void (*put64)(struct buffer *input, uint64_t val);
};

extern struct xdr xdr_be;

#endif
