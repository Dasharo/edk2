#include "Flashing.h"

#include <IndustryStandard/SmBios.h>
#include <Library/BaseCryptLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CbfsLib.h>
#include <Library/DebugLib.h>
#include <Library/EfiVarsLib.h>
#include <Library/FmapLib.h>
#include <Library/FmpDeviceLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PciLib.h>
#include <Library/PrintLib.h>
#include <Library/SmmStoreLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/Smbios.h>

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
  BOOLEAN      TopSwapActive;
} MigrationData;

typedef struct {
  CONST UINT16  Alg;
  CONST UINT16  Size;
  CONST UINT8   Data[];
} __attribute__((packed)) HashStruct;

typedef struct km_hash {
  CONST UINT64      Usage;
  CONST HashStruct  Hash;
} __attribute__((packed)) KmHash;

typedef struct {
  CONST UINT64  StructureId;
  CONST UINT8   StructVersion;
  CONST UINT8   Reserved1[3];
  CONST UINT16  KeySignatureOffset;
  CONST UINT8   Reserved2[3];
  CONST UINT8   KeyManifestRevision;
  CONST UINT8   KmSvn;
  CONST UINT8   KeyManifestId;
  CONST UINT16  KmPubKeyHashAlg;
  CONST UINT16  KeyCount;
  CONST KmHash  KeyHash[];
} __attribute__((packed)) KeyManifestHeader;

typedef struct {
  CONST UINT8   Version;
  CONST UINT16  KeySize;
  CONST UINT32  Exponent;
  CONST UINT8   Modulus[];
} __attribute__((packed)) BtgPubKey;

typedef struct {
  CONST UINT8   Version;
  CONST UINT16  KeyAlg;
} __attribute__((packed)) KeyAndSigHeader;

typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT16  Handle;
} __attribute__((packed)) SmbiosHeader;

typedef struct {
  UINT8   HeciName;
  UINT32  Reg[6];
} __attribute__((packed)) FwstsRecord;

typedef struct {
  SmbiosHeader  Header;
  UINT8         Version;
  UINT8         Count;
  FwstsRecord   Record;
  // We only care about the first record and we're not sure how many there are
  //FwstsRecord  Record[CONFIG_MAX_MEI_DEVICES];
  //UINT8        Eos[2];
} __attribute__((packed)) FwstsSmbiosTable;

STATIC
UINT32
TranslateOffsetForTopSwap (
  IN CONST Fmap  *Map,
  IN UINT32      Offset,
  IN BOOLEAN     TopSwapActive
  )
{
  CONST FmapArea  *Bootblock;
  CONST FmapArea  *TopSwap;

  if (!TopSwapActive) {
    return Offset;
  }

  //
  // Intel Top Swap swaps only the two topmost windows. In this layout those are
  // BOOTBLOCK and TOPSWAP. FMAP offsets are physical, but a full flash dump taken
  // while TS is active reflects the swapped view in those windows, so translate
  // the source offsets accordingly.
  //
  Bootblock = FmapFindArea (Map, "BOOTBLOCK");
  TopSwap   = FmapFindArea (Map, "TOPSWAP");
  if ((Bootblock == NULL) || (TopSwap == NULL)) {
    return Offset;
  }

  if (Bootblock->size != TopSwap->size) {
    return Offset;
  }

  if ((Offset >= Bootblock->offset) && (Offset < Bootblock->offset + Bootblock->size)) {
    return (UINT32) (TopSwap->offset + (Offset - Bootblock->offset));
  }

  if ((Offset >= TopSwap->offset) && (Offset < TopSwap->offset + TopSwap->size)) {
    return (UINT32) (Bootblock->offset + (Offset - TopSwap->offset));
  }

  return Offset;
}

