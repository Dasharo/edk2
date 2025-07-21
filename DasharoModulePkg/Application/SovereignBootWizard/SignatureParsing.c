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
  IN  BM_SECURITY_CONTEXT       *SecCtx
  )
{
  EFI_STATUS                    DbStatus;
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

    DbStatus = IsSignatureFoundInDatabase (
                  EFI_IMAGE_SECURITY_DATABASE1,
                  ImageDigest,
                  &CertType,
                  ImageDigestSize,
                  &IsFound
                  );
    if (!EFI_ERROR (DbStatus) || IsFound) {
      SecCtx->ImageIsInDb = TRUE;
    }

    DbStatus = IsSignatureFoundInDatabase (
                  EFI_IMAGE_SECURITY_DATABASE,
                  ImageDigest,
                  &CertType,
                  ImageDigestSize,
                  &IsFound
                  );
    if (!EFI_ERROR (DbStatus) && IsFound) {
      SecCtx->ImageIsInDbx = TRUE;
    }
  }
}

VOID
FillCertificateEntries (
  IN      VOID                      *FileBuffer,
  IN      UINTN                     FileSize,
  IN OUT  BM_SECURITY_CONTEXT       *SecCtx,
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
  BM_CERT_ENTRY                 *NewCertEntry;
  UINT8                         *CertBuffer;
  UINTN                         BufferLength;
  UINT8                         *TrustedCert;
  UINTN                         TrustedCertLength;
  UINTN                         CertCount;

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
      DEBUG ((EFI_D_INFO, "Image Security Data Size too small\n"));
      break;
    }

    WinCertificate = (WIN_CERTIFICATE *)(FileBuffer + Offset);
    if ((SecDataDirLeft < WinCertificate->dwLength) ||
        (SecDataDirLeft - WinCertificate->dwLength <
         ALIGN_SIZE (WinCertificate->dwLength)))
    {
      DEBUG ((EFI_D_INFO, "Image Security Data Size too small\n"));
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
        DEBUG ((EFI_D_INFO, "PKCS Cert Data too small\n"));
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
        DEBUG ((EFI_D_INFO, "WIN Cert UEFI Data too small\n"));
        break;
      }

      if (!CompareGuid (&WinCertUefiGuid->CertType, &gEfiCertPkcs7Guid)) {
        DEBUG ((EFI_D_INFO, "Cert Type not PKCS7\n"));
        continue;
      }

      AuthData     = WinCertUefiGuid->CertData;
      AuthDataSize = WinCertUefiGuid->Hdr.dwLength - OFFSET_OF (WIN_CERTIFICATE_UEFI_GUID, CertData);
    } else {
      if (WinCertificate->dwLength < sizeof (WIN_CERTIFICATE)) {
        DEBUG ((EFI_D_INFO, "Unknown Cert Data too small\n"));
        break;
      }

      DEBUG ((EFI_D_INFO, "Unknown Cert Data, skipping\n"));
      continue;
    }

    NewCertEntry = (BM_CERT_ENTRY *) AllocateZeroPool (sizeof(BM_CERT_ENTRY));

    if (NewCertEntry == NULL) {
      DEBUG ((EFI_D_ERROR, "Not enough free memory for certificate data\n"));
      return;
    }

    NewCertEntry->Signature = SOVEREIGN_BOOT_CERT_ENTRY_SIGNATURE;

    // Obtain the signer's certificate
    if (!Pkcs7GetSigners (AuthData, AuthDataSize,
                          &CertBuffer, &BufferLength,
                          &TrustedCert, &TrustedCertLength)) {
      FreePool (NewCertEntry);
      DEBUG ((EFI_D_INFO, "Could not get PKCS7 signers\n"));
      continue;
    }
    if ((BufferLength == 0) || (CertBuffer == NULL) || ((*CertBuffer) == 0)) {
      FreePool (NewCertEntry);
      DEBUG ((EFI_D_INFO, "PKCS7 signers data invalid\n"));
      continue;
    }

    // Save the buffer to the signer's certificate
    NewCertEntry->CertData = TrustedCert;
    NewCertEntry->CertDataSize = TrustedCertLength;

    // Free unused cert chain
    Pkcs7FreeSigners (CertBuffer);

    NewCertEntry->CertDigest = (UINT8 *)AllocateZeroPool(SHA256_DIGEST_SIZE);
    if (NewCertEntry->CertDigest != NULL) {
      CalculateCertHash(
        NewCertEntry->CertData,
        NewCertEntry->CertDataSize,
        HASHALG_SHA256,
        NewCertEntry->CertDigest);
      NewCertEntry->CertDigestSize = SHA256_DIGEST_SIZE;
    }

    HashStatus = HashPeImageByType (
                   FileBuffer,
                   FileSize,
                   AuthData,
                   AuthDataSize,
                   ImageDigest,
                   &ImageDigestSize,
                   &NewCertEntry->CertType);
    if (EFI_ERROR (HashStatus)) {
      if (TrustedCert != NULL) {
        Pkcs7FreeSigners (TrustedCert);
      }
      FreePool (NewCertEntry->CertDigest);
      FreePool (NewCertEntry);
      DEBUG ((EFI_D_ERROR, "Failed to calculate image hash\n"));
      continue;
    }

    //
    // Check the digital signature against the valid certificate in allowed database (db).
    if (IsAllowedByDb (AuthData, AuthDataSize, ImageDigest, ImageDigestSize)) {
      NewCertEntry->ImageIsVerified = TRUE;
      NewCertEntry->CertIsInDb = TRUE;
    }

    //
    // Check the digital signature against the revoked certificate in forbidden database (dbx).
    //
    if (IsForbiddenByDbx (AuthData, AuthDataSize, ImageDigest, ImageDigestSize)) {
      NewCertEntry->ImageIsVerified = FALSE;
      NewCertEntry->CertIsInDbx = TRUE;
    }

    DEBUG ((EFI_D_INFO, "Certificate Details:\n"
      "\tImageIsVerified: %u\n"
      "\tCertIsInDb: %u\n"
      "\tCertIsInDbx: %u\n",
      NewCertEntry->ImageIsVerified,
      NewCertEntry->CertIsInDb,
      NewCertEntry->CertIsInDbx));

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
  IN  BM_MENU_ENTRY  *Entry
  )
{
  BM_SECURITY_CONTEXT           *SecCtx;
  BM_LOAD_CONTEXT               *LoadCtx;
  EFI_IMAGE_DATA_DIRECTORY      *SecDir;
  EFI_DEVICE_PATH_PROTOCOL      *FullFilePath;
  VOID                          *ImageBase;
  UINTN                         ImageSize;
  UINT32                        AuthStatus;
  EFI_STATUS                    Status;
  EFI_GUID                      CertType;

  FullFilePath = NULL;
  LoadCtx = (BM_LOAD_CONTEXT *)Entry->VariableContext;
  SecCtx = (BM_SECURITY_CONTEXT *)AllocateZeroPool(sizeof(BM_SECURITY_CONTEXT));
  Entry->SecurityContext = SecCtx;

  DEBUG ((EFI_D_INFO, "%a: Processing %s\n", __FUNCTION__, LoadCtx->Description));

  if (SecCtx == NULL) {
    DEBUG ((EFI_D_INFO, "Not enough memory for security context\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  // Expand the Device PAth to the file if needed
  if (LoadCtx->NeedsPathExpansion) {
    FullFilePath = EfiBootManagerGetNextLoadOptionDevicePath (LoadCtx->FilePath, FullFilePath);
    DEBUG ((EFI_D_INFO, "Expanded file path %s\n",
      ConvertDevicePathToText(FullFilePath, FALSE, TRUE)));
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
    DEBUG ((EFI_D_INFO, "Failed to read file: %r\n", Status));
    goto ON_EXIT;
  }

  Status = GetImageSecDataDir (ImageBase, ImageSize, &SecDir);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_INFO, "Failed to get Image Security Data\n"));
    goto ON_EXIT;
  }

  // Always calculate SHA256 hash of the image and store it for display and
  // possible trust/distrust choice.
  SecCtx->ImageDigest = (UINT8 *)AllocateZeroPool(SHA256_DIGEST_SIZE);
  HashPeImage (ImageBase, ImageSize, HASHALG_SHA256, SecCtx->ImageDigest, &SecCtx->ImageDigestSize, &CertType);

  //
  // Start Image Validation.
  //
  VerifyImageHashInDatabases (ImageBase, ImageSize, SecCtx);

  // No certificates, end here
  if ((SecDir == NULL) || (SecDir->Size == 0)) {
    DEBUG ((EFI_D_INFO, "No Image Security Data, image is unsigned\n"));
    SecCtx->ImageIsSigned = FALSE;
    SecCtx->NumCertificates = 0;
    goto ON_EXIT;
  }

  SecCtx->ImageIsSigned = TRUE;

  // Parse certificates
  InitializeListHead (&SecCtx->Certs);
  FillCertificateEntries (ImageBase, ImageSize, SecCtx, SecDir);
  Status = EFI_SUCCESS;

  DEBUG ((EFI_D_INFO, "Image Details:\n"
    "\tImageIsInDbx: %u\n"
    "\tImageIsInDb: %u\n"
    "\tImageIsSigned: %u\n"
    "\tAuthenticationStatus: %u\n"
    "\tNumCertificates: %u\n",
    SecCtx->ImageIsInDbx,
    SecCtx->ImageIsInDb,
    SecCtx->ImageIsSigned,
    SecCtx->AuthenticationStatus,
    SecCtx->NumCertificates));

ON_EXIT:

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
BM_CERT_ENTRY *
GetCertEntry (
  BM_MENU_ENTRY  *MenuEntry,
  UINTN           CertNumber
  )
{
  BM_CERT_ENTRY        *NewCertEntry;
  BM_SECURITY_CONTEXT  *SecCtx;
  UINTN                Index;
  LIST_ENTRY           *List;

  SecCtx = (BM_SECURITY_CONTEXT *) MenuEntry->SecurityContext;

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

  NewCertEntry = CR (List, BM_CERT_ENTRY, CertLink, SOVEREIGN_BOOT_CERT_ENTRY_SIGNATURE);

  return NewCertEntry;
}

/**
  Parse hash value from EFI_SIGNATURE_DATA, and save in the CHAR16 type array.
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

  *BufferToReturn = AllocateZeroPool (TotalSize);
  if (*BufferToReturn == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  for (Index = 0, BufferIndex = 0; Index < DigestSize; Index = Index + 1) {
    BufferIndex += UnicodeSPrint (&(*BufferToReturn)[BufferIndex], TotalSize - sizeof (CHAR16) * BufferIndex, L"%02x", Digest[Index]);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
UpdateCertInfo (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN  UINTN                               OptionNumber,
  IN  UINTN                               CertNumber
  )
{
  BM_MENU_ENTRY        *BootloaderEntry;
  BM_CERT_ENTRY        *CertificateEntry;
  BM_SECURITY_CONTEXT  *SecurityContext;
  EFI_STRING           NewString;
  EFI_STRING           OldString;
  EFI_STATUS           Status;

  BootloaderEntry   = GetMenuEntry (&BootOptionMenu, OptionNumber);
  if (BootloaderEntry == NULL) {
    return EFI_NO_MEDIA;
  }

  SecurityContext = (BM_SECURITY_CONTEXT *)BootloaderEntry->SecurityContext;
  if (SecurityContext == NULL) {
    return EFI_NO_MEDIA;
  }

  Private->FormData.ImageUnsigned = (!SecurityContext->ImageIsSigned ||
                                     (SecurityContext->NumCertificates == 0));

  OldString = HiiGetString (Private->HiiHandle, STRING_TOKEN (STR_KEY_FINGERPRINT_HASH), NULL);

  // Image is unsigned? Show its hash instead of certificates
  if (Private->FormData.ImageUnsigned) {
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_KEY_FINGERPRINT), L"Image is unsigned !!!\nImage hash (SHA-256):", NULL);

    Status = ParseHashValue (SecurityContext->ImageDigest, SecurityContext->ImageDigestSize, &NewString);
    if (!EFI_ERROR (Status) && (NewString != NULL)) {
      HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_KEY_FINGERPRINT_HASH), NewString, NULL);
    } else {
      HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_KEY_FINGERPRINT_HASH), L"Image hash could not be obtained.", NULL);
    }

    // Free previous string if any
    if (OldString != NULL) {
      FreePool (OldString);
    }

    return EFI_SUCCESS;
  }

  // Image is signed, show its certificate(s)
  CertificateEntry = GetCertEntry(BootloaderEntry, CertNumber);
  if (CertificateEntry == NULL) {
    return EFI_NO_MEDIA;
  }

  HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_KEY_FINGERPRINT), L"Certificate fingerprint (SHA-256):", NULL);

  Status = ParseHashValue (CertificateEntry->CertDigest, CertificateEntry->CertDigestSize, &NewString);
  if (!EFI_ERROR (Status) && (NewString != NULL)) {
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_KEY_FINGERPRINT_HASH), NewString, NULL);
  } else {
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_KEY_FINGERPRINT_HASH), L"Could not obtain certificate fingerprint", NULL);
  }

  // Free previous string if any
  if (OldString != NULL) {
    FreePool (OldString);
  }

  // TODO  key details

  return EFI_SUCCESS;
}
