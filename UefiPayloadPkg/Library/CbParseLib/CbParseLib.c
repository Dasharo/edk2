/** @file
  This library will parse the coreboot table in memory and extract those required
  information.

  Copyright (c) 2014 - 2016, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

  Copyright (c) 2010 The Chromium OS Authors. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-3-Clause
**/

#include <Uefi/UefiBaseType.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/PciLib.h>
#include <Library/IoLib.h>
#include <Library/BlParseLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/HiiImage.h>
#include <Protocol/HiiDatabase.h>
#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/Pci22.h>
#include <Coreboot.h>

/**
  Convert a packed value from cbuint64 to a UINT64 value.

  @param  val      The pointer to packed data.

  @return          the UNIT64 value after conversion.

**/
UINT64
cb_unpack64 (
  IN struct cbuint64 val
  )
{
  return LShiftU64 (val.hi, 32) | val.lo;
}


/**
  Returns the sum of all elements in a buffer of 16-bit values.  During
  calculation, the carry bits are also been added.

  @param  Buffer      The pointer to the buffer to carry out the sum operation.
  @param  Length      The size, in bytes, of Buffer.

  @return Sum         The sum of Buffer with carry bits included during additions.

**/
UINT16
CbCheckSum16 (
  IN UINT16   *Buffer,
  IN UINTN    Length
  )
{
  UINT32      Sum;
  UINT32      TmpValue;
  UINTN       Idx;
  UINT8       *TmpPtr;

  Sum = 0;
  TmpPtr = (UINT8 *)Buffer;
  for(Idx = 0; Idx < Length; Idx++) {
    TmpValue  = TmpPtr[Idx];
    if (Idx % 2 == 1) {
      TmpValue <<= 8;
    }

    Sum += TmpValue;

    // Wrap
    if (Sum >= 0x10000) {
      Sum = (Sum + (Sum >> 16)) & 0xFFFF;
    }
  }

  return (UINT16)((~Sum) & 0xFFFF);
}


/**
  Check the coreboot table if it is valid.

  @param  Header            Pointer to coreboot table

  @retval TRUE              The coreboot table is valid.
  @retval Others            The coreboot table is not valid.

**/
BOOLEAN
IsValidCbTable (
  IN struct cb_header   *Header
  )
{
  UINT16                 CheckSum;

  if ((Header == NULL) || (Header->table_bytes == 0)) {
    return FALSE;
  }

  if (Header->signature != CB_HEADER_SIGNATURE) {
    return FALSE;
  }

  //
  // Check the checksum of the coreboot table header
  //
  CheckSum = CbCheckSum16 ((UINT16 *)Header, sizeof (*Header));
  if (CheckSum != 0) {
    DEBUG ((DEBUG_ERROR, "Invalid coreboot table header checksum\n"));
    return FALSE;
  }

  CheckSum = CbCheckSum16 ((UINT16 *)((UINT8 *)Header + sizeof (*Header)), Header->table_bytes);
  if (CheckSum != Header->table_checksum) {
    DEBUG ((DEBUG_ERROR, "Incorrect checksum of all the coreboot table entries\n"));
    return FALSE;
  }

  return TRUE;
}


/**
  This function retrieves the parameter base address from boot loader.

  This function will get bootloader specific parameter address for UEFI payload.
  e.g. HobList pointer for Slim Bootloader, and coreboot table header for Coreboot.

  @retval NULL            Failed to find the GUID HOB.
  @retval others          GUIDed HOB data pointer.

**/
VOID *
EFIAPI
GetParameterBase (
  VOID
  )
{
  struct cb_header   *Header;
  struct cb_record   *Record;
  UINT8              *TmpPtr;
  UINT8              *CbTablePtr;
  UINTN              Idx;

  //
  // coreboot could pass coreboot table to UEFI payload
  //
  Header = (struct cb_header *)(UINTN)GET_BOOTLOADER_PARAMETER ();
  if (IsValidCbTable (Header)) {
    return Header;
  }

  //
  // Find simplified coreboot table in memory range 0 ~ 4KB.
  // Some GCC version does not allow directly access to NULL pointer,
  // so start the search from 0x10 instead.
  //
  for (Idx = 16; Idx < 4096; Idx += 16) {
    Header = (struct cb_header *)Idx;
    if (Header->signature == CB_HEADER_SIGNATURE) {
      break;
    }
  }

  if (Idx >= 4096) {
    return NULL;
  }

  //
  // Check the coreboot header
  //
  if (!IsValidCbTable (Header)) {
    return NULL;
  }

  //
  // Find full coreboot table in high memory
  //
  CbTablePtr = NULL;
  TmpPtr = (UINT8 *)Header + Header->header_bytes;
  for (Idx = 0; Idx < Header->table_entries; Idx++) {
    Record = (struct cb_record *)TmpPtr;
    if (Record->tag == CB_TAG_FORWARD) {
      CbTablePtr = (VOID *)(UINTN)((struct cb_forward *)(UINTN)Record)->forward;
      break;
    }
    TmpPtr += Record->size;
  }

  //
  // Check the coreboot header in high memory
  //
  if (!IsValidCbTable ((struct cb_header *)CbTablePtr)) {
    return NULL;
  }

  SET_BOOTLOADER_PARAMETER ((UINT32)(UINTN)CbTablePtr);

  return CbTablePtr;
}


