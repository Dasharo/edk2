#ifndef FLASHING_H__
#define FLASHING_H__

/**
  Read current firmware in full and return as newly allocated pool memory.

  @return NULL  On error.
**/
VOID *
EFIAPI
ReadCurrentFirmware (
  BOOLEAN IsDescriptorLocked
  );

/**
  Migrates data from current firmware to new image before it's written.

  @param[in] Current  Current image used as a source of data.
  @param[in] New      New image which gets patched.

  @return NULL  On error.
**/
VOID *
EFIAPI
MergeFirmwareImages (
  IN CONST VOID  *Current,
  IN CONST VOID  *New
  );

/**
  Checks if the given range overlaps a write-protected IFD range.

  @param[in] Image       Firmware image to check
  @param[in] ImageLen    Length of the image
  @param[in] RangeOffset Offset in the image to check
  @param[in] RangeOffset Length of the range to check

  @return TRUE if range is writeable, FALSE otherwise.
**/
BOOLEAN
EFIAPI
IsRangeWriteable (
  IN CONST VOID *Image,
  IN CONST UINTN ImageLen,
  IN CONST UINTN RangeOffset,
  IN CONST UINTN RangeLen
  );

/**
  Checks if the two coreboot images are Boot Guard enabled and are using the
  same OEM root key for signing.

  @param[in] Current     Current firmware image
  @param[in] CurrentLen  Size of current firmware image
  @param[in] Updated     Updated firmware image
  @param[in] UpdatedLen  Size of updated firmware image

  @return TRUE     If the two images are compatible with each other
  @return FALSE    If the two images are incompatiblE
**/
BOOLEAN
EFIAPI
AreImageBtgKeysCompatible (
  IN CONST VOID  *Current,
  IN CONST VOID  *Updated,
  IN CONST UINTN ImageSize
  );

#endif // FLASHING_H__