STATIC
BOOLEAN
IsTopSwapActive (
  VOID
  )
{
  //
  // Intel Top Swap status via PCH BIOS Control (0:1f.0, offset 0xDC).
  // Bit 4 indicates whether Top Swap is currently active.
  //
  UINT8  BiosCntl;

  BiosCntl = PciRead8 (PCI_LIB_ADDRESS (0, 31, 0, 0xDC));
  return (BiosCntl & (1u << 4)) != 0;
}

STATIC
BOOLEAN
GetFmap (
  IN  CONST UINT8  *Image,
  IN  UINTN        Size,
  OUT CONST Fmap   **Map,
  IN  BOOLEAN      TopSwapActive
  )
{
  INTN            FmapOffset;
  CONST FmapArea  *FmapRegion;
  UINT32          ExpectedOffset;

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

  ExpectedOffset = TranslateOffsetForTopSwap (*Map, FmapRegion->offset, TopSwapActive);
  if (ExpectedOffset != FmapOffset) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): wrong FMAP offset (expected: 0x%x, actual: 0x%x)\n",
      __FUNCTION__,
      ExpectedOffset,
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
  BOOLEAN IsDescriptorLocked
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

      // We allow for this failure if descriptor is locked because when locked,
      // certain regions of the flash are read-protected. We can't know which
      // ones they are until we've read enough of the firmware to get to the
      // FMAP.
      if (!IsDescriptorLocked) {
        FreePool (Image);
        return NULL;
      }
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
  UINT32          CurrentOffset;
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

  CurrentOffset = TranslateOffsetForTopSwap (Data->CurrentFmap, CurrentRegion->offset, Data->TopSwapActive);

  CopyMem (
    Data->Updated + UpdatedRegion->offset,
    Data->Current + CurrentOffset,
    UpdatedRegion->size
    );

  return REGION_MIGRATED;
}

STATIC
EFI_STATUS
CopyVariable (
  IN OUT EfiVars         *Storage,
  IN     CHAR16          *VarName,
  IN     CONST EFI_GUID  *Vendor
  )
{
  EFI_STATUS  Status;
  VOID        *VarData;
  UINTN       NameSize;
  UINTN       VarSize;
  EfiVar      *Var;
  UINT32      Attrs;

  NameSize = StrSize (VarName);

  VarName = AllocateCopyPool (NameSize, VarName);
  if (VarName == NULL) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to clone EFI variable name: %g-%s\n",
      __FUNCTION__,
      Vendor,
      VarName
      ));
    return EFI_OUT_OF_RESOURCES;
  }

  Status = GetVariable3 (VarName, Vendor, &VarData, &VarSize, &Attrs);
  if (EFI_ERROR (Status)) {
    FreePool (VarName);
    return Status;
  }

  if (!(Attrs & EFI_VARIABLE_NON_VOLATILE)) {
    FreePool (VarName);
    FreePool (VarData);
    return EFI_SUCCESS;
  }

  Var = EfiVarsCreateVar (Storage);
  if (Var == NULL) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to allocate EFI variable structure for %g-%s\n",
      __FUNCTION__,
      Vendor,
      VarName
      ));
    FreePool (VarName);
    FreePool (VarData);
    return EFI_OUT_OF_RESOURCES;
  }

  Var->Name     = VarName;
  Var->NameSize = NameSize;
  Var->Data     = VarData;
  Var->DataSize = VarSize;
  Var->Guid     = *Vendor;
  Var->Attrs    = Attrs;

  return Status;
}

