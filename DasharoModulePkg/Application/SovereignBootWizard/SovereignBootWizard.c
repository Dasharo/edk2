/** @file
Sovereign Boot Wizard implementation.

Copyright (c) 2025, 3mdeb Sp z o.o. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "SovereignBootWizard.h"

SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA *mPrivateData   = NULL;
BOOLEAN mBootloadersInitted = FALSE;

STATIC CHAR16 mSvBootDataVarName[] = SV_BOOT_DATA_VAR;
STATIC CHAR16 mVarStoreName[] = L"SvBootFormData";
STATIC CHAR16 mSvBootConfigVarName[] = SV_BOOT_CONFIG_VAR;

STATIC BOOLEAN mBootloadersShown = FALSE;

STATIC HII_VENDOR_DEVICE_PATH  mHiiVendorDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8) (sizeof (VENDOR_DEVICE_PATH)),
        (UINT8) ((sizeof (VENDOR_DEVICE_PATH)) >> 8)
      }
    },
    SOVEREIGN_BOOT_WIZARD_FORMSET_GUID
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

STATIC UINTN mBootloaderIndex = 0;

EFI_STATUS
EFIAPI
SovereignBootWizardUnload (
  IN EFI_HANDLE  ImageHandle
  );


/**
  This function allows a caller to extract the current configuration for one
  or more named elements from the target driver.


  @param This            Points to the EFI_HII_CONFIG_ACCESS_PROTOCOL.
  @param Request         A null-terminated Unicode string in <ConfigRequest> format.
  @param Progress        On return, points to a character in the Request string.
                         Points to the string's null terminator if request was successful.
                         Points to the most recent '&' before the first failing name/value
                         pair (or the beginning of the string if the failure is in the
                         first name/value pair) if the request was not successful.
  @param Results         A null-terminated Unicode string in <ConfigAltResp> format which
                         has all values filled in for the names in the Request string.
                         String to be allocated by the called function.

  @retval  EFI_SUCCESS            The Results is filled with the requested values.
  @retval  EFI_OUT_OF_RESOURCES   Not enough memory to store the results.
  @retval  EFI_INVALID_PARAMETER  Request is illegal syntax, or unknown name.
  @retval  EFI_NOT_FOUND          Routing data doesn't match any storage in this driver.

**/
EFI_STATUS
EFIAPI
ExtractConfig (
  IN  CONST EFI_HII_CONFIG_ACCESS_PROTOCOL   *This,
  IN  CONST EFI_STRING                       Request,
  OUT EFI_STRING                             *Progress,
  OUT EFI_STRING                             *Results
  )
{
  EFI_STATUS                            Status;
  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA     *Private;
  UINTN                                 BufferSize;
  EFI_STRING                            ConfigRequestHdr;
  EFI_STRING                            ConfigRequest;
  UINTN                                 Size;

  if (Progress == NULL || Results == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *Progress = Request;
  if (Request != NULL &&
      !HiiIsConfigHdrMatch (Request, &gSovereignBootWizardFormSetGuid, mVarStoreName)) {
    return EFI_NOT_FOUND;
  }

  Private = SOVEREIGN_BOOT_WIZARD_PRIVATE_FROM_THIS (This);

  BufferSize = sizeof (SOVEREIGN_BOOT_WIZARD_FORM_DATA);
  ConfigRequest = Request;
  if (Request == NULL || (StrStr (Request, L"OFFSET") == NULL)) {
    // Request has no request element, construct full request string.
    // Allocate and fill a buffer large enough to hold the <ConfigHdr> template
    // followed by "&OFFSET=0&WIDTH=WWWWWWWWWWWWWWWW" followed by a Null-terminator.
    ConfigRequestHdr = HiiConstructConfigHdr (
        &gSovereignBootWizardFormSetGuid,
        mVarStoreName,
        Private->AppHandle
        );
    Size = (StrLen (ConfigRequestHdr) + 32 + 1) * sizeof (CHAR16);
    ConfigRequest = AllocateZeroPool (Size);
    ASSERT (ConfigRequest != NULL);
    UnicodeSPrint (
        ConfigRequest,
        Size,
        L"%s&OFFSET=0&WIDTH=%016LX",
        ConfigRequestHdr,
        (UINT64) BufferSize
        );
    FreePool (ConfigRequestHdr);
  }

  // Convert fields of binary structure to string representation.
  Status = Private->HiiConfigRouting->BlockToConfig (
      Private->HiiConfigRouting,
      ConfigRequest,
      (CONST UINT8 *) &Private->FormData,
      BufferSize,
      Results,
      Progress
      );
  ASSERT_EFI_ERROR (Status);

  // Free config request string if it was allocated.
  if (ConfigRequest != Request) {
    FreePool (ConfigRequest);
  }

  if (Request != NULL && StrStr (Request, L"OFFSET") == NULL) {
    *Progress = Request + StrLen (Request);
  }

  return Status;
}

/**
  This function processes the results of changes in configuration.

  @param  This                   Points to the EFI_HII_CONFIG_ACCESS_PROTOCOL.
  @param  Configuration          A null-terminated Unicode string in <ConfigResp>
                                 format.
  @param  Progress               A pointer to a string filled in with the offset of
                                 the most recent '&' before the first failing
                                 name/value pair (or the beginning of the string if
                                 the failure is in the first name/value pair) or
                                 the terminating NULL if all was successful.

  @retval EFI_SUCCESS            The Results is processed successfully.
  @retval EFI_INVALID_PARAMETER  Configuration is NULL.
  @retval EFI_NOT_FOUND          Routing data doesn't match any storage in this
                                 driver.
  @retval EFI_DEVICE_ERROR       If value is 44, return error for testing.

**/
EFI_STATUS
EFIAPI
RouteConfig (
  IN  CONST EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN  CONST EFI_STRING                      Configuration,
  OUT EFI_STRING                            *Progress
  )
{
  EFI_STATUS                                Status;
  UINTN                                     BufferSize;
  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA         *PrivateData;

  if ((Configuration == NULL) || (Progress == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  PrivateData      = SOVEREIGN_BOOT_WIZARD_PRIVATE_FROM_THIS (This);
  *Progress        = Configuration;

  if (!HiiIsConfigHdrMatch (Configuration, &gSovereignBootWizardFormSetGuid, NULL)) {
    return EFI_NOT_FOUND;
  }

  if (HiiIsConfigHdrMatch (Configuration, &gSovereignBootWizardFormSetGuid, mSvBootDataVarName)) {
    return EFI_UNSUPPORTED;
  }

  if (!HiiIsConfigHdrMatch (Configuration, &gSovereignBootWizardFormSetGuid, mSvBootConfigVarName)) {
    return EFI_UNSUPPORTED;
  }

  BufferSize = sizeof (SOVEREIGN_BOOT_WIZARD_FORM_DATA);
  Status     = PrivateData->HiiConfigRouting->ConfigToBlock (
                  PrivateData->HiiConfigRouting,
                  Configuration,
                  (UINT8 *)&PrivateData->FormData,
                  &BufferSize,
                  Progress
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
DeleteAllSecureBootVariables (
  VOID
  )
{
  EFI_STATUS  Status, TempStatus;

  Status = DeletePlatformKey ();
  DEBUG ((DEBUG_INFO, "%a - PK Delete = %r\n", __func__, Status));
  // If the PK is not found, then our work here is done.
  if (Status == EFI_NOT_FOUND) {
    Status = EFI_SUCCESS;
  }
  // If any other error occurred, let's inform the caller that the PK delete in particular failed.
  else if (EFI_ERROR (Status)) {
    Status = EFI_ABORTED;
  }

  //
  // If any of THESE steps have an error, report the error but attempt to delete all keys.
  // Using TempStatus will prevent an error from being trampled by an EFI_SUCCESS.
  // Overwrite Status ONLY if TempStatus is an error.
  //
  // If the error is EFI_NOT_FOUND, we can safely ignore it since we were trying to delete
  // the variables anyway.
  //
  TempStatus = DeleteKEK ();
  DEBUG ((DEBUG_INFO, "%a - KEK Delete = %r\n", __func__, TempStatus));
  if (EFI_ERROR (TempStatus) && (TempStatus != EFI_NOT_FOUND)) {
    Status = EFI_ACCESS_DENIED;
  }

  TempStatus = DeleteDb ();
  DEBUG ((DEBUG_INFO, "%a - db Delete = %r\n", __func__, TempStatus));
  if (EFI_ERROR (TempStatus) && (TempStatus != EFI_NOT_FOUND)) {
    Status = EFI_ACCESS_DENIED;
  }

  TempStatus = DeleteDbx ();
  DEBUG ((DEBUG_INFO, "%a - dbx Delete = %r\n", __func__, TempStatus));
  if (EFI_ERROR (TempStatus) && (TempStatus != EFI_NOT_FOUND)) {
    Status = EFI_ACCESS_DENIED;
  }

  TempStatus = DeleteDbt ();
  DEBUG ((DEBUG_INFO, "%a - dbt Delete = %r\n", __func__, TempStatus));
  if (EFI_ERROR (TempStatus) && (TempStatus != EFI_NOT_FOUND)) {
    Status = EFI_ACCESS_DENIED;
  }

  return Status;
}

EFI_STATUS
EnrollDefaultSecureBootVariables (
  VOID
  )
{
  EFI_STATUS Status;

  Status = EnrollDbFromDefault ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = EnrollDbxFromDefault ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Default dbt may not exists and is not critical error if fails
  EnrollDbtFromDefault ();

  Status = EnrollKEKFromDefault ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EnrollPKFromDefault ();
}

EFI_STATUS
RestoreSecureBootDefaults (
  VOID
  )
{
  EFI_STATUS Status;

  Status = SetSecureBootMode (CUSTOM_SECURE_BOOT_MODE);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = DeleteAllSecureBootVariables ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = EnrollDefaultSecureBootVariables ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return SetSecureBootMode (STANDARD_SECURE_BOOT_MODE);
}

/**
  This function processes the results of changes in configuration.

  @param  This                   Points to the EFI_HII_CONFIG_ACCESS_PROTOCOL.
  @param  Action                 Specifies the type of action taken by the browser.
  @param  QuestionId             A unique value which is sent to the original
                                 exporting driver so that it can identify the type
                                 of data to expect.
  @param  Type                   The type of value for the question.
  @param  Value                  A pointer to the data being sent to the original
                                 exporting driver.
  @param  ActionRequest          On return, points to the action requested by the
                                 callback function.

  @retval EFI_SUCCESS            The callback successfully handled the action.
  @retval EFI_OUT_OF_RESOURCES   Not enough storage is available to hold the
                                 variable and its data.
  @retval EFI_DEVICE_ERROR       The variable could not be saved.
  @retval EFI_UNSUPPORTED        The specified Action is not supported by the
                                 callback.

**/
EFI_STATUS
EFIAPI
Callback (
  IN  CONST EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN  EFI_BROWSER_ACTION                    Action,
  IN  EFI_QUESTION_ID                       QuestionId,
  IN  UINT8                                 Type,
  IN  EFI_IFR_TYPE_VALUE                    *Value,
  OUT EFI_BROWSER_ACTION_REQUEST            *ActionRequest
  )
{
  EFI_STATUS                           Status;
  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA   *PrivateData;
  EFI_INPUT_KEY                        Key;
  UINTN                                BufferSize;
  SOVEREIGN_BOOT_WIZARD_NV_CONFIG      SvConfig;

  if (((Value == NULL) && (Action != EFI_BROWSER_ACTION_FORM_OPEN) && (Action != EFI_BROWSER_ACTION_FORM_CLOSE)) ||
      (ActionRequest == NULL))
  {
    return EFI_INVALID_PARAMETER;
  }

  Status      = EFI_SUCCESS;
  PrivateData = SOVEREIGN_BOOT_WIZARD_PRIVATE_FROM_THIS (This);

  DEBUG ((EFI_D_INFO, "Callback: Action %x QuestionID %x Type %x\n", Action, QuestionId, Type));

  switch (Action) {
    case EFI_BROWSER_ACTION_CHANGING:
      switch (QuestionId) {
        case SELECT_DEFAULT_SECURE_BOOT_QUESTION_ID:
        {
          // 1. Set the Sovering Boot option to disabled.
          // 2. Unset the system provisioned state (just in case it was
          //    provisioned before).
          BufferSize = sizeof (SOVEREIGN_BOOT_WIZARD_NV_CONFIG);
          SetMem(&SvConfig, BufferSize, 0);
          Status = gRT->SetVariable (
                          mSvBootConfigVarName,
                          &gSovereignBootWizardFormSetGuid,
                          EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
                          BufferSize,
                          &SvConfig
                          );
          if (EFI_ERROR (Status)) {
            return Status;
          }
          // 3. Restore default keys if necessary. Maybe use NV VendorKeys to
          //    indicate if key restoration is required.
          Status = RestoreSecureBootDefaults ();
          if (EFI_ERROR (Status)) {
            return Status;
          }
          // 4. Reset the system to boot in a fresh state. Can't really avoid
          //    the reset as we cannot exit the form in any other action than
          //    EFI_BROWSER_ACTION_CHANGED, but it can't be invoked for
          //    EFI_IFR_TYPE_ACTION nor EFI_IFR_TYPE_REF. Also we should reset
          //    in case Secure Boot keys get restored to defaults. We can show
          //    a pop-up to inform about the actions we make here.
          do {
            CreatePopUp (
              EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE,
              &Key,
              L"",
              L"Default Secure Boot configuration has been restored.",
              L"",
              L"Press ENTER to reset the system ...",
              L"",
              NULL
              );
          } while (Key.UnicodeChar != CHAR_CARRIAGE_RETURN);

          gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
          break;
        }
        default:
          break;
      }

      break;
    case EFI_BROWSER_ACTION_CHANGED:
    {
      switch (QuestionId) {
        case EXIT_FORM1_QUESTION_ID:
        case EXIT_FORM2_QUESTION_ID:
        case EXIT_FORM3_QUESTION_ID:
        case EXIT_FORM9_QUESTION_ID:
          *ActionRequest = EFI_BROWSER_ACTION_REQUEST_EXIT;
          break;
        case DO_NOT_TRUST_KEY_FORM2_QUESTION_ID:
        case TRUST_KEY_FORM2_QUESTION_ID:
          if (mBootloadersInitted) {
            Status = UpdateBootloaderPage (PrivateData, ++mBootloaderIndex);
            if (Status == EFI_NO_MEDIA) {
              do {
                CreatePopUp (
                  EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE,
                  &Key,
                  L"",
                  L"No more bootloaders found.",
                  L"",
                  L"Press ENTER to continue ...",
                  L"",
                  NULL
                  );
              } while (Key.UnicodeChar != CHAR_CARRIAGE_RETURN);
              // TODO: Here
              // 1. If nothing has been selected as trusted so far, warn popup.
              // 2. Create ephemeral PK. Enroll it and enable Secure Boot.
              // 3. Set the SV boto variable to provisioned state.
              // 4. Boot the first trusted bootloader.

              // No more bootloaders, reset the bootloader index and
              // go to ineractive mode form for now.
              mBootloaderIndex = 0;
              Status = PrivateData->FormBrowser2->SendForm (
                            PrivateData->FormBrowser2,
                            &PrivateData->HiiHandle,
                            1,
                            &gSovereignBootWizardFormSetGuid,
                            SOVEREIGN_BOOT_WIZARD_INTERACTIVE_MODE_FORM_ID,
                            NULL,
                            ActionRequest
                            );
            } else if (Status == EFI_NOT_FOUND) {
              do {
                CreatePopUp (
                  EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE,
                  &Key,
                  L"",
                  L"Could not get bootloader description.",
                  L"",
                  L"Press ENTER to continue ...",
                  L"",
                  NULL
                  );
              } while (Key.UnicodeChar != CHAR_CARRIAGE_RETURN);
              // Unexpected failure when parsing bootloader, reset bootloader index
              // and go back to welcome form
              mBootloaderIndex = 0;
              Status = PrivateData->FormBrowser2->SendForm (
                            PrivateData->FormBrowser2,
                            &PrivateData->HiiHandle,
                            1,
                            &gSovereignBootWizardFormSetGuid,
                            SOVEREIGN_BOOT_WIZARD_WELCOME_FORM_ID,
                            NULL,
                            ActionRequest
                            );
            }
          } else {
            Status = EFI_NO_MEDIA;
          }
          break;
        default:
          break;
      }

      break;
    }
    case EFI_BROWSER_ACTION_FORM_OPEN:
    {
      switch (QuestionId) {
        case DO_NOT_TRUST_KEY_FORM2_QUESTION_ID:
        case TRUST_KEY_AND_BOOT_FORM2_QUESTION_ID:
        case TRUST_KEY_FORM2_QUESTION_ID:
        case SHOW_KEY_DETAILS_FORM2_QUESTION_ID:
          if (!mBootloadersInitted) {
            Status = GetBootOptions (PrivateData);
            DEBUG ((EFI_D_INFO, "GetBootOptions: %r\n", mBootloaderIndex, Status));
            if (!EFI_ERROR (Status)) {
              mBootloadersInitted = TRUE;
              if (!mBootloadersShown) {
                Status = UpdateBootloaderPage (PrivateData, mBootloaderIndex);
                DEBUG ((EFI_D_INFO, "UpdateBootloaderPage(%d): %r\n", mBootloaderIndex, Status));
                mBootloadersShown = !EFI_ERROR (Status);
              }
            } else {
              do {
                CreatePopUp (
                  EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE,
                  &Key,
                  L"",
                  L"Could not find any bootloaders.",
                  L"",
                  L"Press ENTER to continue ...",
                  L"",
                  NULL
                  );
              } while (Key.UnicodeChar != CHAR_CARRIAGE_RETURN);
              // No bootloaders, go back to welcome form
              Status = PrivateData->FormBrowser2->SendForm (
                            PrivateData->FormBrowser2,
                            &PrivateData->HiiHandle,
                            1,
                            &gSovereignBootWizardFormSetGuid,
                            SOVEREIGN_BOOT_WIZARD_WELCOME_FORM_ID,
                            NULL,
                            NULL
                            );
            }
          }
          break;

        default:
          break;
      }

      break;
    }
    default:
      Status = EFI_UNSUPPORTED;
      break;
  }

  return Status;
}

/**
  Main entry for this driver.

  @param ImageHandle     Image handle this driver.
  @param SystemTable     Pointer to SystemTable.

  @retval EFI_SUCESS     This function always complete successfully.

**/
EFI_STATUS
EFIAPI
SovereignBootWizardInit (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                             FormBrowserStatus;
  EFI_STATUS                             Status;
  EFI_HII_HANDLE                         HiiHandle;
  EFI_SCREEN_DESCRIPTOR                  Screen;
  EFI_HII_DATABASE_PROTOCOL              *HiiDatabase;
  EFI_HII_STRING_PROTOCOL                *HiiString;
  EFI_FORM_BROWSER2_PROTOCOL             *FormBrowser2;
  EFI_HII_CONFIG_ROUTING_PROTOCOL        *HiiConfigRouting;
  EFI_CONFIG_KEYWORD_HANDLER_PROTOCOL    *HiiKeywordHandler;
  EFI_HII_POPUP_PROTOCOL                 *PopupHandler;
  UINTN                                  BufferSize;
  SOVEREIGN_BOOT_WIZARD_CONFIG_DATA       *ConfigData;
  SOVEREIGN_BOOT_WIZARD_NV_CONFIG         *SvConfig;
  EFI_BOOT_MODE                          BootMode;
  EFI_INPUT_KEY                          HotKey;
  EDKII_FORM_BROWSER_EXTENSION2_PROTOCOL *FormBrowserEx2;
  EFI_DEVICE_PATH_TO_TEXT_PROTOCOL       *DevPathToText;
  CHAR16                                 *NewString;
  EFI_HANDLE                             AppHandle;
  EFI_FORM_ID                            FormId;

  NewString = NULL;
  AppHandle = NULL;

  ZeroMem (&Screen, sizeof (EFI_SCREEN_DESCRIPTOR));
  gST->ConOut->QueryMode (gST->ConOut, gST->ConOut->Mode->Mode, &Screen.RightColumn, &Screen.BottomRow);

  Screen.TopRow    = 3;
  Screen.BottomRow = Screen.BottomRow - 3;

  //
  // Initialize driver private data
  //
  mPrivateData = AllocateZeroPool (sizeof (SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA));
  if (mPrivateData == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  mPrivateData->Signature = SOVEREIGN_BOOT_PRIVATE_SIGNATURE;

  mPrivateData->ConfigAccess.ExtractConfig = ExtractConfig;
  mPrivateData->ConfigAccess.RouteConfig   = RouteConfig;
  mPrivateData->ConfigAccess.Callback      = Callback;

  //
  // Locate Hii Database protocol
  //
  Status = gBS->LocateProtocol (&gEfiHiiDatabaseProtocolGuid, NULL, (VOID **)&HiiDatabase);
  if (EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
    return Status;
  }

  mPrivateData->HiiDatabase = HiiDatabase;

  //
  // Locate HiiString protocol
  //
  Status = gBS->LocateProtocol (&gEfiHiiStringProtocolGuid, NULL, (VOID **)&HiiString);
  if (EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
    return Status;
  }

  mPrivateData->HiiString = HiiString;

  //
  // Locate Formbrowser2 protocol
  //
  Status = gBS->LocateProtocol (&gEfiFormBrowser2ProtocolGuid, NULL, (VOID **)&FormBrowser2);
  if (EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
    return Status;
  }

  mPrivateData->FormBrowser2 = FormBrowser2;

  //
  // Locate ConfigRouting protocol
  //
  Status = gBS->LocateProtocol (&gEfiHiiConfigRoutingProtocolGuid, NULL, (VOID **)&HiiConfigRouting);
  if (EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
    return Status;
  }

  mPrivateData->HiiConfigRouting = HiiConfigRouting;

  //
  // Locate keyword handler protocol
  //
  Status = gBS->LocateProtocol (&gEfiConfigKeywordHandlerProtocolGuid, NULL, (VOID **)&HiiKeywordHandler);
  if (EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
    return Status;
  }

  mPrivateData->HiiKeywordHandler = HiiKeywordHandler;

  //
  // Locate HiiPopup protocol
  //
  Status = gBS->LocateProtocol (&gEfiHiiPopupProtocolGuid, NULL, (VOID **)&PopupHandler);
  if (EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
    return Status;
  }

  mPrivateData->HiiPopup = PopupHandler;

  Status = gBS->LocateProtocol (&gEfiDevicePathToTextProtocolGuid, NULL, (VOID **)&DevPathToText);
    if (EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
    return Status;
  }

  mPrivateData->DevPathToText = DevPathToText;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &AppHandle,
                  &gEfiDevicePathProtocolGuid,
                  &mHiiVendorDevicePath,
                  &gEfiHiiConfigAccessProtocolGuid,
                  &mPrivateData->ConfigAccess,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  mPrivateData->AppHandle = AppHandle;

  //
  // Publish our HII data
  //
  HiiHandle = HiiAddPackages (
                   &gSovereignBootWizardFormSetGuid,
                   AppHandle,
                   SovereignBootWizardVfrBin,
                   SovereignBootWizardStrings,
                   NULL
                   );
  if (HiiHandle == NULL) {
    SovereignBootWizardUnload (ImageHandle);
    return EFI_OUT_OF_RESOURCES;
  }

  mPrivateData->HiiHandle = HiiHandle;

  SvConfig = &mPrivateData->NvConfig;
  ZeroMem (SvConfig, sizeof (SOVEREIGN_BOOT_WIZARD_NV_CONFIG));

  BufferSize = sizeof (SOVEREIGN_BOOT_WIZARD_NV_CONFIG);
  Status     = gRT->GetVariable (mSvBootConfigVarName, &gSovereignBootWizardFormSetGuid, NULL, &BufferSize, SvConfig);
  if (EFI_ERROR (Status)) {
    // If variable doesn't exist, store zero to indicate unprovisioned state.
    Status = gRT->SetVariable (
                    mSvBootConfigVarName,
                    &gSovereignBootWizardFormSetGuid,
                    EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
                    sizeof (SOVEREIGN_BOOT_WIZARD_NV_CONFIG),
                    SvConfig
                    );
    if (EFI_ERROR (Status)) {
      SovereignBootWizardUnload (ImageHandle);
      return Status;
    }
  }

  // Check Boot Mode
  BootMode = GetBootModeHob ();
  // On flash update do not run the application
  if (BootMode == BOOT_ON_FLASH_UPDATE) {
    SovereignBootWizardUnload (ImageHandle);
    return EFI_UNSUPPORTED;
  }

  //
  // Initialize configuration data
  //
  ConfigData = &mPrivateData->ConfigData;
  ZeroMem (ConfigData, sizeof (SOVEREIGN_BOOT_WIZARD_CONFIG_DATA));

  BufferSize = sizeof (SOVEREIGN_BOOT_WIZARD_CONFIG_DATA);
  Status     = gRT->GetVariable (mSvBootDataVarName, &gSovereignBootWizardFormSetGuid, NULL, &BufferSize, ConfigData);
  if (EFI_ERROR (Status)) {
    // Unknown launch cause, try to determine if it is first launch or not
    if (!SvConfig->SvBootProvisioned ||
        BootMode == BOOT_WITH_DEFAULT_SETTINGS ||
        BootMode == BOOT_WITH_MFG_MODE_SETTINGS
       )
    {
      ConfigData->AppLaunchCause = SV_BOOT_LAUNCH_BOOT_WITH_DEFAULT_SETTINGS;
    }
  } else {
    // If not provisioned, the launch cause can not be verification failure
    if (!SvConfig->SvBootProvisioned &&
        ConfigData->AppLaunchCause == SV_BOOT_LAUNCH_IMAGE_VERIFICATION_FAILED)
    {
      ConfigData->AppLaunchCause = SV_BOOT_LAUNCH_BOOT_WITH_DEFAULT_SETTINGS;
    }
  }

  // Handle invalid value
  if (ConfigData->AppLaunchCause >= SV_BOOT_LAUNCH_MAX) {
    ConfigData->AppLaunchCause = SV_BOOT_LAUNCH_BOOT_WITH_DEFAULT_SETTINGS;
  }

  switch (ConfigData->AppLaunchCause) {
  case SV_BOOT_LAUNCH_BOOT_WITH_DEFAULT_SETTINGS:
    NewString = HiiGetString(HiiHandle, STRING_TOKEN (STR_LAUNCH_CAUSE_DEFAULT_SETTINGS), NULL);
    break;
  case SV_BOOT_LAUNCH_IMAGE_VERIFICATION_FAILED:
    // Override the "do not trust key" to avoid displaying "next bootloader"
    NewString = HiiGetString(HiiHandle, STRING_TOKEN (STR_DO_NOT_TRUST_KEY2), NULL);
    if (NewString != NULL) {
      HiiSetString(HiiHandle, STRING_TOKEN (STR_DO_NOT_TRUST_KEY), NewString, NULL);
    }

    NewString = HiiGetString(HiiHandle, STRING_TOKEN (STR_LAUNCH_CAUSE_VERIFICATION_FAILED), NULL);
    break;
  case SV_BOOT_LAUNCH_VIA_SETUP:
    NewString = HiiGetString(HiiHandle, STRING_TOKEN (STR_LAUNCH_CAUSE_SETUP), NULL);
    break;
  default:
    NewString = NULL;
    break;
  }

  if (NewString != NULL) {
    HiiSetString(HiiHandle, STRING_TOKEN (STR_LAUNCH_REASON), NewString, NULL);
  }

  //
  // Override Hotkeys, F9 and F10 won't be needed by this application
  //
  FormBrowserStatus = gBS->LocateProtocol (&gEdkiiFormBrowserEx2ProtocolGuid, NULL, (VOID **)&FormBrowserEx2);
  if (!EFI_ERROR (FormBrowserStatus)) {
    HotKey.UnicodeChar = CHAR_NULL;
    HotKey.ScanCode    = SCAN_F9;
    FormBrowserEx2->RegisterHotKey (&HotKey, 0, 0, NULL);
    HotKey.ScanCode = SCAN_F10;
    FormBrowserEx2->RegisterHotKey (&HotKey, 0, 0, NULL);
  }

  if (SvConfig->SvBootProvisioned) {
    if (ConfigData->AppLaunchCause == SV_BOOT_LAUNCH_IMAGE_VERIFICATION_FAILED) {
      FormId = SOVEREIGN_BOOT_WIZARD_CONFIG_FORM_ID;
    } else {
      FormId = SOVEREIGN_BOOT_WIZARD_INTERACTIVE_MODE_FORM_ID;
    }
  } else {
    FormId = SOVEREIGN_BOOT_WIZARD_WELCOME_FORM_ID;
  }

  //
  // turn off the watchdog timer
  //
  gBS->SetWatchdogTimer (0, 0, 0, NULL);

  // Display the form
  Status = FormBrowser2->SendForm (
                            FormBrowser2,
                            &HiiHandle,
                            1,
                            &gSovereignBootWizardFormSetGuid,
                            FormId,
                            NULL,
                            NULL
                            );

  ASSERT_EFI_ERROR (Status);

  if (!EFI_ERROR (FormBrowserStatus)) {
    //
    // Register the default HotKey F9 and F10 again.
    //
    HotKey.UnicodeChar = CHAR_NULL;
    HotKey.ScanCode    = SCAN_F10;
    NewString          = HiiGetString (HiiHandle, STRING_TOKEN (FUNCTION_TEN_STRING), NULL);
    ASSERT (NewString != NULL);
    FormBrowserEx2->RegisterHotKey (&HotKey, BROWSER_ACTION_SUBMIT, 0, NewString);
    FreePool (NewString);

    HotKey.ScanCode = SCAN_F9;
    NewString       = HiiGetString (HiiHandle, STRING_TOKEN (FUNCTION_NINE_STRING), NULL);
    ASSERT (NewString != NULL);
    FormBrowserEx2->RegisterHotKey (&HotKey, BROWSER_ACTION_DEFAULT, EFI_HII_DEFAULT_CLASS_STANDARD, NewString);
    FreePool (NewString);
  }

  Status = SovereignBootWizardUnload (ImageHandle);

  return Status;
}

/**
  Unloads the application and its installed protocol.

  @param[in]  ImageHandle       Handle that identifies the image to be unloaded.

  @retval EFI_SUCCESS           The image has been unloaded.
**/
EFI_STATUS
EFIAPI
SovereignBootWizardUnload (
  IN EFI_HANDLE  ImageHandle
  )
{
  UINTN  Index;

  ASSERT (mPrivateData != NULL);

  if (mPrivateData->HiiHandle != NULL) {
    HiiRemovePackages (mPrivateData->HiiHandle);
  }

  if (mPrivateData->AppHandle != NULL) {
    gBS->UninstallMultipleProtocolInterfaces (
           mPrivateData->AppHandle,
           &gEfiDevicePathProtocolGuid,
           &mHiiVendorDevicePath,
           &gEfiHiiConfigAccessProtocolGuid,
           &mPrivateData->ConfigAccess,
           NULL
           );
    mPrivateData->AppHandle = NULL;
  }

  for (Index = 0; Index < NAME_VALUE_NAME_NUMBER; Index++) {
    if (mPrivateData->NameValueName[Index] != NULL) {
      FreePool (mPrivateData->NameValueName[Index]);
    }
  }

  FreePool (mPrivateData);
  mPrivateData = NULL;

  return EFI_SUCCESS;
}