/**
  Find coreboot record with given Tag.

  @param  Tag                The tag id to be found

  @retval NULL              The Tag is not found.
  @retval Others            The pointer to the record found.

**/
VOID *
FindCbTag (
  IN  UINT32         Tag
  )
{
  struct cb_header   *Header;
  struct cb_record   *Record;
  UINT8              *TmpPtr;
  UINT8              *TagPtr;
  UINTN              Idx;

  Header = (struct cb_header *) GetParameterBase ();

  TagPtr = NULL;
  TmpPtr = (UINT8 *)Header + Header->header_bytes;
  for (Idx = 0; Idx < Header->table_entries; Idx++) {
    Record = (struct cb_record *)TmpPtr;
    if (Record->tag == Tag) {
      TagPtr = TmpPtr;
      break;
    }
    TmpPtr += Record->size;
  }

  return TagPtr;
}


/**
  Find the given table with TableId from the given coreboot memory Root.

  @param  Root               The coreboot memory table to be searched in
  @param  TableId            Table id to be found
  @param  MemTable           To save the base address of the memory table found
  @param  MemTableSize       To save the size of memory table found

  @retval RETURN_SUCCESS            Successfully find out the memory table.
  @retval RETURN_INVALID_PARAMETER  Invalid input parameters.
  @retval RETURN_NOT_FOUND          Failed to find the memory table.

**/
RETURN_STATUS
FindCbMemTable (
  IN  struct cbmem_root  *Root,
  IN  UINT32             TableId,
  OUT VOID               **MemTable,
  OUT UINT32             *MemTableSize
  )
{
  UINTN                  Idx;
  BOOLEAN                IsImdEntry;
  struct cbmem_entry     *Entries;

  if ((Root == NULL) || (MemTable == NULL)) {
    return RETURN_INVALID_PARAMETER;
  }
  //
  // Check if the entry is CBMEM or IMD
  // and handle them separately
  //
  Entries = Root->entries;
  if (Entries[0].magic == CBMEM_ENTRY_MAGIC) {
    IsImdEntry = FALSE;
  } else {
    Entries = (struct cbmem_entry *)((struct imd_root *)Root)->entries;
    if (Entries[0].magic == IMD_ENTRY_MAGIC) {
      IsImdEntry = TRUE;
    } else {
      return RETURN_NOT_FOUND;
    }
  }

  for (Idx = 0; Idx < Root->num_entries; Idx++) {
    if (Entries[Idx].id == TableId) {
      if (IsImdEntry) {
        *MemTable = (VOID *) ((UINTN)Entries[Idx].start + (UINTN)Root);
      } else {
        *MemTable = (VOID *) (UINTN)Entries[Idx].start;
      }
      if (MemTableSize != NULL) {
        *MemTableSize = Entries[Idx].size;
      }

      DEBUG ((DEBUG_INFO, "Find CbMemTable Id 0x%x, base %p, size 0x%x\n",
        TableId, *MemTable, Entries[Idx].size));
      return RETURN_SUCCESS;
    }
  }

  return RETURN_NOT_FOUND;
}

/**
  Acquire the coreboot memory table with the given table id

  @param  TableId            Table id to be searched
  @param  MemTable           Pointer to the base address of the memory table
  @param  MemTableSize       Pointer to the size of the memory table

  @retval RETURN_SUCCESS     Successfully find out the memory table.
  @retval RETURN_INVALID_PARAMETER  Invalid input parameters.
  @retval RETURN_NOT_FOUND   Failed to find the memory table.

**/
RETURN_STATUS
ParseCbMemTable (
  IN  UINT32               TableId,
  OUT VOID                 **MemTable,
  OUT UINT32               *MemTableSize
  )
{
  EFI_STATUS               Status;
  struct cb_memory         *rec;
  struct cb_memory_range   *Range;
  UINT64                   Start;
  UINT64                   Size;
  UINTN                    Index;
  struct cbmem_root        *CbMemRoot;

  if (MemTable == NULL) {
    return RETURN_INVALID_PARAMETER;
  }

  *MemTable = NULL;
  Status    = RETURN_NOT_FOUND;

  //
  // Get the coreboot memory table
  //
  rec = (struct cb_memory *)FindCbTag (CB_TAG_MEMORY);
  if (rec == NULL) {
    return Status;
  }

  for (Index = 0; Index < MEM_RANGE_COUNT(rec); Index++) {
    Range = MEM_RANGE_PTR(rec, Index);
    Start = cb_unpack64(Range->start);
    Size = cb_unpack64(Range->size);

    if ((Range->type == CB_MEM_TABLE) && (Start > 0x1000)) {
      CbMemRoot = (struct  cbmem_root *)(UINTN)(Start + Size - DYN_CBMEM_ALIGN_SIZE);
      Status = FindCbMemTable (CbMemRoot, TableId, MemTable, MemTableSize);
      if (!EFI_ERROR (Status)) {
        break;
      }
    }
  }

  return Status;
}



