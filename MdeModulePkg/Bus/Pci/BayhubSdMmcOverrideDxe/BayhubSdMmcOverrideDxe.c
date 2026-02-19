/** @file
  Entry point for BayHub SD/MMC Override DXE driver.

  Installs EDKII_SD_MMC_OVERRIDE_PROTOCOL so that SdMmcPciHcDxe can locate it
  via LocateProtocol and apply BayHub-specific SD/eMMC host controller
  workarounds.

  Copyright (c) 2018 - 2019, BayHub Tech inc. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Protocol/SdMmcOverride.h>

#include "BayhubHost.h"

/**
  Driver entry point.  Installs the EDKII_SD_MMC_OVERRIDE_PROTOCOL instance
  that provides BayHub-specific overrides for SdMmcPciHcDxe.

  @param[in]  ImageHandle  The handle for this driver image.
  @param[in]  SystemTable  Pointer to the EFI system table.

  @retval EFI_SUCCESS  Protocol installed successfully.
  @retval other        Protocol installation failed.

**/
EFI_STATUS
EFIAPI
BayhubSdMmcOverrideDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEdkiiSdMmcOverrideProtocolGuid,
                  &BhtOverride,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  DEBUG ((DEBUG_INFO, "%a: installed BayHub SD/MMC override protocol\n", __func__));

  return Status;
}
