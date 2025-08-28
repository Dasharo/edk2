/** @file
  This module installs gVariableFlashInfoHobGuid PPI that serves as a source of
  information about variable storage for VariableFlashInfoLib.

Copyright (c) 2025, 3mdeb Sp. z o.o. All rights reserved.<BR>
SPDX-License-Identifier: GPL-2.0-or-later

**/

#include <PiPei.h>

#include <Coreboot.h>
#include <Guid/VariableFlashInfo.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BlParseLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/PeiServicesLib.h>

EFI_PEI_PPI_DESCRIPTOR  mPpiListVariable = {
  (EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST),
  &gVariableFlashInfoHobGuid,
  NULL
};

/**
  Main entry for SMMSTORE PEIM.

  @param[in]  FileHandle              Handle of the file being invoked.
  @param[in]  PeiServices             Pointer to PEI Services table.

  @retval EFI_SUCCESS  On success.
  @retval Others       On trouble with SMMSTORE information or PPI.

**/
EFI_STATUS
EFIAPI
SmmStorePeiInitialize (
  IN       EFI_PEI_FILE_HANDLE  FileHandle,
  IN CONST EFI_PEI_SERVICES     **PeiServices
  )
{
  EFI_STATUS           Status;
  SMMSTORE_INFO        SmmStoreInfo;
  VARIABLE_FLASH_INFO  VariableFlashInfo;
  UINT32               NvStorageBase;
  UINT32               NvStorageSize;
  UINT32               NvVariableSize;
  UINT32               FtwWorkingSize;
  UINT32               FtwSpareSize;

  Status = ParseSMMSTOREInfo (&SmmStoreInfo);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "SmmStorePei: ParseSMMSTOREInfo() failed: %r.\n",
      Status
      ));
    return Status;
  }

  NvStorageSize = SmmStoreInfo.NumBlocks * SmmStoreInfo.BlockSize;
  NvStorageBase = SmmStoreInfo.MmioAddress;
  DEBUG ((
    DEBUG_INFO,
    "SmmStorePei: NvStorageBase: 0x%x, NvStorageSize: 0x%x\n",
    NvStorageBase,
    NvStorageSize
    ));

  FtwSpareSize   = (SmmStoreInfo.NumBlocks / 2) * SmmStoreInfo.BlockSize;
  FtwWorkingSize = SmmStoreInfo.BlockSize;
  NvVariableSize = NvStorageSize - FtwSpareSize - FtwWorkingSize;
  if (NvVariableSize >= 0x80000000) {
    DEBUG ((
      DEBUG_ERROR,
      "SmmStorePei: NvStorageSize is too large: 0x%x > 0x80000000.\n",
      NvVariableSize
      ));
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (&VariableFlashInfo, sizeof (VariableFlashInfo));

  VariableFlashInfo.Version               = VARIABLE_FLASH_INFO_HOB_VERSION;
  VariableFlashInfo.NvVariableBaseAddress = NvStorageBase;
  VariableFlashInfo.NvVariableLength      = NvVariableSize;
  VariableFlashInfo.FtwSpareBaseAddress   = NvStorageBase + NvVariableSize + FtwWorkingSize;
  VariableFlashInfo.FtwSpareLength        = FtwSpareSize;
  VariableFlashInfo.FtwWorkingBaseAddress = NvStorageBase + NvVariableSize;
  VariableFlashInfo.FtwWorkingLength      = FtwWorkingSize;

  BuildGuidDataHob (&gVariableFlashInfoHobGuid, &VariableFlashInfo, sizeof (VariableFlashInfo));
  return PeiServicesInstallPpi (&mPpiListVariable);
}