/**
  Acquire the memory information from the coreboot table in memory.

  @param  MemInfoCallback     The callback routine
  @param  Params              Pointer to the callback routine parameter

  @retval RETURN_SUCCESS     Successfully find out the memory information.
  @retval RETURN_NOT_FOUND   Failed to find the memory information.

**/
RETURN_STATUS
EFIAPI
ParseMemoryInfo (
  IN  BL_MEM_INFO_CALLBACK  MemInfoCallback,
  IN  VOID                  *Params
  )
{
  struct cb_memory         *rec;
  struct cb_memory_range   *Range;
  UINTN                    Index;
  MEMROY_MAP_ENTRY         MemoryMap;
  UINT32                   Tolud;

  Tolud = PciRead32(PCI_LIB_ADDRESS(0,0,0,0xbc)) & 0xFFF00000;

  //
  // Get the coreboot memory table
  //
  rec = (struct cb_memory *)FindCbTag (CB_TAG_MEMORY);
  if (rec == NULL) {
    return RETURN_NOT_FOUND;
  }

  for (Index = 0; Index < MEM_RANGE_COUNT(rec); Index++) {
    Range = MEM_RANGE_PTR(rec, Index);
    MemoryMap.Base = cb_unpack64(Range->start);
    MemoryMap.Size = cb_unpack64(Range->size);
    MemoryMap.Type = (UINT8)Range->type;

    switch (Range->type) {
      case CB_MEM_RAM:
        MemoryMap.Type = EFI_RESOURCE_SYSTEM_MEMORY;
        MemoryMap.Flag = EFI_RESOURCE_ATTRIBUTE_PRESENT;
        break;
      /* Only MMIO is marked reserved */
      case CB_MEM_RESERVED:
        /*
         * Reserved memory Below TOLUD can't be MMIO except legacy VGA which
         * is reported elsewhere as reserved.
         */
        if (MemoryMap.Base < Tolud) {
          MemoryMap.Type = EFI_RESOURCE_MEMORY_RESERVED;
          MemoryMap.Flag = EFI_RESOURCE_ATTRIBUTE_PRESENT;
        } else {
          MemoryMap.Type = EFI_RESOURCE_MEMORY_MAPPED_IO;
          MemoryMap.Flag = EFI_RESOURCE_ATTRIBUTE_PRESENT;
        }
        break;
      case CB_MEM_UNUSABLE:
        MemoryMap.Type = EFI_RESOURCE_MEMORY_RESERVED;
        MemoryMap.Flag = 0;
        break;
      case CB_MEM_VENDOR_RSVD:
        MemoryMap.Type = EFI_RESOURCE_FIRMWARE_DEVICE;
        MemoryMap.Flag = EFI_RESOURCE_ATTRIBUTE_PRESENT;
        break;
      /* ACPI/SMBIOS/CBMEM has it's own tag */
      case CB_MEM_ACPI:
      case CB_MEM_TABLE:
        MemoryMap.Type = EFI_RESOURCE_MEMORY_RESERVED;
        MemoryMap.Flag = EFI_RESOURCE_ATTRIBUTE_PRESENT;
        break;
      default:
        continue;
    }

    DEBUG ((DEBUG_INFO, "%d. %016lx - %016lx [%02x]\n",
            Index, MemoryMap.Base, MemoryMap.Base + MemoryMap.Size - 1, MemoryMap.Type));

    MemInfoCallback (&MemoryMap, Params);
  }

  return RETURN_SUCCESS;
}


/**
  Acquire acpi table and smbios table from coreboot

  @param  SystemTableInfo          Pointer to the system table info

  @retval RETURN_SUCCESS            Successfully find out the tables.
  @retval RETURN_NOT_FOUND          Failed to find the tables.

**/
RETURN_STATUS
EFIAPI
ParseSystemTable (
  OUT SYSTEM_TABLE_INFO     *SystemTableInfo
  )
{
  EFI_STATUS       Status;
  VOID             *MemTable;
  UINT32           MemTableSize;

  Status = ParseCbMemTable (SIGNATURE_32 ('T', 'B', 'M', 'S'), &MemTable, &MemTableSize);
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }
  SystemTableInfo->SmbiosTableBase = (UINT64) (UINTN)MemTable;
  SystemTableInfo->SmbiosTableSize = MemTableSize;

  Status = ParseCbMemTable (SIGNATURE_32 ('I', 'P', 'C', 'A'), &MemTable, &MemTableSize);
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }
  SystemTableInfo->AcpiTableBase = (UINT64) (UINTN)MemTable;
  SystemTableInfo->AcpiTableSize = MemTableSize;

  return Status;
}