STATIC
EFI_STATUS
CopyVariables (
  IN OUT EfiVars  *Storage
  )
{
  EFI_STATUS  Status;
  CHAR16      *Name;
  CHAR16      *NewBuf;
  UINTN       MaxNameSize;
  UINTN       NameSize;
  EFI_GUID    Guid;

  MaxNameSize = 32 * sizeof (CHAR16);
  Name = AllocateZeroPool (MaxNameSize);
  if (Name == NULL)
    return EFI_OUT_OF_RESOURCES;

  while (TRUE) {
    NameSize = MaxNameSize;
    Status = gRT->GetNextVariableName (&NameSize, Name, &Guid);
    if (Status == EFI_BUFFER_TOO_SMALL) {
      NewBuf = AllocatePool (NameSize);
      if (NewBuf == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        break;
      }

      StrnCpyS (
        NewBuf,
        NameSize / sizeof (CHAR16),
        Name,
        MaxNameSize / sizeof (CHAR16)
        );
      FreePool (Name);

      Name = NewBuf;
      MaxNameSize = NameSize;

      Status = gRT->GetNextVariableName (&NameSize, Name, &Guid);
    }

    if (Status == EFI_NOT_FOUND) {
      Status = EFI_SUCCESS;
      break;
    }

    if (EFI_ERROR (Status))
      break;

    CopyVariable (Storage, Name, &Guid);
  }

  FreePool (Name);
  return Status;
}

STATIC
BOOLEAN
MigrateVariables (
  IN CONST MigrationData  *Data
  )
{
  EFI_STATUS      Status;
  CONST FmapArea  *UpdatedRegion;
  MemRange        Fv;
  EfiVars         Storage;

  UpdatedRegion = FmapFindArea (Data->UpdatedFmap, "SMMSTORE");
  if (UpdatedRegion == NULL) {
    DEBUG ((
      DEBUG_WARN,
      "%a(): failed to find SMMSTORE in updated firmware\n",
      __FUNCTION__
      ));
    return TRUE;
  }

  Fv.Start = Data->Updated + UpdatedRegion->offset;
  Fv.Length = UpdatedRegion->size;

  if (!EfiVarsInit (Fv, &Storage)) {
    DEBUG ((
      DEBUG_WARN,
      "%a(): failed to open SMMSTORE in updated firmware\n",
      __FUNCTION__
      ));
    return TRUE;
  }

  Status = CopyVariables (&Storage);
  if (EFI_ERROR (Status)) {
    EfiVarsFree (&Storage);
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to copy EFI variables to updated firmware: %r\n",
      __FUNCTION__,
      Status
      ));
    return TRUE;
  }

  if (!EfiVarsWrite (&Storage)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to write SMMSTORE to updated firmware\n",
      __FUNCTION__
      ));
    EfiVarsFree (&Storage);
    return FALSE;
  }

  EfiVarsFree (&Storage);
  return TRUE;
}

STATIC
BOOLEAN
MigrateBOOTBLOCK (
  IN CONST MigrationData  *Data
  )
{
  RegionMigrationStatus  Status;

  DEBUG ((
    DEBUG_INFO,
    "%a(): Entered, will try to migrate BOOTBLOCK\n",
    __FUNCTION__
    ));

  Status = MigrateRegion ("BOOTBLOCK", Data, TRUE);

  return Status == REGION_MIGRATED
      || Status == REGION_NOT_IN_SRC
      || Status == REGION_NOT_IN_DST;
}

