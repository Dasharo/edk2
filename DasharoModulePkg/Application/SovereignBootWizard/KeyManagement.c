/** @file
Sovereign Boot Wizard implementation.

Copyright (c) 2025, 3mdeb Sp z o.o. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "SovereignBootWizard.h"

STATIC EFI_STATUS
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

STATIC EFI_STATUS
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

EFI_STATUS
PrepareSbVariablesForSvBoot (
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

  // For Sovereign Boot we want to keep dbx to prohibit booting
  // known revoked, buggy and malicious images.
  Status = EnrollDbxFromDefault ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return SetSecureBootMode (STANDARD_SECURE_BOOT_MODE);
}

/** Creates EFI Signature List structure.

  @param[in]      Data     A pointer to signature data.
  @param[in]      Size     Size of signature data.
  @param[in]      SigType  GUID representing signature type.
  @param[out]     SigList  Created Signature List.

  @retval  EFI_SUCCESS           Signature List was created successfully.
  @retval  EFI_OUT_OF_RESOURCES  Failed to allocate memory.
**/
STATIC EFI_STATUS
CreateSigList (
  IN VOID                 *Data,
  IN UINTN                Size,
  IN EFI_GUID             *SigType,
  OUT EFI_SIGNATURE_LIST  **SigList
  )
{
  UINTN               SigListSize;
  EFI_SIGNATURE_LIST  *TmpSigList;
  EFI_SIGNATURE_DATA  *SigData;

  //
  // Allocate data for Signature Database
  //
  SigListSize = sizeof (EFI_SIGNATURE_LIST) + sizeof (EFI_SIGNATURE_DATA) - 1 + Size;
  TmpSigList  = (EFI_SIGNATURE_LIST *)AllocateZeroPool (SigListSize);
  if (TmpSigList == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  TmpSigList->SignatureListSize   = (UINT32)SigListSize;
  TmpSigList->SignatureSize       = (UINT32)(sizeof (EFI_SIGNATURE_DATA) - 1 + Size);
  TmpSigList->SignatureHeaderSize = 0;
  CopyGuid (&TmpSigList->SignatureType, SigType);

  //
  // Copy key data
  //
  SigData = (EFI_SIGNATURE_DATA *)(TmpSigList + 1);
  CopyGuid (&SigData->SignatureOwner, &gSovereignBootWizardFormSetGuid);
  CopyMem (&SigData->SignatureData[0], Data, Size);

  *SigList = TmpSigList;

  return EFI_SUCCESS;
}

/**
  Enroll a new hash into Signature Database (DB or DBX).

  @param[in] PrivateData     The module's private data.
  @param[in] VariableName    Variable name of signature database, must be
                             EFI_IMAGE_SECURITY_DATABASE or EFI_IMAGE_SECURITY_DATABASE1.

  @retval   EFI_SUCCESS            New X509 is enrolled successfully.
  @retval   EFI_OUT_OF_RESOURCES   Could not allocate needed resources.

**/
STATIC EFI_STATUS
EnrollHashToSigDB (
  IN SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *PrivateData,
  IN CHAR16                              *VariableName
  )
{
  EFI_SIGNATURE_LIST                   *SigDBHash;
  EFI_CERT_X509_SHA256                 SigCertHashData;
  UINTN                                DataSize;
  UINTN                                SigDBSize;
  UINT32                               Attr;
  EFI_TIME                             Time;
  SV_MENU_ENTRY                        *BootloaderEntry;
  SV_SECURITY_CONTEXT                  *SecurityContext;
  SV_CERT_ENTRY                        *CertificateEntry;
  EFI_STATUS                           Status;
  BOOLEAN                              Trust;
  EFI_INPUT_KEY                        Key;

  BootloaderEntry = GetMenuEntry (&BootOptionMenu, mBootloaderIndex);
  if (BootloaderEntry == NULL) {
    return EFI_NO_MEDIA;
  }

  SecurityContext = (SV_SECURITY_CONTEXT *)BootloaderEntry->SecurityContext;
  if (SecurityContext == NULL) {
    return EFI_NO_MEDIA;
  }

  if (SecurityContext->ImageIsSigned) {
    CertificateEntry = GetCertEntry (BootloaderEntry, mCertIndex);
    if (CertificateEntry == NULL) {
      return EFI_NO_MEDIA;
    }
  }

  DataSize      = 0;
  SigDBSize     = 0;
  SigDBHash     = NULL;
  Trust = (StrCmp(VariableName, EFI_IMAGE_SECURITY_DATABASE) == 0);

  SigDBSize = sizeof (EFI_SIGNATURE_LIST) + sizeof (EFI_SIGNATURE_DATA) - 1;

  if (SecurityContext->ImageIsSigned) {
    // EDK2 only checks X509 certificates in DB, so enroll whole cert to DB.
    // Otherwise enroll SHA256 to DBX to save space.
    if (Trust) {
      SigDBSize += CertificateEntry->CertDataSize;
      Status = CreateSigList (
        CertificateEntry->CertData,
        CertificateEntry->CertDataSize,
        &gEfiCertX509Guid,
        &SigDBHash
        );
    } else {
      SigDBSize += sizeof (EFI_CERT_X509_SHA256);
      // If enrolling certificate hash to DBX, set to revoke always (keep revocation time 0).
      SetMem (&SigCertHashData.TimeOfRevocation, sizeof(EFI_TIME), 0);
      CopyMem (&SigCertHashData.ToBeSignedHash, CertificateEntry->CertDigest, CertificateEntry->CertDigestSize);
      Status = CreateSigList (
        &SigCertHashData,
        sizeof (EFI_CERT_X509_SHA256),
        &CertificateEntry->CertType,
        &SigDBHash
        );
    }
  } else {
    SigDBSize += SHA256_DIGEST_SIZE;
    Status = CreateSigList (
      SecurityContext->ImageDigest,
      SecurityContext->ImageDigestSize,
      &SecurityContext->HashType,
      &SigDBHash
      );
  }

  if (SigDBHash == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  //
  // Check if signature database entry has been already populated.
  // If true, use EFI_VARIABLE_APPEND_WRITE attribute to append the
  // new signature data to original variable
  //
  Attr = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_RUNTIME_ACCESS
         | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS;
  Status = GetCurrentTime (&Time);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Fail to fetch valid time data: %r\n", Status));
    goto ON_EXIT;
  }

  Status = CreateTimeBasedPayload (&SigDBSize, (UINT8 **)&SigDBHash, &Time);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to create time-based data payload: %r\n", Status));
    goto ON_EXIT;
  }

  Status = gRT->GetVariable (
                  VariableName,
                  &gEfiImageSecurityDatabaseGuid,
                  NULL,
                  &DataSize,
                  NULL
                  );
  if (Status == EFI_BUFFER_TOO_SMALL) {
    Attr |= EFI_VARIABLE_APPEND_WRITE;
  } else if (Status != EFI_NOT_FOUND) {
    goto ON_EXIT;
  }

  if (SecurityContext->ImageIsSigned && Trust) {
    if (!CertificateEntry->CertIsValid) {
      do {
        CreatePopUp (
          EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE,
          &Key,
          L"",
          L"The certificate is not yet valid or expired.",
          L"Can not add the certificate as trusted."
          L"",
          L"Press ENTER to abort the process...",
          L"",
          NULL
          );
      } while (Key.UnicodeChar != CHAR_CARRIAGE_RETURN);
      Status = EFI_ABORTED;
      goto ON_EXIT;
    }
    if (!CertificateEntry->SignatureValid) {
      do {
        CreatePopUp (
          EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE,
          &Key,
          L"",
          L"The image signature verification failed with this certificate.",
          L"Can not add the certificate as trusted."
          L"",
          L"Press ENTER to abort the process...",
          L"",
          NULL
          );
      } while (Key.UnicodeChar != CHAR_CARRIAGE_RETURN);
      Status = EFI_ABORTED;
      goto ON_EXIT;
    }
    if (PrivateData->ConfigData.AppLaunchCause == SV_BOOT_LAUNCH_IMAGE_VERIFICATION_FAILED) {
      if (CertificateEntry->CertIsInDbx) {
        do {
          CreatePopUp (
            EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE,
            &Key,
            L"",
            L"This certificate is currently untrusted.",
            L"Removing certificates from DBX is not yet supported.",
            L"Can not add the certificate as trusted."
            L"",
            L"Press ENTER to abort the process...",
            L"",
            NULL
            );
        } while (Key.UnicodeChar != CHAR_CARRIAGE_RETURN);
        Status = EFI_ABORTED;
        goto ON_EXIT;
      }
    }
  }

  Status = SetSecureBootMode (CUSTOM_SECURE_BOOT_MODE);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to set custom Secure Boot mode: %r\n", Status));
    goto ON_EXIT;
  }

  Status = gRT->SetVariable (
                  VariableName,
                  &gEfiImageSecurityDatabaseGuid,
                  Attr,
                  SigDBSize,
                  SigDBHash
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to write the %s variable: %r\n", VariableName, Status));
  } else {
    if (Trust) {
      mFirstTrustedBootloader = (INTN)mBootloaderIndex;
    }
    // If image is unsigned or added as untrusted we have to increment the
    // bootloader index to show next one. No point in displaying other
    // signatures if one of them is already in DBX, as the image will not
    // pass Secure Boot verification. Otherwise move to next certificate.
    if (!SecurityContext->ImageIsSigned || !Trust) {
      mBootloaderIndex++;
      mCertIndex = 0;
      DEBUG ((DEBUG_INFO, "Moving to next bootloader %u\n", mBootloaderIndex));
    } else {
      mCertIndex++;
      DEBUG ((DEBUG_INFO, "Moving to next certificate %u for bootloader %u\n", mCertIndex, mBootloaderIndex));
    }
  }

  Status = SetSecureBootMode (STANDARD_SECURE_BOOT_MODE);

