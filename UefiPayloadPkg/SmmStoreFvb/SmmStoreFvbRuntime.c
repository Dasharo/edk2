/** @file  SmmStoreFvbRuntime.c

  Copyright (c) 2022, 9elements GmbH<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Guid/FaultTolerantWrite.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/DevicePathLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/PcdLib.h>
#include <Library/SmmStoreLib.h>
#include <Library/HobLib.h>

#include "SmmStoreFvbRuntime.h"

STATIC EFI_EVENT  mSmmStoreVirtualAddrChangeEvent;

//
// Global variable declarations
//
SMMSTORE_INSTANCE  *mSmmStoreInstance;

SMMSTORE_INSTANCE  mSmmStoreInstanceTemplate = {
  SMMSTORE_SIGNATURE, // Signature
  NULL,               // Handle ... NEED TO BE FILLED
  {
    FvbGetAttributes,      // GetAttributes
    FvbSetAttributes,      // SetAttributes
    FvbGetPhysicalAddress, // GetPhysicalAddress
    FvbGetBlockSize,       // GetBlockSize
    FvbRead,               // Read
    FvbWrite,              // Write
    FvbEraseBlocks,        // EraseBlocks
    NULL,                  // ParentHandle
  }, //  FvbProtoccol
  0, // BlockSize ... NEED TO BE FILLED
  0, // LastBlock ... NEED TO BE FILLED
  0, // MmioAddress ... NEED TO BE FILLED
  {
    {
      {
        HARDWARE_DEVICE_PATH,
        HW_MEMMAP_DP,
        {
          (UINT8)(sizeof (MEMMAP_DEVICE_PATH)),
          (UINT8)(sizeof (MEMMAP_DEVICE_PATH) >> 8)
        }
      },
      EfiMemoryMappedIO,
      (EFI_PHYSICAL_ADDRESS)0, // NEED TO BE FILLED
      (EFI_PHYSICAL_ADDRESS)0, // NEED TO BE FILLED
    },
    {
      END_DEVICE_PATH_TYPE,
      END_ENTIRE_DEVICE_PATH_SUBTYPE,
      {
        END_DEVICE_PATH_LENGTH,
        0
      }
    }
  } // DevicePath
};

/**
  Initialize the SmmStore instance.


  @param[in]      FvBase         The physical MMIO base address of the FV containing
                                 the variable store.

  @param[in]      NumberofBlocks Number of blocks within the FV.
  @param[in]      BlockSize      The size in bytes of one block within the FV.
  @param[in, out] Instance       The SmmStore instance to initialize.

**/
STATIC
EFI_STATUS
SmmStoreInitInstance (
  IN EFI_PHYSICAL_ADDRESS   FvBase,
  IN UINTN                  NumberofBlocks,
  IN UINTN                  BlockSize,
  IN OUT SMMSTORE_INSTANCE  *Instance
  )
{
  EFI_STATUS             Status;
  FV_MEMMAP_DEVICE_PATH  *FvDevicePath;

  ASSERT (Instance != NULL);

  Instance->BlockSize   = BlockSize;
  Instance->LastBlock   = NumberofBlocks - 1;
  Instance->MmioAddress = FvBase;

  FvDevicePath                                = &Instance->DevicePath;
  FvDevicePath->MemMapDevPath.StartingAddress = FvBase;
  FvDevicePath->MemMapDevPath.EndingAddress   = FvBase + BlockSize * NumberofBlocks - 1;

  Status = FvbInitialize (Instance);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Instance->Handle,
                  &gEfiDevicePathProtocolGuid,
                  &Instance->DevicePath,
                  &gEfiFirmwareVolumeBlockProtocolGuid,
                  &Instance->FvbProtocol,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((DEBUG_INFO, "%a: Created a new instance\n", __func__));

  return Status;
}

/**
  Fixup internal data so that EFI can be call in virtual mode.
  Call the passed in Child Notify event and convert any pointers in
  lib to virtual mode.

  @param[in]    Event   The Event that is being processed
  @param[in]    Context Event Context
**/
STATIC
VOID
EFIAPI
SmmStoreVirtualNotifyEvent (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  SmmStoreLibVirtualAddressChange (EfiConvertPointer);

  // Convert Fvb
  EfiConvertPointer (0x0, (VOID **)&mSmmStoreInstance->FvbProtocol.EraseBlocks);
  EfiConvertPointer (0x0, (VOID **)&mSmmStoreInstance->FvbProtocol.GetAttributes);
  EfiConvertPointer (0x0, (VOID **)&mSmmStoreInstance->FvbProtocol.GetBlockSize);
  EfiConvertPointer (0x0, (VOID **)&mSmmStoreInstance->FvbProtocol.GetPhysicalAddress);
  EfiConvertPointer (0x0, (VOID **)&mSmmStoreInstance->FvbProtocol.Read);
  EfiConvertPointer (0x0, (VOID **)&mSmmStoreInstance->FvbProtocol.SetAttributes);
  EfiConvertPointer (0x0, (VOID **)&mSmmStoreInstance->FvbProtocol.Write);
  EfiConvertPointer (0x0, (VOID **)&mSmmStoreInstance->MmioAddress);
  EfiConvertPointer (0x0, (VOID **)&mSmmStoreInstance);

  return;
}

