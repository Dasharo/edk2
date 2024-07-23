/** @file
  This PEIM will parse bootloader information and report resource information into pei core.
  This file contains the main entrypoint of the PEIM.

Copyright (c) 2014 - 2019, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/
#include "BlSupportPei.h"

#define LEGACY_8259_MASK_REGISTER_MASTER  0x21
#define LEGACY_8259_MASK_REGISTER_SLAVE   0xA1

#define E820_RAM        1
#define E820_RESERVED   2
#define E820_ACPI       3
#define E820_NVS        4
#define E820_UNUSABLE   5
#define E820_DISABLED   6
#define E820_PMEM       7
#define E820_UNDEFINED  8

EFI_MEMORY_TYPE_INFORMATION mDefaultMemoryTypeInformation[] = {
  { EfiACPIReclaimMemory,   FixedPcdGet32 (PcdMemoryTypeEfiACPIReclaimMemory) },
  { EfiACPIMemoryNVS,       FixedPcdGet32 (PcdMemoryTypeEfiACPIMemoryNVS) },
  { EfiReservedMemoryType,  FixedPcdGet32 (PcdMemoryTypeEfiReservedMemoryType) },
  { EfiRuntimeServicesData, FixedPcdGet32 (PcdMemoryTypeEfiRuntimeServicesData) },
  { EfiRuntimeServicesCode, FixedPcdGet32 (PcdMemoryTypeEfiRuntimeServicesCode) },
  { EfiMaxMemoryType,       0     }
};

EFI_PEI_PPI_DESCRIPTOR   mPpiBootMode[] = {
  {
    EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST,
    &gEfiPeiMasterBootModePpiGuid,
    NULL
  }
};

EFI_PEI_GRAPHICS_DEVICE_INFO_HOB mDefaultGraphicsDeviceInfo = {
  MAX_UINT16, MAX_UINT16, MAX_UINT16, MAX_UINT16, MAX_UINT8,  MAX_UINT8
};

STATIC UINT32  mTopOfLowerUsableDram = 0;

/**
  Create memory mapped io resource hob.

  @param  MmioBase    Base address of the memory mapped io range
  @param  MmioSize    Length of the memory mapped io range

**/
VOID
BuildMemoryMappedIoRangeHob (
  EFI_PHYSICAL_ADDRESS        MmioBase,
  UINT64                      MmioSize
  )
{
  BuildResourceDescriptorHob (
    EFI_RESOURCE_MEMORY_MAPPED_IO,
    (EFI_RESOURCE_ATTRIBUTE_PRESENT    |
    EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
    EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_TESTED),
    MmioBase,
    MmioSize
    );

  BuildMemoryAllocationHob (
    MmioBase,
    MmioSize,
    EfiMemoryMappedIO
    );
}

/**
  Check the integrity of firmware volume header

  @param[in]  FwVolHeader   A pointer to a firmware volume header

  @retval     TRUE          The firmware volume is consistent
  @retval     FALSE         The firmware volume has corrupted.

**/
STATIC
BOOLEAN
IsFvHeaderValid (
  IN EFI_FIRMWARE_VOLUME_HEADER    *FwVolHeader
  )
{
  UINT16 Checksum;

  // Skip nv storage fv
  if (CompareMem (&FwVolHeader->FileSystemGuid, &gEfiFirmwareFileSystem2Guid, sizeof(EFI_GUID)) != 0 ) {
    return FALSE;
  }

  if ( (FwVolHeader->Revision != EFI_FVH_REVISION)   ||
     (FwVolHeader->Signature != EFI_FVH_SIGNATURE) ||
     (FwVolHeader->FvLength == ((UINTN) -1))       ||
     ((FwVolHeader->HeaderLength & 0x01 ) !=0) )  {
    return FALSE;
  }

  Checksum = CalculateCheckSum16 ((UINT16 *) FwVolHeader, FwVolHeader->HeaderLength);
  if (Checksum != 0) {
    DEBUG (( DEBUG_ERROR,
              "ERROR - Invalid Firmware Volume Header Checksum, change 0x%04x to 0x%04x\r\n",
              FwVolHeader->Checksum,
              (UINT16)( Checksum + FwVolHeader->Checksum )));
    return TRUE; //FALSE; Need update UEFI build tool when patching entrypoin @start of fd.
  }

  return TRUE;
}