STATIC
BOOLEAN
MigrateCOREBOOT (
  IN CONST MigrationData  *Data
  )
{
  RegionMigrationStatus  Status;

  DEBUG ((
    DEBUG_INFO,
    "%a(): Entered, will try to migrate COREBOOT\n",
    __FUNCTION__
    ));

  Status = MigrateRegion ("COREBOOT", Data, TRUE);

  return Status == REGION_MIGRATED;
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
MigrateGbeRegion (
  IN CONST MigrationData  *Data
  )
{
  RegionMigrationStatus  Status;

  Status = MigrateRegion ("SI_GBE", Data, TRUE);

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
  OUT struct cbfs_image  *Cbfs,
  IN CONST CHAR8         *CbfsRegionName
  )
{
  struct buffer   Buf;
  CONST FmapArea  *CbRegion;

  CbRegion = FmapFindArea (Fmap, CbfsRegionName);
  if (CbRegion == NULL) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to find %a region\n",
      __FUNCTION__,
      CbfsRegionName
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
  CONST CHAR8        *CurrentCbfsRegion;
  CONST CHAR8        *UpdatedCbfsRegion;

  //
  // Only BOOTBLOCK and TOPSWAP are hardware-swapped. COREBOOT and COREBOOT_TS
  // are separate physical regions, with the boot selection based on the TS bit.
  //
  CurrentCbfsRegion = Data->TopSwapActive ? "COREBOOT_TS" : "COREBOOT";

  //
  // Slot B is the update target. Prefer COREBOOT_TS in the updated image,
  // fall back to COREBOOT if COREBOOT_TS does not exist.
  //
  UpdatedCbfsRegion = "COREBOOT_TS";
  if (FmapFindArea (Data->UpdatedFmap, UpdatedCbfsRegion) == NULL) {
    UpdatedCbfsRegion = "COREBOOT";
  }

  if (!GetCbfs (Data->Current, Data->CurrentFmap, &CurrentCbfs, CurrentCbfsRegion)) {
    DEBUG ((DEBUG_ERROR, "%a(): failed to load current CBFS\n", __FUNCTION__));
    return FALSE;
  }

  if (!GetCbfs (Data->Updated, Data->UpdatedFmap, &UpdatedCbfs, UpdatedCbfsRegion)) {
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
  Checks if the given range is writable according to Intel Flash Descriptor
  access restrictions.

  @param[in] Image       Firmware image to check
  @param[in] ImageLen    Length of the image
  @param[in] RangeOffset Offset in the image to check
  @param[in] RangeLen    Length of the range to check

  @return TRUE if range is writeable, FALSE otherwise.
**/
BOOLEAN
EFIAPI
IsRangeWriteable (
  IN CONST VOID *Image,
  IN CONST UINTN ImageLen,
  IN CONST UINTN RangeOffset,
  IN CONST UINTN RangeLen
  )
{
  CONST Fmap     *FlashMap;
  CONST FmapArea *Region;

  // Reject out-of-bounds upfront
  if (RangeOffset >= ImageLen || RangeLen > ImageLen - RangeOffset)
    return FALSE;

  // Something went wrong, assume nothing is writable. Update won't touch anything.
  if (!GetFmap (Image, ImageLen, &FlashMap, IsTopSwapActive ())) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to parse current firmware\n",
      __FUNCTION__
      ));
    return FALSE;
  }

  // If descriptor is locked, IFD, ME, GbE and DevExp2 regions are not
  // writeable. This is at least true for Dasharo firmware and TrustRoot
  // provisioning scripts.
  Region = FmapFindArea(FlashMap, "SI_DESC");

  // There is no descriptor, so everything should be unlocked.
  if (!Region)
    return TRUE;

  // Range overlaps locked descriptor.
  if (RangeOffset + RangeLen > Region->offset &&
      Region->offset + Region->size > RangeOffset)
    return FALSE;

  Region = FmapFindArea(FlashMap, "SI_ME");

  // Range exists and overlaps locked ME.
  if (Region && RangeOffset + RangeLen > Region->offset &&
      Region->offset + Region->size > RangeOffset)
    return FALSE;

  Region = FmapFindArea(FlashMap, "RW_UNUSED");

  // Range exists and overlaps locked RW_UNUSED.
  if (Region && RangeOffset + RangeLen > Region->offset &&
      Region->offset + Region->size > RangeOffset)
    return FALSE;

  Region = FmapFindArea(FlashMap, "SI_GBE");

  // While SI_GBE is not typically locked, its size is smaller than the SMMSTORE
  // erase/write size, and it sits between two locked regions. Explicitly avoid
  // touching it to reduce ambiguity.
  if (Region && RangeOffset + RangeLen > Region->offset &&
      Region->offset + Region->size > RangeOffset)
    return FALSE;

  // We're only adding a TOPSWAP region if we're using TOP_SWAP_REDUNDANCY.
  // I think it can be reliably used to gate locking the Slot A regions with
  // less code than using a new PCD, but also with less flexibility
  Region = FmapFindArea(FlashMap, "TOPSWAP");
  if (Region) {
    // The regions BOOTBLOCK and COREBOOT are to remain read-only golden copies
    // of the firmware if we're using TOP_SWAP_REDUNDANCY
    Region = FmapFindArea(FlashMap, "BOOTBLOCK");

    // Range exists and overlaps locked BOOTBLOCK.
    if (Region && RangeOffset + RangeLen > Region->offset &&
        Region->offset + Region->size > RangeOffset){
      return FALSE;
    }

    Region = FmapFindArea(FlashMap, "COREBOOT");

    // Range exists and overlaps locked COREBOOT.
    if (Region && RangeOffset + RangeLen > Region->offset &&
        Region->offset + Region->size > RangeOffset){
      return FALSE;
    }
  }

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

  Data.TopSwapActive = IsTopSwapActive ();

  if (!GetFmap (Data.Current, Data.FwSize, &Data.CurrentFmap, Data.TopSwapActive)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to parse current firmware\n",
      __FUNCTION__
      ));
    goto Fail;
  }

  if (!GetFmap (Data.Updated, Data.FwSize, &Data.UpdatedFmap, FALSE)) {
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

  if (!MigrateGbeRegion (&Data)) {
    DEBUG ((DEBUG_ERROR, "%a(): MigrateGbeRegion () failed\n", __FUNCTION__));
    goto Fail;
  }

  if (!MigrateSmbiosData (&Data)) {
    DEBUG ((DEBUG_ERROR, "%a(): MigrateSmbiosData () failed\n", __FUNCTION__));
    goto Fail;
  }

  if (!MigrateBOOTBLOCK (&Data)) {
    DEBUG ((DEBUG_ERROR, "%a(): MigrateBOOTBLOCK () failed\n", __FUNCTION__));
    goto Fail;
  }

  if (!MigrateCOREBOOT (&Data)) {
    DEBUG ((DEBUG_ERROR, "%a(): MigrateCOREBOOT () failed\n", __FUNCTION__));
    goto Fail;
  }

  return Data.Updated;

Fail:
  FreePool (Data.Updated);
  return NULL;
}

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
  )
{
  EFI_STATUS     Status;
  UINTN          VarSize;
  UINT8          DescriptorWriteable;

  VarSize = sizeof (DescriptorWriteable);
  Status = gRT->GetVariable (
      L"DescriptorWriteable",
      &gDasharoSystemFeaturesGuid,
      NULL,
      &VarSize,
      &DescriptorWriteable
      );

  // Variable does not exist on platforms without a descriptor (e.g. AMD)
  if (EFI_ERROR(Status))
    return FALSE;

  return !DescriptorWriteable;
}

