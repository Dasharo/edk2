/* SPDX-License-Identifier: GPL-2.0-only */

#include "common.h"

/* The assumption in all this code is that we're given a pointer to enough data.
 * Hence, we do not check for underflow.
 */
static uint8_t get8(struct buffer *input)
{
	uint8_t ret = *input->data++;
	input->size--;
	return ret;
}

static uint16_t get16be(struct buffer *input)
{
	uint16_t ret;
	ret = get8(input) << 8;
	ret |= get8(input);
	return ret;
}

static uint32_t get32be(struct buffer *input)
{
	uint32_t ret;
	ret = get16be(input) << 16;
	ret |= get16be(input);
	return ret;
}

static uint64_t get64be(struct buffer *input)
{
	uint64_t ret;
	ret = get32be(input);
	ret <<= 32;
	ret |= get32be(input);
	return ret;
}

static void put8(struct buffer *input, uint8_t val)
{
	input->data[input->size] = val;
	input->size++;
}

static void put16be(struct buffer *input, uint16_t val)
{
	put8(input, val >> 8);
	put8(input, val);
}

static void put32be(struct buffer *input, uint32_t val)
{
	put16be(input, val >> 16);
	put16be(input, val);
}

static void put64be(struct buffer *input, uint64_t val)
{
	put32be(input, val >> 32);
	put32be(input, val);
}

struct xdr xdr_be = {
	get8, get16be, get32be, get64be,
	put8, put16be, put32be, put64be
};
