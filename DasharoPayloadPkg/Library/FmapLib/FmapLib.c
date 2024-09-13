/** @file
  coreboot flash map data parsing library.

Copyright (c) 2024, coreboot project<BR>
SPDX-License-Identifier: BSD-3-Clause or GPL-2.0-only

**/

#include <Library/FmapLib.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

/* No-op on x86, but still useful to have. */
#define le32toh(x) (x)
#define le16toh(x) (x)

STATIC
BOOLEAN
IsPrint (
  IN CHAR8  Chr
  )
{
  return (Chr > 32 && Chr < 126);
}

/* Make a best-effort assessment if the given fmap is real */
STATIC
UINTN
IsValidFmap (
  IN CONST Fmap *Map
  )
{
  UINTN Idx;

  if (CompareMem (Map, FMAP_SIGNATURE, AsciiStrLen (FMAP_SIGNATURE)) != 0)
    return 0;
  /* strings containing the magic tend to fail here */
  if (Map->ver_major != FMAP_VER_MAJOR)
    return 0;
  /* a basic consistency check: flash should be larger than fmap */
  if (le32toh (Map->size) <
    sizeof (*Map) + le16toh (Map->nareas) * sizeof (FmapArea))
    return 0;

  /* fmap-alikes along binary data tend to fail on having a valid,
   * null-terminated string in the name field.*/
  Idx = 0;
  while (Idx < FMAP_STRLEN) {
    if (Map->name[Idx] == 0)
      break;
    if (!IsPrint (Map->name[Idx]))
      return 0;
    if (Idx == FMAP_STRLEN - 1) {
      /* name is specified to be null terminated single-word string
       * without spaces. We did not break in the 0 test, we know it
       * is a printable spaceless string but we're seeing FMAP_STRLEN
       * symbols, which is one too many.
       */
      return 0;
    }
    Idx++;
  }
  return 1;

}

/* returns size of fmap data structure if successful, <0 to indicate error */
INTN
EFIAPI
FmapSize (
  IN CONST Fmap *Map
  )
{
  if (!Map)
    return -1;

  return sizeof (*Map) + (le16toh (Map->nareas) * sizeof (FmapArea));
}

/* brute force linear search */
STATIC
INTN
FmapLsearch (
  IN CONST UINT8 *Image,
  IN UINTN Len
  )
{
  INTN Offset;
  BOOLEAN FmapFound;

  FmapFound = FALSE;

  for (Offset = 0; Offset < Len - sizeof (Fmap); Offset++) {
    if (IsValidFmap ((CONST Fmap *) &Image[Offset])) {
      FmapFound = 1;
      break;
    }
  }

  if (!FmapFound)
    return -1;

  if (Offset + FmapSize ((CONST Fmap *) &Image[Offset]) > Len)
    return -1;

  return Offset;
}

/* if Image length is a power of 2, use binary search */
STATIC
INTN
FmapBsearch (
  IN CONST UINT8 *Image,
  IN UINTN Len
  )
{
  INTN Offset;
  UINTN Stride;
  BOOLEAN FmapFound;

  FmapFound = FALSE;

  /*
   * For efficient operation, we start with the largest Stride possible
   * and then decrease the Stride on each iteration. Also, check for a
   * remainder when modding the Offset with the previous Stride. This
   * makes it so that each Offset is only checked once.
   */
  for (Stride = Len / 2; Stride >= 16; Stride /= 2) {
    if (FmapFound)
      break;

    for (Offset = 0; Offset < Len - sizeof (Fmap); Offset += Stride) {
      if ((Offset % (Stride * 2) == 0) && (Offset != 0))
          continue;
      if (IsValidFmap ((CONST Fmap *) &Image[Offset])) {
        FmapFound = 1;
        break;
      }
    }
  }

  if (!FmapFound)
    return -1;

  if (Offset + FmapSize ((CONST Fmap *) &Image[Offset]) > Len)
    return -1;

  return Offset;
}

STATIC
BOOLEAN
IsPowerOf2 (
  UINTN Number
)
{
  return Number != 0 && (Number & (Number - 1)) == 0;
}

INTN
EFIAPI
FmapFind (
  CONST UINT8 *Image,
  UINTN Len
  )
{
  if ((Image == NULL) || (Len == 0))
    return -1;

  if (IsPowerOf2 (Len))
    return FmapBsearch (Image, Len);

  return FmapLsearch (Image, Len);
}

CONST FmapArea *
EFIAPI
FmapFindArea (
  IN CONST Fmap *Map,
  IN CONST CHAR8 *Name
)
{
  UINTN Idx;

  if (!Map || !Name)
    return NULL;

  for (Idx = 0; Idx < le16toh (Map->nareas); Idx++) {
    if (AsciiStrCmp ((CONST CHAR8 *) Map->areas[Idx].name, Name) == 0) {
      return &Map->areas[Idx];
    }
  }

  return NULL;
}
