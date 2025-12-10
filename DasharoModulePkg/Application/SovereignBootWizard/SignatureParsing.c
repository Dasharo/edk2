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

STATIC BOOLEAN
CertIsValid (
  IN SV_CERT_ENTRY                     *CertificateEntry
  )
{
  EFI_STATUS                           Status;
  BOOLEAN                              Valid;
  EFI_TIME                             Time;
  UINT8                                CertValidFrom[64];
  UINTN                                CertValidFromLen;
  UINT8                                CertValidTo[64];
  UINTN                                CertValidToLen;
  OPENSSL_ASN1_TIME                    *CurrentTime;

  if (CertificateEntry == NULL) {
    return FALSE;
  }

  Valid = TRUE;
  CurrentTime = NULL;

  Status = GetCurrentTime (&Time);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Fail to fetch valid time data: %r\n", Status));
    return FALSE;
  }

  // We should skip presenting expired or not valid certificates. Adding an
  // expired certificate to DB will cause verification failure.
  CurrentTime = EfiTimeToAsn1Time (&Time);
  if (CurrentTime == NULL) {
    DEBUG ((DEBUG_ERROR, "Fail to convert time data\n"));
    Valid = FALSE;
    goto ON_EXIT;
  }

  // If enrolling to DB, check the expiry date
  CertValidFromLen = 64;
  CertValidToLen  = 64;
  if (!X509GetValidity(CertificateEntry->CertData,
                       CertificateEntry->CertDataSize,
                       CertValidFrom,
                       &CertValidFromLen,
                       CertValidTo,
                       &CertValidToLen)) {
    DEBUG ((DEBUG_ERROR, "Could not get certificate validity\n"));
    Valid = FALSE;
    goto ON_EXIT;
  }

  if (X509CompareDateTime (CurrentTime, CertValidTo) >= 0) {
    DEBUG ((DEBUG_INFO, "Certificate already expired\n"));
    Valid = FALSE;
  }

  if (X509CompareDateTime (CurrentTime, CertValidFrom) < 0) {
    DEBUG ((DEBUG_INFO, "Certificate not yet valid\n"));
    Valid = FALSE;
  }

ON_EXIT:

  FREE_NON_NULL (CurrentTime);

  return Valid;
}

STATIC BOOLEAN
CertIsCA (
  IN SV_CERT_ENTRY                     *CertificateEntry
  )
{
  UINT8                         *Constraints;
  UINTN                         ConstraintsSize;

  ConstraintsSize = 0;
  Constraints = NULL;

  X509GetExtendedBasicConstraints (CertificateEntry->CertData,
                                   CertificateEntry->CertDataSize,
                                   NULL,
                                   &ConstraintsSize);

  if (ConstraintsSize == 0) {
    return FALSE;
  }

  Constraints = AllocateZeroPool (ConstraintsSize);
  if (Constraints == NULL) {
    return FALSE;
  }

  if (X509GetExtendedBasicConstraints (CertificateEntry->CertData,
                                       CertificateEntry->CertDataSize,
                                       Constraints,
                                       &ConstraintsSize)) {
    if (ConstraintsSize < 2) {
      FreePool (Constraints);
      return FALSE;
    }
    // If SEQUENCE byte found and length is 0, the not CA
    if ((Constraints[0] == 0x30) && (Constraints[1] == 0x00)) {
      FreePool (Constraints);
      return FALSE;
    }
    if (ConstraintsSize < 5) {
      FreePool (Constraints);
      return FALSE;
    }
    // If SEQUENCE byte found and length is at least 3
    if ((Constraints[0] == 0x30) && (Constraints[1] >= 0x03)) {
      // If Type is Boolean and its length is 1 then return its value
      if (Constraints[2] == 0x01 && Constraints[3] == 0x01) {
        FreePool (Constraints);
        return (Constraints[4] != 0);
      }
    }
  }

  FreePool (Constraints);

  return FALSE;
}