ON_EXIT:

  FREE_NON_NULL (SigDBHash);

  return Status;
}

EFI_STATUS
AddKeyOrHashAsTrustedOrUntrusted (
  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA   *PrivateData,
  BOOLEAN                              Trust
  )
{
  EFI_STRING                           Message;
  EFI_HII_POPUP_SELECTION              UserSelection;
  EFI_STRING                           HeaderString;
  EFI_STRING                           HashString;
  EFI_STRING                           PopupMessage;
  UINTN                                PopupMessageSize;
  EFI_STATUS                           Status;
  CHAR16                               ErrorMessage[MAXIMUM_VALUE_CHARACTERS + 8];
  EFI_INPUT_KEY                        Key;

  Message = HiiGetString (
              PrivateData->HiiHandle,
              Trust ? STRING_TOKEN(STR_SV_TRUST_KEY_QUESTION) : STRING_TOKEN(STR_SV_UNTRUST_KEY_QUESTION),
              NULL);
  HeaderString = HiiGetString (PrivateData->HiiHandle, STRING_TOKEN(STR_KEY_FINGERPRINT), NULL);
  HashString = HiiGetString (PrivateData->HiiHandle, STRING_TOKEN(STR_KEY_FINGERPRINT_HASH), NULL);

  // Size of the whole message plus couple new lines
  PopupMessageSize = StrSize(Message) + StrSize(HeaderString) + StrSize(HashString) + 6;
  PopupMessage = (CHAR16 *) AllocateZeroPool(PopupMessageSize);

  if (PopupMessage == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  UnicodeSPrint (PopupMessage, PopupMessageSize, L"%s\n%s\n%s", Message, HeaderString, HashString);
  HiiSetString (PrivateData->HiiHandle, STRING_TOKEN (STR_SV_TRUST_KEY_POPUP), PopupMessage, NULL) ;

  Status = PrivateData->HiiPopup->CreatePopup (
             PrivateData->HiiPopup,
             EfiHiiPopupStyleInfo,
             EfiHiiPopupTypeYesNo,
             PrivateData->HiiHandle,
             STRING_TOKEN (STR_SV_TRUST_KEY_POPUP),
             &UserSelection
             );
  if (UserSelection == EfiHiiPopupSelectionYes) {
    // Add key or hash to DB or DBX

    Status = EnrollHashToSigDB (
               PrivateData,
               Trust ? EFI_IMAGE_SECURITY_DATABASE : EFI_IMAGE_SECURITY_DATABASE1
               );

    if (EFI_ERROR (Status) && Status != EFI_ABORTED) {
      UnicodeSPrint (
        ErrorMessage,
        (MAXIMUM_VALUE_CHARACTERS + 8) * sizeof (CHAR16),
        L"Error: %r",
        Status);

      CreatePopUp (
        EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE,
        &Key,
        L"",
        Trust ? L"Could not add the certificate/image as trusted" :
                L"Could not add the certificate/image as untrusted",
        L"",
        ErrorMessage,
        L"",
        L"Press any key to close this window...",
        L"",
        NULL
        );
    }
  } else {
    Status = EFI_ABORTED;
  }

  FreePool (PopupMessage);

  return Status;
}

STATIC EFI_STATUS
EnrollEphemeralPk (
  VOID
  )
{
  VOID                                 *RsaCtx;
  EFI_SIGNATURE_LIST                   *SigList;
  EFI_SIGNATURE_DATA                   *SigData;
  UINTN                                SigListSize;
  UINTN	                               KeySize;
  EFI_STATUS                           Status;
  UINT32                               Attr;
  EFI_TIME                             Time;
  CONST UINT32                         Exponent = 0x10001;

  KeySize = 2048;
  SigList = NULL;

  RsaCtx = RsaNew ();
  if (RsaCtx == NULL) {
    DEBUG ((DEBUG_ERROR, "Failed to allocate RSA context\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  DEBUG ((DEBUG_ERROR, "Generating ephemeral PK...\n"));

  // Size of the exponent must be 3, because it is treated as big endian. It
  // also has a side effect that no matter the endianness of the host, the
  // exponent will be correct.
  if (!RsaGenerateKey (RsaCtx, KeySize, (CONST UINT8 *)&Exponent, sizeof(Exponent) - 1)) {
    DEBUG ((DEBUG_ERROR, "Failed to generate RSA key\n"));
    return EFI_DEVICE_ERROR;
  }

  DEBUG ((DEBUG_ERROR, "PK generation done. Constructing signature list and enroling it\n"));

  KeySize = KeySize / 8;
  SigListSize = sizeof (EFI_SIGNATURE_LIST) + sizeof (EFI_SIGNATURE_DATA) - 1 + KeySize;
  SigList = (EFI_SIGNATURE_LIST *) AllocateZeroPool (SigListSize);
  if (SigList == NULL) {
    DEBUG ((DEBUG_ERROR, "Failed to allocate signature list for PK\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  SigList->SignatureHeaderSize = 0;
  SigList->SignatureListSize = SigListSize;
  SigList->SignatureSize = (UINT32)(SigListSize - sizeof (EFI_SIGNATURE_LIST));
  CopyGuid(&SigList->SignatureType, &gEfiCertRsa2048Guid);

  SigData = (EFI_SIGNATURE_DATA *)((UINT8 *)SigList + sizeof (EFI_SIGNATURE_LIST));
  CopyGuid(&SigData->SignatureOwner, &gSovereignBootWizardFormSetGuid);

  if (!RsaGetKey(RsaCtx, RsaKeyN, &SigData->SignatureData[0], &KeySize)) {
    DEBUG ((DEBUG_ERROR, "Failed toget RSA public key modulus\n"));
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  if (KeySize != (2048 / 8)) {
    DEBUG ((DEBUG_ERROR, "Invalid RSA key modulus size\n"));
    Status = EFI_BAD_BUFFER_SIZE;
    goto ON_EXIT;
  }

  Status = GetCurrentTime (&Time);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Could not get current time\n"));
    goto ON_EXIT;
  }

  Attr = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_RUNTIME_ACCESS
         | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS;

  Status = CreateTimeBasedPayload (&SigListSize, (UINT8 **)&SigList, &Time);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to create time-based data payload: %r\n", Status));
    goto ON_EXIT;
  }

  Status = SetSecureBootMode (CUSTOM_SECURE_BOOT_MODE);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to set custom Secure Boot mode: %r\n", Status));
    goto ON_EXIT;
  }

  Status = gRT->SetVariable (
                  EFI_PLATFORM_KEY_NAME,
                  &gEfiGlobalVariableGuid,
                  Attr,
                  SigListSize,
                  SigList
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to write the PK variable: %r\n", Status));
  }

  Status = SetSecureBootMode (STANDARD_SECURE_BOOT_MODE);

ON_EXIT:

  FREE_NON_NULL (SigList);

  if (RsaCtx != NULL) {
    RsaFree (RsaCtx);
  }

  return Status;
}

STATIC BOOLEAN
IsDbEmpty (
  VOID
)
{
  EFI_STATUS Status;
  UINTN      DataSize;

  DataSize = 0;

  Status = gRT->GetVariable (
                  EFI_IMAGE_SECURITY_DATABASE,
                  &gEfiImageSecurityDatabaseGuid,
                  NULL,
                  &DataSize,
                  NULL
                  );
  if (Status == EFI_BUFFER_TOO_SMALL) {
    // Variable exists and size is greater than 0, so not empty
    return FALSE;
  } else if (Status == EFI_SUCCESS && DataSize > 0) {
    return FALSE;
  }

  return TRUE;
}

EFI_STATUS
FinalizeSvBootProvisioning (
  VOID
  )
{
  EFI_INPUT_KEY                        Key;
  EFI_STATUS                           Status;
  UINTN                                DataSize;
  SOVEREIGN_BOOT_WIZARD_NV_CONFIG      SvConfig;

  if (IsDbEmpty ()) {
    CreatePopUp (
      EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE,
      &Key,
      L"",
      L"Trusted signature database is empty, because you have not trusted any bootloader",
      L"Can not finalize Sovereign Boot provisioning.",
      L"",
      L"Press any key to close this window...",
      L"",
      NULL
      );

    return EFI_ABORTED;
  }

  Status = EnrollEphemeralPk ();
  if (EFI_ERROR (Status)) {
    CreatePopUp (
      EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE,
      &Key,
      L"",
      L"Failed to enroll ephemeral Platform Key, Secure Boot will not be enabled.",
      L"Can not finalize Sovereign Boot provisioning.",
      L"",
      L"Press any key to close this window...",
      L"",
      NULL
      );

    return EFI_ABORTED;
  }

  DataSize = sizeof (SOVEREIGN_BOOT_WIZARD_NV_CONFIG);
  SvConfig.SvBootEnabled = TRUE;
  SvConfig.SvBootProvisioned = TRUE;
  Status = gRT->SetVariable (
                  SV_BOOT_CONFIG_VAR,
                  &gSovereignBootWizardFormSetGuid,
                  EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
                  DataSize,
                  &SvConfig
                  );

  if (EFI_ERROR (Status)) {
    CreatePopUp (
      EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE,
      &Key,
      L"",
      L"Failed to update, Sovereign Boot internal state to provisioned.",
      L"The wizard will show up again on next boot to provision again.",
      L"",
      L"Press any key to close this window...",
      L"",
      NULL
      );

    return EFI_ABORTED;
  }

  return Status;
}