/**
  Extracts the BtgPubKey struct from a Key Manifest buffer.

  @param[in]  Km             Key Manifest to parse
  @param[in]  ImageLen       Size of the Key Manifest to check
  @param[out] OemRootKey     The extracted OemRootKey
  @param[out] OemRootKeySize Size of the extracted OemRootKey

  @return EFI_INVALID_PARAMETER    If the input parameters or values in the Key
                                   Manifest are invalid
  @return EFI_SUCCESS              If extraction was successful
**/
STATIC
EFI_STATUS
GetOemRootKeyFromKm (
  IN CONST VOID     *KmBuffer,
  IN CONST UINTN    KmSize,
  IN OUT BtgPubKey  **OemRootKey,
  OUT UINTN         *OemRootKeySize
  )
{
  BtgPubKey *PubKey;
  KeyAndSigHeader *PubKeyHeader;
  KeyManifestHeader *Km = (KeyManifestHeader*) KmBuffer;

  if ((Km == NULL) ||
      (OemRootKey == NULL) ||
      (OemRootKeySize == NULL) ||
      (KmSize < sizeof(KeyManifestHeader)) ||
      (KmSize < Km->KeySignatureOffset + sizeof(KeyAndSigHeader) + sizeof(BtgPubKey)) ||
      (Km->StructureId != 0x5F5F4D59454B5F5F)) // __KEYM__
    return EFI_INVALID_PARAMETER;

  PubKeyHeader = (KeyAndSigHeader*)((UINT8*)Km + Km->KeySignatureOffset);
  PubKey = (BtgPubKey*)((UINT8*)Km + Km->KeySignatureOffset + sizeof(KeyAndSigHeader));

  if (PubKeyHeader->KeyAlg != 0x1) // RSA
    return EFI_INVALID_PARAMETER;

  *OemRootKeySize = sizeof(BtgPubKey) + PubKey->KeySize / 8;
  *OemRootKey = PubKey;

  return EFI_SUCCESS;
}