/**
  Find the serial port information

  @param  SERIAL_PORT_INFO   Pointer to serial port info structure

  @retval RETURN_SUCCESS     Successfully find the serial port information.
  @retval RETURN_NOT_FOUND   Failed to find the serial port information .

**/
RETURN_STATUS
EFIAPI
ParseSerialInfo (
  OUT SERIAL_PORT_INFO     *SerialPortInfo
  )
{
  struct cb_serial          *CbSerial;

  CbSerial = FindCbTag (CB_TAG_SERIAL);
  if (CbSerial == NULL) {
    return RETURN_NOT_FOUND;
  }

  SerialPortInfo->BaseAddr    = CbSerial->baseaddr;
  SerialPortInfo->RegWidth    = CbSerial->regwidth;
  SerialPortInfo->Type        = CbSerial->type;
  SerialPortInfo->Baud        = CbSerial->baud;
  SerialPortInfo->InputHertz  = CbSerial->input_hertz;
  SerialPortInfo->UartPciAddr = CbSerial->uart_pci_addr;

  return RETURN_SUCCESS;
}

/**
  Find the video frame buffer information

  @param  GfxInfo             Pointer to the EFI_PEI_GRAPHICS_INFO_HOB structure

  @retval RETURN_SUCCESS     Successfully find the video frame buffer information.
  @retval RETURN_NOT_FOUND   Failed to find the video frame buffer information .

**/
RETURN_STATUS
EFIAPI
ParseGfxInfo (
  OUT EFI_PEI_GRAPHICS_INFO_HOB         *GfxInfo
  )
{
  struct cb_framebuffer                 *CbFbRec;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *GfxMode;

  if (GfxInfo == NULL) {
    return RETURN_INVALID_PARAMETER;
  }

  CbFbRec = FindCbTag (CB_TAG_FRAMEBUFFER);
  if (CbFbRec == NULL) {
    return RETURN_NOT_FOUND;
  }

  DEBUG ((DEBUG_INFO, "Found coreboot video frame buffer information\n"));
  DEBUG ((DEBUG_INFO, "physical_address: 0x%lx\n", CbFbRec->physical_address));
  DEBUG ((DEBUG_INFO, "x_resolution: 0x%x\n", CbFbRec->x_resolution));
  DEBUG ((DEBUG_INFO, "y_resolution: 0x%x\n", CbFbRec->y_resolution));
  DEBUG ((DEBUG_INFO, "bits_per_pixel: 0x%x\n", CbFbRec->bits_per_pixel));
  DEBUG ((DEBUG_INFO, "bytes_per_line: 0x%x\n", CbFbRec->bytes_per_line));

  DEBUG ((DEBUG_INFO, "red_mask_size: 0x%x\n", CbFbRec->red_mask_size));
  DEBUG ((DEBUG_INFO, "red_mask_pos: 0x%x\n", CbFbRec->red_mask_pos));
  DEBUG ((DEBUG_INFO, "green_mask_size: 0x%x\n", CbFbRec->green_mask_size));
  DEBUG ((DEBUG_INFO, "green_mask_pos: 0x%x\n", CbFbRec->green_mask_pos));
  DEBUG ((DEBUG_INFO, "blue_mask_size: 0x%x\n", CbFbRec->blue_mask_size));
  DEBUG ((DEBUG_INFO, "blue_mask_pos: 0x%x\n", CbFbRec->blue_mask_pos));
  DEBUG ((DEBUG_INFO, "reserved_mask_size: 0x%x\n", CbFbRec->reserved_mask_size));
  DEBUG ((DEBUG_INFO, "reserved_mask_pos: 0x%x\n", CbFbRec->reserved_mask_pos));

  GfxMode = &GfxInfo->GraphicsMode;
  GfxMode->Version              = 0;
  GfxMode->HorizontalResolution = CbFbRec->x_resolution;
  GfxMode->VerticalResolution   = CbFbRec->y_resolution;
  GfxMode->PixelsPerScanLine    = (CbFbRec->bytes_per_line << 3) / CbFbRec->bits_per_pixel;
  if ((CbFbRec->red_mask_pos == 0) && (CbFbRec->green_mask_pos == 8) && (CbFbRec->blue_mask_pos == 16)) {
    GfxMode->PixelFormat = PixelRedGreenBlueReserved8BitPerColor;
  } else if ((CbFbRec->blue_mask_pos == 0) && (CbFbRec->green_mask_pos == 8) && (CbFbRec->red_mask_pos == 16)) {
     GfxMode->PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  }
  GfxMode->PixelInformation.RedMask      = ((1 << CbFbRec->red_mask_size)      - 1) << CbFbRec->red_mask_pos;
  GfxMode->PixelInformation.GreenMask    = ((1 << CbFbRec->green_mask_size)    - 1) << CbFbRec->green_mask_pos;
  GfxMode->PixelInformation.BlueMask     = ((1 << CbFbRec->blue_mask_size)     - 1) << CbFbRec->blue_mask_pos;
  GfxMode->PixelInformation.ReservedMask = ((1 << CbFbRec->reserved_mask_size) - 1) << CbFbRec->reserved_mask_pos;

  GfxInfo->FrameBufferBase = CbFbRec->physical_address;
  GfxInfo->FrameBufferSize = CbFbRec->bytes_per_line *  CbFbRec->y_resolution;

  return RETURN_SUCCESS;
}

