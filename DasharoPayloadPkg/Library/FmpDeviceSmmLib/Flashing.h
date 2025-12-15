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
  Checks if the Intel Flash Descriptor is locked. A locked descriptor means that
  certain regions of the SPI flash are locked.

  @return TRUE     If the descriptor is locked
  @return FALSE    If the descriptor is unlocked
**/
BOOLEAN
EFIAPI
IsDescriptorLocked (
  VOID
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
  Retrieves the SHA-384 hash of the OEM Root Key from a Coreboot image.

  This function parses the input firmware image to locate the FMAP and CBFS.
  It searches for the 'key_manifest.bin' file, extracts the OEM Public Key
  (BtgPubKey), and computes a SHA-384 hash.

  The hash is calculated over the concatenation of the Key Modulus followed
  immediately by the Key Exponent (Hash = SHA384(Modulus || Exponent)).

  @param[in]  Image           Pointer to the start of the Coreboot firmware image.
  @param[in]  ImageLen        The size of the firmware image in bytes.
  @param[out] OemRootKeyHash  Double pointer to retrieve the allocated hash buffer.
                              On EFI_SUCCESS, this will point to a 48-byte buffer
                              containing the SHA-384 digest.
                              The caller is responsible for freeing this buffer
                              using FreePool().

  @retval EFI_SUCCESS             The hash was successfully retrieved and memory allocated.
  @retval EFI_INVALID_PARAMETER   The extracted key length is invalid or too short to contain the struct.
  @retval EFI_NOT_FOUND           FMAP, CBFS, or 'key_manifest.bin' could not be found.
  @retval EFI_OUT_OF_RESOURCES    Could not allocate memory for the hash output or temporary buffers.
  @retval EFI_ABORTED             Cryptographic hash calculation failed.
**/
EFI_STATUS
EFIAPI
GetOemRootKeyHash (
  IN CONST VOID *Image,
  IN UINTN ImageLen,
  IN OUT UINT8 **OemRootKeyHash
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
