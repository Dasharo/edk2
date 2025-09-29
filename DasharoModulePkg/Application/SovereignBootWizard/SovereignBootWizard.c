/** @file
Sovereign Boot Wizard implementation.

Copyright (c) 2025, 3mdeb Sp z o.o. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "SovereignBootWizard.h"

SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA *mPrivateData   = NULL;
BOOLEAN mBootloadersInitted;

STATIC CHAR16 mSvBootDataVarName[] = SV_BOOT_DATA_VAR;
STATIC CHAR16 mVarStoreName[] = L"SvBootFormData";
STATIC CHAR16 mSvBootConfigVarName[] = SV_BOOT_CONFIG_VAR;

STATIC BOOLEAN mBootloadersShown;

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

UINTN mBootloaderIndex;
UINTN mCertIndex;
INTN mFirstTrustedBootloader;

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
    FREE_NON_NULL (ConfigRequestHdr);
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
BootTheBootloader (
  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA   *PrivateData,
  UINTN                                BootloaderIndex
  )
{
  SV_MENU_ENTRY                        *BootloaderEntry;
  SV_LOAD_CONTEXT                      *BootloaderContext;
  EFI_BOOT_MANAGER_LOAD_OPTION         BootOption;
  EFI_BOOT_MANAGER_LOAD_OPTION         *BootOptions;
  UINTN                                BootOptionCount;
  INTN                                 OptionIndex;
  EFI_STATUS                           Status;

  BootloaderEntry   = GetMenuEntry (&BootOptionMenu, BootloaderIndex);
  if (BootloaderEntry == NULL) {
    DEBUG ((DEBUG_INFO, "Bootloader %u entry not found\n", BootloaderIndex));
    return EFI_NO_MEDIA;
  }

  BootloaderContext = (SV_LOAD_CONTEXT *)BootloaderEntry->VariableContext;
  if (BootloaderContext == NULL) {
    DEBUG ((DEBUG_INFO, "Bootloader %u load context not found\n", BootloaderIndex));
    return EFI_NO_MEDIA;
  }

  Status = EfiBootManagerInitializeLoadOption (
            &BootOption,
            LoadOptionNumberUnassigned,
            LoadOptionTypeBoot,
            LOAD_OPTION_ACTIVE,
            BootloaderContext->Description,
            BootloaderContext->FilePath,
            BootloaderContext->OptionalData,
            BootloaderContext->OptionalDataSize
            );

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to prepare load option: %r\n", Status));
    return Status;
  }

  BootOptions = EfiBootManagerGetLoadOptions (
                  &BootOptionCount,
                  LoadOptionTypeBoot
                  );

  OptionIndex = EfiBootManagerFindLoadOption (
                  &BootOption,
                  BootOptions,
                  BootOptionCount
                  );

  if (gST->ConOut != NULL) {
    gST->ConOut->ClearScreen (gST->ConOut);
  }

  // TODO: Make this bootloader the first boot priority
  if (OptionIndex == -1) {
    Status = EfiBootManagerAddLoadOptionVariable (&BootOption, MAX_UINTN);
    if (EFI_ERROR (Status)) {
      EfiBootManagerFreeLoadOption (&BootOption);
      EfiBootManagerFreeLoadOptions (BootOptions, BootOptionCount);
      DEBUG ((DEBUG_ERROR, "Failed to add load option variable: %r\n", Status));
      return Status;
    }
    DEBUG ((DEBUG_INFO, "Booting %s\n", BootloaderContext->Description));
    EfiBootManagerBoot (&BootOption);
  } else {
    DEBUG ((DEBUG_INFO, "Booting %s\n", BootloaderContext->Description));
    EfiBootManagerBoot (&BootOptions[OptionIndex]);
  }

  EfiBootManagerFreeLoadOption (&BootOption);
  EfiBootManagerFreeLoadOptions (BootOptions, BootOptionCount);

  return EFI_SUCCESS;
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
  BROWSER_SETTING_SCOPE                Scope;
  UINTN                                BootloaderToBoot;

  if (((Value == NULL) && (Action != EFI_BROWSER_ACTION_FORM_OPEN) && (Action != EFI_BROWSER_ACTION_FORM_CLOSE)) ||
      (ActionRequest == NULL))
  {
    return EFI_INVALID_PARAMETER;
  }

  Status      = EFI_SUCCESS;
  PrivateData = SOVEREIGN_BOOT_WIZARD_PRIVATE_FROM_THIS (This);

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
              L"Press ENTER to reset the system...",
              L"",
              NULL
              );
          } while (Key.UnicodeChar != CHAR_CARRIAGE_RETURN);

          gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
          break;
        }
        case SELECT_SOVEREIGN_BOOT_QUESTION_ID:
        {
          // When selecting Sovereign Boot, we have to wipe out the SB
          // variables except keeping default dbx. Then the wizard will
          // proceed with enrolling trusted keys into db.
          Status = PrepareSbVariablesForSvBoot ();
          break;
        }
        case DO_NOT_TRUST_KEY_FORM2_QUESTION_ID:
        {
          // Add cert or image hash to DBX
          Status = AddKeyOrHashAsTrustedOrUntrusted(PrivateData, FALSE);
          break;
        }
        case TRUST_KEY_FORM2_QUESTION_ID:
        {
          // Add cert or image hash to DB
          Status = AddKeyOrHashAsTrustedOrUntrusted(PrivateData, TRUE);
          break;
        }
        case SHOW_KEY_DETAILS_FORM2_QUESTION_ID:
        {
          // Update the strings when opening certificate details form
          Status = UpdateCertDetails (PrivateData);
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
          if (PrivateData->ConfigData.AppLaunchCause == SV_BOOT_LAUNCH_VIA_SETUP) {
            Scope = FormSetLevel;
          } else {
            Scope = SystemLevel;
          }
          PrivateData->FormBrowserEx2->SetScope (Scope);
          Status = PrivateData->FormBrowserEx2->ExecuteAction(BROWSER_ACTION_EXIT, 0);

          *ActionRequest = EFI_BROWSER_ACTION_REQUEST_EXIT;
          break;
        case SKIP_KEY_FORM2_QUESTION_ID:
          if (PrivateData->FormData.ImageUnsigned) {
            mBootloaderIndex++;
          } else {
            mCertIndex++;
          }
          // fallthrough
        case DO_NOT_TRUST_KEY_FORM2_QUESTION_ID:
        case TRUST_KEY_FORM2_QUESTION_ID:
          if (mBootloadersInitted) {
            Status = UpdateBootloaderPage (PrivateData);
            if (Status == EFI_NO_MEDIA) {
              // If we failed image verification and do not trust the image, simply exit
              if (QuestionId == DO_NOT_TRUST_KEY_FORM2_QUESTION_ID &&
                  PrivateData->ConfigData.AppLaunchCause == SV_BOOT_LAUNCH_IMAGE_VERIFICATION_FAILED) {
                  PrivateData->FormBrowserEx2->SetScope (SystemLevel);
                  Status = PrivateData->FormBrowserEx2->ExecuteAction(BROWSER_ACTION_EXIT, 0);

                  *ActionRequest = EFI_BROWSER_ACTION_REQUEST_EXIT;
                  return Status;
              }
              do {
                CreatePopUp (
                  EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE,
                  &Key,
                  L"",
                  L"No more bootloaders found, to finalize provisioning process",
                  L"the Wizard will create ephemeral PK and enable UEFI Secure Boot.",
                  L"",
                  L"Press ENTER to continue...",
                  L"",
                  NULL
                  );
              } while (Key.UnicodeChar != CHAR_CARRIAGE_RETURN);

              // 1. If nothing has been selected as trusted so far, warn popup.
              // 2. Create ephemeral PK. Enroll it and enable Secure Boot.
              // 3. Set the SV boot variable to provisioned state.
              // FinalizeSvBootProvisioning will handle all above
              Status = FinalizeSvBootProvisioning ();
              if (!EFI_ERROR (Status)) {
                if (mFirstTrustedBootloader != -1) {
                  do {
                    CreatePopUp (
                      EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE,
                      &Key,
                      L"",
                      L"Sovereign Boot provisioning successful.",
                      L"The Wizard will now boot the first trusted bootloader.",
                      L"",
                      L"Press ENTER to boot...",
                      L"",
                      NULL
                      );
                  } while (Key.UnicodeChar != CHAR_CARRIAGE_RETURN);
                  // 4. Boot the first trusted bootloader.
                  BootTheBootloader(PrivateData, (UINTN)mFirstTrustedBootloader);
                  // If we return from the bootloader, exit the form completely.
                  mBootloaderIndex = 0;
                  if (PrivateData->ConfigData.AppLaunchCause == SV_BOOT_LAUNCH_VIA_SETUP) {
                    Scope = FormSetLevel;
                  } else {
                    Scope = SystemLevel;
                  }
                  PrivateData->FormBrowserEx2->SetScope (Scope);
                  Status = PrivateData->FormBrowserEx2->ExecuteAction(BROWSER_ACTION_EXIT, 0);

                  *ActionRequest = EFI_BROWSER_ACTION_REQUEST_EXIT;
                } else {
                  do {
                    CreatePopUp (
                      EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE,
                      &Key,
                      L"",
                      L"Could not find first trusted bootloader.",
                      L"Wizard will reset the system and let firmware decide what to boot next\n",
                      L"",
                      L"Press ENTER to reset the system...",
                      L"",
                      NULL
                      );
                  } while (Key.UnicodeChar != CHAR_CARRIAGE_RETURN);

                  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
                }
              } else {
                // Unexpected failure when finalizing the provisioning, reset
                // bootloader index and go back to welcome form
                mBootloaderIndex = 0;
                PrivateData->FormBrowserEx2->SetScope (FormSetLevel);
                PrivateData->FormBrowserEx2->ExecuteAction(BROWSER_ACTION_EXIT, 0);
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
            } else if (EFI_ERROR(Status)) {
              do {
                CreatePopUp (
                  EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE,
                  &Key,
                  L"",
                  L"Could not get bootloader description.",
                  L"",
                  L"Press ENTER to continue...",
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
          }
          break;
        case TRUST_KEY_AND_BOOT_FORM2_QUESTION_ID:
          BootloaderToBoot = mBootloaderIndex;
          // Add cert or image hash to DB
          Status = AddKeyOrHashAsTrustedOrUntrusted(PrivateData, TRUE);
          if (EFI_ERROR (Status)) {
            // If we are already provisioned and fail, simply exit the wizard.
            // If the image verification fails, a string will be shown on the
            // screen by the boot manager.
            if (PrivateData->NvConfig.SvBootProvisioned) {
              if (PrivateData->ConfigData.AppLaunchCause == SV_BOOT_LAUNCH_VIA_SETUP) {
                Scope = FormSetLevel;
              } else {
                Scope = SystemLevel;
              }
              PrivateData->FormBrowserEx2->SetScope (Scope);
              Status = PrivateData->FormBrowserEx2->ExecuteAction(BROWSER_ACTION_EXIT, 0);

              *ActionRequest = EFI_BROWSER_ACTION_REQUEST_EXIT;
            }
            return Status;
          }
          // If we failed image verification and decided to trust the image, simply boot it
          if (PrivateData->ConfigData.AppLaunchCause == SV_BOOT_LAUNCH_IMAGE_VERIFICATION_FAILED) {
              Status = BootTheBootloader (PrivateData, BootloaderToBoot);
              PrivateData->FormBrowserEx2->SetScope (SystemLevel);
              Status = PrivateData->FormBrowserEx2->ExecuteAction(BROWSER_ACTION_EXIT, 0);

              *ActionRequest = EFI_BROWSER_ACTION_REQUEST_EXIT;
              return Status;
          }

          // All is left here is to enroll PK to enable Secure Boot, set
          // Sovereign Boot to provisioned and boot.
          Status = FinalizeSvBootProvisioning ();
          if (!EFI_ERROR (Status)) {
            CreatePopUp (
              EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE,
              NULL,
              L"",
              L"Sovereign Boot provisioning successful.",
              L"The Wizard will now boot the selected bootloader.",
              L"",
              NULL
              );
            gBS->Stall (2 * 1000 * 1000);
            Status = BootTheBootloader (PrivateData, BootloaderToBoot);
            // Do not go back to wizard after booting
            if (PrivateData->ConfigData.AppLaunchCause == SV_BOOT_LAUNCH_VIA_SETUP) {
              Scope = FormSetLevel;
            } else {
              Scope = SystemLevel;
            }
            PrivateData->FormBrowserEx2->SetScope (Scope);
            Status = PrivateData->FormBrowserEx2->ExecuteAction(BROWSER_ACTION_EXIT, 0);

            *ActionRequest = EFI_BROWSER_ACTION_REQUEST_EXIT;
          } else {
            // Unexpected failure when finalizing the provisioning, reset
            // bootloader index and go back to welcome form
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
          if (!mBootloadersInitted) {
            Status = GetBootOptions (PrivateData);
            if (!EFI_ERROR (Status)) {
              mBootloadersInitted = TRUE;
              if (!mBootloadersShown) {
                Status = UpdateBootloaderPage (PrivateData);
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
                  L"Press ENTER to exit Sovereign Boot configuration...",
                  L"",
                  NULL
                  );
              } while (Key.UnicodeChar != CHAR_CARRIAGE_RETURN);
              // If we failed image verification but the image
              // is not a correct boot option on HDD, simply exit
              if (PrivateData->ConfigData.AppLaunchCause == SV_BOOT_LAUNCH_IMAGE_VERIFICATION_FAILED) {
                  PrivateData->FormBrowserEx2->SetScope (SystemLevel);
                  Status = PrivateData->FormBrowserEx2->ExecuteAction(BROWSER_ACTION_EXIT, 0);
                  *ActionRequest = EFI_BROWSER_ACTION_REQUEST_EXIT;
                  return Status;
              }
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

  HiiSetBrowserData (
    &gSovereignBootWizardFormSetGuid,
    mVarStoreName,
    sizeof (SOVEREIGN_BOOT_WIZARD_FORM_DATA),
    (CONST UINT8 *)&PrivateData->FormData,
    NULL);

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
  SOVEREIGN_BOOT_WIZARD_CONFIG_DATA      *ConfigData;
  SOVEREIGN_BOOT_WIZARD_NV_CONFIG        *SvConfig;
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

  Status = gBS->LocateProtocol (&gEdkiiFormBrowserEx2ProtocolGuid, NULL, (VOID **)&FormBrowserEx2);
  if (EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
    return Status;
  }
  mPrivateData->FormBrowserEx2 = FormBrowserEx2;

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

  if (Status == EFI_ALREADY_STARTED) {
    Status = EFI_SUCCESS;
  }

  if (EFI_ERROR (Status)) {
    SovereignBootWizardUnload (ImageHandle);
    return Status;
  }

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
    // Ensure the variable is set if there was an error reading it.
    ConfigData->AlreadyStarted = TRUE;
    gRT->SetVariable (
      mSvBootDataVarName,
      &gSovereignBootWizardFormSetGuid,
      EFI_VARIABLE_BOOTSERVICE_ACCESS,
      BufferSize,
      ConfigData);
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
    ConfigData->AlreadyStarted = TRUE;
    gRT->SetVariable (
      mSvBootDataVarName,
      &gSovereignBootWizardFormSetGuid,
      EFI_VARIABLE_BOOTSERVICE_ACCESS,
      BufferSize,
      ConfigData);
  }

  // If the variable was not set properly, the wizard was probably launched by
  // mistake. The correct path to launch the wizard always sets the variable.
  if (ConfigData->AppLaunchCause == SV_BOOT_LAUNCH_UNDEFINED) {
    return SovereignBootWizardUnload (ImageHandle);
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
  if (mPrivateData->FormBrowserEx2 != NULL) {
    HotKey.UnicodeChar = CHAR_NULL;
    HotKey.ScanCode    = SCAN_F9;
    mPrivateData->FormBrowserEx2->RegisterHotKey (&HotKey, 0, 0, NULL);
    HotKey.ScanCode = SCAN_F10;
    mPrivateData->FormBrowserEx2->RegisterHotKey (&HotKey, 0, 0, NULL);
  }

  mBootloaderIndex = 0;
  mCertIndex = 0;
  mFirstTrustedBootloader = -1;
  mBootloadersInitted = FALSE;
  mBootloadersShown = FALSE;

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

  if (mPrivateData->FormBrowserEx2 != NULL) {
    //
    // Register the default HotKey F9 and F10 again.
    //
    HotKey.UnicodeChar = CHAR_NULL;
    HotKey.ScanCode    = SCAN_F10;
    NewString          = HiiGetString (HiiHandle, STRING_TOKEN (FUNCTION_TEN_STRING), NULL);
    ASSERT (NewString != NULL);
    mPrivateData->FormBrowserEx2->RegisterHotKey (&HotKey, BROWSER_ACTION_SUBMIT, 0, NewString);
    FREE_NON_NULL (NewString);

    HotKey.ScanCode = SCAN_F9;
    NewString       = HiiGetString (HiiHandle, STRING_TOKEN (FUNCTION_NINE_STRING), NULL);
    ASSERT (NewString != NULL);
    mPrivateData->FormBrowserEx2->RegisterHotKey (&HotKey, BROWSER_ACTION_DEFAULT, EFI_HII_DEFAULT_CLASS_STANDARD, NewString);
    FREE_NON_NULL (NewString);
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
    FREE_NON_NULL (mPrivateData->NameValueName[Index]);
  }

  // Free all pools from certificate, bootloader contexts and entries
  FreeBootMenuEntries ();
  FREE_NON_NULL (mPrivateData);
  mPrivateData = NULL;

  return EFI_SUCCESS;
}
