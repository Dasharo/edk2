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
SERIAL_REDIRECTION_POLICY_PROTOCOL mSerialRedirectionPolicy;

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
  mPs2ControllerPolicy.Revision			= PS2_CONTROLLER_POLICY_PROTOCOL_REVISION_01;
  mPs2ControllerPolicy.Ps2ControllerEnabled	= TRUE;
  mSerialRedirectionPolicy.Revision = PS2_CONTROLLER_POLICY_PROTOCOL_REVISION_01;
  mSerialRedirectionPolicy.SerialRedirectionEnabled = FALSE;

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

  VarSize = sizeof(BOOLEAN);
  Status = GetVariable2 (
             L"SerialRedirection",
             &gDasharoSystemFeaturesGuid,
             (VOID **) &EfiVar,
             &VarSize
             );


  if (EFI_ERROR (Status))
    mSerialRedirectionPolicy.SerialRedirectionEnabled = FixedPcdGetBool(PcdSerialRedirectionDefaultState);
  else if (!EFI_ERROR (Status) && (VarSize == sizeof(*EfiVar)))
    mSerialRedirectionPolicy.SerialRedirectionEnabled = *EfiVar;

  /* Check if second port redirection is enabled */
  if (FixedPcdGetBool (PcdHave2ndUart)) {
    VarSize = sizeof(BOOLEAN);
    Status = GetVariable2 (
               L"SerialRedirection2",
               &gDasharoSystemFeaturesGuid,
               (VOID **) &EfiVar,
               &VarSize
               );

    if (EFI_ERROR (Status))
      mSerialRedirectionPolicy.SerialRedirectionEnabled |= FixedPcdGetBool(PcdSerialRedirection2DefaultState);
    else if (!EFI_ERROR (Status) && (VarSize == sizeof(*EfiVar)))
      mSerialRedirectionPolicy.SerialRedirectionEnabled |= *EfiVar;
  }

  if (mSerialRedirectionPolicy.SerialRedirectionEnabled) {
    gBS->InstallMultipleProtocolInterfaces (
      &ImageHandle,
      &gDasharoSerialRedirectionPolicyGuid,
      &mSerialRedirectionPolicy,
      NULL
      );
    DEBUG ((EFI_D_INFO, "Boot Policy: Enabling Serial Redirection\n"));
  }

  return EFI_SUCCESS;
}