STATIC BOOLEAN
CertHasMicrosoftInCommonNames (
  IN  SV_CERT_ENTRY                       *CertificateEntry
  )
{
  CHAR8                                   StringBuffer[500];
  UINTN                                   StringBufferSize;

  StringBufferSize = sizeof (StringBuffer);
  SetMem (StringBuffer, StringBufferSize, 0);
  if (!RETURN_ERROR (X509GetIssuerCommonName (CertificateEntry->CertData,
                                              CertificateEntry->CertDataSize,
                                              StringBuffer,
                                              &StringBufferSize))) {
    if (AsciiStrStr (StringBuffer, "Microsoft") != NULL) {
      return TRUE;
    }
  }

  StringBufferSize = sizeof (StringBuffer);
  SetMem (StringBuffer, StringBufferSize, 0);
  if (!RETURN_ERROR (X509GetCommonName (CertificateEntry->CertData,
                                        CertificateEntry->CertDataSize,
                                        StringBuffer,
                                        &StringBufferSize))) {
    if (AsciiStrStr (StringBuffer, "Microsoft") != NULL) {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC BOOLEAN
CertHashMatchesMicrosoft (
  IN  SV_CERT_ENTRY                       *CertificateEntry
  )
{
  UINTN  Cert;

  for (Cert = 0; Cert < MicrosoftCertificatesArraySize; Cert++) {
    if (!CompareMem (CertificateEntry->CertDigest,
                     MicrosoftCertificates[Cert].CertHash,
                     CertificateEntry->CertDigestSize)) {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC EFI_STATUS
CreateNewCert (
  IN  SV_SECURITY_CONTEXT       *SecCtx,
  IN  UINT8                     *CertData,
  IN  UINTN                     CertDataSize,
  IN  VOID                      *FileBuffer,
  IN  UINTN                     FileSize,
  IN  UINT8                     *AuthData,
  IN  UINTN                     AuthDataSize,
  IN OUT SV_CERT_ENTRY          **CertEntry
  )
{
  SV_CERT_ENTRY                 *NewCertEntry;
  EFI_STATUS                    Status;
  EFI_GUID                      CertType;
  UINT8                         ImageDigest[MAX_DIGEST_SIZE];
  UINTN                         ImageDigestSize;
  BOOLEAN                       IsFound;
  UINTN                         Index;

  if ((SecCtx == NULL) || (CertData == NULL) || (FileBuffer == NULL) ||
      (AuthData == NULL) || (CertEntry == NULL) || (CertDataSize == 0) ||
      (FileSize == 0) || (AuthDataSize == 0)) {
    DEBUG ((DEBUG_ERROR, "CreateNewCert, Invalid parameter\n"));
    return EFI_INVALID_PARAMETER;
  }

  NewCertEntry = (SV_CERT_ENTRY *) AllocateZeroPool (sizeof(SV_CERT_ENTRY));

  if (NewCertEntry == NULL) {
    DEBUG ((DEBUG_ERROR, "Not enough free memory for certificate entry\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  *CertEntry = NewCertEntry;

  NewCertEntry->Signature = SOVEREIGN_BOOT_CERT_ENTRY_SIGNATURE;
  NewCertEntry->CertData = AllocateCopyPool (CertDataSize, CertData);

  if (NewCertEntry->CertData == NULL) {
    DEBUG ((DEBUG_ERROR, "Not enough free memory for certificate data\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  NewCertEntry->CertDataSize = CertDataSize;
  NewCertEntry->CertIsValid = CertIsValid(NewCertEntry);

  if (CalculateCertHash (NewCertEntry->CertData, NewCertEntry->CertDataSize, HASHALG_SHA256, NewCertEntry->CertDigest)) {
    NewCertEntry->CertDigestSize = SHA256_DIGEST_SIZE;
    CopyGuid (&NewCertEntry->CertType, &gEfiCertX509Sha256Guid);
  } else {
    DEBUG ((DEBUG_ERROR, "Could not calculate TBS certificate hash\n"));
    return EFI_DEVICE_ERROR;
  }

  Status = HashPeImageByType (
             FileBuffer,
             FileSize,
             AuthData,
             AuthDataSize,
             ImageDigest,
             &ImageDigestSize,
             &CertType);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to calculate image hash\n"));
    return Status;
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
  if (!EFI_ERROR (Status) && IsFound) {
    NewCertEntry->CertIsInDb = TRUE;
  }

  // Check if certificate is in DBX
  IsFound = FALSE;
  Status = IsSignatureFoundInDatabase (
                EFI_IMAGE_SECURITY_DATABASE1,
                NewCertEntry->CertData,
                &gEfiCertX509Guid,
                NewCertEntry->CertDataSize,
                &IsFound
                );
  if (!EFI_ERROR (Status) && IsFound) {
    NewCertEntry->CertIsInDbx = TRUE;
  }

  // Check if certificate hash is in DBX
  IsFound = FALSE;
  Status = IsCertHashFoundInDbx (
                NewCertEntry->CertData,
                NewCertEntry->CertDataSize,
                NULL,
                0,
                NULL,
                &IsFound
                );
  if (!EFI_ERROR (Status) && IsFound) {
    NewCertEntry->CertIsInDbx = TRUE;
  }

  NewCertEntry->SignatureValid = AuthenticodeVerify (
                                   AuthData, AuthDataSize,
                                   NewCertEntry->CertData, NewCertEntry->CertDataSize,
                                   ImageDigest, ImageDigestSize);

  NewCertEntry->CertIsCA = CertIsCA (NewCertEntry);


  // If signature is invalid, Authenticode method won't work
  if (NewCertEntry->SignatureValid) {
    for (Index = 0; Index < MicrosoftCertificatesArraySize; Index++) {
      if (AuthenticodeVerify (AuthData, AuthDataSize,
                              MicrosoftCertificates[Index].CertData,
                              MicrosoftCertificates[Index].CertLength,
                              ImageDigest, ImageDigestSize)) {
        NewCertEntry->CertIsMicrosoft = TRUE;
        break;
      }
    }
  } else {
    // If signature is invalid, hashes are our best bet to detect the
    // Microsoft certificates.
    NewCertEntry->CertIsMicrosoft = CertHashMatchesMicrosoft (NewCertEntry);
    // If hashes do not match, then maybe it is not the CA, so we have to
    // check common names to be sure.
    if (!NewCertEntry->CertIsMicrosoft) {
      NewCertEntry->CertIsMicrosoft = CertHasMicrosoftInCommonNames (NewCertEntry);
    }
  }

  // Mark the image as unverified if it is in DBX.
  if (IsForbiddenByDbx (AuthData, AuthDataSize, ImageDigest, ImageDigestSize)) {
    SecCtx->ImageIsVerified = FALSE;
  }

  DEBUG ((DEBUG_INFO, "Certificate Details:\n"
    "  SignatureValid: %u\n"
    "  CertIsInDb: %u\n"
    "  CertIsInDbx: %u\n"
    "  CertIsMicrosoft: %u\n"
    "  CertIsValid: %u\n"
    "  CertIsCA: %u\n",
    NewCertEntry->SignatureValid,
    NewCertEntry->CertIsInDb,
    NewCertEntry->CertIsInDbx,
    NewCertEntry->CertIsMicrosoft,
    NewCertEntry->CertIsValid,
    NewCertEntry->CertIsCA));

  DEBUG ((DEBUG_INFO, "Certificate hash:\n"));
  for (Index = 0; Index < NewCertEntry->CertDigestSize; Index++) {
    DEBUG ((DEBUG_INFO, "%02X", NewCertEntry->CertDigest[Index]));
  }
  DEBUG ((DEBUG_INFO, "\n"));

  return EFI_SUCCESS;
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
  EFI_STATUS                    CertStatus;
  SV_CERT_ENTRY                 *NewCertEntry;
  UINT8                         *CertBuffer;
  UINTN                         BufferLength;
  UINT8                         *UnchainedCert;
  UINTN                         UnchainedCertLength;
  UINTN                         CertCount;
  UINT8                         *CertPtr;
  UINT8                         NumCert;

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

    // Obtain the signer's certificates
    if (!Pkcs7GetCertificatesList (AuthData, AuthDataSize,
                                   &CertBuffer, &BufferLength,
                                   &UnchainedCert, &UnchainedCertLength)) {
      DEBUG ((DEBUG_ERROR, "Could not get PKCS7 signers\n"));
      continue;
    }
    if ((BufferLength != 0) && (CertBuffer != NULL)) {
      NumCert = CertBuffer[0];
    } else {
      NumCert = 0;
    }

    if (NumCert == 0) {
      DEBUG ((DEBUG_INFO, "Chained certificates not found\n"));
    } else {
      DEBUG ((DEBUG_INFO, "Found %u chained certificates\n", NumCert));
    }

    // Skip the number of certs
    CertPtr = CertBuffer + 1;
    while (NumCert > 0) {
      NewCertEntry = NULL;
      CertStatus = CreateNewCert (
                     SecCtx,
                     &CertPtr[4],
                     *(UINT32 *)CertPtr,
                     FileBuffer,
                     FileSize,
                     AuthData,
                     AuthDataSize,
                     &NewCertEntry
                   );
      if (!EFI_ERROR (CertStatus)) {
        InsertTailList (&SecCtx->Certs, &NewCertEntry->CertLink);
        CertCount++;
      } else {
        FREE_NON_NULL (NewCertEntry);
      }

      CertPtr += *(UINT32 *)CertPtr; // Certificate size
      CertPtr += sizeof (UINT32); // Certificate size field
      NumCert--;
    }
    if (CertBuffer != NULL) {
      Pkcs7FreeSigners (CertBuffer);
    }

    if ((UnchainedCertLength != 0) && (UnchainedCert != NULL)) {
      NumCert = UnchainedCert[0];
    } else {
      NumCert = 0;
    }

    if (NumCert == 0) {
      DEBUG ((DEBUG_INFO, "Unchained certificates not found\n"));
    } else {
      DEBUG ((DEBUG_INFO, "Found %u unchained certificates\n", NumCert));
    }

    // Skip the number of certs
    CertPtr = UnchainedCert + 1;
    while (NumCert > 0) {
      NewCertEntry = NULL;
      CertStatus = CreateNewCert (
                     SecCtx,
                     &CertPtr[4],
                     *(UINT32 *)CertPtr,
                     FileBuffer,
                     FileSize,
                     AuthData,
                     AuthDataSize,
                     &NewCertEntry
                   );
      if (!EFI_ERROR (CertStatus)) {
        InsertTailList (&SecCtx->Certs, &NewCertEntry->CertLink);
        CertCount++;
      } else {
        DEBUG ((DEBUG_ERROR, "Failed to add new certificate: %r\n", CertStatus));
        FREE_NON_NULL (NewCertEntry);
      }

      CertPtr += *(UINT32 *)CertPtr; // Certificate size
      CertPtr += sizeof (UINT32); // Certificate size field
      NumCert--;
    }
    if (UnchainedCert != NULL) {
      Pkcs7FreeSigners (UnchainedCert);
    }
  }

  if (Offset != SecDataDirEnd) {
    DEBUG ((DEBUG_ERROR, "The Size in Certificate Table or the attribute certificate table is corrupted.\n"));
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
  EFI_GUID                      HashType;

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

  if (LoadCtx->NeedsPathExpansion) {
    FREE_NON_NULL (FullFilePath);
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

  // Additional hashes for checks in interactive mode.
  HashPeImage (ImageBase,
               ImageSize,
               HASHALG_SHA384,
               SecCtx->ImageSha384Digest,
               &SecCtx->ImageSha384DigestSize,
               &HashType);

  HashPeImage (ImageBase,
               ImageSize,
               HASHALG_SHA512,
               SecCtx->ImageSha512Digest,
               &SecCtx->ImageSha512DigestSize,
               &HashType);

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
  // Assume verified. If anything goes wrong with certificate parsing, it will
  // be set to FALSE in FillCertificateEntries.
  SecCtx->ImageIsVerified = TRUE;

  // Parse certificates
  InitializeListHead (&SecCtx->Certs);
  FillCertificateEntries (ImageBase, ImageSize, SecCtx, SecDir);
  Status = EFI_SUCCESS;

ON_EXIT:
  DEBUG ((DEBUG_INFO, "Image Details:\n"
    "  ImageIsInDbx: %u\n"
    "  ImageIsInDb: %u\n"
    "  ImageIsSigned: %u\n"
    "  ImageIsVerified: %u\n"
    "  AuthenticationStatus: %u\n"
    "  NumCertificates: %u\n",
    SecCtx->ImageIsInDbx,
    SecCtx->ImageIsInDb,
    SecCtx->ImageIsSigned,
    SecCtx->ImageIsVerified,
    SecCtx->AuthenticationStatus,
    SecCtx->NumCertificates));

  FREE_NON_NULL (ImageBase);

  return Status;
}

VOID
RefreshImageSecurityInfo (
  IN SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *PrivateData
  )
{
  SV_MENU_ENTRY       *BootloaderEntry;
  SV_SECURITY_CONTEXT *SecurityContext;
  SV_CERT_ENTRY       *CertificateEntry;
  UINTN               Index;
  BOOLEAN             MsCertFound;
  BOOLEAN             NonMsCertFound;
  BOOLEAN             FoundInDbx;
  BOOLEAN             FoundInDb;
  BOOLEAN             InvalidSigFound;

  BootloaderEntry = GetMenuEntry (&mBootOptionMenu, mBootloaderIndex);
  if (BootloaderEntry == NULL) {
    return;
  }

  FreeSecurityContext (BootloaderEntry->SecurityContext);
  FREE_NON_NULL (BootloaderEntry->SecurityContext);

  FillSecurityContext (BootloaderEntry);

  MsCertFound = FALSE;
  NonMsCertFound = FALSE;
  FoundInDbx = FALSE;
  FoundInDb = FALSE;
  InvalidSigFound = FALSE;

  if (BootloaderEntry->SecurityContext == NULL) {
    return;
  }

  SecurityContext = (SV_SECURITY_CONTEXT *)BootloaderEntry->SecurityContext;
  for (Index = 0; Index < SecurityContext->NumCertificates; Index = Index + 1) {
    CertificateEntry = GetCertEntry(BootloaderEntry, Index);
    if (CertificateEntry == NULL) {
      continue;
    }

    if (CertificateEntry->CertIsMicrosoft) {
      MsCertFound = TRUE;
    } else {
      NonMsCertFound = TRUE;
    }

    if (CertificateEntry->CertIsInDbx) {
      FoundInDbx = TRUE;
    }

    if (CertificateEntry->CertIsInDb) {
      FoundInDb = TRUE;
    }

    if (!CertificateEntry->SignatureValid || !CertificateEntry->CertIsValid) {
      InvalidSigFound = TRUE;
    }

    if (Index == mCertIndex) {
      PrivateData->FormData.CertInDb = CertificateEntry->CertIsInDb;
      PrivateData->FormData.CertInDbx = CertificateEntry->CertIsInDbx;
      PrivateData->FormData.CertIsValid = CertificateEntry->CertIsValid;
      PrivateData->FormData.CertIsMicrosoft = CertificateEntry->CertIsMicrosoft;
    }
  }

  PrivateData->FormData.SignedByMs = MsCertFound;
  PrivateData->FormData.SignedByMsOnly = (!NonMsCertFound && MsCertFound);
  PrivateData->FormData.HasInvalidSignature = InvalidSigFound;
  PrivateData->FormData.ImageHashIsInDb = SecurityContext->ImageIsInDb;
  PrivateData->FormData.ImageHashIsInDbx = SecurityContext->ImageIsInDbx;

  if (SecurityContext->ImageIsSigned) {
    if (!SecurityContext->ImageIsVerified || FoundInDbx || SecurityContext->ImageIsInDbx) {
      PrivateData->FormData.ImageTrusted = IMAGE_STATE_UNTRUSTED;
    } else if (FoundInDb || SecurityContext->ImageIsInDb) {
      PrivateData->FormData.ImageTrusted = IMAGE_STATE_TRUSTED;
    } else {
      PrivateData->FormData.ImageTrusted = IMAGE_STATE_UNDECIDED;
    }
  } else {
    if (SecurityContext->ImageIsInDbx) {
      PrivateData->FormData.ImageTrusted = IMAGE_STATE_UNTRUSTED;
    } else if (SecurityContext->ImageIsInDb) {
      PrivateData->FormData.ImageTrusted = IMAGE_STATE_TRUSTED;
    } else {
      PrivateData->FormData.ImageTrusted = IMAGE_STATE_UNDECIDED;
    }
  }

  DEBUG ((DEBUG_INFO, "RefreshImageSecurityInfo:\n"
    "  MsCertFound: %u\n"
    "  NonMsCertFound: %u\n"
    "  InvalidSigFound: %u\n"
    "  ImageIsSigned: %u\n"
    "  ImageIsVerified: %u\n"
    "  FoundInDbx: %u\n"
    "  FoundInDb: %u\n"
    "  ImageIsInDb: %u\n"
    "  ImageIsInDbx: %u\n"
    "  ImageTrusted: %u\n",
    MsCertFound,
    NonMsCertFound,
    InvalidSigFound,
    SecurityContext->ImageIsSigned,
    SecurityContext->ImageIsVerified,
    FoundInDbx,
    FoundInDb,
    SecurityContext->ImageIsInDb,
    SecurityContext->ImageIsInDbx,
    PrivateData->FormData.ImageTrusted));
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
  Format raw hex buffer, and save in the CHAR16 type array.
  The buffer is callee allocated and should be freed by the caller.

  @param[in]    Digest                    The pointer to the hash value.
  @param[in]    DigestSize                The size of the hash.
  @param[out]   BufferToReturn            Buffer to save the hash value.

  @retval       EFI_INVALID_PARAMETER     Invalid Hash or Buffer.
  @retval       EFI_OUT_OF_RESOURCES      A memory allocation failed.
  @retval       EFI_SUCCESS               Operation success.
**/
EFI_STATUS
FormatHexBuffer (
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

  BootloaderEntry   = GetMenuEntry (&mBootOptionMenu, OptionNumber);
  if (BootloaderEntry == NULL) {
    return EFI_NO_MEDIA;
  }

  SecurityContext = (SV_SECURITY_CONTEXT *)BootloaderEntry->SecurityContext;
  if (SecurityContext == NULL) {
    return EFI_NO_MEDIA;
  }

  Private->FormData.ImageUnsigned = (!SecurityContext->ImageIsSigned ||
                                     (SecurityContext->NumCertificates == 0));

  if (Private->ConfigData.AppLaunchCause != SV_BOOT_LAUNCH_IMAGE_VERIFICATION_FAILED) {
    if (!mAltAccessMode) {
      if (SecurityContext->ImageIsInDb || SecurityContext->ImageIsInDbx) {
        DEBUG ((DEBUG_INFO, "Bootloader %u already (un)trusted\n", OptionNumber));
        return EFI_NO_MEDIA;
      }
    }
  }

  // Image is unsigned? Show its hash instead of certificates
  if (Private->FormData.ImageUnsigned) {
    Status = ParseHashValue (SecurityContext->ImageDigest, SecurityContext->ImageDigestSize, &NewString);
    if (!EFI_ERROR (Status)) {
      HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_KEY_FINGERPRINT_HASH), NewString, NULL);
      FREE_NON_NULL (NewString);
    } else {
      HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_KEY_FINGERPRINT_HASH), L"Image hash could not be obtained.", NULL);
    }

    return EFI_SUCCESS;
  }

  if (Private->ConfigData.AppLaunchCause != SV_BOOT_LAUNCH_IMAGE_VERIFICATION_FAILED) {
    // Do not show images that are not verified (one of the certs in the
    // signatures is unstrusted/in DBX), because we won't be able to boot it
    // anyways.
    if (!SecurityContext->ImageIsVerified && !mAltAccessMode) {
      DEBUG ((DEBUG_INFO, "Image %u already untrusted\n", mBootloaderIndex));
      mCertIndex = 0;
      return EFI_NO_MEDIA;
    }
  }

  // Image is signed, show its certificate(s)
  while (mCertIndex < SecurityContext->NumCertificates) {
    CertificateEntry = GetCertEntry(BootloaderEntry, mCertIndex);
    if (CertificateEntry == NULL) {
      DEBUG ((DEBUG_ERROR, "Certificate %u not found\n", mCertIndex));
      return EFI_NO_MEDIA;
    }

    // Do not show already trusted/utrusted, invalid or microsoft certificates
    if (Private->ConfigData.AppLaunchCause != SV_BOOT_LAUNCH_IMAGE_VERIFICATION_FAILED) {
      if (!mAltAccessMode) {
        if (CertificateEntry->CertIsMicrosoft) {
          DEBUG ((DEBUG_INFO, "Certificate %u belongs to Microsoft\n",
                  mCertIndex));
          mCertIndex++;
          continue;
        }
        if (CertificateEntry->CertIsInDb) {
          DEBUG ((DEBUG_INFO, "Certificate %u already trusted\n",
                  mCertIndex));
          mCertIndex++;
          continue;
        }
      }
    }

    break;
  }

  // Haven't found any cert to show
  if (mCertIndex >= SecurityContext->NumCertificates) {
    DEBUG ((DEBUG_INFO, "Could not find a cerificate to show for bootloader %u\n", OptionNumber));
    return EFI_NO_MEDIA;
  }

  if (!CertificateEntry->SignatureValid || !CertificateEntry->CertIsValid) {
    Private->FormData.HasInvalidSignature = TRUE;
  } else {
    Private->FormData.HasInvalidSignature = FALSE;
  }

  // Special case if AppLaunchCause is SV_BOOT_LAUNCH_IMAGE_VERIFICATION_FAILED
  // We still want to show what the system attempted to boot and failed, even
  // if it is signed by MS certificate.
  Private->FormData.SignedByMs = CertificateEntry->CertIsMicrosoft;

  Status = ParseHashValue (CertificateEntry->CertDigest, CertificateEntry->CertDigestSize, &NewString);
  if (!EFI_ERROR (Status)) {
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_KEY_FINGERPRINT_HASH), NewString, NULL);
    FREE_NON_NULL (NewString);
  } else {
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_KEY_FINGERPRINT_HASH), L"Could not obtain certificate fingerprint", NULL);
  }

  return EFI_SUCCESS;
}

VOID
UpdateCertValidityStrings (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN  SV_CERT_ENTRY                       *CertificateEntry
  )
{
  CHAR16                                  DateBuffer[50];
  UINT8                                   CertValidFrom[64];
  UINTN                                   CertValidFromLen;
  UINT8                                   CertValidTo[64];
  UINTN                                   CertValidToLen;
  OPENSSL_ASN1_TIME                       *CertTime;

  CertValidFromLen = 64;
  CertValidToLen  = 64;

  if (X509GetValidity(CertificateEntry->CertData,
                      CertificateEntry->CertDataSize,
                      CertValidFrom,
                      &CertValidFromLen,
                      CertValidTo,
                      &CertValidToLen)) {

    CertTime = (OPENSSL_ASN1_TIME *)CertValidTo;
    SetMem (DateBuffer, sizeof (DateBuffer), 0);
    FormatAsn1Time (CertTime, DateBuffer, sizeof (DateBuffer));
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_VALIDITY_AFTER_DATE), DateBuffer, NULL);

    CertTime = (OPENSSL_ASN1_TIME *)CertValidFrom;
    SetMem (DateBuffer, sizeof (DateBuffer), 0);
    FormatAsn1Time (CertTime, DateBuffer, sizeof (DateBuffer));
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
  CHAR8                                   StringBuffer[BUFFER_MAX_SIZE];
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
      FREE_NON_NULL (NewString);
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
    goto ON_EXIT;
  }

  ModulusBuffer = (UINT8 *) AllocateZeroPool (PubKeyModSize);
  if (ModulusBuffer == NULL) {
    DEBUG ((DEBUG_ERROR, "Could not allocate memory for modulus\n"));
    goto ON_EXIT;
  }

  if (!RsaGetKey (X509PubKey, RsaKeyN, ModulusBuffer, &PubKeyModSize)) {
    DEBUG ((DEBUG_ERROR, "Could not get key modulus\n"));
    goto ON_EXIT;
  }

  if (!RsaGetKey (X509PubKey, RsaKeyE, (UINT8 *)&Exponent, &PubKeyExpSize)) {
    DEBUG ((DEBUG_ERROR, "Could not get key exponent\n"));
    goto ON_EXIT;
  }

  NewString = NULL;
  if (!EFI_ERROR (FormatHexBuffer (ModulusBuffer, PubKeyModSize, &NewString))) {
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_CERT_KEY_MODULUS_HEX), NewString, NULL);
  }
  FREE_NON_NULL (NewString);

  SetMem(ExponentString, sizeof (ExponentString), 0);
  UnicodeSPrint(ExponentString, sizeof (ExponentString), L"0x%X", Exponent);
  HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_CERT_KEY_EXPONENT2), ExponentString, NULL);


ON_EXIT:
  RsaFree (X509PubKey);
  FREE_NON_NULL (ModulusBuffer);
}