/**
  Checks if an image has Boot Guard enabled, by checking is KM and BPM are
  present in the image.

  @param[in] Image     Image to check
  @param[in] ImageLen  Size of the image to check

  @return TRUE    If Boot Guard BPM and KM are present
  @return FALSE   If Boot Guard BPM and KM are absent
**/
STATIC
BOOLEAN
IsBootGuardEnabled (
  IN CONST VOID  *Image,
  IN CONST UINTN ImageLen
  )
{
  EFI_STATUS         Status;
  struct cbfs_file   *File;
  struct cbfs_image  Cbfs;
  CONST Fmap         *FlashMap;
  BtgPubKey          *OemKey;
  UINTN              OemKeyLen;

  if (!GetFmap (Image, ImageLen, &FlashMap, IsTopSwapActive ())) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to parse firmware\n",
      __FUNCTION__
      ));
    return FALSE;
  }

  // Reading from the Slot A partition for simplicity and compatibility.
  // If Boot Guard is enabled, both slots must contain the manifest to work.
  if (!GetCbfs (Image, FlashMap, &Cbfs, "COREBOOT")) {
    DEBUG ((DEBUG_ERROR, "%a(): failed to load CBFS\n", __FUNCTION__));
    return FALSE;
  }

  File = cbfs_get_entry (&Cbfs, "key_manifest.bin");
  if (File == NULL)
    return FALSE;

  // Check if the KM contains valid data
  Status = GetOemRootKeyFromKm(
    CBFS_SUBHEADER(File),
    File->len,
    &OemKey,
    &OemKeyLen
  );
  if (EFI_ERROR(Status))
    return FALSE;

  File = cbfs_get_entry (&Cbfs, "boot_policy_manifest.bin");
  if (File == NULL)
    return FALSE;

  return TRUE;
}

//
// SMBIOS Type 14: Group Associations
//
typedef struct {
  SMBIOS_STRUCTURE  Hdr;
  SMBIOS_TABLE_STRING GroupName;
  UINT8             ItemType;
  UINT16            ItemHandle;
} SMBIOS_TABLE_TYPE14_SINGLE_ITEM;

/**
  Helper to retrieve a string from an SMBIOS structure by Index.
**/
CHAR8*
GetSmbiosString (
  IN SMBIOS_STRUCTURE_POINTER  SmbiosRecord,
  IN SMBIOS_TABLE_STRING       StringIndex
  )
{
  CHAR8  *String;
  UINT8  Index;

  String = (CHAR8 *)(SmbiosRecord.Raw + SmbiosRecord.Hdr->Length);

  for (Index = 1; Index <= StringIndex; Index++) {
    if (Index == StringIndex) {
      return String;
    }
    while (*String != 0) {
      String++;
    }
    String++;
    if (*String == 0) {
      return NULL;
    }
  }
  return NULL;
}

