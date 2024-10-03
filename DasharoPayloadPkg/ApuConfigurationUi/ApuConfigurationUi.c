/** @file
  Dasharo APU Configuration UI for Setup Front Page.

  Copyright (c) 2024, 3mdeb All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Guid/ApuConfigurationGuid.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/DevicePathLib.h>
#include <Library/DebugLib.h>
#include <Library/HiiLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Uefi/UefiMultiPhase.h>
///
/// HII specific Vendor Device Path definition.
///
typedef struct {
  VENDOR_DEVICE_PATH             VendorDevicePath;
  EFI_DEVICE_PATH_PROTOCOL       End;
} HII_VENDOR_DEVICE_PATH;

extern UINT8 ApuConfigurationUiVfrBin[];
extern UINT8 ApuConfigurationUiStrings[];

EFI_HANDLE mApuConfigDriverHandle = NULL;
EFI_HII_HANDLE mApuConfigHiiHandle = NULL;

APU_CONFIGURATION_VARSTORE_DATA mDefaultApuConfig = {
  .CorePerfBoost = TRUE,
  .WatchdogEnable = FALSE,
  .WatchdogTimeout = 60,
  .PciePwrMgmt = FALSE,
};

HII_VENDOR_DEVICE_PATH  mApuConfigVendorDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8) (sizeof (VENDOR_DEVICE_PATH)),
        (UINT8) ((sizeof (VENDOR_DEVICE_PATH)) >> 8)
      }
    },
    APU_CONFIGURATION_FORMSET_GUID
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      (UINT8) (END_DEVICE_PATH_LENGTH),
      (UINT8) ((END_DEVICE_PATH_LENGTH) >> 8)
    }
  }
};


/**

  Initialize Boot Maintenance Menu library.

  @param ImageHandle     The image handle.
  @param SystemTable     The system table.

  @retval  EFI_SUCCESS  Install Boot manager menu success.
  @retval  Other        Return error status.gBPDisplayLibGuid

**/
EFI_STATUS
EFIAPI
ApuConfigurationUiEntry (
  IN EFI_HANDLE                            ImageHandle,
  IN EFI_SYSTEM_TABLE                      *SystemTable
  )
{
  EFI_STATUS                               Status;
  UINTN                                    Size;
  APU_CONFIGURATION_VARSTORE_DATA          ApuConfig;

  Size = sizeof (ApuConfig);
  Status = gRT->GetVariable (
                  APU_CONFIGURATION_VAR,
                  &gApuConfigurationFormsetGuid,
                  NULL,
                  &Size,
                  &ApuConfig
                  );
  //
  // Ensure the variable exists before changing the form
  //
  if (EFI_ERROR (Status)) {
    Status = gRT->SetVariable (
                    APU_CONFIGURATION_VAR,
                    &gApuConfigurationFormsetGuid,
                    EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
                    sizeof (mDefaultApuConfig),
                    &mDefaultApuConfig
                    );
    ASSERT_EFI_ERROR (Status);
  }


  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mApuConfigDriverHandle,
                  &gEfiDevicePathProtocolGuid,
                  &mApuConfigVendorDevicePath,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Publish our HII data
  //
  mApuConfigHiiHandle = HiiAddPackages (
                   &gApuConfigurationFormsetGuid,
                   mApuConfigDriverHandle,
                   ApuConfigurationUiVfrBin,
                   ApuConfigurationUiStrings,
                   NULL
                   );

  if (mApuConfigHiiHandle == NULL) {
    gBS->UninstallProtocolInterface (
           mApuConfigDriverHandle,
           &gEfiDevicePathProtocolGuid,
           &mApuConfigVendorDevicePath
           );

    return EFI_OUT_OF_RESOURCES;
  }

  return Status;
}

/**
  Destructor of Boot Maintenance menu library.

  @param  ImageHandle   The firmware allocated handle for the EFI image.
  @param  SystemTable   A pointer to the EFI System Table.

  @retval EFI_SUCCESS   The destructor completed successfully.
  @retval Other value   The destructor did not complete successfully.

**/
EFI_STATUS
EFIAPI
ApuConfigurationUiUnload (
  IN EFI_HANDLE        ImageHandle
  )
{

  if (mApuConfigDriverHandle != NULL) {
    gBS->UninstallProtocolInterface (
           mApuConfigDriverHandle,
           &gEfiDevicePathProtocolGuid,
           &mApuConfigVendorDevicePath
           );
    mApuConfigDriverHandle = NULL;
  }

  if (mApuConfigHiiHandle != NULL) {
    HiiRemovePackages (mApuConfigHiiHandle);
    mApuConfigHiiHandle = NULL;
  }

  return EFI_SUCCESS;
}