/**
  Install FvInfo PPI and create fv hobs for remained fvs

**/
VOID
PeiReportRemainedFvs (
  VOID
  )
{
  UINT8*  TempPtr;
  UINT8*  EndPtr;

  TempPtr = (UINT8* )(UINTN) PcdGet32 (PcdPayloadFdMemBase);
  EndPtr = (UINT8* )(UINTN) (PcdGet32 (PcdPayloadFdMemBase) + PcdGet32 (PcdPayloadFdMemSize));

  for (;TempPtr < EndPtr;) {
    if (IsFvHeaderValid ((EFI_FIRMWARE_VOLUME_HEADER* )TempPtr)) {
      if (TempPtr != (UINT8* )(UINTN) PcdGet32 (PcdPayloadFdMemBase))  {
        // Skip the PEI FV
        DEBUG((DEBUG_INFO, "Found one valid fv : 0x%lx.\n", TempPtr, ((EFI_FIRMWARE_VOLUME_HEADER* )TempPtr)->FvLength));

        PeiServicesInstallFvInfoPpi (
          NULL,
          (VOID *) (UINTN) TempPtr,
          (UINT32) (UINTN) ((EFI_FIRMWARE_VOLUME_HEADER* )TempPtr)->FvLength,
          NULL,
          NULL
          );
        BuildFvHob ((EFI_PHYSICAL_ADDRESS)(UINTN) TempPtr, ((EFI_FIRMWARE_VOLUME_HEADER* )TempPtr)->FvLength);
      }
    }
    TempPtr += ((EFI_FIRMWARE_VOLUME_HEADER* )TempPtr)->FvLength;
  }
}

/**
  Find the board related info from ACPI table

  @param  AcpiTableBase          ACPI table start address in memory
  @param  AcpiBoardInfo          Pointer to the acpi board info strucutre

  @retval RETURN_SUCCESS     Successfully find out all the required information.
  @retval RETURN_NOT_FOUND   Failed to find the required info.

**/
RETURN_STATUS
ParseAcpiInfo (
  IN   UINT64                                   AcpiTableBase,
  OUT  ACPI_BOARD_INFO                          *AcpiBoardInfo
  )
{
  EFI_ACPI_3_0_ROOT_SYSTEM_DESCRIPTION_POINTER  *Rsdp;
  EFI_ACPI_DESCRIPTION_HEADER                   *Rsdt;
  UINT32                                        *Entry32;
  UINTN                                         Entry32Num;
  EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE     *Fadt;
  EFI_ACPI_DESCRIPTION_HEADER                   *Xsdt;
  UINT64                                        *Entry64;
  UINTN                                         Entry64Num;
  UINTN                                         Idx;
  UINT32                                        *Signature;
  EFI_ACPI_MEMORY_MAPPED_CONFIGURATION_BASE_ADDRESS_TABLE_HEADER *MmCfgHdr;
  EFI_ACPI_MEMORY_MAPPED_ENHANCED_CONFIGURATION_SPACE_BASE_ADDRESS_ALLOCATION_STRUCTURE *MmCfgBase;

  Rsdp = (EFI_ACPI_3_0_ROOT_SYSTEM_DESCRIPTION_POINTER *)(UINTN)AcpiTableBase;
  DEBUG ((DEBUG_INFO, "Rsdp at 0x%p\n", Rsdp));
  DEBUG ((DEBUG_INFO, "Rsdt at 0x%x, Xsdt at 0x%lx\n", Rsdp->RsdtAddress, Rsdp->XsdtAddress));

  //
  // Search Rsdt First
  //
  Fadt     = NULL;
  MmCfgHdr = NULL;
  Rsdt     = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)(Rsdp->RsdtAddress);
  if (Rsdt != NULL) {
    Entry32  = (UINT32 *)(Rsdt + 1);
    Entry32Num = (Rsdt->Length - sizeof(EFI_ACPI_DESCRIPTION_HEADER)) >> 2;
    for (Idx = 0; Idx < Entry32Num; Idx++) {
      Signature = (UINT32 *)(UINTN)Entry32[Idx];
      if (*Signature == EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE) {
        Fadt = (EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE *)Signature;
        DEBUG ((DEBUG_INFO, "Found Fadt in Rsdt\n"));
      }

      if (*Signature == EFI_ACPI_5_0_PCI_EXPRESS_MEMORY_MAPPED_CONFIGURATION_SPACE_BASE_ADDRESS_DESCRIPTION_TABLE_SIGNATURE) {
        MmCfgHdr = (EFI_ACPI_MEMORY_MAPPED_CONFIGURATION_BASE_ADDRESS_TABLE_HEADER *)Signature;
        DEBUG ((DEBUG_INFO, "Found MM config address in Rsdt\n"));
      }

      if ((Fadt != NULL) && (MmCfgHdr != NULL)) {
        goto Done;
      }
    }
  }

  //
  // Search Xsdt Second
  //
  Xsdt     = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)(Rsdp->XsdtAddress);
  if (Xsdt != NULL) {
    Entry64  = (UINT64 *)(Xsdt + 1);
    Entry64Num = (Xsdt->Length - sizeof(EFI_ACPI_DESCRIPTION_HEADER)) >> 3;
    for (Idx = 0; Idx < Entry64Num; Idx++) {
      Signature = (UINT32 *)(UINTN)Entry64[Idx];
      if (*Signature == EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE) {
        Fadt = (EFI_ACPI_3_0_FIXED_ACPI_DESCRIPTION_TABLE *)Signature;
        DEBUG ((DEBUG_INFO, "Found Fadt in Xsdt\n"));
      }

      if (*Signature == EFI_ACPI_5_0_PCI_EXPRESS_MEMORY_MAPPED_CONFIGURATION_SPACE_BASE_ADDRESS_DESCRIPTION_TABLE_SIGNATURE) {
        MmCfgHdr = (EFI_ACPI_MEMORY_MAPPED_CONFIGURATION_BASE_ADDRESS_TABLE_HEADER *)Signature;
        DEBUG ((DEBUG_INFO, "Found MM config address in Xsdt\n"));
      }

      if ((Fadt != NULL) && (MmCfgHdr != NULL)) {
        goto Done;
      }
    }
  }

  if (Fadt == NULL) {
    return RETURN_NOT_FOUND;
  }

