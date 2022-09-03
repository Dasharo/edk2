/*++
Copyright (c)  1999  - 2014, Intel Corporation. All rights reserved
                                                                                   
  SPDX-License-Identifier: BSD-2-Clause-Patent
                                                                                   
--*/

/** @file
**/

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiLib.h>
#include "NetworkBootPolicy.h"

#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

NETWORK_BOOT_POLICY_PROTOCOL  mNetworkBootPolicy;

/**
  Entry point for the Platform GOP Policy Driver.
  @param ImageHandle       Image handle of this driver.
  @param SystemTable       Global system service table.
  @retval EFI_SUCCESS           Initialization complete.
  @retval EFI_OUT_OF_RESOURCES  Do not have enough resources to initialize the driver.
**/

EFI_STATUS
EFIAPI
NetworkBootPolicyEntryPoint (
  IN EFI_HANDLE       ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )

{
  EFI_STATUS  Status = EFI_SUCCESS;
  BOOLEAN *NetBootVar = &mNetworkBootPolicy.NetworkBootEnabled;
  UINTN VarSize = sizeof(*NetBootVar);

  gBS = SystemTable->BootServices;
  gRT = SystemTable->RuntimeServices;

  gBS->SetMem (
         &mNetworkBootPolicy,
         sizeof (NETWORK_BOOT_POLICY_PROTOCOL),
         0
         );

  mNetworkBootPolicy.Revision                = NETWORK_BOOT_POLICY_PROTOCOL_REVISION_01;
  mNetworkBootPolicy.NetworkBootEnabled      = 0; // disable by default

  Status = GetVariable2 (
             L"NetworkBoot",
             &gDasharoSystemFeaturesGuid,
             (VOID **) &NetBootVar,
             &VarSize
             );

  if ((Status != EFI_NOT_FOUND) && (VarSize == sizeof(*NetBootVar))) {

    mNetworkBootPolicy.NetworkBootEnabled = *NetBootVar;

    if (mNetworkBootPolicy.NetworkBootEnabled)
      Status = gBS->InstallMultipleProtocolInterfaces (
                      &ImageHandle,
                      &gDasharoNetworkBootPolicyGuid,
                      &mNetworkBootPolicy,
                      NULL
                      );
  }

  return Status;
}