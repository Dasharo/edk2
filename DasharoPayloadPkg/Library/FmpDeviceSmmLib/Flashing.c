#include "Flashing.h"

#include <Library/DebugLib.h>
#include <Library/FmpDeviceLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/SmmStoreLib.h>

/**
  Read current firmware in full and return as newly allocated pool memory.

  @return NULL  On error.
**/
VOID *
EFIAPI
ReadCurrentFirmware (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT8       *Image;
  UINTN       FwSize;
  UINTN       Block;
  UINTN       BlockSize;
  UINTN       NumBytes;

  Status = SmmStoreLibGetBlockSize (&BlockSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): SmmStoreLibGetBlockSize() failed with: %r\n",
      __FUNCTION__,
      Status
      ));
    return NULL;
  }

  Status = FmpDeviceGetSize (&FwSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): FmpDeviceGetSize() failed with: %r\n",
      __FUNCTION__,
      Status
      ));
    return NULL;
  }

  Image = AllocatePool (FwSize);
  if (Image == NULL) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to allocate current image buffer\n",
      __FUNCTION__
      ));
    return NULL;
  }

  for (Block = 0; Block < FwSize / BlockSize; Block++) {
    NumBytes = BlockSize;
    Status = SmmStoreLibReadAnyBlock (
               Block,
               0,
               &NumBytes,
               Image + Block * BlockSize
               );
    if (EFI_ERROR (Status) || NumBytes != BlockSize) {
      DEBUG ((
        DEBUG_ERROR,
        "%a(): read %d out of %d bytes of flash at 0x%x (%r)\n",
        __FUNCTION__,
        NumBytes,
        BlockSize,
        Block * BlockSize,
        Status
        ));
      FreePool (Image);
      return NULL;
    }
  }

  return Image;
}

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
  )
{
  EFI_STATUS  Status;
  UINT8       *Merged;
  UINTN       FwSize;

  Status = FmpDeviceGetSize (&FwSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): FmpDeviceGetSize() failed with: %r\n",
      __FUNCTION__,
      Status
      ));
    return NULL;
  }

  Merged = AllocateCopyPool (FwSize, New);
  if (Merged == NULL) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to allocate merged image buffer\n",
      __FUNCTION__
      ));
    return NULL;
  }

  // TODO: perform data migration here.

  return Merged;
}
