/** @file
Sovereign Boot Wizard signature parsing.

Copyright (c) 2025, 3mdeb Sp z o.o. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "SovereignBootWizard.h"

#define ALIGNMENT_SIZE  8
#define ALIGN_SIZE(a)  (((a) % ALIGNMENT_SIZE) ? ALIGNMENT_SIZE - ((a) % ALIGNMENT_SIZE) : 0)

/**
  Read file content into BufferPtr, the size of the allocate buffer
  is *FileSize plus AdditionAllocateSize.

  @param[in]       FileHandle            The file to be read.
  @param[in, out]  BufferPtr             Pointers to the pointer of allocated buffer.
  @param[out]      FileSize              Size of input file
  @param[out]      AuthStatus            File authentication status.

  @retval   EFI_SUCCESS                  The file was read into the buffer.
  @retval   EFI_INVALID_PARAMETER        A parameter was invalid.
  @retval   EFI_NOT_FOUND                The file was not found.
**/
EFI_STATUS
ReadFileContent (
  IN      EFI_DEVICE_PATH_PROTOCOL  *FilePath,
  IN OUT  VOID                      **BufferPtr,
  OUT     UINTN                     *FileSize,
  OUT     UINT32                    *AuthStatus
  )

{
  if ((FilePath == NULL) || (FileSize == NULL) || (BufferPtr == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *BufferPtr = GetFileBufferByFilePath (TRUE, FilePath, FileSize, AuthStatus);

  if (*BufferPtr == NULL || *FileSize == 0) {
    return EFI_NOT_FOUND;
  }

  return EFI_SUCCESS;
}

VOID
VerifyImageHashInDatabases (
  IN  VOID                      *FileBuffer,
  IN  UINTN                     FileSize,
  IN  SV_SECURITY_CONTEXT       *SecCtx
  )
{
  EFI_STATUS                    Status;
  BOOLEAN                       IsFound;
  UINT8                         HashAlg;
  UINT8                         ImageDigest[MAX_DIGEST_SIZE];
  UINTN                         ImageDigestSize;
  EFI_GUID                      CertType;

  HashAlg = HASHALG_MAX;
  while (HashAlg > 0) {
    HashAlg--;

    if (!HashPeImage (FileBuffer, FileSize, HashAlg, ImageDigest, &ImageDigestSize, &CertType)) {
      continue;
    }

    IsFound = FALSE;
    Status = IsSignatureFoundInDatabase (
                EFI_IMAGE_SECURITY_DATABASE1,
                ImageDigest,
                &CertType,
                ImageDigestSize,
                &IsFound
                );
    if (EFI_ERROR (Status) || IsFound) {
      SecCtx->ImageIsInDbx = TRUE;
    }

    IsFound = FALSE;
    Status = IsSignatureFoundInDatabase (
                EFI_IMAGE_SECURITY_DATABASE,
                ImageDigest,
                &CertType,
                ImageDigestSize,
                &IsFound
                );
    if (!EFI_ERROR (Status) && IsFound) {
      SecCtx->ImageIsInDb = TRUE;
    }
  }
}

VOID
FillCertificateEntries (
  IN      VOID                      *FileBuffer,
  IN      UINTN                     FileSize,
  IN OUT  SV_SECURITY_CONTEXT       *SecCtx,
  IN      EFI_IMAGE_DATA_DIRECTORY  *SecDataDir
  )
{
  UINT32                        SecDataDirEnd;
  UINT32                        SecDataDirLeft;
  WIN_CERTIFICATE               *WinCertificate;
  WIN_CERTIFICATE_EFI_PKCS      *PkcsCertData;
  WIN_CERTIFICATE_UEFI_GUID     *WinCertUefiGuid;
  UINT32                        Offset;
  UINT8                         *AuthData;
  UINTN                         AuthDataSize;
  EFI_STATUS                    HashStatus;
  UINT8                         ImageDigest[MAX_DIGEST_SIZE];
  UINTN                         ImageDigestSize;
  SV_CERT_ENTRY                 *NewCertEntry;
  UINT8                         *CertBuffer;
  UINTN                         BufferLength;
  UINT8                         *TrustedCert;
  UINTN                         TrustedCertLength;
  UINTN                         CertCount;
  EFI_GUID                      CertType;
  UINTN                         Index;
  EFI_STATUS                    Status;
  BOOLEAN                       IsFound;

  CertCount = 0;
  //
  // Verify the signature of the image, multiple signatures are allowed as per PE/COFF Section 4.7
  // "Attribute Certificate Table".
  // The first certificate starts at offset (SecDataDir->VirtualAddress) from the start of the file.
  //
  SecDataDirEnd = SecDataDir->VirtualAddress + SecDataDir->Size;
  for (Offset = SecDataDir->VirtualAddress;
       Offset < SecDataDirEnd;
       Offset += (WinCertificate->dwLength + ALIGN_SIZE (WinCertificate->dwLength)))
  {
    SecDataDirLeft = SecDataDirEnd - Offset;
    if (SecDataDirLeft <= sizeof (WIN_CERTIFICATE)) {
      DEBUG ((DEBUG_ERROR, "Image Security Data Size too small\n"));
      break;
    }

    WinCertificate = (WIN_CERTIFICATE *)(FileBuffer + Offset);
    if ((SecDataDirLeft < WinCertificate->dwLength) ||
        (SecDataDirLeft - WinCertificate->dwLength <
         ALIGN_SIZE (WinCertificate->dwLength)))
    {
      DEBUG ((DEBUG_ERROR, "Image Security Data Size too small\n"));
      break;
    }

    //
    // Verify the image's Authenticode signature, only DER-encoded PKCS#7 signed data is supported.
    //
    if (WinCertificate->wCertificateType == WIN_CERT_TYPE_PKCS_SIGNED_DATA) {
      //
      // The certificate is formatted as WIN_CERTIFICATE_EFI_PKCS which is described in the
      // Authenticode specification.
      //
      PkcsCertData = (WIN_CERTIFICATE_EFI_PKCS *)WinCertificate;
      if (PkcsCertData->Hdr.dwLength <= sizeof (PkcsCertData->Hdr)) {
        DEBUG ((DEBUG_ERROR, "PKCS Cert Data too small\n"));
        break;
      }

      AuthData     = PkcsCertData->CertData;
      AuthDataSize = PkcsCertData->Hdr.dwLength - sizeof (PkcsCertData->Hdr);
    } else if (WinCertificate->wCertificateType == WIN_CERT_TYPE_EFI_GUID) {
      //
      // The certificate is formatted as WIN_CERTIFICATE_UEFI_GUID which is described in UEFI Spec.
      //
      WinCertUefiGuid = (WIN_CERTIFICATE_UEFI_GUID *)WinCertificate;
      if (WinCertUefiGuid->Hdr.dwLength <= OFFSET_OF (WIN_CERTIFICATE_UEFI_GUID, CertData)) {
        DEBUG ((DEBUG_ERROR, "WIN Cert UEFI Data too small\n"));
        break;
      }

      if (!CompareGuid (&WinCertUefiGuid->CertType, &gEfiCertPkcs7Guid)) {
        DEBUG ((DEBUG_ERROR, "Cert Type not PKCS7\n"));
        continue;
      }

      AuthData     = WinCertUefiGuid->CertData;
      AuthDataSize = WinCertUefiGuid->Hdr.dwLength - OFFSET_OF (WIN_CERTIFICATE_UEFI_GUID, CertData);
    } else {
      if (WinCertificate->dwLength < sizeof (WIN_CERTIFICATE)) {
        DEBUG ((DEBUG_ERROR, "Unknown Cert Data too small\n"));
        break;
      }

      DEBUG ((DEBUG_INFO, "Unknown Cert Data, skipping\n"));
      continue;
    }

    NewCertEntry = (SV_CERT_ENTRY *) AllocateZeroPool (sizeof(SV_CERT_ENTRY));

    if (NewCertEntry == NULL) {
      DEBUG ((DEBUG_ERROR, "Not enough free memory for certificate data\n"));
      return;
    }

    NewCertEntry->Signature = SOVEREIGN_BOOT_CERT_ENTRY_SIGNATURE;

    // Obtain the signer's certificate
    if (!Pkcs7GetSigners (AuthData, AuthDataSize,
                          &CertBuffer, &BufferLength,
                          &TrustedCert, &TrustedCertLength)) {
      FreePool (NewCertEntry);
      DEBUG ((DEBUG_ERROR, "Could not get PKCS7 signers\n"));
      continue;
    }
    if ((BufferLength == 0) || (CertBuffer == NULL) || ((*CertBuffer) == 0)) {
      FreePool (NewCertEntry);
      DEBUG ((DEBUG_ERROR, "PKCS7 signers data invalid\n"));
      continue;
    }

    // Free unused cert chain
    Pkcs7FreeSigners (CertBuffer);

    // Save the buffer to the signer's certificate
    NewCertEntry->CertData = TrustedCert;
    NewCertEntry->CertDataSize = TrustedCertLength;

    if (CalculateCertHash (TrustedCert, TrustedCertLength, HASHALG_SHA256, NewCertEntry->CertDigest)) {
      NewCertEntry->CertDigestSize = SHA256_DIGEST_SIZE;
      CopyGuid (&NewCertEntry->CertType, &gEfiCertX509Sha256Guid);
    } else {
      DEBUG ((DEBUG_ERROR, "Could not calculate TBS certificate hash\n"));
    }

    HashStatus = HashPeImageByType (
                   FileBuffer,
                   FileSize,
                   AuthData,
                   AuthDataSize,
                   ImageDigest,
                   &ImageDigestSize,
                   &CertType);
    if (EFI_ERROR (HashStatus)) {
      if (TrustedCert != NULL) {
        Pkcs7FreeSigners (TrustedCert);
      }
      FreePool (NewCertEntry);
      DEBUG ((EFI_D_ERROR, "Failed to calculate image hash\n"));
      continue;
    }

    //
    // Verify the digital signature and check against databases
    //

    // EDK2 checks the DB only against full X509 certificates
    IsFound = FALSE;
    Status = IsSignatureFoundInDatabase (
                  EFI_IMAGE_SECURITY_DATABASE,
                  NewCertEntry->CertData,
                  &gEfiCertX509Guid,
                  NewCertEntry->CertDataSize,
                  &IsFound
                  );
    DEBUG ((DEBUG_INFO, "IsSignatureFoundInDatabase: %u, %r\n", IsFound, Status));
    if (!EFI_ERROR (Status) && IsFound) {
      NewCertEntry->CertIsInDb = TRUE;
    } else {
      NewCertEntry->CertIsInDb = FALSE;
    }

    NewCertEntry->CertIsInDbx = IsForbiddenByDbx (AuthData, AuthDataSize, ImageDigest, ImageDigestSize);
    // TODO, doesn't work. Iterate over whole EFI_CERT_STACK of CertBuffer?
    NewCertEntry->ImageIsVerified = AuthenticodeVerify (
                                      AuthData, AuthDataSize,
                                      TrustedCert, TrustedCertLength,
                                      ImageDigest, ImageDigestSize);


    DEBUG ((DEBUG_INFO, "Certificate Details:\n"
      "  ImageIsVerified: %u\n"
      "  CertIsInDb: %u\n"
      "  CertIsInDbx: %u\n"
      "  CertIsMicrosoft: %u\n",
      NewCertEntry->ImageIsVerified,
      NewCertEntry->CertIsInDb,
      NewCertEntry->CertIsInDbx,
      NewCertEntry->CertIsMicrosoft));

    DEBUG ((DEBUG_INFO, "Certificate hash:\n"));
    for (Index = 0; Index < NewCertEntry->CertDigestSize; Index++) {
      DEBUG ((DEBUG_INFO, "%02X", NewCertEntry->CertDigest[Index]));
    }
    DEBUG ((DEBUG_INFO, "\n"));

    InsertTailList (&SecCtx->Certs, &NewCertEntry->CertLink);
    CertCount++;
  }

  if (Offset != SecDataDirEnd) {
    DEBUG ((EFI_D_ERROR, "The Size in Certificate Table or the attribute certificate table is corrupted.\n"));
  }

  SecCtx->NumCertificates = CertCount;
}

/**
  Fill the security information from the image.

  @param Entry                 The boot option entry data.

  @return EFI_NOT_FOUND        Fail to find file.
  @return EFI_OUT_OF_RESOURCES Out of memory.
  @return EFI_SUCESS           Success to fill security context.

**/
EFI_STATUS
FillSecurityContext (
  IN  SV_MENU_ENTRY  *Entry
  )
{
  SV_SECURITY_CONTEXT           *SecCtx;
  SV_LOAD_CONTEXT               *LoadCtx;
  EFI_IMAGE_DATA_DIRECTORY      *SecDir;
  EFI_DEVICE_PATH_PROTOCOL      *FullFilePath;
  VOID                          *ImageBase;
  UINTN                         ImageSize;
  UINT32                        AuthStatus;
  EFI_STATUS                    Status;

  FullFilePath = NULL;
  LoadCtx = (SV_LOAD_CONTEXT *)Entry->VariableContext;
  SecCtx = (SV_SECURITY_CONTEXT *)AllocateZeroPool (sizeof(SV_SECURITY_CONTEXT));
  Entry->SecurityContext = SecCtx;

  if (SecCtx == NULL) {
    DEBUG ((DEBUG_ERROR, "Not enough memory for security context\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  // Expand the Device PAth to the file if needed
  if (LoadCtx->NeedsPathExpansion) {
    FullFilePath = EfiBootManagerGetNextLoadOptionDevicePath (LoadCtx->FilePath, FullFilePath);
  } else {
    FullFilePath = LoadCtx->FilePath;
  }

  //
  // Read the whole file content
  //
  Status = ReadFileContent (
             FullFilePath,
             (VOID **)&ImageBase,
             &ImageSize,
             &AuthStatus
             );

  if (LoadCtx->NeedsPathExpansion && (FullFilePath != NULL)) {
    FreePool (FullFilePath);
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to read file: %r\n", Status));
    goto ON_EXIT;
  }

  Status = GetImageSecDataDir (ImageBase, ImageSize, &SecDir);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to get Image Security Data\n"));
    goto ON_EXIT;
  }

  // Always calculate SHA256 hash of the image and store it for display and
  // possible trust/distrust choice.
  HashPeImage (ImageBase,
               ImageSize,
               HASHALG_SHA256,
               SecCtx->ImageDigest,
               &SecCtx->ImageDigestSize,
               &SecCtx->HashType);

  //
  // Start Image Validation.
  //
  VerifyImageHashInDatabases (ImageBase, ImageSize, SecCtx);

  // No certificates, end here
  if ((SecDir == NULL) || (SecDir->Size == 0)) {
    DEBUG ((DEBUG_INFO, "No Image Security Data, image is unsigned\n"));
    SecCtx->ImageIsSigned = FALSE;
    SecCtx->NumCertificates = 0;
    goto ON_EXIT;
  }

  SecCtx->ImageIsSigned = TRUE;

  // Parse certificates
  InitializeListHead (&SecCtx->Certs);
  FillCertificateEntries (ImageBase, ImageSize, SecCtx, SecDir);
  Status = EFI_SUCCESS;

ON_EXIT:
  DEBUG ((DEBUG_INFO, "Image Details:\n"
    "  ImageIsInDbx: %u\n"
    "  ImageIsInDb: %u\n"
    "  ImageIsSigned: %u\n"
    "  AuthenticationStatus: %u\n"
    "  NumCertificates: %u\n",
    SecCtx->ImageIsInDbx,
    SecCtx->ImageIsInDb,
    SecCtx->ImageIsSigned,
    SecCtx->AuthenticationStatus,
    SecCtx->NumCertificates));

  if (ImageBase != NULL) {
    FreePool (ImageBase);
  }

  return Status;
}

/**
  Get the Cert Entry from the list in Menu Entry.

  If CertNumber is great or equal to the number of Cert
  Entry in the list, then ASSERT.

  @param MenuOption      The Menu Entry to read the cert entry.
  @param MenuNumber      The index of Cert Entry.

  @return The Menu Entry.

**/
SV_CERT_ENTRY *
GetCertEntry (
  SV_MENU_ENTRY  *MenuEntry,
  UINTN           CertNumber
  )
{
  SV_CERT_ENTRY        *NewCertEntry;
  SV_SECURITY_CONTEXT  *SecCtx;
  UINTN                Index;
  LIST_ENTRY           *List;

  SecCtx = (SV_SECURITY_CONTEXT *) MenuEntry->SecurityContext;

  if (SecCtx == NULL) {
    return NULL;
  }

  if (CertNumber >= SecCtx->NumCertificates) {
    return NULL;
  }

  List = SecCtx->Certs.ForwardLink;
  for (Index = 0; Index < CertNumber; Index++) {
    List = List->ForwardLink;
  }

  NewCertEntry = CR (List, SV_CERT_ENTRY, CertLink, SOVEREIGN_BOOT_CERT_ENTRY_SIGNATURE);

  return NewCertEntry;
}

/**
  Parse hash value from buffer, and save in the CHAR16 type array.
  The buffer is callee allocated and should be freed by the caller.

  @param[in]    Digest                    The pointer to the hash value.
  @param[in]    DigestSize                The size of the hash.
  @param[out]   BufferToReturn            Buffer to save the hash value.

  @retval       EFI_INVALID_PARAMETER     Invalid Hash or Buffer.
  @retval       EFI_OUT_OF_RESOURCES      A memory allocation failed.
  @retval       EFI_SUCCESS               Operation success.
**/
EFI_STATUS
ParseHashValue (
  IN  UINT8                  *Digest,
  IN  UINTN                  DigestSize,
  OUT CHAR16                 **BufferToReturn
  )
{
  UINTN  Index;
  UINTN  BufferIndex;
  UINTN  TotalSize;

  if ((Digest == NULL) || (DigestSize == 0) || (BufferToReturn == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Each byte will split two Hex-number.
  //
  TotalSize = (DigestSize * 2 * sizeof (CHAR16));
  // One extra byte for NULL termination
  TotalSize +=  sizeof (CHAR16);

  *BufferToReturn = AllocateZeroPool (TotalSize);
  if (*BufferToReturn == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  for (Index = 0, BufferIndex = 0; Index < DigestSize; Index++) {
    BufferIndex += UnicodeSPrint (&(*BufferToReturn)[BufferIndex], TotalSize - sizeof (CHAR16) * BufferIndex, L"%02x", Digest[Index]);
  }

  return EFI_SUCCESS;
}

/**
  Parse key modulus from buffer, and save in the CHAR16 type array.
  The buffer is callee allocated and should be freed by the caller.

  @param[in]    Digest                    The pointer to the hash value.
  @param[in]    DigestSize                The size of the hash.
  @param[out]   BufferToReturn            Buffer to save the hash value.

  @retval       EFI_INVALID_PARAMETER     Invalid Hash or Buffer.
  @retval       EFI_OUT_OF_RESOURCES      A memory allocation failed.
  @retval       EFI_SUCCESS               Operation success.
**/
EFI_STATUS
ParseKeyModulus (
  IN  UINT8                  *Digest,
  IN  UINTN                  DigestSize,
  OUT CHAR16                 **BufferToReturn
  )
{
  UINTN  Index;
  UINTN  BufferIndex;
  UINTN  TotalSize;
  UINTN  BytesPerLine;
  UINTN  Line;

  if ((Digest == NULL) || (DigestSize == 0) || (BufferToReturn == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  BufferIndex = 0;
  BytesPerLine = 16;
  Line = ((DigestSize + BytesPerLine - 1) / BytesPerLine);

  //
  // Each byte will be split into two hex numbers + colon delimiter. Each line
  // will start with 4 spaces and end with newline (/r/n) so need another 6
  // chars per line.
  //
  TotalSize = ((DigestSize * 3) + (Line * 6)) * sizeof (CHAR16);
  // One extra byte for NULL termination
  TotalSize +=  sizeof (CHAR16);

  *BufferToReturn = AllocateZeroPool (TotalSize);
  if (*BufferToReturn == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  BufferIndex += UnicodeSPrint (
                   &(*BufferToReturn)[BufferIndex],
                   TotalSize - sizeof (CHAR16) * BufferIndex,
                   L"    ");
  for (Index = 0; Index < DigestSize; Index++) {
    if ((Index > 0) && (Index % BytesPerLine == 0)) {
      BufferIndex += UnicodeSPrint (
                       &(*BufferToReturn)[BufferIndex],
                       TotalSize - sizeof (CHAR16) * BufferIndex,
                       L"\n    ");
    }

    BufferIndex += UnicodeSPrint (
                     &(*BufferToReturn)[BufferIndex],
                     TotalSize - sizeof (CHAR16) * BufferIndex,
                     Index == (DigestSize - 1) ? L"%02x" : L"%02x:",
                     Digest[Index]);

  }

  return EFI_SUCCESS;
}

EFI_STATUS
UpdateCertInfo (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN  UINTN                               OptionNumber
  )
{
  SV_MENU_ENTRY        *BootloaderEntry;
  SV_CERT_ENTRY        *CertificateEntry;
  SV_SECURITY_CONTEXT  *SecurityContext;
  EFI_STRING           NewString;
  EFI_STATUS           Status;

  BootloaderEntry   = GetMenuEntry (&BootOptionMenu, OptionNumber);
  if (BootloaderEntry == NULL) {
    return EFI_NO_MEDIA;
  }

  SecurityContext = (SV_SECURITY_CONTEXT *)BootloaderEntry->SecurityContext;
  if (SecurityContext == NULL) {
    return EFI_NO_MEDIA;
  }

  Private->FormData.ImageUnsigned = (!SecurityContext->ImageIsSigned ||
                                     (SecurityContext->NumCertificates == 0));

  if (SecurityContext->ImageIsInDb || SecurityContext->ImageIsInDbx) {
    DEBUG ((DEBUG_INFO, "Bootloader %u already (un)trusted\n", OptionNumber));
    return EFI_NO_MEDIA;
  }

  // Image is unsigned? Show its hash instead of certificates
  if (Private->FormData.ImageUnsigned) {

    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_KEY_FINGERPRINT), L"Image hash (SHA-256):\n!!! Image is unsigned !!!", NULL);

    Status = ParseHashValue (SecurityContext->ImageDigest, SecurityContext->ImageDigestSize, &NewString);
    if (!EFI_ERROR (Status)) {
      HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_KEY_FINGERPRINT_HASH), NewString, NULL);
      FreePool (NewString);
    } else {
      HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_KEY_FINGERPRINT_HASH), L"Image hash could not be obtained.", NULL);
    }

    return EFI_SUCCESS;
  }

  // Image is signed, show its certificate(s)
  while (mCertIndex < SecurityContext->NumCertificates) {
    CertificateEntry = GetCertEntry(BootloaderEntry, mCertIndex);
    if (CertificateEntry == NULL) {
      DEBUG ((DEBUG_ERROR, "Certificate %u not found\n", mCertIndex));
      return EFI_NO_MEDIA;
    }

    // Do not show already trusted/utrusted or microsoft certificates
    if (CertificateEntry->CertIsInDb ||
        CertificateEntry->CertIsInDbx ||
        CertificateEntry->CertIsMicrosoft) {
      DEBUG ((DEBUG_INFO, "Certificate %u already (un)trusted or belongs to Microsoft\n", mCertIndex));
      mCertIndex++;
      continue;
    }

    break;
  }

  // Haven't found any cert to show
  if (mCertIndex >= SecurityContext->NumCertificates) {
    DEBUG ((DEBUG_INFO, "Could not find a cerificate to show for bootloader %u\n", OptionNumber));
    return EFI_NO_MEDIA;
  }

  HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_KEY_FINGERPRINT), L"Certificate fingerprint (SHA-256):", NULL);

  Status = ParseHashValue (CertificateEntry->CertDigest, CertificateEntry->CertDigestSize, &NewString);
  if (!EFI_ERROR (Status)) {
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_KEY_FINGERPRINT_HASH), NewString, NULL);
    FreePool (NewString);
  } else {
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_KEY_FINGERPRINT_HASH), L"Could not obtain certificate fingerprint", NULL);
  }

  return EFI_SUCCESS;
}

