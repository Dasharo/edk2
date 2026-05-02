/** @file
  Dasharo Save & Exit UI library.

  Publishes a small HII formset on the Front Page (via classguid =
  gEfiIfrFrontPageGuid) containing action items for Save / Discard /
  Reset. Each action's question ID is dispatched to a browser action
  request inside the config-access callback.

  Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "DasharoSaveExit.h"

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/HiiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include <Protocol/DevicePath.h>
#include <Protocol/FormBrowserEx.h>
#include <Protocol/FormBrowserEx2.h>
#include <Protocol/HiiConfigAccess.h>

#include <Guid/MdeModuleHii.h>

extern UINT8  DasharoSaveExitUiLibStrings[];

#define DASHARO_SAVE_EXIT_PRIVATE_DATA_SIGNATURE  SIGNATURE_32 ('D', 'S', 'E', 'X')

typedef struct {
  UINTN                              Signature;
  EFI_HII_HANDLE                     HiiHandle;
  EFI_HANDLE                         DriverHandle;
  EFI_HII_CONFIG_ACCESS_PROTOCOL     ConfigAccess;
} DASHARO_SAVE_EXIT_PRIVATE_DATA;

STATIC EFI_GUID  mDasharoSaveExitGuid = DASHARO_SAVE_EXIT_GUID;

STATIC
EFI_STATUS
EFIAPI
ExtractConfig (
  IN  CONST EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN  CONST EFI_STRING                      Request,
  OUT EFI_STRING                            *Progress,
  OUT EFI_STRING                            *Results
  )
{
  if (Progress != NULL) {
    *Progress = (EFI_STRING)Request;
  }
  // No varstore — actions only.
  return EFI_NOT_FOUND;
}

STATIC
EFI_STATUS
EFIAPI
RouteConfig (
  IN  CONST EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN  CONST EFI_STRING                      Configuration,
  OUT EFI_STRING                            *Progress
  )
{
  if (Progress != NULL) {
    *Progress = (EFI_STRING)Configuration;
  }
  return EFI_NOT_FOUND;
}

STATIC
EFI_STATUS
EFIAPI
Callback (
  IN     CONST EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN     EFI_BROWSER_ACTION                    Action,
  IN     EFI_QUESTION_ID                       QuestionId,
  IN     UINT8                                 Type,
  IN OUT EFI_IFR_TYPE_VALUE                    *Value,
  OUT    EFI_BROWSER_ACTION_REQUEST            *ActionRequest
  )
{
  if (Action != EFI_BROWSER_ACTION_CHANGED) {
    return EFI_UNSUPPORTED;
  }

  switch (QuestionId) {
    case KEY_SAVE_AND_EXIT: {
      // We need a real save-then-reset, system-wide. ExecuteAction with
      // SystemLevel scope walks every dirty formset's pending values and
      // commits them via ConfigRouting; ResetSystem then reboots.
      EDKII_FORM_BROWSER_EXTENSION2_PROTOCOL  *FormBrowserEx2;
      EFI_STATUS                              LookupStatus;

      LookupStatus = gBS->LocateProtocol (
                            &gEdkiiFormBrowserEx2ProtocolGuid,
                            NULL,
                            (VOID **)&FormBrowserEx2
                            );
      if (!EFI_ERROR (LookupStatus) && (FormBrowserEx2 != NULL)) {
        if (FormBrowserEx2->SetScope != NULL) {
          FormBrowserEx2->SetScope (SystemLevel);
        }
        if (FormBrowserEx2->ExecuteAction != NULL) {
          FormBrowserEx2->ExecuteAction (BROWSER_ACTION_SUBMIT, 0);
        }
      }
      gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
      // Unreachable.
      return EFI_SUCCESS;
    }

    case KEY_DISCARD_AND_EXIT: {
      // FORM_DISCARD_EXIT only exits the current form (back to Front
      // Page). To exit setup entirely we drive the discard + exit
      // through ExecuteAction at SystemLevel scope, which sets the
      // browser's gExitRequired flag.
      EDKII_FORM_BROWSER_EXTENSION2_PROTOCOL  *FormBrowserEx2;
      EFI_STATUS                              LookupStatus;

      LookupStatus = gBS->LocateProtocol (
                            &gEdkiiFormBrowserEx2ProtocolGuid,
                            NULL,
                            (VOID **)&FormBrowserEx2
                            );
      if (!EFI_ERROR (LookupStatus) && (FormBrowserEx2 != NULL)) {
        if (FormBrowserEx2->SetScope != NULL) {
          FormBrowserEx2->SetScope (SystemLevel);
        }
        if (FormBrowserEx2->ExecuteAction != NULL) {
          FormBrowserEx2->ExecuteAction (
                            BROWSER_ACTION_DISCARD | BROWSER_ACTION_EXIT,
                            0
                            );
        }
      }
      *ActionRequest = EFI_BROWSER_ACTION_REQUEST_EXIT;
      return EFI_SUCCESS;
    }

    case KEY_SAVE:
      *ActionRequest = EFI_BROWSER_ACTION_REQUEST_FORM_APPLY;
      return EFI_SUCCESS;

    case KEY_DISCARD:
      *ActionRequest = EFI_BROWSER_ACTION_REQUEST_FORM_DISCARD;
      return EFI_SUCCESS;

    case KEY_RESET_SYSTEM:
      // Hard reset — discards any unsaved changes by definition.
      gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
      // Unreachable.
      return EFI_SUCCESS;
  }

  return EFI_UNSUPPORTED;
}

STATIC DASHARO_SAVE_EXIT_PRIVATE_DATA  mPrivate = {
  DASHARO_SAVE_EXIT_PRIVATE_DATA_SIGNATURE,
  NULL,
  NULL,
  {
    ExtractConfig,
    RouteConfig,
    Callback
  }
};

STATIC HII_VENDOR_DEVICE_PATH  mHiiVendorDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8)(sizeof (VENDOR_DEVICE_PATH)),
        (UINT8)((sizeof (VENDOR_DEVICE_PATH)) >> 8)
      }
    },
    DASHARO_SAVE_EXIT_GUID
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      (UINT8)(END_DEVICE_PATH_LENGTH),
      (UINT8)((END_DEVICE_PATH_LENGTH) >> 8)
    }
  }
};

EFI_STATUS
EFIAPI
DasharoSaveExitUiLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mPrivate.DriverHandle,
                  &gEfiDevicePathProtocolGuid,    &mHiiVendorDevicePath,
                  &gEfiHiiConfigAccessProtocolGuid, &mPrivate.ConfigAccess,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  mPrivate.HiiHandle = HiiAddPackages (
                         &mDasharoSaveExitGuid,
                         mPrivate.DriverHandle,
                         DasharoSaveExitVfrBin,
                         DasharoSaveExitUiLibStrings,
                         NULL
                         );
  ASSERT (mPrivate.HiiHandle != NULL);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
DasharoSaveExitUiLibDestructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  if (mPrivate.HiiHandle != NULL) {
    HiiRemovePackages (mPrivate.HiiHandle);
  }
  if (mPrivate.DriverHandle != NULL) {
    gBS->UninstallMultipleProtocolInterfaces (
           mPrivate.DriverHandle,
           &gEfiDevicePathProtocolGuid,    &mHiiVendorDevicePath,
           &gEfiHiiConfigAccessProtocolGuid, &mPrivate.ConfigAccess,
           NULL
           );
  }
  return EFI_SUCCESS;
}