/**
  Find the video frame buffer device information

  @param  GfxDeviceInfo      Pointer to the EFI_PEI_GRAPHICS_DEVICE_INFO_HOB structure

  @retval RETURN_SUCCESS     Successfully find the video frame buffer information.
  @retval RETURN_NOT_FOUND   Failed to find the video frame buffer information.

**/
RETURN_STATUS
EFIAPI
ParseGfxDeviceInfo (
  OUT EFI_PEI_GRAPHICS_DEVICE_INFO_HOB       *GfxDeviceInfo
  )
{
  return RETURN_NOT_FOUND;
}

/**
  Find the SMM store information

  @param  SMMSTOREInfo       Pointer to the SMMSTORE_INFO structure

  @retval RETURN_SUCCESS     Successfully find the SMM store buffer information.
  @retval RETURN_NOT_FOUND   Failed to find the SMM store buffer information .

**/
RETURN_STATUS
EFIAPI
ParseSMMSTOREInfo (
  OUT SMMSTORE_INFO       *SMMSTOREInfo
  )
{
  struct cb_smmstorev2                  *CbSSRec;

  if (SMMSTOREInfo == NULL) {
    return RETURN_INVALID_PARAMETER;
  }

  CbSSRec = FindCbTag (CB_TAG_SMMSTOREV2);
  if (CbSSRec == NULL) {
    return RETURN_NOT_FOUND;
  }

  DEBUG ((DEBUG_INFO, "Found SMM Store information\n"));
  DEBUG ((DEBUG_INFO, "block size: 0x%x\n", CbSSRec->block_size));
  DEBUG ((DEBUG_INFO, "number of blocks: 0x%x\n", CbSSRec->num_blocks));
  DEBUG ((DEBUG_INFO, "communication buffer: 0x%x\n", CbSSRec->com_buffer));
  DEBUG ((DEBUG_INFO, "communication buffer size: 0x%x\n", CbSSRec->com_buffer_size));
  DEBUG ((DEBUG_INFO, "MMIO address of store: 0x%x\n", CbSSRec->mmap_addr));

  SMMSTOREInfo->ComBuffer = CbSSRec->com_buffer;
  SMMSTOREInfo->ComBufferSize = CbSSRec->com_buffer_size;
  SMMSTOREInfo->BlockSize = CbSSRec->block_size;
  SMMSTOREInfo->NumBlocks = CbSSRec->num_blocks;
  SMMSTOREInfo->MmioAddress = CbSSRec->mmap_addr;
  SMMSTOREInfo->ApmCmd = CbSSRec->apm_cmd;

  return RETURN_SUCCESS;
}

/**
  Acquire boot logo from coreboot

  @param  BmpAddress          Pointer to the bitmap file
  @param  BmpSize             Size of the image

  @retval RETURN_SUCCESS            Successfully find the boot logo.
  @retval RETURN_NOT_FOUND          Failed to find the boot logo.

**/
RETURN_STATUS
EFIAPI
ParseBootLogo (
  OUT UINT64 *BmpAddress,
  OUT UINT32 *BmpSize
  )
{
  struct cb_cbmem_ref *CbLogo;
  struct cb_bootlogo_header *CbLogoHeader;

  CbLogo = FindCbTag (CB_TAG_LOGO);
  if (CbLogo == NULL) {
    DEBUG ((DEBUG_INFO, "Did not find BootLogo tag\n"));
    return RETURN_NOT_FOUND;
  }

  CbLogoHeader = (struct cb_bootlogo_header*)(UINTN) CbLogo->cbmem_addr;

  *BmpAddress = CbLogo->cbmem_addr + sizeof(*CbLogoHeader);
  *BmpSize = CbLogoHeader->size;

  return RETURN_SUCCESS;
}

/**
  Find the Tcg Physical Presence store information

  @param  PPIInfo       Pointer to the TCG_PHYSICAL_PRESENCE_INFO structure

  @retval RETURN_SUCCESS     Successfully find the SMM store buffer information.
  @retval RETURN_NOT_FOUND   Failed to find the SMM store buffer information .

**/
RETURN_STATUS
EFIAPI
ParseTPMPPIInfo (
  OUT TCG_PHYSICAL_PRESENCE_INFO       *PPIInfo
  )
{
  struct cb_tpm_physical_presence       *CbTPPRec;
  UINT8 VersionMajor;
  UINT8 VersionMinor;

  if (PPIInfo == NULL) {
    return RETURN_INVALID_PARAMETER;
  }

  CbTPPRec = FindCbTag (CB_TAG_TPM_PPI_HANDOFF);
  if (CbTPPRec == NULL) {
    return RETURN_NOT_FOUND;
  }

  VersionMajor = CbTPPRec->ppi_version >> 4;
  VersionMinor = CbTPPRec->ppi_version & 0xF;

  DEBUG ((DEBUG_INFO, "Found Tcg Physical Presence information\n"));
  DEBUG ((DEBUG_INFO, "PpiAddress: 0x%x\n", CbTPPRec->ppi_address));
  DEBUG ((DEBUG_INFO, "TpmVersion: 0x%x\n", CbTPPRec->tpm_version));
  DEBUG ((DEBUG_INFO, "PpiVersion: %x.%x\n", VersionMajor, VersionMinor));

  PPIInfo->PpiAddress = CbTPPRec->ppi_address;
  if (CbTPPRec->tpm_version == LB_TPM_VERSION_TPM_VERSION_1_2) {
    PPIInfo->TpmVersion = UEFIPAYLOAD_TPM_VERSION_1_2;
  } else if (CbTPPRec->tpm_version == LB_TPM_VERSION_TPM_VERSION_2) {
    PPIInfo->TpmVersion = UEFIPAYLOAD_TPM_VERSION_2;
  }
  if (VersionMajor == 1 && VersionMinor >= 3) {
    PPIInfo->PpiVersion = UEFIPAYLOAD_TPM_PPI_VERSION_1_30;
  }

  return RETURN_SUCCESS;
}

