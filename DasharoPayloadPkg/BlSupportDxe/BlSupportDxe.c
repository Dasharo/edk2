/** @file
  This driver will report some MMIO/IO resources to dxe core, extract smbios and acpi
  tables from bootloader.

  Copyright (c) 2014 - 2019, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/
#include "BlSupportDxe.h"

/**
  Reserve MMIO/IO resource in GCD

  @param  IsMMIO        Flag of whether it is mmio resource or io resource.
  @param  GcdType       Type of the space.
  @param  BaseAddress   Base address of the space.
  @param  Length        Length of the space.
  @param  Alignment     Align with 2^Alignment
  @param  ImageHandle   Handle for the image of this driver.

  @retval EFI_SUCCESS   Reserve successful
**/
EFI_STATUS
ReserveResourceInGcd (
  IN BOOLEAN               IsMMIO,
  IN UINTN                 GcdType,
  IN EFI_PHYSICAL_ADDRESS  BaseAddress,
  IN UINT64                Length,
  IN UINTN                 Alignment,
  IN EFI_HANDLE            ImageHandle
  )
{
  EFI_STATUS                              Status;
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR         GcdDescriptor;

  Status = gDS->GetMemorySpaceDescriptor ((EFI_PHYSICAL_ADDRESS)BaseAddress, &GcdDescriptor);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "Failed to look up memory space: 0x%lx 0x%lx\n",
      BaseAddress,
      Length
      ));
    return EFI_ACCESS_DENIED;
  }

  if (GcdDescriptor.GcdMemoryType != EfiGcdMemoryTypeNonExistent) {
    DEBUG ((
      DEBUG_ERROR,
      "Skipping to add memory space: 0x%lx 0x%lx, already exists\n",
      BaseAddress,
      Length
      ));
    return EFI_SUCCESS;
  }

  if (IsMMIO) {
    Status = gDS->AddMemorySpace (
                    GcdType,
                    BaseAddress,
                    Length,
                    EFI_MEMORY_UC
                    );
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "Failed to add memory space :0x%lx 0x%lx\n",
        BaseAddress,
        Length
        ));
    }
    ASSERT_EFI_ERROR (Status);
    Status = gDS->AllocateMemorySpace (
                    EfiGcdAllocateAddress,
                    GcdType,
                    Alignment,
                    Length,
                    &BaseAddress,
                    ImageHandle,
                    NULL
                    );
    ASSERT_EFI_ERROR (Status);
  } else {
    Status = gDS->AddIoSpace (
                    GcdType,
                    BaseAddress,
                    Length
                    );
    ASSERT_EFI_ERROR (Status);
    Status = gDS->AllocateIoSpace (
                    EfiGcdAllocateAddress,
                    GcdType,
                    Alignment,
                    Length,
                    &BaseAddress,
                    ImageHandle,
                    NULL
                    );
    ASSERT_EFI_ERROR (Status);
  }
  return Status;
}