/**
  Check if the platform is Fused (Manufacturing Mode Closed) using SMBIOS shadows.

  @retval TRUE   Platform is FUSED (Production Mode).
  @retval FALSE  Platform is UNFUSED (Manufacturing Mode) or Error.
**/
BOOLEAN
IsPlatformFused (
  VOID
  )
{
  EFI_STATUS                 Status;
  EFI_SMBIOS_PROTOCOL        *Smbios;
  EFI_SMBIOS_HANDLE          SmbiosHandle;
  EFI_SMBIOS_TYPE            SmbiosType;
  EFI_SMBIOS_TABLE_HEADER    *SmbiosRecord;
  SMBIOS_TABLE_TYPE14_SINGLE_ITEM *Type14;
  CHAR8                      *GroupName;
  UINT16                     TargetHandle;
  BOOLEAN                    FoundTargetHandle;
  FwstsSmbiosTable           *FwstsTable;

  Status = gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL, (VOID **)&Smbios);
  if (EFI_ERROR (Status))
    return TRUE;

  SmbiosHandle = SMBIOS_HANDLE_PI_RESERVED;
  SmbiosType = 14;
  TargetHandle = 0xFFFF;
  FoundTargetHandle = FALSE;

  while (!EFI_ERROR (Smbios->GetNext (Smbios, &SmbiosHandle, &SmbiosType, &SmbiosRecord, NULL))) {
    Type14 = (SMBIOS_TABLE_TYPE14_SINGLE_ITEM *) SmbiosRecord;

    SMBIOS_STRUCTURE_POINTER TempStruct;
    TempStruct.Hdr = SmbiosRecord;

    GroupName = GetSmbiosString (TempStruct, Type14->GroupName);
    if (GroupName != NULL && AsciiStrCmp (GroupName, "$MEI") == 0) {
      TargetHandle = Type14->ItemHandle;
      FoundTargetHandle = TRUE;
      break;
    }
  }

  if (!FoundTargetHandle)
    return TRUE;

  SmbiosHandle = TargetHandle;
  Status = Smbios->GetNext (Smbios, &SmbiosHandle, NULL, &SmbiosRecord, NULL);

  if (EFI_ERROR(Status) || SmbiosRecord->Handle != TargetHandle) {
     SmbiosHandle = SMBIOS_HANDLE_PI_RESERVED;
     FoundTargetHandle = FALSE;
     do {
       Status = Smbios->GetNext (Smbios, &SmbiosHandle, NULL, &SmbiosRecord, NULL);
       if (!EFI_ERROR(Status) && SmbiosRecord->Handle == TargetHandle) {
         FoundTargetHandle = TRUE;
         break;
       }
     } while (!EFI_ERROR(Status));

     if (!FoundTargetHandle) return TRUE;
  }

  FwstsTable = (FwstsSmbiosTable *)SmbiosRecord;

  if (SmbiosRecord->Length < sizeof(FwstsSmbiosTable))
    return TRUE;

  if ((FwstsTable->Record.Reg[5] & BIT30))
    return TRUE;
  else
    return FALSE;
}

