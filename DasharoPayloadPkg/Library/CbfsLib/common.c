/* common utility functions for cbfstool */
/* SPDX-License-Identifier: GPL-2.0-only */

#include "common.h"
#include "cbfs.h"

/* Small, OS/libc independent runtime check for endianness */
int is_big_endian(void)
{
	static const uint32_t inttest = 0x12345678;
	const uint8_t inttest_lsb = *(const uint8_t *)&inttest;
	if (inttest_lsb == 0x12) {
		return 1;
	}
	return 0;
}