EFI_STATUS
EFIAPI
BlDxeInstallSMBIOStables(
  IN UINT64    SmbiosTableBase,
  IN UINT32    SmbiosTableSize
)
{
  EFI_STATUS                    Status;
  SMBIOS_TABLE_ENTRY_POINT      *SmbiosTable;
  SMBIOS_TABLE_3_0_ENTRY_POINT  *Smbios30Table;
  SMBIOS_STRUCTURE_POINTER      Smbios;
  SMBIOS_STRUCTURE_POINTER      SmbiosEnd;
  CHAR8                         *String;
  EFI_SMBIOS_HANDLE             SmbiosHandle;
  EFI_SMBIOS_PROTOCOL           *SmbiosProto;

  //
  // Locate Smbios protocol.
  //
  Status = gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL, (VOID **)&SmbiosProto);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to locate gEfiSmbiosProtocolGuid\n",
        __FUNCTION__
    ));
    return Status;
  }

  Smbios30Table = (SMBIOS_TABLE_3_0_ENTRY_POINT *)(UINTN)(SmbiosTableBase);
  SmbiosTable = (SMBIOS_TABLE_ENTRY_POINT *)(UINTN)(SmbiosTableBase);

  if (CompareMem (Smbios30Table->AnchorString, "_SM3_", 5) == 0) {
    Smbios.Hdr = (SMBIOS_STRUCTURE *) (UINTN) Smbios30Table->TableAddress;
    SmbiosEnd.Raw = (UINT8 *) (UINTN) (Smbios30Table->TableAddress + Smbios30Table->TableMaximumSize);
    if (Smbios30Table->TableMaximumSize > SmbiosTableSize) {
      DEBUG((EFI_D_INFO, "%a: SMBIOS table size greater than reported by bootloader\n",
          __FUNCTION__));
    }
  } else if (CompareMem (SmbiosTable->AnchorString, "_SM_", 4) == 0) {
    Smbios.Hdr    = (SMBIOS_STRUCTURE *) (UINTN) SmbiosTable->TableAddress;
    SmbiosEnd.Raw = (UINT8 *) ((UINTN) SmbiosTable->TableAddress + SmbiosTable->TableLength);

    if (SmbiosTable->TableLength > SmbiosTableSize) {
      DEBUG((EFI_D_INFO, "%a: SMBIOS table size greater than reported by bootloader\n",
          __FUNCTION__
      ));
    }
  } else {
    DEBUG ((DEBUG_ERROR, "%a: No valid SMBIOS table found\n",
        __FUNCTION__
    ));
    return EFI_NOT_FOUND;
  }

  do {
    // Check for end marker
    if (Smbios.Hdr->Type == 127) {
      break;
    }

    // Install the table
    SmbiosHandle = SMBIOS_HANDLE_PI_RESERVED;
    Status = SmbiosProto->Add (
                          SmbiosProto,
                          gImageHandle,
                          &SmbiosHandle,
                          Smbios.Hdr
                          );
    ASSERT_EFI_ERROR (Status);

    //
    // Go to the next SMBIOS structure. Each SMBIOS structure may include 2 parts:
    // 1. Formatted section; 2. Unformatted string section. So, 2 steps are needed
    // to skip one SMBIOS structure.
    //

    //
    // Step 1: Skip over formatted section.
    //
    String = (CHAR8 *) (Smbios.Raw + Smbios.Hdr->Length);

    //
    // Step 2: Skip over unformatted string section.
    //
    do {
      //
      // Each string is terminated with a NULL(00h) BYTE and the sets of strings
      // is terminated with an additional NULL(00h) BYTE.
      //
      for ( ; *String != 0; String++) {
      }

      if (*(UINT8*)++String == 0) {
        //
        // Pointer to the next SMBIOS structure.
        //
        Smbios.Raw = (UINT8 *)++String;
        break;
      }
    } while (TRUE);
  } while (Smbios.Raw < SmbiosEnd.Raw);

  return EFI_SUCCESS;
}

