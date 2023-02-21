/*++
Copyright (c)  1999  - 2014, Intel Corporation. All rights reserved
                                                                                   
  SPDX-License-Identifier: BSD-2-Clause-Patent
                                                                                   
--*/

/** @file
**/

#include <Library/PcdLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiLib.h>
#include "BootPolicies.h"
#include "Library/DasharoSystemFeaturesUiLib/DasharoSystemFeaturesHii.h"

#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

NETWORK_BOOT_POLICY_PROTOCOL  mNetworkBootPolicy;
USB_STACK_POLICY_PROTOCOL mUsbStackPolicy;
USB_MASS_STORAGE_POLICY_PROTOCOL mUsbMassStoragePolicy;
PS2_CONTROLLER_POLICY_PROTOCOL mPs2ControllerPolicy;

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
  EFI_STATUS  Status = EFI_SUCCESS;
  BOOLEAN *EfiVar;
  UINTN VarSize = sizeof(BOOLEAN);
  UINT8 PcdVal = 0;

  gBS = SystemTable->BootServices;
  gRT = SystemTable->RuntimeServices;

  mNetworkBootPolicy.Revision			= NETWORK_BOOT_POLICY_PROTOCOL_REVISION_01;
  mNetworkBootPolicy.NetworkBootEnabled		= FALSE; // disable by default
  mUsbStackPolicy.Revision			= USB_STACK_POLICY_PROTOCOL_REVISION_01;
  mUsbStackPolicy.UsbStackEnabled		= TRUE;
  mUsbMassStoragePolicy.Revision		= USB_MASS_STORAGE_POLICY_PROTOCOL_REVISION_01;
  mUsbMassStoragePolicy.UsbMassStorageEnabled	= TRUE;
  mPs2ControllerPolicy.Revision			= PS2_CONTROLLER_POLICY_PROTOCOL_REVISION_01;
  mPs2ControllerPolicy.Ps2ControllerEnabled	= TRUE;

  Status = GetVariable2 (
             L"NetworkBoot",
             &gDasharoSystemFeaturesGuid,
             (VOID **) &EfiVar,
             &VarSize
             );


  if (Status == EFI_NOT_FOUND)
    mNetworkBootPolicy.NetworkBootEnabled = FixedPcdGetBool(PcdDefaultNetworkBootEnable);
  else if ((Status != EFI_NOT_FOUND) && (VarSize == sizeof(*EfiVar)))
    mNetworkBootPolicy.NetworkBootEnabled = *EfiVar;

  if (mNetworkBootPolicy.NetworkBootEnabled) {
    gBS->InstallMultipleProtocolInterfaces (
      &ImageHandle,
      &gDasharoNetworkBootPolicyGuid,
      &mNetworkBootPolicy,
      NULL
      );
    DEBUG ((EFI_D_INFO, "Boot Policy: Enabling network stack\n"));
  }

  Status = GetVariable2 (
             L"UsbDriverStack",
             &gDasharoSystemFeaturesGuid,
             (VOID **) &EfiVar,
             &VarSize
             );

  if ((Status == EFI_SUCCESS) && (VarSize == sizeof(*EfiVar)))
    mUsbStackPolicy.UsbStackEnabled = *EfiVar;
  else
    mUsbStackPolicy.UsbStackEnabled = TRUE; // enable USB by default

  if (mUsbStackPolicy.UsbStackEnabled) {
    gBS->InstallMultipleProtocolInterfaces (
      &ImageHandle,
      &gDasharoUsbDriverPolicyGuid,
      &mUsbStackPolicy,
      NULL
      );
    DEBUG ((EFI_D_INFO, "Boot Policy: Enabling USB stack\n"));
  } else {
    DEBUG ((EFI_D_INFO, "Boot Policy: Not enabling USB stack\n"));
  }

  Status = GetVariable2 (
             L"UsbMassStorage",
             &gDasharoSystemFeaturesGuid,
             (VOID **) &EfiVar,
             &VarSize
             );

  if ((Status == EFI_SUCCESS) && (VarSize == sizeof(*EfiVar)))
    mUsbMassStoragePolicy.UsbMassStorageEnabled = *EfiVar;
  else
    mUsbMassStoragePolicy.UsbMassStorageEnabled = TRUE; // enable USB boot by default

  if (mUsbMassStoragePolicy.UsbMassStorageEnabled && mUsbStackPolicy.UsbStackEnabled) {
    gBS->InstallMultipleProtocolInterfaces (
      &ImageHandle,
      &gDasharoUsbMassStoragePolicyGuid,
      &mUsbMassStoragePolicy,
      NULL
      );
    DEBUG ((EFI_D_INFO, "Boot Policy: Enabling USB Mass Storage\n"));
  } else {
    DEBUG ((EFI_D_INFO, "Boot Policy: Not enabling USB Mass Storage\n"));
  }

  Status = GetVariable2 (
             L"Ps2Controller",
             &gDasharoSystemFeaturesGuid,
             (VOID **) &EfiVar,
             &VarSize
             );

  if ((Status != EFI_NOT_FOUND) && (VarSize == sizeof(*EfiVar)))
    mPs2ControllerPolicy.Ps2ControllerEnabled = *EfiVar;
  else
    mPs2ControllerPolicy.Ps2ControllerEnabled = TRUE; // enable PS2 by default

  if (mPs2ControllerPolicy.Ps2ControllerEnabled) {
    gBS->InstallMultipleProtocolInterfaces (
      &ImageHandle,
      &gDasharoPs2ControllerPolicyGuid,
      &mPs2ControllerPolicy,
      NULL
      );
    DEBUG ((EFI_D_INFO, "Boot Policy: Enabling PS2 Controller\n"));
  }

  Status = GetVariable2 (
           L"IommuConfig",
           &gDasharoSystemFeaturesGuid,
           (VOID **) &EfiVar,
           &VarSize
           );

  if ((Status != EFI_NOT_FOUND) && (VarSize == sizeof(*EfiVar))){
    PcdVal = PcdGet8(PcdVTdPolicyPropertyMask);
    if (*EfiVar){
      PcdVal |= 0x01;
      if (*EfiVar == DMA_MODE_ENABLE_EBS){
        PcdVal |= 0x02;
        
        DEBUG ((EFI_D_INFO, "Boot Policy: IOMMU handoff at ExitBootServices\n"));
      }
      else{
        PcdVal &= (~0x02);
        DEBUG ((EFI_D_INFO, "Boot Policy: IOMMU handoff at ReadyToBoot\n"));
      }
      PcdSet8S(PcdVTdPolicyPropertyMask, PcdVal);
    } else {
      PcdSet8S(PcdVTdPolicyPropertyMask, PcdVal & (~0x03));
      DEBUG ((EFI_D_INFO, "Boot Policy: DMA protection disabled\n"));
    }
  }

  return EFI_SUCCESS;
}