STATIC
CONST CHAR8 *
GetRecoveryReasonString(
  IN UINT8 code
  )
{
  switch ((enum vb2_nv_recovery)code) {
  case VB2_RECOVERY_NOT_REQUESTED: /* 0x00 */
    return "recovery not requested";
  case VB2_RECOVERY_LEGACY: /* 0x01 */
    return "recovery requested from legacy utility";
  case VB2_RECOVERY_RO_MANUAL: /* 0x02 */
    return "recovery button pressed";
  case VB2_RECOVERY_RO_INVALID_RW: /* 0x03 */
    return "RW firmware failed signature check";
  case VB2_RECOVERY_DEPRECATED_RO_S3_RESUME: /* 0x04 */
    return "S3 resume failed";
  case VB2_RECOVERY_DEPRECATED_RO_TPM_ERROR: /* 0x05 */
    return "TPM error in read-only firmware";
  case VB2_RECOVERY_RO_SHARED_DATA: /* 0x06 */
    return "shared data error in read-only firmware";
  case VB2_RECOVERY_DEPRECATED_RO_TEST_S3: /* 0x07 */
    return "test error from S3Resume()";
  case VB2_RECOVERY_DEPRECATED_RO_TEST_LFS: /* 0x08 */
    return "test error from LoadFirmwareSetup()";
  case VB2_RECOVERY_DEPRECATED_RO_TEST_LF: /* 0x09 */
    return "test error from LoadFirmware()";
  case VB2_RECOVERY_DEPRECATED_RW_NOT_DONE: /* 0x10 */
    return "RW firmware check not done";
  case VB2_RECOVERY_DEPRECATED_RW_DEV_FLAG_MISMATCH: /* 0x11 */
    return "RW firmware developer flag mismatch";
  case VB2_RECOVERY_DEPRECATED_RW_REC_FLAG_MISMATCH: /* 0x12 */
    return "RW firmware recovery flag mismatch";
  case VB2_RECOVERY_FW_KEYBLOCK: /* 0x13 */
    return "RW firmware unable to verify keyblock";
  case VB2_RECOVERY_FW_KEY_ROLLBACK: /* 0x14 */
    return "RW firmware key version rollback detected";
  case VB2_RECOVERY_DEPRECATED_RW_DATA_KEY_PARSE: /* 0x15 */
    return "RW firmware unable to parse data key";
  case VB2_RECOVERY_FW_PREAMBLE: /* 0x16 */
    return "RW firmware unable to verify preamble";
  case VB2_RECOVERY_FW_ROLLBACK: /* 0x17 */
    return "RW firmware version rollback detected";
  case VB2_RECOVERY_DEPRECATED_FW_HEADER_VALID: /* 0x18 */
    return "RW firmware header is valid";
  case VB2_RECOVERY_DEPRECATED_FW_GET_FW_BODY: /* 0x19 */
    return "RW firmware unable to get firmware body";
  case VB2_RECOVERY_DEPRECATED_FW_HASH_WRONG_SIZE: /* 0x1a */
    return "RW firmware hash is wrong size";
  case VB2_RECOVERY_FW_BODY: /* 0x1b */
    return "RW firmware unable to verify firmware body";
  case VB2_RECOVERY_DEPRECATED_FW_VALID: /* 0x1c */
    return "RW firmware is valid";
  case VB2_RECOVERY_DEPRECATED_FW_NO_RO_NORMAL: /* 0x1d */
    return "RW firmware read-only normal path is not supported";
  case VB2_RECOVERY_RO_FIRMWARE: /* 0x20 */
    return "firmware problem outside of verified boot";
  case VB2_RECOVERY_RO_TPM_REBOOT: /* 0x21 */
    return "TPM requires a system reboot (should be transient)";
  case VB2_RECOVERY_EC_SOFTWARE_SYNC: /* 0x22 */
    return "EC software sync error";
  case VB2_RECOVERY_EC_UNKNOWN_IMAGE: /* 0x23 */
    return "EC software sync unable to determine active EC image";
  case VB2_RECOVERY_DEPRECATED_EC_HASH: /* 0x24 */
    return "EC software sync error obtaining EC image hash";
  case VB2_RECOVERY_DEPRECATED_EC_EXPECTED_IMAGE: /* 0x25 */
    return "EC software sync error obtaining expected EC image from BIOS";
  case VB2_RECOVERY_EC_UPDATE: /* 0x26 */
    return "EC software sync error updating EC";
  case VB2_RECOVERY_EC_JUMP_RW: /* 0x27 */
    return "EC software sync unable to jump to EC-RW";
  case VB2_RECOVERY_EC_PROTECT: /* 0x28 */
    return "EC software sync protection error";
  case VB2_RECOVERY_EC_EXPECTED_HASH: /* 0x29 */
    return "EC software sync error obtaining expected EC hash from BIOS";
  case VB2_RECOVERY_DEPRECATED_EC_HASH_MISMATCH: /* 0x2a */
    return "EC software sync error comparing expected EC hash and image";
  case VB2_RECOVERY_SECDATA_FIRMWARE_INIT: /* 0x2b */
    return "firmware secure NVRAM (TPM) initialization error";
  case VB2_RECOVERY_GBB_HEADER: /* 0x2c */
    return "error parsing GBB header";
  case VB2_RECOVERY_TPM_CLEAR_OWNER: /* 0x2d */
    return "error trying to clear TPM owner";
  case VB2_RECOVERY_DEV_SWITCH: /* 0x2e */
    return "error reading or updating developer switch";
  case VB2_RECOVERY_FW_SLOT: /* 0x2f */
    return "error selecting RW firmware slot";
  case VB2_RECOVERY_AUXFW_UPDATE: /* 0x30 */
    return "error updating auxiliary firmware";
  case VB2_RECOVERY_INTEL_CSE_LITE_SKU: /* 0x31 */
    return "Intel CSE Lite SKU firmware failure";
  case VB2_RECOVERY_RO_UNSPECIFIED: /* 0x3f */
    return "unspecified/unknown error in RO firmware";
  case VB2_RECOVERY_DEPRECATED_RW_DEV_SCREEN: /* 0x41 */
    return "user requested recovery from dev-mode warning screen";
  case VB2_RECOVERY_DEPRECATED_RW_NO_OS: /* 0x42 */
    return "no OS kernel detected (or kernel rollback attempt?)";
  case VB2_RECOVERY_RW_INVALID_OS: /* 0x43 */
    return "OS kernel or rootfs failed signature check";
  case VB2_RECOVERY_DEPRECATED_RW_TPM_ERROR: /* 0x44 */
    return "TPM error in rewritable firmware";
  case VB2_RECOVERY_DEPRECATED_RW_DEV_MISMATCH: /* 0x45 */
    return "RW firmware in dev mode, but dev switch is off";
  case VB2_RECOVERY_RW_SHARED_DATA: /* 0x46 */
    return "shared data error in rewritable firmware";
  case VB2_RECOVERY_DEPRECATED_RW_TEST_LK: /* 0x47 */
    return "test error from LoadKernel()";
  case VB2_RECOVERY_DEPRECATED_RW_NO_DISK: /* 0x48 */
    return "no bootable storage device in system";
  case VB2_RECOVERY_TPM_E_FAIL: /* 0x49 */
    return "TPM error that was not fixed by reboot";
  case VB2_RECOVERY_RO_TPM_S_ERROR: /* 0x50 */
    return "TPM setup error in read-only firmware";
  case VB2_RECOVERY_RO_TPM_W_ERROR: /* 0x51 */
    return "TPM write error in read-only firmware";
  case VB2_RECOVERY_RO_TPM_L_ERROR: /* 0x52 */
    return "TPM lock error in read-only firmware";
  case VB2_RECOVERY_RO_TPM_U_ERROR: /* 0x53 */
    return "TPM update error in read-only firmware";
  case VB2_RECOVERY_RW_TPM_R_ERROR: /* 0x54 */
    return "TPM read error in rewritable firmware";
  case VB2_RECOVERY_RW_TPM_W_ERROR: /* 0x55 */
    return "TPM write error in rewritable firmware";
  case VB2_RECOVERY_RW_TPM_L_ERROR: /* 0x56 */
    return "TPM lock error in rewritable firmware";
  case VB2_RECOVERY_EC_HASH_FAILED: /* 0x57 */
    return "EC software sync unable to get EC image hash";
  case VB2_RECOVERY_EC_HASH_SIZE: /* 0x58 */
    return "EC software sync invalid image hash size";
  case VB2_RECOVERY_LK_UNSPECIFIED: /* 0x59 */
    return "unspecified error while trying to load kernel";
  case VB2_RECOVERY_RW_NO_DISK: /* 0x5a */
    return "no bootable storage device in system";
  case VB2_RECOVERY_RW_NO_KERNEL: /* 0x5b */
    return "no bootable kernel found on disk";
  case VB2_RECOVERY_DEPRECATED_RW_BCB_ERROR: /* 0x5c */
    return "BCB partition error on disk";
  case VB2_RECOVERY_SECDATA_KERNEL_INIT: /* 0x5d */
    return "kernel secure NVRAM (TPM) initialization error";
  case VB2_RECOVERY_DEPRECATED_FW_FASTBOOT: /* 0x5e */
    return "fastboot-mode requested in firmware";
  case VB2_RECOVERY_RO_TPM_REC_HASH_L_ERROR: /* 0x5f */
    return "recovery hash space lock error in RO firmware";
  case VB2_RECOVERY_TPM_DISABLE_FAILED: /* 0x60 */
    return "failed to disable TPM before running untrusted code";
  case VB2_RECOVERY_ALTFW_HASH_MISMATCH: /* 0x61 */
    return "verification of alternate bootloader payload failed";
  case VB2_RECOVERY_SECDATA_FWMP_INIT: /* 0x62 */
    return "FWMP secure NVRAM (TPM) initialization error";
  case VB2_RECOVERY_CR50_BOOT_MODE: /* 0x63 */
    return "failed to get boot mode from Cr50";
  case VB2_RECOVERY_ESCAPE_NO_BOOT: /* 0x64 */
    return "attempt to escape from NO_BOOT mode was detected";
  case VB2_RECOVERY_RW_UNSPECIFIED: /* 0x7f */
    return "unspecified/unknown error in RW firmware";
  case VB2_RECOVERY_DEPRECATED_KE_DM_VERITY: /* 0x81 */
    return "DM-verity error";
  case VB2_RECOVERY_DEPRECATED_KE_UNSPECIFIED: /* 0xbf */
    return "unspecified/unknown error in kernel";
  case VB2_RECOVERY_US_TEST: /* 0xc1 */
    return "recovery mode test from user-mode";
  case VB2_RECOVERY_DEPRECATED_BCB_USER_MODE: /* 0xc2 */
    return "user-mode requested recovery via BCB";
  case VB2_RECOVERY_DEPRECATED_US_FASTBOOT: /* 0xc3 */
    return "user-mode requested fastboot mode";
  case VB2_RECOVERY_TRAIN_AND_REBOOT: /* 0xc4 */
    return "user-mode requested DRAM train and reboot";
  case VB2_RECOVERY_US_UNSPECIFIED: /* 0xff */
    return "unspecified/unknown error in user-mode";
  }
  return "unknown error code";
}