Done:

  AcpiBoardInfo->PmCtrlRegBase   = Fadt->Pm1aCntBlk;
  AcpiBoardInfo->PmTimerRegBase  = Fadt->PmTmrBlk;
  AcpiBoardInfo->ResetRegAddress = Fadt->ResetReg.Address;
  AcpiBoardInfo->ResetValue      = Fadt->ResetValue;
  AcpiBoardInfo->PmEvtBase       = Fadt->Pm1aEvtBlk;
  AcpiBoardInfo->PmGpeEnBase     = Fadt->Gpe0Blk + Fadt->Gpe0BlkLen / 2;

  if (MmCfgHdr != NULL) {
    MmCfgBase = (EFI_ACPI_MEMORY_MAPPED_ENHANCED_CONFIGURATION_SPACE_BASE_ADDRESS_ALLOCATION_STRUCTURE *)((UINT8*) MmCfgHdr + sizeof (*MmCfgHdr));
    AcpiBoardInfo->PcieBaseAddress = MmCfgBase->BaseAddress;
    AcpiBoardInfo->PcieBaseSize = (MmCfgBase->EndBusNumber + 1 - MmCfgBase->StartBusNumber) * 4096 * 32 * 8;
  } else {
    AcpiBoardInfo->PcieBaseAddress = 0;
    AcpiBoardInfo->PcieBaseSize = 0;
  }
  DEBUG ((DEBUG_INFO, "PmCtrl  Reg 0x%lx\n",  AcpiBoardInfo->PmCtrlRegBase));
  DEBUG ((DEBUG_INFO, "PmTimer Reg 0x%lx\n",  AcpiBoardInfo->PmTimerRegBase));
  DEBUG ((DEBUG_INFO, "Reset   Reg 0x%lx\n",  AcpiBoardInfo->ResetRegAddress));
  DEBUG ((DEBUG_INFO, "Reset   Value 0x%x\n", AcpiBoardInfo->ResetValue));
  DEBUG ((DEBUG_INFO, "PmEvt   Reg 0x%lx\n",  AcpiBoardInfo->PmEvtBase));
  DEBUG ((DEBUG_INFO, "PmGpeEn Reg 0x%lx\n",  AcpiBoardInfo->PmGpeEnBase));
  DEBUG ((DEBUG_INFO, "PcieBaseAddr 0x%lx\n", AcpiBoardInfo->PcieBaseAddress));
  DEBUG ((DEBUG_INFO, "PcieBaseSize 0x%lx\n", AcpiBoardInfo->PcieBaseSize));

  //
  // Verify values for proper operation
  //
  ASSERT(Fadt->Pm1aCntBlk != 0);
  ASSERT(Fadt->PmTmrBlk != 0);
  ASSERT(Fadt->ResetReg.Address != 0);
  ASSERT(Fadt->Pm1aEvtBlk != 0);
  ASSERT(Fadt->Gpe0Blk != 0);

  DEBUG_CODE_BEGIN ();
    BOOLEAN    SciEnabled;

    //
    // Check the consistency of SCI enabling
    //

    //
    // Get SCI_EN value
    //
   if (Fadt->Pm1CntLen == 4) {
      SciEnabled = (IoRead32 (Fadt->Pm1aCntBlk) & BIT0)? TRUE : FALSE;
    } else {
      //
      // if (Pm1CntLen == 2), use 16 bit IO read;
      // if (Pm1CntLen != 2 && Pm1CntLen != 4), use 16 bit IO read as a fallback
      //
      SciEnabled = (IoRead16 (Fadt->Pm1aCntBlk) & BIT0)? TRUE : FALSE;
    }

    if (!(Fadt->Flags & EFI_ACPI_5_0_HW_REDUCED_ACPI) &&
        (Fadt->SmiCmd == 0) &&
       !SciEnabled) {
      //
      // The ACPI enabling status is inconsistent: SCI is not enabled but ACPI
      // table does not provide a means to enable it through FADT->SmiCmd
      //
      DEBUG ((DEBUG_ERROR, "ERROR: The ACPI enabling status is inconsistent: SCI is not"
        " enabled but the ACPI table does not provide a means to enable it through FADT->SmiCmd."
        " This may cause issues in OS.\n"));
    }
  DEBUG_CODE_END ();

  return RETURN_SUCCESS;
}

