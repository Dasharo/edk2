/** @file
  coreboot flash map data parsing library.

Copyright (c) 2024, coreboot project<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

/* SPDX-License-Identifier: BSD-3-Clause or GPL-2.0-only */

#ifndef FLASHMAP_LIB_FMAP_H__
#define FLASHMAP_LIB_FMAP_H__

#define FMAP_SIGNATURE    "__FMAP__"
#define FMAP_VER_MAJOR    1          /* this header's FMAP minor version */
#define FMAP_VER_MINOR    1          /* this header's FMAP minor version */
#define FMAP_STRLEN      32          /* maximum length for strings (including *
                                      * null-terminator) */

/* Mapping of volatile and static regions in firmware binary */
typedef struct {
  UINT32 offset;            /* offset relative to base */
  UINT32 size;              /* size in bytes */
  UINT8  name[FMAP_STRLEN]; /* descriptive name */
  UINT16 flags;             /* flags for this area */
} __attribute__((packed)) FmapArea;

typedef struct {
  UINT8  signature[8];      /* "__FMAP__" (0x5F5F464D41505F5F) */
  UINT8  ver_major;         /* major version */
  UINT8  ver_minor;         /* minor version */
  UINT64 base;              /* address of the firmware binary */
  UINT32 size;              /* size of firmware binary in bytes */
  UINT8  name[FMAP_STRLEN]; /* name of this firmware binary */
  UINT16 nareas;            /* number of areas described by fmap_areas[] below */
  FmapArea areas[];
} __attribute__((packed)) Fmap;

/*
 * FmapFind - find FMAP signature in a binary Image
 *
 * @Image:  binary Image
 * @Len:  length of binary Image
 *
 * This function does no error checking. The caller is responsible for
 * verifying that the contents are sane.
 *
 * returns offset of FMAP signature to indicate success
 * returns <0 to indicate failure
 */
INTN
EFIAPI
FmapFind (
  CONST UINT8 *Image,
  UINTN Len
  );

/*
 * fmap_size - returns size of fmap data structure (including areas)
 *
 * @Map:  fmap
 *
 * returns size of fmap structure if successful
 * returns <0 to indicate failure
 */
INTN
EFIAPI
FmapSize (
  IN CONST Fmap *Map
  );

/*
 * FmapFindArea - find an fmap_area entry (by name) and return pointer to it
 *
 * @Map:  fmap structure to parse
 * @Name: name of area to find
 *
 * returns a pointer to the entry in the fmap structure if successful
 * returns NULL to indicate failure or if no matching area entry is found
 */
CONST FmapArea *
EFIAPI
FmapFindArea (
  IN CONST Fmap *Map,
  IN CONST CHAR8 *Name
);

#endif  /* FLASHMAP_LIB_FMAP_H__*/