/**
  Acquire Vboot recovery information from coreboot

  @param  RecoveryCode        Recovery reason code, zero if not in recovery mode.
  @param  RecoveryReason      Why are we in recovery boot as a string.

  @retval RETURN_SUCCESS      Successfully found VBoot data.
  @retval RETURN_NOT_FOUND    Failed to find VBoot data.

**/
RETURN_STATUS
EFIAPI
ParseVBootWorkbuf (
  OUT UINT8        *RecoveryCode,
  OUT CONST CHAR8 **RecoveryReason
  )
{
  struct cb_cbmem_entry *CbmemEntry;
  struct cb_vboot_workbuf_v2 *Workbuf;

  if (RecoveryCode == NULL || RecoveryReason == NULL) {
    return RETURN_INVALID_PARAMETER;
  }

  CbmemEntry = FindCbTag (CB_TAG_VBOOT_WORKBUF);
  if (CbmemEntry == NULL) {
    DEBUG ((DEBUG_INFO, "Did not find VBootWorkbuf tag\n"));
    return RETURN_NOT_FOUND;
  }

  Workbuf = (struct cb_vboot_workbuf_v2 *)(UINTN)CbmemEntry->address;
  if (Workbuf->magic != VB2_SHARED_DATA_MAGIC) {
    DEBUG ((DEBUG_INFO, "VBootWorkbuf tag data is wrong\n"));
    return RETURN_NOT_FOUND;
  }

  if (Workbuf->struct_version_major != VB2_SHARED_DATA_VERSION_MAJOR) {
    DEBUG ((DEBUG_INFO, "VBootWorkbuf tag data is of wrong major version\n"));
    return RETURN_NOT_FOUND;
  }

  *RecoveryCode = Workbuf->recovery_reason;
  *RecoveryReason = GetRecoveryReasonString(Workbuf->recovery_reason);

  return RETURN_SUCCESS;
}

PACKED struct timestamp_entry {
	UINT32	entry_id;
	UINT64	entry_stamp;
};

PACKED struct timestamp_table {
	UINT64	base_time;
	UINT16	max_entries;
	UINT16	tick_freq_mhz;
	UINT32	num_entries;
	struct timestamp_entry entries[0]; /* Variable number of entries */
};


/**
  Parse the coreboot timestamps

  @retval RETURN_SUCCESS     Successfully find the timestamps information.
  @retval RETURN_NOT_FOUND   Failed to find the tiemstamps information .

**/
RETURN_STATUS
EFIAPI
ParseTimestampTable (
  OUT FIRMWARE_SEC_PERFORMANCE *Performance
  )
{
  struct timestamp_table                  *CbTsRec;

  if (Performance == NULL) {
    return RETURN_INVALID_PARAMETER;
  }

  CbTsRec = FindCbTag (CB_TAG_TIMESTAMPS);
  if (CbTsRec == NULL) {
    return RETURN_NOT_FOUND;
  }

  /* ResetEnd must be reported in nanoseconds, not ticks */
  Performance->ResetEnd = DivU64x32(CbTsRec->base_time, CbTsRec->tick_freq_mhz);
  return RETURN_SUCCESS;
}