VOID
UpdateKeyStringsFromSigList (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN  EFI_SIGNATURE_DATA                  *Data
  )
{
  UINT8                                   *ModulusBuffer;
  CHAR16                                  *NewString;
  UINT16                                  ExponentString[20];

  HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_CERT_KEY_MODULUS_HEX), L"Unknown", NULL);
  HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_CERT_KEY_EXPONENT2), L"Unknown", NULL);

  ModulusBuffer = (UINT8 *)Data->SignatureData;

  NewString = NULL;
  if (!EFI_ERROR (FormatHexBuffer (ModulusBuffer, WIN_CERT_UEFI_RSA2048_SIZE, &NewString))) {
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_CERT_KEY_MODULUS_HEX), NewString, NULL);
  }
  FREE_NON_NULL (NewString);

  SetMem(ExponentString, sizeof (ExponentString), 0);
  HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_CERT_KEY_EXPONENT2), L"0x10001", NULL);
}

VOID
UpdateTimeString (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN  EFI_TIME                            *Time
  )
{
  CHAR16         TimeString[BUFFER_MAX_SIZE];

  ZeroMem (TimeString, sizeof (TimeString));
  UnicodeSPrint (
    TimeString,
    sizeof (TimeString),
    L"%02d-%02d-%04d %02d:%02d:%02d",
    Time->Day,
    Time->Month,
    Time->Year,
    Time->Hour,
    Time->Minute,
    Time->Second
    );

  HiiSetString(Private->HiiHandle, STRING_TOKEN(STR_SIGNATURE_DATA_REVOCATION_TIME2), TimeString, NULL);
}