/**
  Check whether a flash update was interrupted by a reboot and if so,
  recover variable storage by completing the operation.

  @param[in]  MmioAddress  MMIO base of the variable storage.
  @param[in]  BlockSize    Block size of the variable storage.

  @retval EFI_SUCCESS  No recovery needed or it was done successfully.
  @retval other        Recovery went wrong.
**/
STATIC
EFI_STATUS
RecoverVariableStorage (
  IN EFI_PHYSICAL_ADDRESS  MmioAddress,
  IN UINTN                 BlockSize
  )
{
  EFI_STATUS                            Status;
  UINTN                                 TargetOffset;
  UINTN                                 SpareOffset;
  UINTN                                 Copied;
  UINTN                                 NumBytes;
  VOID                                  *Buffer;
  EFI_HOB_GUID_TYPE                     *GuidHob;
  FAULT_TOLERANT_WRITE_LAST_WRITE_DATA  *FtwLastWrite;

  GuidHob = GetFirstGuidHob (&gEdkiiFaultTolerantWriteGuid);
  if (GuidHob == NULL) {
    DEBUG ((DEBUG_INFO, "%a: Recovery is not needed or not possible.\n", __FUNCTION__));
    return EFI_SUCCESS;
  }

  DEBUG ((DEBUG_INFO, "%a: Recovery is needed.\n", __FUNCTION__));
  FtwLastWrite = GET_GUID_HOB_DATA (GuidHob);

  // Validate alignment assumptions used in the loop below.
  TargetOffset = FtwLastWrite->TargetAddress - MmioAddress;
  SpareOffset  = FtwLastWrite->SpareAddress - MmioAddress;
  if (FtwLastWrite->Length % BlockSize != 0 ||
      TargetOffset % BlockSize != 0 || SpareOffset % BlockSize != 0) {
    DEBUG ((DEBUG_ERROR, "%a: Recovery information doesn't match SMMSTORE blocks.\n", __FUNCTION__));
    return EFI_INVALID_PARAMETER;
  }

  Buffer = AllocatePool (BlockSize);
  if (Buffer == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate a memory for recovery.\n", __FUNCTION__));
    return EFI_OUT_OF_RESOURCES;
  }

  Status = EFI_SUCCESS;
  for (Copied = 0; Copied < FtwLastWrite->Length; Copied += BlockSize) {
    NumBytes = BlockSize;
    Status   = SmmStoreLibRead ((SpareOffset + Copied) / BlockSize, 0, &NumBytes, Buffer);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Failed to read flash: %r.\n", __FUNCTION__, Status));
      break;
    }
    if (NumBytes != BlockSize) {
      DEBUG ((DEBUG_ERROR, "%a: Incomplete flash read: %d != %d.\n", __FUNCTION__, NumBytes, BlockSize));
      Status = EFI_DEVICE_ERROR;
      break;
    }

    Status = SmmStoreLibWrite ((TargetOffset + Copied) / BlockSize, 0, &NumBytes, Buffer);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Failed to write flash: %r.\n", __FUNCTION__, Status));
      break;
    }
    if (NumBytes != BlockSize) {
      DEBUG ((DEBUG_ERROR, "%a: Incomplete flash write: %d != %d.\n", __FUNCTION__, NumBytes, BlockSize));
      Status = EFI_DEVICE_ERROR;
      break;
    }
  }

  if (!EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "%a: Recovered variable storage.\n", __FUNCTION__));
  }

  FreePool (Buffer);
  return Status;
}

