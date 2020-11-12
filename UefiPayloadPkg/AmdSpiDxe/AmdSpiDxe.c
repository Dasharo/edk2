/** @file  AmdSpiStoreDxe.c

  Copyright (c) 2020, 3mdeb Embedded Systems Consulting<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/PcdLib.h>
#include <Library/HobLib.h>
#include <Library/AmdSpiLib.h>

#include "AmdSpiDxe.h"

STATIC EFI_EVENT mAmdSpiVirtualAddrChangeEvent;

//
// Global variable declarations
//
AMD_SPI_INSTANCE *mAMDSpiInstance;

AMD_SPI_INSTANCE  mAMDSpiInstanceTemplate = {
  AMD_SPI_SIGNATURE, // Signature
  NULL, // Handle ... NEED TO BE FILLED
  {
    0, // MediaId ... NEED TO BE FILLED
    FALSE, // RemovableMedia
    TRUE, // MediaPresent
    FALSE, // LogicalPartition
    FALSE, // ReadOnly
    FALSE, // WriteCaching;
    0, // BlockSize ... NEED TO BE FILLED
    4, //  IoAlign
    0, // LastBlock ... NEED TO BE FILLED
    0, // LowestAlignedLba
    1, // LogicalBlocksPerPhysicalBlock
  }, //Media;

  {
    FvbGetAttributes, // GetAttributes
    FvbSetAttributes, // SetAttributes
    FvbGetPhysicalAddress,  // GetPhysicalAddress
    FvbGetBlockSize,  // GetBlockSize
    FvbRead,  // Read
    FvbWrite, // Write
    FvbEraseBlocks, // EraseBlocks
    NULL, //ParentHandle
  }, //  FvbProtoccol;
  {
    {
      {
        HARDWARE_DEVICE_PATH,
        HW_VENDOR_DP,
        {
          (UINT8)(OFFSET_OF (NOR_FLASH_DEVICE_PATH, End)),
          (UINT8)(OFFSET_OF (NOR_FLASH_DEVICE_PATH, End) >> 8)
        }
      },
      { 0x0, 0x0, 0x0, { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 } }, // GUID ... NEED TO BE FILLED
    },
    0, // Index
    {
      END_DEVICE_PATH_TYPE,
      END_ENTIRE_DEVICE_PATH_SUBTYPE,
      { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 }
    }
    } // DevicePath
};

STATIC
EFI_STATUS
AmdSpiCreateInstance (
  IN UINTN                  NumberofBlocks,
  IN UINTN                  BlockSize,
  OUT AMD_SPI_INSTANCE**    AMDSpiInstance
  )
{
  EFI_STATUS Status;
  AMD_SPI_INSTANCE* Instance;

  ASSERT(AMDSpiInstance != NULL);

  Instance = AllocateRuntimeCopyPool (sizeof(AMD_SPI_INSTANCE),&mAMDSpiInstanceTemplate);
  if (Instance == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Instance->Media.MediaId = 0;
  Instance->Media.BlockSize = BlockSize;
  Instance->Media.LastBlock = NumberofBlocks - 1;

  CopyGuid (&Instance->DevicePath.Vendor.Guid, &gEfiCallerIdGuid);
  Instance->DevicePath.Index = (UINT8)0;

  Status = AmdSpiFvbInitialize (Instance);
  if (EFI_ERROR(Status)) {
    FreePool (Instance);
    return Status;
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                &Instance->Handle,
                &gEfiDevicePathProtocolGuid, &Instance->DevicePath,
                &gEfiFirmwareVolumeBlockProtocolGuid, &Instance->FvbProtocol,
                NULL
                );
  if (EFI_ERROR(Status)) {
    FreePool (Instance);
    return Status;
  }

  DEBUG((DEBUG_INFO, "%a: Created a new instance\n", __FUNCTION__));

  *AMDSpiInstance = Instance;
  return Status;
}

/**
  Fixup internal data so that EFI can be call in virtual mode.
  Call the passed in Child Notify event and convert any pointers in
  lib to virtual mode.

  @param[in]    Event   The Event that is being processed
  @param[in]    Context Event Context
**/
VOID
EFIAPI
BlAMDSpiVirtualNotifyEvent (
  IN EFI_EVENT        Event,
  IN VOID             *Context
  )
{
  // Convert Fvb
  EfiConvertPointer (0x0, (VOID**)&mAMDSpiInstance->FvbProtocol.EraseBlocks);
  EfiConvertPointer (0x0, (VOID**)&mAMDSpiInstance->FvbProtocol.GetAttributes);
  EfiConvertPointer (0x0, (VOID**)&mAMDSpiInstance->FvbProtocol.GetBlockSize);
  EfiConvertPointer (0x0, (VOID**)&mAMDSpiInstance->FvbProtocol.GetPhysicalAddress);
  EfiConvertPointer (0x0, (VOID**)&mAMDSpiInstance->FvbProtocol.Read);
  EfiConvertPointer (0x0, (VOID**)&mAMDSpiInstance->FvbProtocol.SetAttributes);
  EfiConvertPointer (0x0, (VOID**)&mAMDSpiInstance->FvbProtocol.Write);

  return;
}