/**
  Main entry for the bootloader support DXE module.

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.
  @param[in] SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS       The entry point is executed successfully.
  @retval other             Some error occurs when executing this entry point.

**/
EFI_STATUS
EFIAPI
BlDxeEntryPoint (
  IN EFI_HANDLE              ImageHandle,
  IN EFI_SYSTEM_TABLE        *SystemTable
  )
{
  EFI_STATUS                              Status;
  EFI_HOB_GUID_TYPE                       *GuidHob;
  SYSTEM_TABLE_INFO                       *SystemTableInfo;
  EFI_PEI_GRAPHICS_INFO_HOB               *GfxInfo;
  EFI_GUID                                FwGuid;
  UINT32                                  FwVersion;
  UINT32                                  FwLsv;
  UINT32                                  FwSize;
  EFI_SYSTEM_RESOURCE_TABLE               *Esrt;
  EFI_SYSTEM_RESOURCE_ENTRY               *Esre;

  Status = EFI_SUCCESS;
  //
  // Report MMIO/IO Resources
  //
  Status = ReserveResourceInGcd (TRUE, EfiGcdMemoryTypeMemoryMappedIo, 0xFEC00000, SIZE_4KB, 0, ImageHandle); // IOAPIC
  ASSERT_EFI_ERROR (Status);

  Status = ReserveResourceInGcd (TRUE, EfiGcdMemoryTypeMemoryMappedIo, 0xFED00000, SIZE_1KB, 0, ImageHandle); // HPET
  ASSERT_EFI_ERROR (Status);

  //
  // Find the system table information guid hob
  //
  GuidHob = GetFirstGuidHob (&gUefiSystemTableInfoGuid);
  ASSERT (GuidHob != NULL);
  SystemTableInfo = (SYSTEM_TABLE_INFO *)GET_GUID_HOB_DATA (GuidHob);

  //
  // Install Smbios Table
  //
  if (SystemTableInfo->SmbiosTableBase != 0 && SystemTableInfo->SmbiosTableSize != 0) {
    DEBUG ((DEBUG_ERROR, "Install Smbios Table at 0x%lx, length 0x%x\n", SystemTableInfo->SmbiosTableBase, SystemTableInfo->SmbiosTableSize));

    if (BlDxeInstallSMBIOStables(SystemTableInfo->SmbiosTableBase,
        SystemTableInfo->SmbiosTableSize) != EFI_SUCCESS) {
      Status = gBS->InstallConfigurationTable (&gEfiSmbiosTableGuid, (VOID *)(UINTN)SystemTableInfo->SmbiosTableBase);
      ASSERT_EFI_ERROR (Status);
    }
  }

  //
  // Find the frame buffer information and update PCDs
  //
  GuidHob = GetFirstGuidHob (&gEfiGraphicsInfoHobGuid);
  if (GuidHob != NULL) {
    GfxInfo = (EFI_PEI_GRAPHICS_INFO_HOB *)GET_GUID_HOB_DATA (GuidHob);
    Status = PcdSet32S (PcdVideoHorizontalResolution, GfxInfo->GraphicsMode.HorizontalResolution);
    ASSERT_EFI_ERROR (Status);
    Status = PcdSet32S (PcdVideoVerticalResolution, GfxInfo->GraphicsMode.VerticalResolution);
    ASSERT_EFI_ERROR (Status);
    Status = PcdSet32S (PcdSetupVideoHorizontalResolution, GfxInfo->GraphicsMode.HorizontalResolution);
    ASSERT_EFI_ERROR (Status);
    Status = PcdSet32S (PcdSetupVideoVerticalResolution, GfxInfo->GraphicsMode.VerticalResolution);
    ASSERT_EFI_ERROR (Status);
  }

  Status = ParseFwInfo (&FwGuid, &FwVersion, &FwLsv, &FwSize);
  if (!EFI_ERROR (Status)) {
    Esrt = AllocateZeroPool (sizeof (EFI_SYSTEM_RESOURCE_TABLE) + sizeof (EFI_SYSTEM_RESOURCE_ENTRY));
    ASSERT (Esrt != NULL);

    Esrt->FwResourceVersion  = EFI_SYSTEM_RESOURCE_TABLE_FIRMWARE_RESOURCE_VERSION;
    Esrt->FwResourceCount    = 1;
    Esrt->FwResourceCountMax = 1;

    Esre = (EFI_SYSTEM_RESOURCE_ENTRY *)&Esrt[1];
    CopyMem (&Esre->FwClass, &FwGuid, sizeof (EFI_GUID));
    Esre->FwType                   = ESRT_FW_TYPE_SYSTEMFIRMWARE;
    Esre->FwVersion                = FwVersion;
    Esre->LowestSupportedFwVersion = FwLsv;

    Status = gBS->InstallConfigurationTable (&gEfiSystemResourceTableGuid, Esrt);
    ASSERT_EFI_ERROR (Status);
  }

  return EFI_SUCCESS;
}

