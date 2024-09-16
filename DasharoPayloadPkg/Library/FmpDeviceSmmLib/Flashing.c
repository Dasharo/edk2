#include "Flashing.h"

#include <Library/BaseMemoryLib.h>
#include <Library/CbfsLib.h>
#include <Library/DebugLib.h>
#include <Library/FmapLib.h>
#include <Library/FmpDeviceLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/SmmStoreLib.h>

typedef enum {
  REGION_MIGRATED,
  REGION_NOT_IN_SRC,
  REGION_NOT_IN_DST,
  REGION_AT_DIFFERENT_OFFSET,
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

  @param[in] RegionName       Name of the region to migrate.
  @param[in] Data             New image which gets patched.
  @param[in] OffsetSensitive  Whether mismatched offset is a fatal error.

  @return RegionMigrationStatus  Status which might not be considered an error
                                 depending on the region.
**/
STATIC
RegionMigrationStatus
MigrateRegion (
  IN CONST CHAR8          *RegionName,
  IN CONST MigrationData  *Data,
  IN BOOLEAN              OffsetSensitive
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

  if (OffsetSensitive && CurrentRegion->offset != UpdatedRegion->offset) {
    DEBUG ((
      DEBUG_WARN,
      "%a(): %a regions' offsets don't match (current: 0x%x, updated: 0x%x)\n",
      __FUNCTION__,
      RegionName,
      CurrentRegion->offset,
      UpdatedRegion->offset
      ));
    return REGION_AT_DIFFERENT_OFFSET;
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

  Status = MigrateRegion ("SMMSTORE", Data, FALSE);

  return Status == REGION_MIGRATED
      || Status == REGION_NOT_IN_DST;
}

STATIC
BOOLEAN
MigrateRomhole (
  IN CONST MigrationData  *Data
  )
{
  RegionMigrationStatus  Status;

  Status = MigrateRegion ("ROMHOLE", Data, TRUE);

  return Status == REGION_MIGRATED
      || Status == REGION_NOT_IN_SRC
      || Status == REGION_NOT_IN_DST;
}

STATIC
BOOLEAN
MigrateBootLogo (
  IN CONST MigrationData  *Data
  )
{
  RegionMigrationStatus  Status;

  Status = MigrateRegion ("BOOTSPLASH", Data, FALSE);

  return Status == REGION_MIGRATED
      || Status == REGION_NOT_IN_SRC
      || Status == REGION_NOT_IN_DST;
}

STATIC
BOOLEAN
MigrateFile (
  CONST CHAR8        *Name,
  struct cbfs_image  *CurrentCbfs,
  struct cbfs_image  *UpdatedCbfs
  )
{
  struct buffer     Buf;
  struct cbfs_file  *Entry;
  struct cbfs_file  *Header;

  Entry = cbfs_get_entry (CurrentCbfs, Name);
  if (Entry == NULL) {
    DEBUG ((
      DEBUG_WARN,
      "%a(): failed to find %a in current CBFS\n",
      __FUNCTION__,
      Name
      ));
    return FALSE;
  }

  buffer_init (
    &Buf,
    Entry->filename,
    CBFS_SUBHEADER (Entry),
    ntohl (Entry->len)
    );

  Header = cbfs_create_file_header (ntohl (Entry->type), Buf.size, Name);
  if (Header == NULL) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to allocate header for %a file\n",
      __FUNCTION__,
      Name
      ));
    return FALSE;
  }

  //
  // An unlikely situation, but won't hurt to handle it.
  //
  if (cbfs_get_entry (UpdatedCbfs, Name) != NULL) {
    DEBUG ((
      DEBUG_INFO,
      "%a(): found %a in updated CBFS\n",
      __FUNCTION__,
      Name
      ));
    if (cbfs_remove_entry (UpdatedCbfs, Name) != 0) {
      DEBUG ((
        DEBUG_ERROR,
        "%a(): failed to remove %a from updated CBFS\n",
        __FUNCTION__,
        Name
        ));
      return FALSE;
    }
  }

  if (cbfs_add_entry (UpdatedCbfs, &Buf, 0, Header, 0) != 0) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to add %a to updated CBFS\n",
      __FUNCTION__,
      Name
      ));
    FreePool (Header);
    return FALSE;
  }

  FreePool (Header);
  return TRUE;
}

STATIC
BOOLEAN
GetCbfs (
  IN CONST UINT8         *Image,
  IN CONST Fmap          *Fmap,
  OUT struct cbfs_image  *Cbfs
  )
{
  struct buffer   Buf;
  CONST FmapArea  *CbRegion;

  CbRegion = FmapFindArea (Fmap, "COREBOOT");
  if (CbRegion == NULL) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to find COREBOOT region\n",
      __FUNCTION__
      ));
    return FALSE;
  }

  buffer_init (&Buf, NULL, (UINT8 *) Image + CbRegion->offset, CbRegion->size);
  if (cbfs_image_from_buffer (Cbfs, &Buf, ~0U) != 0) {
    DEBUG ((DEBUG_ERROR, "%a(): failed to load CBFS\n", __FUNCTION__));
    return FALSE;
  }

  return TRUE;
}

/**
  Migrates data from current firmware to new image before it's written.

  @param[in] Data  Description of old and new firmware images.

  @return TRUE     On successful migration of data that was found.
  @return FALSE    On error.
**/
STATIC
BOOLEAN
MigrateSmbiosData (
  IN CONST MigrationData  *Data
  )
{
  struct cbfs_image  CurrentCbfs;
  struct cbfs_image  UpdatedCbfs;

  if (!GetCbfs (Data->Current, Data->CurrentFmap, &CurrentCbfs)) {
    DEBUG ((DEBUG_ERROR, "%a(): failed to load current CBFS\n", __FUNCTION__));
    return FALSE;
  }

  if (!GetCbfs (Data->Updated, Data->UpdatedFmap, &UpdatedCbfs)) {
    DEBUG ((DEBUG_ERROR, "%a(): failed to load updated CBFS\n", __FUNCTION__));
    return FALSE;
  }

  //
  // Not considering these errors fatal.
  //
  if (!MigrateFile ("serial_number", &CurrentCbfs, &UpdatedCbfs)) {
    DEBUG ((
      DEBUG_WARN,
      "%a(): failed to migrate 'serial_number' CBFS file\n",
      __FUNCTION__
      ));
  }
  if (!MigrateFile ("system_uuid", &CurrentCbfs, &UpdatedCbfs)) {
    DEBUG ((
      DEBUG_WARN,
      "%a(): failed to migrate 'system_uuid' CBFS file\n",
      __FUNCTION__
      ));
  }

  //
  // TODO: if CONFIG_CBFS_VERIFICATION is on, need to update CBFS hash here
  //       (file can have hashes too, but they seem optional)
  //

  return TRUE;
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

  if (!MigrateRomhole (&Data)) {
    DEBUG ((DEBUG_ERROR, "%a(): MigrateRomhole () failed\n", __FUNCTION__));
    goto Fail;
  }

  if (!MigrateBootLogo (&Data)) {
    DEBUG ((DEBUG_ERROR, "%a(): MigrateBootLogo () failed\n", __FUNCTION__));
    goto Fail;
  }

  if (!MigrateSmbiosData (&Data)) {
    DEBUG ((DEBUG_ERROR, "%a(): MigrateSmbiosData () failed\n", __FUNCTION__));
    goto Fail;
  }

  return Data.Updated;

Fail:
  FreePool (Data.Updated);
  return NULL;
}