VOID
UpdateHashStringsFromSigData (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN  EFI_SIGNATURE_DATA                  *Data,
  IN  UINT32                              DataSize
  )
{
  CHAR16                                  *HexString;

  HexString = NULL;
  if (!EFI_ERROR (FormatHexBuffer (Data->SignatureData, DataSize, &HexString))) {
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_SIGNATURE_DATA_RAW_HEX), HexString, NULL);
  }
  FREE_NON_NULL (HexString);
}

VOID
FillKeyHashStrings (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN  EFI_SIGNATURE_LIST                  *List,
  IN  EFI_SIGNATURE_DATA                  *Data
  )
{
  UINT32    DataSize;
  EFI_TIME  *Time;

  Private->FormData.IsCertHash = FALSE;

  switch (Private->FormData.SignatureType) {
  case SIGNATURE_TYPE_RSA2048:
    UpdateKeyStringsFromSigList (Private, Data);
    return;
  case SIGNATURE_TYPE_RSA2048_SHA256:
    DataSize = List->SignatureSize - sizeof (EFI_GUID);
    break;
  case SIGNATURE_TYPE_SHA1:
    DataSize = 20;
    break;
  case SIGNATURE_TYPE_SHA224:
    DataSize = 28;
    break;
  case SIGNATURE_TYPE_X509_SHA256:
  case SIGNATURE_TYPE_X509_SM3:
    Private->FormData.IsCertHash = TRUE;
  case SIGNATURE_TYPE_SHA256:
  case SIGNATURE_TYPE_SM3:
    DataSize = 32;
    break;
  case SIGNATURE_TYPE_X509_SHA384:
    Private->FormData.IsCertHash = TRUE;
  case SIGNATURE_TYPE_SHA384:
    DataSize = 48;
    break;
  case SIGNATURE_TYPE_X509_SHA512:
    Private->FormData.IsCertHash = TRUE;
  case SIGNATURE_TYPE_SHA512:
    DataSize = 64;
    break;
  case SIGNATURE_TYPE_UNKNOWN:
  default:
    UpdateHashStringsFromSigData (Private, Data, List->SignatureSize);
    return;
  }

  if (Private->FormData.IsCertHash) {
    Time = (EFI_TIME *)(Data->SignatureData + DataSize);
    UpdateTimeString (Private, Time);
  }

  UpdateHashStringsFromSigData (Private, Data, DataSize);
}