VOID
UpdateCertValidtyStrings (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN  SV_CERT_ENTRY                       *CertificateEntry
  )
{
  CHAR16                                  DateBuffer[30];
  UINT8                                   CertValidFrom[64];
  UINTN                                   CertValidFromLen;
  UINT8                                   CertValidTo[64];
  UINTN                                   CertValidToLen;
  MBED_TLS_DATETIME_OBECT                 *CertValidTime;

  CertValidFromLen = 64;
  CertValidToLen  = 64;

  if (X509GetValidity(CertificateEntry->CertData,
                      CertificateEntry->CertDataSize,
                      CertValidFrom,
                      &CertValidFromLen,
                      CertValidTo,
                      &CertValidToLen)) {

    CertValidTime = (MBED_TLS_DATETIME_OBECT *)CertValidTo;
    SetMem (DateBuffer, sizeof (DateBuffer), 0);
    UnicodeSPrint (
      DateBuffer,
      sizeof (DateBuffer),
      L"%04u-%02u-%02u %02u:%02u:%02u",
      (UINT16)(CertValidTime->Year & 0xFFFF),
      (UINT8)(CertValidTime->Month & 0xFF),
      (UINT8)(CertValidTime->Day & 0xFF),
      (UINT8)(CertValidTime->Hour & 0xFF),
      (UINT8)(CertValidTime->Minute & 0xFF),
      (UINT8)(CertValidTime->Second & 0xFF));
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_VALIDITY_AFTER_DATE), DateBuffer, NULL);

    CertValidTime = (MBED_TLS_DATETIME_OBECT *)CertValidFrom;
    SetMem (DateBuffer, sizeof (DateBuffer), 0);
    UnicodeSPrint (
      DateBuffer,
      sizeof (DateBuffer),
      L"%04u-%02u-%02u %02u:%02u:%02u",
      (UINT16)(CertValidTime->Year & 0xFFFF),
      (UINT8)(CertValidTime->Month & 0xFF),
      (UINT8)(CertValidTime->Day & 0xFF),
      (UINT8)(CertValidTime->Hour & 0xFF),
      (UINT8)(CertValidTime->Minute & 0xFF),
      (UINT8)(CertValidTime->Second & 0xFF));

    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_VALIDITY_BEFORE_DATE), DateBuffer, NULL);

  } else {
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_VALIDITY_AFTER_DATE),  L"Unknown", NULL);
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_VALIDITY_BEFORE_DATE), L"Unknown", NULL);
  }
}

