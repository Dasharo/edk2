#include "Flashing.h"

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/FmapLib.h>
#include <Library/FmpDeviceLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/SmmStoreLib.h>

typedef enum {
  REGION_MIGRATED,
  REGION_NOT_IN_SRC,
  REGION_NOT_IN_DST,
  REGION_OF_DIFFERENT_SIZE,
} RegionMigrationStatus;

typedef struct {
  CONST UINT8  *Current;
  UINT8        *Updated;
  CONST Fmap   *CurrentFmap;
  CONST Fmap   *UpdatedFmap;
  UINTN        FwSize;
} MigrationData;

STATIC
BOOLEAN
GetFmap (
  IN  CONST UINT8  *Image,
  IN  UINTN        Size,
  OUT CONST Fmap   **Map
  )
{
  INTN            FmapOffset;
  CONST FmapArea  *FmapRegion;

  FmapOffset = FmapFind (Image, Size);
  if (FmapOffset < 0) {
    DEBUG ((DEBUG_ERROR, "%a(): failed to find FMAP\n", __FUNCTION__));
    *Map = NULL;
    return FALSE;
  }

  *Map = (CONST Fmap *) (Image + FmapOffset);

  if ((*Map)->size > Size) {
    DEBUG ((DEBUG_ERROR, "%a(): FMAP is larger than firmware\n", __FUNCTION__));
    return FALSE;
  }

  FmapRegion = FmapFindArea (*Map, "FMAP");
  if (FmapRegion == NULL) {
    DEBUG ((DEBUG_ERROR, "%a(): FMAP doesn't describe itself\n", __FUNCTION__));
    return FALSE;
  }

  if (FmapRegion->offset != FmapOffset) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): wrong FMAP offset (expected: 0x%x, actual: 0x%x)\n",
      __FUNCTION__,
      FmapRegion->offset,
      FmapOffset
      ));
    return FALSE;
  }

  return TRUE;
}

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
  Migrates a flash map region from current firmware to a new one.

  @param[in] RegionName  Name of the region to migrate.
  @param[in] Data        New image which gets patched.

  @return RegionMigrationStatus  Status which might not be considered an error
                                 depending on the region.
**/
STATIC
RegionMigrationStatus
MigrateRegion (
  IN CONST CHAR8          *RegionName,
  IN CONST MigrationData  *Data
  )
{
  CONST FmapArea  *CurrentRegion;
  CONST FmapArea  *UpdatedRegion;

  CurrentRegion = FmapFindArea (Data->CurrentFmap, RegionName);
  if (CurrentRegion == NULL) {
    DEBUG ((
      DEBUG_WARN,
      "%a(): failed to find %a region in current firmware\n",
      __FUNCTION__,
      RegionName
      ));
    return REGION_NOT_IN_SRC;
  }

  UpdatedRegion = FmapFindArea (Data->UpdatedFmap, RegionName);
  if (UpdatedRegion == NULL) {
    DEBUG ((
      DEBUG_WARN,
      "%a(): failed to find %a region in new firmware\n",
      __FUNCTION__,
      RegionName
      ));
    return REGION_NOT_IN_DST;
  }

  if (CurrentRegion->size != UpdatedRegion->size) {
    DEBUG ((
      DEBUG_WARN,
      "%a(): %a regions don't match in size (current: 0x%x, updated: 0x%x)\n",
      __FUNCTION__,
      RegionName,
      CurrentRegion->size,
      UpdatedRegion->size
      ));
    return REGION_OF_DIFFERENT_SIZE;
  }

  CopyMem (
    Data->Updated + UpdatedRegion->offset,
    Data->Current + CurrentRegion->offset,
    UpdatedRegion->size
    );

  return REGION_MIGRATED;
}

STATIC
BOOLEAN
MigrateVariables (
  IN CONST MigrationData  *Data
  )
{
  RegionMigrationStatus  Status;

  Status = MigrateRegion ("SMMSTORE", Data);

  return Status == REGION_MIGRATED
      || Status == REGION_NOT_IN_DST;
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
  EFI_STATUS     Status;
  MigrationData  Data;

  Data.Current = Current;

  //
  // The assumption is that current firmware image contains all the interesting
  // data, i.e. if there was anything that needed to be flushed, it was flushed
  // before the snapshot was taken.
  //

  Status = FmpDeviceGetSize (&Data.FwSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): FmpDeviceGetSize() failed with: %r\n",
      __FUNCTION__,
      Status
      ));
    return NULL;
  }

  Data.Updated = AllocateCopyPool (Data.FwSize, New);
  if (Data.Updated == NULL) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to allocate merged image buffer\n",
      __FUNCTION__
      ));
    return NULL;
  }

  if (!GetFmap (Data.Current, Data.FwSize, &Data.CurrentFmap)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to parse current firmware\n",
      __FUNCTION__
      ));
    goto Fail;
  }

  if (!GetFmap (Data.Updated, Data.FwSize, &Data.UpdatedFmap)) {
    DEBUG ((
      DEBUG_WARN,
      "%a(): failed to parse updated firmware\n",
      __FUNCTION__
      ));
    if (Data.UpdatedFmap == NULL) {
      //
      // Not a hard error.  It's conceivable that for a capsule to be used to
      // flash non-coreboot firmware.
      //
      return Data.Updated;
    }
    goto Fail;
  }

  if (!MigrateVariables (&Data)) {
    DEBUG ((DEBUG_ERROR, "%a(): MigrateVariables() failed\n", __FUNCTION__));
    goto Fail;
  }

  return Data.Updated;

Fail:
  FreePool (Data.Updated);
  return NULL;
}