EFI_STATUS
EFIAPI
BlAmdSpiInitialise (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
  )
{
  EFI_STATUS                              Status;
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR         GcdDescriptor;

  if (PcdGetBool (PcdEmuVariableNvModeEnable)) {
    DEBUG ((DEBUG_WARN, "Variable emulation is active! Skipping driver init.\n"));
    return EFI_SUCCESS;
  }

  Status = AmdSpiInitialize();
  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR,"%a: Failed to initialize AmdSpi\n", __FUNCTION__));
    PcdSetBoolS (PcdEmuVariableNvModeEnable, TRUE);
    return Status;
  }

  mAMDSpiInstance = AllocateRuntimePool (sizeof(AMD_SPI_INSTANCE*));
  if (!mAMDSpiInstance) {
    DEBUG((EFI_D_ERROR, "%a: Out of resources\n", __FUNCTION__));
    PcdSetBoolS (PcdEmuVariableNvModeEnable, TRUE);
    return EFI_OUT_OF_RESOURCES;
  }

  Status = AmdSpiCreateInstance (3, 0x10000, &mAMDSpiInstance);
  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "%a: Fail to create instance for AmdSpi\n",
      __FUNCTION__));
    PcdSetBoolS (PcdEmuVariableNvModeEnable, TRUE);
    return Status;
  }

  //
  // Register for the virtual address change event
  //
  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  BlAMDSpiVirtualNotifyEvent,
                  NULL,
                  &gEfiEventVirtualAddressChangeGuid,
                  &mAmdSpiVirtualAddrChangeEvent
                  );
  ASSERT_EFI_ERROR (Status);

  //
  // Mark the memory mapped store as MMIO memory
  //
  Status      = gDS->GetMemorySpaceDescriptor (PcdGet32(PcdFlashNvStorageVariableBase), &GcdDescriptor);
  if (EFI_ERROR (Status) || GcdDescriptor.GcdMemoryType != EfiGcdMemoryTypeMemoryMappedIo) {
    DEBUG((EFI_D_INFO, "%a: No memory space descriptor for com buffer found\n",
      __FUNCTION__));

    //
    // Add a new entry if not covered by existing mapping
    //
    Status = gDS->AddMemorySpace (
        EfiGcdMemoryTypeMemoryMappedIo,
        PcdGet32(PcdFlashNvStorageVariableBase),
        3 * 0x10000,
        EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
        );
    ASSERT_EFI_ERROR (Status);
  }

  //
  // Mark as runtime service
  //
  Status = gDS->SetMemorySpaceAttributes (
                  PcdGet32(PcdFlashNvStorageVariableBase),
                  3 * 0x10000,
                  EFI_MEMORY_RUNTIME
                  );
  ASSERT_EFI_ERROR (Status);

  return Status;
}