VOID
UpdateCertIssuerAndSubjectStrings (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN  SV_CERT_ENTRY                       *CertificateEntry
  )
{
  CHAR8                                   StringBuffer[500];
  UINTN                                   StringBufferSize;
  CHAR16                                  *NewString;

  HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_CERT_ISSUER2), L"Unknown", NULL);
  HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_CERT_SUBJECT2), L"Unknown", NULL);

  StringBufferSize = sizeof (StringBuffer);
  SetMem (StringBuffer, StringBufferSize, 0);
  if (!RETURN_ERROR (X509GetIssuerCommonName (CertificateEntry->CertData,
                                              CertificateEntry->CertDataSize,
                                              StringBuffer,
                                              &StringBufferSize))) {

    NewString = (CHAR16 *)AllocateZeroPool ((StringBufferSize + 1) * sizeof(CHAR16));
    if (NewString != NULL) {
      if (!RETURN_ERROR (AsciiStrToUnicodeStrS (StringBuffer, NewString, StringBufferSize + 1))) {
        HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_CERT_ISSUER2), NewString, NULL);
      }
      FreePool (NewString);
    }
  } else {
    DEBUG ((DEBUG_INFO, "Could not get Issuer name\n"));
  }

  StringBufferSize = sizeof (StringBuffer);
  SetMem (StringBuffer, StringBufferSize, 0);
  if (!RETURN_ERROR (X509GetCommonName (CertificateEntry->CertData,
                                        CertificateEntry->CertDataSize,
                                        StringBuffer,
                                        &StringBufferSize))) {

    NewString = (CHAR16 *)AllocateZeroPool ((StringBufferSize + 1) * sizeof(CHAR16));
    if (NewString != NULL) {
      if (!RETURN_ERROR (AsciiStrToUnicodeStrS (StringBuffer, NewString, StringBufferSize + 1))) {
        HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_CERT_SUBJECT2), NewString, NULL);
      }
      FreePool (NewString);
    }
  } else {
    DEBUG ((DEBUG_INFO, "Could not get Subject name\n"));
  }
}