/**
  The user Entry Point for module SmmStoreFvbRuntimeDxe. The user code starts with this function.

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.
  @param[in] SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS       The entry point is executed successfully.
  @retval other             Some error occurs when executing this entry point.

**/
EFI_STATUS
EFIAPI
SmmStoreInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  MmioAddress;
  UINTN                 BlockSize;
  UINTN                 BlockCount;
  UINT32                NvStorageBase;
  UINT32                NvStorageSize;
  UINT32                NvVariableSize;
  UINT32                FtwWorkingSize;
  UINT32                FtwSpareSize;

  Status = SmmStoreLibInitialize ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to initialize SmmStoreLib\n", __func__));
    return Status;
  }

  Status = SmmStoreLibGetMmioAddress (&MmioAddress);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to get SmmStore MMIO address\n", __func__));
    SmmStoreLibDeinitialize ();
    return Status;
  }

  Status = SmmStoreLibGetNumBlocks (&BlockCount);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to get SmmStore No. blocks\n", __func__));
    SmmStoreLibDeinitialize ();
    return Status;
  }

  Status = SmmStoreLibGetBlockSize (&BlockSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to get SmmStore block size\n", __func__));
    SmmStoreLibDeinitialize ();
    return Status;
  }

  Status = RecoverVariableStorage (MmioAddress, BlockSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to recover variable storage.\n", __FUNCTION__));
    //
    // Keep going because not much can be done.  One strategy could be wiping whole SMMSTORE
    // and reboot to start with a clean slate, but that may not work if we ended up here.
    //
    // Also, hoping for the best we booted with BOOT_ASSUMING_NO_CONFIGURATION_CHANGES but now
    // we want BOOT_WITH_DEFAULT_SETTINGS.  It's possible to do something like
    //   ((EFI_HOB_HANDOFF_INFO_TABLE *)GetHobList ())->BootMode = BOOT_WITH_DEFAULT_SETTINGS;
    // but it may not be a good idea at this point.
    //
  }

  //
  // The layout (starts at MmioAddress):
  //
  //  -------------------------- ------------- ------------------------------
  // |                          |             |                              |
  // |         Variable         |   Working   |         Spare Range          |
  // |          Range           |    Range    | (larger than variable range) |
  // |                          |             |                              |
  //  <- (BlockCount / 2) - 1 -> <- 1 block -> <----- (BlockCount / 2) ----->
  //           blocks                                      blocks
  //

  NvStorageSize = BlockCount * BlockSize;
  NvStorageBase = MmioAddress;

  FtwSpareSize   = (BlockCount / 2) * BlockSize;
  FtwWorkingSize = 1 * BlockSize;
  NvVariableSize = NvStorageSize - FtwSpareSize - FtwWorkingSize;
  DEBUG ((DEBUG_INFO, "NvStorageBase:0x%x, NvStorageSize:0x%x\n", NvStorageBase, NvStorageSize));

  Status = PcdSet32S (PcdFlashNvStorageVariableSize, NvVariableSize);
  ASSERT_EFI_ERROR (Status);
  Status = PcdSet32S (PcdFlashNvStorageVariableBase, NvStorageBase);
  ASSERT_EFI_ERROR (Status);
  Status = PcdSet64S (PcdFlashNvStorageVariableBase64, NvStorageBase);
  ASSERT_EFI_ERROR (Status);

  Status = PcdSet32S (PcdFlashNvStorageFtwWorkingSize, FtwWorkingSize);
  ASSERT_EFI_ERROR (Status);
  Status = PcdSet32S (PcdFlashNvStorageFtwWorkingBase, NvStorageBase + NvVariableSize);
  ASSERT_EFI_ERROR (Status);
  Status = PcdSet64S (PcdFlashNvStorageFtwWorkingBase64, NvStorageBase + NvVariableSize);
  ASSERT_EFI_ERROR (Status);

  Status = PcdSet32S (PcdFlashNvStorageFtwSpareSize, FtwSpareSize);
  ASSERT_EFI_ERROR (Status);
  Status = PcdSet32S (PcdFlashNvStorageFtwSpareBase, NvStorageBase + NvVariableSize + FtwWorkingSize);
  ASSERT_EFI_ERROR (Status);
  Status = PcdSet64S (PcdFlashNvStorageFtwSpareBase64, NvStorageBase + NvVariableSize + FtwWorkingSize);
  ASSERT_EFI_ERROR (Status);

  mSmmStoreInstance = AllocateRuntimeCopyPool (sizeof (SMMSTORE_INSTANCE), &mSmmStoreInstanceTemplate);
  if (mSmmStoreInstance == NULL) {
    SmmStoreLibDeinitialize ();
    DEBUG ((DEBUG_ERROR, "%a: Out of resources\n", __func__));
    return EFI_OUT_OF_RESOURCES;
  }

  Status = SmmStoreInitInstance (
             MmioAddress,
             BlockCount,
             BlockSize,
             mSmmStoreInstance
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: Fail to create instance for SmmStore\n",
      __func__
      ));
    FreePool (mSmmStoreInstance);
    SmmStoreLibDeinitialize ();
    return Status;
  }

  //
  // Register for the virtual address change event
  //
  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  SmmStoreVirtualNotifyEvent,
                  NULL,
                  &gEfiEventVirtualAddressChangeGuid,
                  &mSmmStoreVirtualAddrChangeEvent
                  );
  ASSERT_EFI_ERROR (Status);

  return Status;
}
