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

/**
  Helper function to populate an EFI_TIME instance.

  @param[in] Time   FileContext cached in SecureBootConfig driver

**/
STATIC
EFI_STATUS
GetCurrentTime (
  IN EFI_TIME  *Time
  )
{
  EFI_STATUS  Status;
  VOID        *TestPointer;

  if (Time == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = gBS->LocateProtocol (&gEfiRealTimeClockArchProtocolGuid, NULL, &TestPointer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ZeroMem (Time, sizeof (EFI_TIME));
  Status = gRT->GetTime (Time, NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(), GetTime() failed, status = '%r'\n",
      __func__,
      Status
      ));
    return Status;
  }

  Time->Pad1       = 0;
  Time->Nanosecond = 0;
  Time->TimeZone   = 0;
  Time->Daylight   = 0;
  Time->Pad2       = 0;

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
  EFI_SIGNATURE_DATA                   *SigDBHashData;
  EFI_CERT_X509_SHA256                 *SigDBCertHashData;
  UINTN                                DataSize;
  UINTN                                SigDBSize;
  UINT32                               Attr;
  EFI_TIME                             Time;
  SV_MENU_ENTRY                        *BootloaderEntry;
  SV_SECURITY_CONTEXT                  *SecurityContext;
  SV_CERT_ENTRY                        *CertificateEntry;
  EFI_STATUS                           Status;
  UINT8                                CertValidFrom[64];
  UINTN                                CertValidFromLen;
  UINT8                                CertValidTo[64];
  UINTN                                CertValidToLen;
  MBED_TLS_DATETIME_OBECT              *CertValidToTime;
  MBED_TLS_DATETIME_OBECT              CurrentTime;
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
  SigDBHashData = NULL;
  SigDBSize = sizeof (EFI_SIGNATURE_LIST) + sizeof (EFI_SIGNATURE_DATA) - 1;

  if (SecurityContext->ImageIsSigned) {
    SigDBSize += sizeof (EFI_CERT_X509_SHA256);
  } else {
    SigDBSize += SHA256_DIGEST_SIZE;
  }

  SigDBHash = (EFI_SIGNATURE_LIST *) AllocateZeroPool (SigDBSize);
  if (SigDBHash == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  //
  // Fill Certificate Database parameters.
  //
  SigDBHash->SignatureListSize   = (UINT32)SigDBSize;
  SigDBHash->SignatureHeaderSize = 0;
  SigDBHash->SignatureSize       = (UINT32)(SigDBSize - sizeof (EFI_SIGNATURE_LIST));

  SigDBHashData = (EFI_SIGNATURE_DATA *)((UINT8 *)SigDBHash + sizeof (EFI_SIGNATURE_LIST));
  CopyGuid (&SigDBHashData->SignatureOwner, &gSovereignBootWizardFormSetGuid);
  if (SecurityContext->ImageIsSigned) {
    CopyGuid (&SigDBHash->SignatureType, &CertificateEntry->CertType);
    SigDBCertHashData = (EFI_CERT_X509_SHA256 *)SigDBHashData->SignatureData;
    CopyMem ((UINT8 *)(SigDBCertHashData->ToBeSignedHash), CertificateEntry->CertDigest, CertificateEntry->CertDigestSize);

    // If enrolling certificate hash to DBX, set to revoke always (keep revocation time 0).
    // If enrolling to DB, set the revocation time to the expiry date
    if (StrCmp(VariableName, EFI_IMAGE_SECURITY_DATABASE) == 0) {
      CertValidFromLen = 64;
      CertValidToLen  = 64;

      if (!X509GetValidity(CertificateEntry->CertData,
                          CertificateEntry->CertDataSize,
                          CertValidFrom,
                          &CertValidFromLen,
                          CertValidTo,
                          &CertValidToLen)) {
        DEBUG ((DEBUG_ERROR, "Could not get certificate validity\n"));
        Status = EFI_NOT_FOUND;
        goto ON_EXIT;
      }

      if (CertValidToLen == 0 || CertValidToLen != sizeof (MBED_TLS_DATETIME_OBECT)) {
        DEBUG ((DEBUG_ERROR, "Invalid certificate validity length\n"));
        Status = EFI_BAD_BUFFER_SIZE;
        goto ON_EXIT;
      }

      CertValidToTime = (MBED_TLS_DATETIME_OBECT *)CertValidTo;
      SigDBCertHashData->TimeOfRevocation.Year = (UINT16)(CertValidToTime->Year & 0xFFFF);
      SigDBCertHashData->TimeOfRevocation.Month = (UINT8)(CertValidToTime->Month & 0xFF);
      SigDBCertHashData->TimeOfRevocation.Day = (UINT8)(CertValidToTime->Day & 0xFF);
      SigDBCertHashData->TimeOfRevocation.Hour = (UINT8)(CertValidToTime->Hour & 0xFF);
      SigDBCertHashData->TimeOfRevocation.Minute = (UINT8)(CertValidToTime->Minute & 0xFF);
      SigDBCertHashData->TimeOfRevocation.Second = (UINT8)(CertValidToTime->Second & 0xFF);
    }
  } else {
    CopyGuid (&SigDBHash->SignatureType, &SecurityContext->HashType);
    CopyMem ((UINT8 *)(SigDBHashData->SignatureData), SecurityContext->ImageDigest, SecurityContext->ImageDigestSize);
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

  // Do not allow to add expired certificate to DB
  if (SecurityContext->ImageIsSigned &&
      (StrCmp(VariableName, EFI_IMAGE_SECURITY_DATABASE) == 0)) {
    CurrentTime.Year = (INT32)Time.Year;
    CurrentTime.Month = (INT32)Time.Month;
    CurrentTime.Day = (INT32)Time.Day;
    CurrentTime.Hour = (INT32)Time.Hour;
    CurrentTime.Minute = (INT32)Time.Minute;
    CurrentTime.Second = (INT32)Time.Second;


    if (X509CompareDateTime (&CurrentTime, CertValidToTime) >= 0) {
        do {
          CreatePopUp (
            EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE,
            &Key,
            L"",
            L"The certificate you want to trust has expired.",
            L"Can not add it to trusted signature database.",
            L"",
            L"Press ENTER to abort the process...",
            L"",
            NULL
            );
        } while (Key.UnicodeChar != CHAR_CARRIAGE_RETURN);

        Status = EFI_SUCCESS;
        goto ON_EXIT;
    }
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
    // If image is unsigned we have to increment the bootloader index to show
    // next one. Otherwise move to next certificate.
    if (!SecurityContext->ImageIsSigned) {
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

  if (SigDBHash != NULL) {
    FreePool (SigDBHash);
  }

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
    if (Trust) {
      Status = EnrollHashToSigDB (PrivateData, EFI_IMAGE_SECURITY_DATABASE);
    } else {
      Status = EnrollHashToSigDB (PrivateData, EFI_IMAGE_SECURITY_DATABASE1);
    }

    if (EFI_ERROR (Status)) {
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
  }

  FreePool (PopupMessage);

  return Status;
}