VOID
FillCertStrings (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN  SV_CERT_ENTRY                       *CertificateEntry
  )
{
  UpdateCertValidityStrings (Private, CertificateEntry);
  UpdateCertIssuerAndSubjectStrings (Private, CertificateEntry);
  UpdateCertSerialNumberString (Private, CertificateEntry);
  UpdateCertKeyStrings (Private, CertificateEntry);
}

EFI_STATUS
UpdateCertDetails (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private
  )
{
  SV_MENU_ENTRY                           *BootloaderEntry;
  SV_CERT_ENTRY                           *CertificateEntry;

  BootloaderEntry   = GetMenuEntry (&mBootOptionMenu, mBootloaderIndex);
  if (BootloaderEntry == NULL) {
    return EFI_NO_MEDIA;
  }

  CertificateEntry = GetCertEntry(BootloaderEntry, mCertIndex);
  if (CertificateEntry == NULL) {
    return EFI_NO_MEDIA;
  }

  FillCertStrings (Private, CertificateEntry);

  Private->FormData.CertInDb = CertificateEntry->CertIsInDb;
  Private->FormData.CertInDbx = CertificateEntry->CertIsInDbx;
  Private->FormData.CertIsValid = CertificateEntry->CertIsValid;
  Private->FormData.CertIsMicrosoft = CertificateEntry->CertIsMicrosoft;

  return EFI_SUCCESS;
}

STATIC VOID
FreeCertificateEntry (
  SV_CERT_ENTRY        *CertEntry
  )
{
  if (CertEntry == NULL) {
    return;
  }

  FREE_NON_NULL (CertEntry->CertData);
}

VOID
FreeSecurityContext (
  SV_SECURITY_CONTEXT  *SecCtx
  )
{
  SV_CERT_ENTRY        *CertEntry;

  if (SecCtx == NULL) {
    return;
  }

  if (SecCtx->NumCertificates == 0) {
    return;
  }

  while (!IsListEmpty (&SecCtx->Certs)) {
    CertEntry = CR (
                  SecCtx->Certs.ForwardLink,
                  SV_CERT_ENTRY,
                  CertLink,
                  SOVEREIGN_BOOT_CERT_ENTRY_SIGNATURE);
    RemoveEntryList (&CertEntry->CertLink);
    FreeCertificateEntry (CertEntry);
  }

  SecCtx->NumCertificates = 0;
}
