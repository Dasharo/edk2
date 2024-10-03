/*++
Copyright (c)  1999  - 2014, Intel Corporation. All rights reserved

  SPDX-License-Identifier: BSD-2-Clause-Patent

--*/

/** @file
**/

#include <Library/PcdLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include <DasharoOptions.h>

/**
  Entry point for the Boot Policies Driver.
  @param ImageHandle       Image handle of this driver.
  @param SystemTable       Global system service table.
  @retval EFI_SUCCESS      Initialization complete.
**/
EFI_STATUS
EFIAPI
InitializeBootPolicies (
  IN EFI_HANDLE       ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )

{
  EFI_STATUS Status;
  UINTN VarSize;
  DASHARO_IOMMU_CONFIG *IommuConfig;
  UINT8 PcdVal = 0;

  gBS = SystemTable->BootServices;
  gRT = SystemTable->RuntimeServices;

  VarSize = sizeof(*IommuConfig);
  Status = GetVariable2 (
           L"IommuConfig",
           &gDasharoSystemFeaturesGuid,
           (VOID **) &IommuConfig,
           &VarSize
           );

  if ((Status == EFI_SUCCESS) && (VarSize == sizeof(*IommuConfig))){
    PcdVal = PcdGet8(PcdVTdPolicyPropertyMask);
    if (IommuConfig->IommuEnable){
      PcdVal |= 0x01;
      if (IommuConfig->IommuHandoff){
        PcdVal |= 0x02;
        DEBUG ((EFI_D_INFO, "Boot Policy: IOMMU will be kept enabled on ExitBootServices\n"));
      }
      else{
        PcdVal &= (~0x02);
        DEBUG ((EFI_D_INFO, "Boot Policy: IOMMU will be disabled on ExitBootServices\n"));
      }
      PcdSet8S(PcdVTdPolicyPropertyMask, PcdVal);
    } else {
      PcdSet8S(PcdVTdPolicyPropertyMask, PcdVal & (~0x03));
      DEBUG ((EFI_D_INFO, "Boot Policy: DMA protection disabled\n"));
    }
  } else {
    PcdSet8S(PcdVTdPolicyPropertyMask, PcdVal & (~0x03));
    DEBUG ((EFI_D_INFO, "Boot Policy: DMA protection disabled\n"));
  }

  return EFI_SUCCESS;
}