/**
  Retrieves the SHA-384 hash of the OEM Root Key from a Coreboot image.

  This function parses the input firmware image to locate the FMAP and CBFS.
  It searches for the 'key_manifest.bin' file, extracts the OEM Public Key
  (BtgPubKey), and computes a SHA-384 hash.

  The SHA-384 hash matches what is generated by Intel's mFIT tool. It also
  matches the values that are burnt into Field Programmable Fuses when End Of
  Manufacturing is performed.

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
  )
{
  EFI_STATUS Status;
  struct cbfs_file *KmFile;
  struct cbfs_image Cbfs;
  CONST Fmap *ImageFmap;
  BtgPubKey *OemKey;
  UINTN OemKeyLen;
  UINT8 *HashInput;
  UINTN HashInputLen;

  if (!GetFmap (Image, ImageLen, &ImageFmap, IsTopSwapActive ())) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to parse firmware\n",
      __FUNCTION__
      ));
    return EFI_NOT_FOUND;
  }

  // Reading from the Slot A partition for simplicity and compatibility.
  // If Boot Guard is enabled, both slots must contain the manifest to work.
  if (!GetCbfs (Image, ImageFmap, &Cbfs, "COREBOOT")) {
    DEBUG ((DEBUG_ERROR, "%a(): failed to load CBFS\n", __FUNCTION__));
    return EFI_NOT_FOUND;
  }

  KmFile = cbfs_get_entry (&Cbfs, "key_manifest.bin");
  if (KmFile == NULL)
    return EFI_NOT_FOUND;

  Status = GetOemRootKeyFromKm (
    CBFS_SUBHEADER(KmFile),
    KmFile->len,
    &OemKey,
    &OemKeyLen
  );

  if (EFI_ERROR (Status))
    return Status;

  HashInputLen = (OemKey->KeySize / 8) + sizeof(OemKey->Exponent);
  HashInput = AllocatePool (HashInputLen);

  CopyMem (HashInput, OemKey->Modulus, OemKey->KeySize / 8);
  CopyMem (HashInput + (OemKey->KeySize / 8), &OemKey->Exponent, sizeof(OemKey->Exponent));

  *OemRootKeyHash = AllocatePool (48);
  if (*OemRootKeyHash == NULL) {
    FreePool (HashInput);
    return EFI_OUT_OF_RESOURCES;
  }

  if (!Sha384HashAll (HashInput, HashInputLen, *OemRootKeyHash)) {
    DEBUG ((DEBUG_ERROR, "%a(): Failed to generate OEM key SHA384 hash\n", __FUNCTION__));
    FreePool (HashInput);
    FreePool (*OemRootKeyHash);
    *OemRootKeyHash = NULL;
    return EFI_ABORTED;
  }

  FreePool (HashInput);

  return EFI_SUCCESS;
}

/**
  Checks if the two coreboot images are Boot Guard enabled and are using the
  same OEM root key for signing.

  @param[in] Current     Current firmware image
  @param[in] Updated     Updated firmware image
  @param[in] ImageSize   Size of updated firmware image

  @return TRUE     If the two images are compatible with each other
  @return FALSE    If the two images are incompatiblE
**/
BOOLEAN
EFIAPI
AreImageBtgKeysCompatible (
  IN CONST VOID  *Current,
  IN CONST VOID  *Updated,
  IN CONST UINTN ImageSize
  )
{
  EFI_STATUS  Status;
  UINT8       *CurrentOemKeyHash, *UpdatedOemKeyHash;
  BOOLEAN     KeysCompatible;

  // Platform is unfused and unlocked, so different BtG key doesn't matter.
  if (!IsPlatformFused () && !IsDescriptorLocked())
    return TRUE;

  // Boot Guard is not currently deployed.
  if (!IsBootGuardEnabled (Current, ImageSize))
    return TRUE;

  // Going from BtG -> No BtG is not possible if the platform is fused.
  if (IsBootGuardEnabled (Current, ImageSize) && !IsBootGuardEnabled (Updated, ImageSize))
    return FALSE;

  Status = GetOemRootKeyHash (Current, ImageSize, &CurrentOemKeyHash);
  if (EFI_ERROR(Status))
    return FALSE;

  Status = GetOemRootKeyHash (Updated, ImageSize, &UpdatedOemKeyHash);
  if (EFI_ERROR(Status)) {
    FreePool (CurrentOemKeyHash);
    return FALSE;
  }

  // BtG keys should be identical for the images to be compatible.
  KeysCompatible = !CompareMem (CurrentOemKeyHash, UpdatedOemKeyHash, 48);

  FreePool (CurrentOemKeyHash);
  FreePool (UpdatedOemKeyHash);

  return KeysCompatible;
}