VOID
UpdateCertSerialNumberString (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN  SV_CERT_ENTRY                       *CertificateEntry
  )
{
  UINT8                                   StringBuffer[200];
  UINTN                                   StringBufferSize;
  CHAR16                                  *NewString;

  HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_CERT_SERIAL_NUMBER2), L"Unknown", NULL);

  StringBufferSize = sizeof (StringBuffer);
  SetMem (StringBuffer, StringBufferSize, 0);
  if (X509GetSerialNumber(CertificateEntry->CertData,
                          CertificateEntry->CertDataSize,
                          StringBuffer,
                          &StringBufferSize)) {

    // Serial number is NULL terminated. But this NULL termination is not a
    // part of the serial number itself, thus we should parse one byte less.
    if (!EFI_ERROR (ParseHashValue (StringBuffer, StringBufferSize - 1, &NewString))) {
      HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_CERT_SERIAL_NUMBER2), NewString, NULL);
      FreePool (NewString);
    }
  }
}

VOID
UpdateCertKeyStrings (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN  SV_CERT_ENTRY                       *CertificateEntry
  )
{
  UINT8                                   *ModulusBuffer;
  CHAR16                                  *NewString;
  UINT32                                  Exponent;
  UINT16                                  ExponentString[20];
  VOID                                    *X509PubKey;
  UINTN                                   PubKeyModSize;
  UINTN                                   PubKeyExpSize;

  HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_CERT_KEY_MODULUS_HEX), L"Unknown", NULL);
  HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_CERT_KEY_EXPONENT2), L"Unknown", NULL);

  if (!RsaGetPublicKeyFromX509 (CertificateEntry->CertData, CertificateEntry->CertDataSize, &X509PubKey)) {
    DEBUG ((DEBUG_ERROR, "Error occurred while parsing the pubkey from certificate.\n"));
    return;
  }

  if (X509PubKey == NULL) {
    DEBUG ((DEBUG_ERROR, "X509 key is NULL\n"));
    return;
  }

  Exponent = 0;
  PubKeyModSize = 0;
  PubKeyExpSize = 0;

  RsaGetKey (X509PubKey, RsaKeyN, NULL, &PubKeyModSize);
  RsaGetKey (X509PubKey, RsaKeyE, NULL, &PubKeyExpSize);

  if (PubKeyExpSize > 4) {
    DEBUG ((DEBUG_ERROR, "Key exponent size too big\n"));
    RsaFree (X509PubKey);
    return;
  }

  ModulusBuffer = (UINT8 *) AllocateZeroPool (PubKeyModSize);
  if (ModulusBuffer == NULL) {
    DEBUG ((DEBUG_ERROR, "Could not allocate memory for modulus\n"));
    RsaFree (X509PubKey);
    return;
  }

  if (!RsaGetKey (X509PubKey, RsaKeyN, ModulusBuffer, &PubKeyModSize)) {
    DEBUG ((DEBUG_ERROR, "Could not get key modulus\n"));
    goto ON_EXIT;
  }

  if (!RsaGetKey (X509PubKey, RsaKeyE, (UINT8 *)&Exponent, &PubKeyExpSize)) {
    DEBUG ((DEBUG_ERROR, "Could not get key exponent\n"));
    goto ON_EXIT;
  }

  if (!EFI_ERROR (ParseKeyModulus (ModulusBuffer, PubKeyModSize, &NewString))) {
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_CERT_KEY_MODULUS_HEX), NewString, NULL);
    FreePool (NewString);
  }

  SetMem(ExponentString, sizeof (ExponentString), 0);
  UnicodeSPrint(ExponentString, sizeof (ExponentString), L"0x%X", Exponent);
  HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_CERT_KEY_EXPONENT2), ExponentString, NULL);


ON_EXIT:
  RsaFree (X509PubKey);
  FreePool (ModulusBuffer);
}

EFI_STATUS
UpdateCertDetails (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private
  )
{
  SV_MENU_ENTRY                           *BootloaderEntry;
  SV_CERT_ENTRY                           *CertificateEntry;

  BootloaderEntry   = GetMenuEntry (&BootOptionMenu, mBootloaderIndex);
  if (BootloaderEntry == NULL) {
    return EFI_NO_MEDIA;
  }

  CertificateEntry = GetCertEntry(BootloaderEntry, mCertIndex);
  if (CertificateEntry == NULL) {
    return EFI_NO_MEDIA;
  }

  UpdateCertValidtyStrings (Private, CertificateEntry);
  UpdateCertIssuerAndSubjectStrings (Private, CertificateEntry);
  UpdateCertSerialNumberString (Private, CertificateEntry);
  UpdateCertKeyStrings (Private, CertificateEntry);

  return EFI_SUCCESS;
}
