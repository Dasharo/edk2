/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __CBFS_H
#define __CBFS_H

#include "cbfs_serialized.h"

/* To make CBFS more friendly to ROM, fill -1 (0xFF) instead of zero. */
#define CBFS_CONTENT_DEFAULT_VALUE	(-1)

#define CBFS_SUBHEADER(_p) ( (void *) ((((uint8_t *) (_p)) + ntohl((_p)->offset))) )

#endif