/**
   Callback function to build resource descriptor HOB

   This function build a HOB based on the memory map entry info.
   It creates only EFI_RESOURCE_MEMORY_MAPPED_IO and EFI_RESOURCE_MEMORY_RESERVED
   resources.

   @param MemoryMapEntry         Memory map entry info got from bootloader.
   @param Params                 A pointer to ACPI_BOARD_INFO.

  @retval EFI_SUCCESS            Successfully build a HOB.
  @retval EFI_INVALID_PARAMETER  Invalid parameter provided.
**/
EFI_STATUS
MemInfoCallbackMmio (
  IN MEMORY_MAP_ENTRY  *MemoryMapEntry,
  IN VOID              *Params
  )
{
  EFI_PHYSICAL_ADDRESS         Base;
  EFI_RESOURCE_TYPE            Type;
  UINT64                       Size;
  EFI_RESOURCE_ATTRIBUTE_TYPE  Attribue;
  ACPI_BOARD_INFO              *AcpiBoardInfo;

  AcpiBoardInfo = (ACPI_BOARD_INFO *)Params;
  if (AcpiBoardInfo == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Skip types already handled in MemInfoCallback
  //
  if ((MemoryMapEntry->Type == E820_RAM) || (MemoryMapEntry->Type == E820_ACPI)) {
    return EFI_SUCCESS;
  }

  if (MemoryMapEntry->Base == AcpiBoardInfo->PcieBaseAddress) {
    //
    // MMCONF is always MMIO
    //
    Type = EFI_RESOURCE_MEMORY_MAPPED_IO;
  } else if (MemoryMapEntry->Base < mTopOfLowerUsableDram) {
    //
    // It's in DRAM and thus must be reserved
    //
    Type = EFI_RESOURCE_MEMORY_RESERVED;
  } else if ((MemoryMapEntry->Base < 0x100000000ULL) && (MemoryMapEntry->Base >= mTopOfLowerUsableDram)) {
    //
    // It's not in DRAM, must be MMIO
    //
    Type = EFI_RESOURCE_MEMORY_MAPPED_IO;
  } else {
    Type = EFI_RESOURCE_MEMORY_RESERVED;
  }

  Base = MemoryMapEntry->Base;
  Size = MemoryMapEntry->Size;

  Attribue = EFI_RESOURCE_ATTRIBUTE_PRESENT |
             EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
             EFI_RESOURCE_ATTRIBUTE_TESTED |
             EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE |
             EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
             EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |
             EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE;

  BuildResourceDescriptorHob (Type, Attribue, (EFI_PHYSICAL_ADDRESS)Base, Size);
  DEBUG ((DEBUG_INFO, "buildhob: base = 0x%lx, size = 0x%lx, type = 0x%x\n", Base, Size, Type));

  if ((MemoryMapEntry->Type == E820_UNUSABLE) ||
      (MemoryMapEntry->Type == E820_DISABLED))
  {
    BuildMemoryAllocationHob (Base, Size, EfiUnusableMemory);
  } else if (MemoryMapEntry->Type == E820_PMEM) {
    BuildMemoryAllocationHob (Base, Size, EfiPersistentMemory);
  }

  return EFI_SUCCESS;
}

/**
   Callback function to find TOLUD (Top of Lower Usable DRAM)

   Estimate where TOLUD (Top of Lower Usable DRAM) resides. The exact position
   would require platform specific code.

   @param MemoryMapEntry         Memory map entry info got from bootloader.
   @param Params                 Not used for now.

  @retval EFI_SUCCESS            Successfully updated mTopOfLowerUsableDram.
**/
EFI_STATUS
FindToludCallback (
  IN MEMORY_MAP_ENTRY  *MemoryMapEntry,
  IN VOID              *Params
  )
{
  //
  // This code assumes that the memory map on this x86 machine below 4GiB is continous
  // until TOLUD. In addition it assumes that the bootloader provided memory tables have
  // no "holes" and thus the first memory range not covered by e820 marks the end of
  // usable DRAM. In addition it's assumed that every reserved memory region touching
  // usable RAM is also covering DRAM, everything else that is marked reserved thus must be
  // MMIO not detectable by bootloader/OS
  //

  //
  // Skip memory types not RAM or reserved
  //
  if ((MemoryMapEntry->Type == E820_UNUSABLE) || (MemoryMapEntry->Type == E820_DISABLED) ||
      (MemoryMapEntry->Type == E820_PMEM))
  {
    return EFI_SUCCESS;
  }

  //
  // Skip resources above 4GiB
  //
  if ((MemoryMapEntry->Base + MemoryMapEntry->Size) > 0x100000000ULL) {
    return EFI_SUCCESS;
  }

  if ((MemoryMapEntry->Type == E820_RAM) || (MemoryMapEntry->Type == E820_ACPI) ||
      (MemoryMapEntry->Type == E820_NVS))
  {
    //
    // It's usable DRAM. Update TOLUD.
    //
    if (mTopOfLowerUsableDram < (MemoryMapEntry->Base + MemoryMapEntry->Size)) {
      mTopOfLowerUsableDram = (UINT32)(MemoryMapEntry->Base + MemoryMapEntry->Size);
    }
  } else {
    //
    // It might be 'reserved DRAM' or 'MMIO'.
    //
    // If it touches usable DRAM at Base assume it's DRAM as well,
    // as it could be bootloader installed tables, TSEG, GTT, ...
    //
    if (mTopOfLowerUsableDram == MemoryMapEntry->Base) {
      mTopOfLowerUsableDram = (UINT32)(MemoryMapEntry->Base + MemoryMapEntry->Size);
    }
  }

  return EFI_SUCCESS;
}

/**
   Callback function to build resource descriptor HOB

   This function build a HOB based on the memory map entry info.
   Only add EFI_RESOURCE_SYSTEM_MEMORY.

   @param MemoryMapEntry         Memory map entry info got from bootloader.
   @param Params                 Not used for now.

  @retval RETURN_SUCCESS        Successfully build a HOB.
**/
EFI_STATUS
MemInfoCallback (
  IN MEMORY_MAP_ENTRY  *MemoryMapEntry,
  IN VOID              *Params
  )
{
  EFI_PHYSICAL_ADDRESS         Base;
  EFI_RESOURCE_TYPE            Type;
  UINT64                       Size;
  EFI_RESOURCE_ATTRIBUTE_TYPE  Attribue;
  PAYLOAD_MEM_INFO             *PldMemInfo;

  //
  // Skip everything not known to be usable DRAM.
  // It will be added later.
  //
  if ((MemoryMapEntry->Type != E820_RAM) && (MemoryMapEntry->Type != E820_ACPI) &&
      (MemoryMapEntry->Type != E820_NVS))
  {
    return RETURN_SUCCESS;
  }

  PldMemInfo = (PAYLOAD_MEM_INFO *)Params;

  Type = EFI_RESOURCE_SYSTEM_MEMORY;
  Base = MemoryMapEntry->Base;
  Size = MemoryMapEntry->Size;

  Attribue = EFI_RESOURCE_ATTRIBUTE_PRESENT |
             EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
             EFI_RESOURCE_ATTRIBUTE_TESTED |
             EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE |
             EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
             EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |
             EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE;

  BuildResourceDescriptorHob (Type, Attribue, (EFI_PHYSICAL_ADDRESS)Base, Size);
  DEBUG ((DEBUG_INFO, "buildhob: base = 0x%lx, size = 0x%lx, type = 0x%x\n", Base, Size, Type));

  if (MemoryMapEntry->Type == E820_ACPI) {
    BuildMemoryAllocationHob (Base, Size, EfiACPIReclaimMemory);
  } else if (MemoryMapEntry->Type == E820_NVS) {
    BuildMemoryAllocationHob (Base, Size, EfiACPIMemoryNVS);
  }

  if (MemoryMapEntry->Type == E820_RAM && MemoryMapEntry->Base < mTopOfLowerUsableDram) {
    if (MemoryMapEntry->Base > PldMemInfo->UsableLowMemTop)
      PldMemInfo->UsableLowMemTop = MemoryMapEntry->Base + MemoryMapEntry->Size;
  }

  return RETURN_SUCCESS;
}

/**
  Check the integrity of firmware volume header.

  @retval  EFI_SUCCESS   - The firmware volume is consistent
  @retval  EFI_NOT_FOUND - The firmware volume has been corrupted.

**/
EFI_STATUS
ValidateFvHeader (
  SMMSTORE_INFO *SmmStoreInfo
  )
{
  UINT16                      Checksum;
  EFI_FIRMWARE_VOLUME_HEADER  *FwVolHeader;
  VARIABLE_STORE_HEADER       *VariableStoreHeader;
  UINTN                       VariableStoreLength;
  UINTN                       FvLength;
  UINT32                      NvStorageSize;
  UINT32                      NvVariableSize;
  UINT32                      FtwWorkingSize;
  UINT32                      FtwSpareSize;


  NvStorageSize = SmmStoreInfo->NumBlocks * SmmStoreInfo->BlockSize;
  FtwSpareSize   = (SmmStoreInfo->NumBlocks / 2) * SmmStoreInfo->BlockSize;
  FtwWorkingSize = SmmStoreInfo->BlockSize;
  NvVariableSize = NvStorageSize - FtwSpareSize - FtwWorkingSize;

  FwVolHeader        = (EFI_FIRMWARE_VOLUME_HEADER *)(UINTN)SmmStoreInfo->MmioAddress;
  if (!FwVolHeader) {
    return EFI_OUT_OF_RESOURCES;
  }


  FvLength = FtwSpareSize + FtwWorkingSize + NvVariableSize;

  //
  // Verify the header revision, header signature, length
  // Length of FvBlock cannot be 2**64-1
  // HeaderLength cannot be an odd number
  //
  if (  (FwVolHeader->Revision  != EFI_FVH_REVISION)
     || (FwVolHeader->Signature != EFI_FVH_SIGNATURE)
     || (FwVolHeader->FvLength  != FvLength)
        )
  {
    DEBUG ((
      DEBUG_INFO,
      "%a: No Firmware Volume header present\n",
      __FUNCTION__
      ));
    return EFI_NOT_FOUND;
  }

  // Check the Firmware Volume Guid
  if ( CompareGuid (&FwVolHeader->FileSystemGuid, &gEfiSystemNvDataFvGuid) == FALSE ) {
    DEBUG ((
      DEBUG_INFO,
      "%a: Firmware Volume Guid non-compatible\n",
      __FUNCTION__
      ));
    return EFI_NOT_FOUND;
  }

  // Verify the header checksum
  Checksum = CalculateSum16 ((UINT16 *)FwVolHeader, FwVolHeader->HeaderLength);
  if (Checksum != 0) {
    DEBUG ((
      DEBUG_INFO,
      "%a: FV checksum is invalid (Checksum:0x%X)\n",
      __FUNCTION__,
      Checksum
      ));
    return EFI_NOT_FOUND;
  }

  VariableStoreHeader = (VARIABLE_STORE_HEADER *)(UINTN)(SmmStoreInfo->MmioAddress + FwVolHeader->HeaderLength);

  // Check the Variable Store Guid
  if (!CompareGuid (&VariableStoreHeader->Signature, &gEfiVariableGuid) &&
      !CompareGuid (&VariableStoreHeader->Signature, &gEfiAuthenticatedVariableGuid))
  {
    DEBUG ((
      DEBUG_INFO,
      "%a: Variable Store Guid non-compatible\n",
      __FUNCTION__
      ));
    return EFI_NOT_FOUND;
  }

  VariableStoreLength = NvVariableSize - FwVolHeader->HeaderLength;
  if (VariableStoreHeader->Size != VariableStoreLength) {
    DEBUG ((
      DEBUG_INFO,
      "%a: Variable Store Length does not match\n",
      __FUNCTION__
      ));
    return EFI_NOT_FOUND;
  }

  return EFI_SUCCESS;
}

/**
  This is the entrypoint of PEIM

  @param  FileHandle  Handle of the file being invoked.
  @param  PeiServices Describes the list of possible PEI Services.

  @retval EFI_SUCCESS if it completed successfully.
**/
EFI_STATUS
EFIAPI
BlPeiEntryPoint (
  IN       EFI_PEI_FILE_HANDLE     FileHandle,
  IN CONST EFI_PEI_SERVICES        **PeiServices
  )
{
  EFI_STATUS                       Status;
  UINT64                           LowMemorySize;
  UINT64                           PeiMemSize = SIZE_64MB;
  EFI_PHYSICAL_ADDRESS             PeiMemBase = 0;
  UINT32                           RegEax;
  UINT8                            PhysicalAddressBits;
  PAYLOAD_MEM_INFO                 PldMemInfo;
  SYSTEM_TABLE_INFO                SysTableInfo;
  SYSTEM_TABLE_INFO                *NewSysTableInfo;
  ACPI_BOARD_INFO                  AcpiBoardInfo;
  ACPI_BOARD_INFO                  *NewAcpiBoardInfo;
  SMMSTORE_INFO                    SMMSTOREInfo;
  SMMSTORE_INFO                    *NewSMMSTOREInfo;
  TCG_PHYSICAL_PRESENCE_INFO       PhysicalPresenceInfo;
  TCG_PHYSICAL_PRESENCE_INFO       *NewPhysicalPresenceInfo;
  EFI_PEI_GRAPHICS_INFO_HOB        GfxInfo;
  EFI_PEI_GRAPHICS_INFO_HOB        *NewGfxInfo;
  EFI_PEI_GRAPHICS_DEVICE_INFO_HOB GfxDeviceInfo;
  EFI_PEI_GRAPHICS_DEVICE_INFO_HOB *NewGfxDeviceInfo;
  FIRMWARE_SEC_PERFORMANCE         Performance;

  //
  // First find TOLUD
  //
  DEBUG ((DEBUG_INFO, "Guessing Top of Lower Usable DRAM:\n"));
  Status = ParseMemoryInfo (FindToludCallback, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((DEBUG_INFO, "Assuming TOLUD = 0x%x\n", mTopOfLowerUsableDram));

  //
  // Parse memory info and build memory HOBs for Usable RAM
  //
  DEBUG ((DEBUG_INFO, "Building ResourceDescriptorHobs for usable memory:\n"));
  ZeroMem (&PldMemInfo, sizeof(PldMemInfo));
  Status = ParseMemoryInfo (MemInfoCallback, &PldMemInfo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Install memory
  //
  LowMemorySize = PldMemInfo.UsableLowMemTop;
  PeiMemBase = (LowMemorySize - PeiMemSize) & (~(BASE_64KB - 1));
  DEBUG ((DEBUG_INFO, "Low memory 0x%lx\n", LowMemorySize));
  DEBUG ((DEBUG_INFO, "PeiMemBase: 0x%lx.\n", PeiMemBase));
  DEBUG ((DEBUG_INFO, "PeiMemSize: 0x%lx.\n", PeiMemSize));
  Status = PeiServicesInstallPeiMemory (PeiMemBase, PeiMemSize);
  ASSERT_EFI_ERROR (Status);

  //
  // Set cache on the physical memory
  //
  MtrrSetMemoryAttribute (BASE_1MB, LowMemorySize - BASE_1MB, CacheWriteBack);
  MtrrSetMemoryAttribute (0, 0xA0000, CacheWriteBack);

  //
  // Create Memory Type Information HOB
  //
  BuildGuidDataHob (
    &gEfiMemoryTypeInformationGuid,
    mDefaultMemoryTypeInformation,
    sizeof(mDefaultMemoryTypeInformation)
    );

  //
  // Create Fv hob
  //
  PeiReportRemainedFvs ();

  BuildMemoryAllocationHob (
    PcdGet32 (PcdPayloadFdMemBase),
    PcdGet32 (PcdPayloadFdMemSize),
    EfiBootServicesData
    );

  //
  // Build CPU memory space and IO space hob
  //
  AsmCpuid (0x80000000, &RegEax, NULL, NULL, NULL);
  if (RegEax >= 0x80000008) {
    AsmCpuid (0x80000008, &RegEax, NULL, NULL, NULL);
    PhysicalAddressBits = (UINT8) RegEax;
  } else {
    PhysicalAddressBits  = 36;
  }

  //
  // Create a CPU hand-off information
  //
  BuildCpuHob (PhysicalAddressBits, 16);

  //
  // Report Local APIC range
  //
  BuildMemoryMappedIoRangeHob (0xFEC80000, SIZE_512KB);

  //
  // Boot mode
  //
  Status = PeiServicesSetBootMode (BOOT_WITH_FULL_CONFIGURATION);
  ASSERT_EFI_ERROR (Status);

  Status = PeiServicesInstallPpi (mPpiBootMode);
  ASSERT_EFI_ERROR (Status);

  //
  // Create guid hob for frame buffer information
  //
  Status = ParseGfxInfo (&GfxInfo);
  if (!EFI_ERROR (Status)) {
    NewGfxInfo = BuildGuidHob (&gEfiGraphicsInfoHobGuid, sizeof (GfxInfo));
    ASSERT (NewGfxInfo != NULL);
    CopyMem (NewGfxInfo, &GfxInfo, sizeof (GfxInfo));
    DEBUG ((DEBUG_INFO, "Created graphics info hob\n"));
  }


  Status = ParseGfxDeviceInfo (&GfxDeviceInfo);
  if (!EFI_ERROR (Status)) {
    NewGfxDeviceInfo = BuildGuidHob (&gEfiGraphicsDeviceInfoHobGuid, sizeof (GfxDeviceInfo));
    ASSERT (NewGfxDeviceInfo != NULL);
    CopyMem (NewGfxDeviceInfo, &GfxDeviceInfo, sizeof (GfxDeviceInfo));
    DEBUG ((DEBUG_INFO, "Created graphics device info hob\n"));
  }

  //
  // Create guid hob for SMMSTORE
  //
  Status = ParseSMMSTOREInfo (&SMMSTOREInfo);
  if (!EFI_ERROR (Status)) {
    NewSMMSTOREInfo = BuildGuidHob (&gEfiSmmStoreInfoHobGuid, sizeof (SMMSTOREInfo));
    ASSERT (NewSMMSTOREInfo != NULL);
    CopyMem (NewSMMSTOREInfo, &SMMSTOREInfo, sizeof (SMMSTOREInfo));
    DEBUG ((DEBUG_INFO, "Created SMMSTORE info hob\n"));

    Status = ValidateFvHeader (&SMMSTOREInfo);
    if (EFI_ERROR (Status)) {
      Status = PeiServicesSetBootMode (BOOT_WITH_DEFAULT_SETTINGS);
      DEBUG ((DEBUG_INFO, "BootMode: Boot with default settings\n"));
      ASSERT_EFI_ERROR (Status);
    } else {
      Status = PeiServicesSetBootMode (BOOT_ASSUMING_NO_CONFIGURATION_CHANGES);
      DEBUG ((DEBUG_INFO, "BootMode: Boot boot assuming no configuration changes\n"));
      ASSERT_EFI_ERROR (Status);
    }
  }

  //
  // Create guid hob for Tcg Physical Presence Interface
  //
  Status = ParseTPMPPIInfo (&PhysicalPresenceInfo);
  if (!EFI_ERROR (Status)) {
    NewPhysicalPresenceInfo = BuildGuidHob (&gEfiTcgPhysicalPresenceInfoHobGuid, sizeof (TCG_PHYSICAL_PRESENCE_INFO));
    ASSERT (NewPhysicalPresenceInfo != NULL);
    CopyMem (NewPhysicalPresenceInfo, &PhysicalPresenceInfo, sizeof (TCG_PHYSICAL_PRESENCE_INFO));
    DEBUG ((DEBUG_INFO, "Created Tcg Physical Presence info hob\n"));
  }

  //
  // Create guid hob for system tables like acpi table and smbios table
  //
  Status = ParseSystemTable(&SysTableInfo);
  ASSERT_EFI_ERROR (Status);
  if (!EFI_ERROR (Status)) {
    NewSysTableInfo = BuildGuidHob (&gUefiSystemTableInfoGuid, sizeof (SYSTEM_TABLE_INFO));
    ASSERT (NewSysTableInfo != NULL);
    CopyMem (NewSysTableInfo, &SysTableInfo, sizeof (SYSTEM_TABLE_INFO));
    DEBUG ((DEBUG_INFO, "Detected Acpi Table at 0x%lx, length 0x%x\n", SysTableInfo.AcpiTableBase, SysTableInfo.AcpiTableSize));
    DEBUG ((DEBUG_INFO, "Detected Smbios Table at 0x%lx, length 0x%x\n", SysTableInfo.SmbiosTableBase, SysTableInfo.SmbiosTableSize));
  }

  //
  // Create guid hob for acpi board information
  //
  Status = ParseAcpiInfo (SysTableInfo.AcpiTableBase, &AcpiBoardInfo);
  ASSERT_EFI_ERROR (Status);
  if (!EFI_ERROR (Status)) {
    NewAcpiBoardInfo = BuildGuidHob (&gUefiAcpiBoardInfoGuid, sizeof (ACPI_BOARD_INFO));
    ASSERT (NewAcpiBoardInfo != NULL);
    CopyMem (NewAcpiBoardInfo, &AcpiBoardInfo, sizeof (ACPI_BOARD_INFO));
    DEBUG ((DEBUG_INFO, "Create acpi board info guid hob\n"));
  }

  //
  // Parse memory info and build memory HOBs for reserved DRAM and MMIO
  //
  DEBUG ((DEBUG_INFO, "Building ResourceDescriptorHobs for reserved memory:\n"));
  Status = ParseMemoryInfo (MemInfoCallbackMmio, &AcpiBoardInfo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Build SEC Performance Data Hob
  Status =   ParseTimestampTable(&Performance);
  if (!EFI_ERROR (Status)) {
    BuildGuidDataHob (&gEfiFirmwarePerformanceGuid, &Performance, sizeof (Performance));
  } else {
    DEBUG ((DEBUG_ERROR, "Error when parsing timestamp info, Status = %r\n", Status));
  }

  //
  // Parse platform specific information.
  //
  Status = ParsePlatformInfo ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Error when parsing platform info, Status = %r\n", Status));
    return Status;
  }

  //
  // Import update capsules, if there are any.
  //
  Status = ParseCapsules (BuildCvHob);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Error when importing update capsules, Status = %r\n", Status));
    return Status;
  }

  //
  // Mask off all legacy 8259 interrupt sources
  //
  IoWrite8 (LEGACY_8259_MASK_REGISTER_MASTER, 0xFF);
  IoWrite8 (LEGACY_8259_MASK_REGISTER_SLAVE,  0xFF);

  return EFI_SUCCESS;
}

