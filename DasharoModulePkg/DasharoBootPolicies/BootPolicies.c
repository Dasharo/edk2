/*++
Copyright (c)  1999  - 2014, Intel Corporation. All rights reserved
                                                                                   
  SPDX-License-Identifier: BSD-2-Clause-Patent
                                                                                   
--*/

/** @file
**/

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiLib.h>
#include "BootPolicies.h"

#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

NETWORK_BOOT_POLICY_PROTOCOL  mNetworkBootPolicy;
USB_STACK_POLICY_PROTOCOL mUsbStackPolicy;
USB_MASS_STORAGE_POLICY_PROTOCOL mUsbMassStoragePolicy;

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

  gBS = SystemTable->BootServices;
  gRT = SystemTable->RuntimeServices;

  mNetworkBootPolicy.Revision			= NETWORK_BOOT_POLICY_PROTOCOL_REVISION_01;
  mNetworkBootPolicy.NetworkBootEnabled		= FALSE; // disable by default
  mUsbStackPolicy.Revision			= USB_STACK_POLICY_PROTOCOL_REVISION_01;
  mUsbStackPolicy.UsbStackEnabled		= TRUE;
  mUsbMassStoragePolicy.Revision		= USB_MASS_STORAGE_POLICY_PROTOCOL_REVISION_01;
  mUsbMassStoragePolicy.UsbMassStorageEnabled	= TRUE;

  Status = GetVariable2 (
             L"NetworkBoot",
             &gDasharoSystemFeaturesGuid,
             (VOID **) &EfiVar,
             &VarSize
             );

  if ((Status != EFI_NOT_FOUND) && (VarSize == sizeof(*EfiVar))) {

    mNetworkBootPolicy.NetworkBootEnabled = *EfiVar;

    if (mNetworkBootPolicy.NetworkBootEnabled)
      gBS->InstallMultipleProtocolInterfaces (
        &ImageHandle,
        &gDasharoNetworkBootPolicyGuid,
        &mNetworkBootPolicy,
        NULL
        );
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

    if (mUsbStackPolicy.UsbStackEnabled)
      gBS->InstallMultipleProtocolInterfaces (
        &ImageHandle,
        &gDasharoUsbDriverPolicyGuid,
        &mUsbStackPolicy,
        NULL
        );

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

    if (mUsbMassStoragePolicy.UsbMassStorageEnabled && mUsbStackPolicy.UsbStackEnabled)
      gBS->InstallMultipleProtocolInterfaces (
        &ImageHandle,
        &gDasharoUsbMassStoragePolicyGuid,
        &mUsbMassStoragePolicy,
        NULL
        );

  return EFI_SUCCESS;
}