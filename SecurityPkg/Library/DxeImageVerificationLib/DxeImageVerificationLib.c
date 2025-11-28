/** @file
  Implement image verification services.

  Caution: This file requires additional review when modified.
  This library will have external input - PE/COFF image.
  This external input must be validated carefully to avoid security issue like
  buffer overflow, integer overflow.

  DxeImageVerificationLibImageRead() function will make sure the PE/COFF image content
  read is within the image buffer.

  DxeImageVerificationHandler(), HashPeImageByType(), HashPeImage() function will accept
  untrusted PE/COFF image and validate its data structure within this image buffer before use.

Copyright (c) 2009 - 2018, Intel Corporation. All rights reserved.<BR>
(C) Copyright 2016 Hewlett Packard Enterprise Development LP<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "ImageVerificationLibInternal.h"
//
// Information on current PE/COFF image
//
STATIC UINTN  LocalImageSize;
STATIC UINT8  *LocalImageBase = NULL;

//
// OID ASN.1 Value for Hash Algorithms
//
UINT8  mHashOidValue[] = {
  0x2B, 0x0E, 0x03, 0x02, 0x1A,                         // OBJ_sha1
  0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x04, // OBJ_sha224
  0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, // OBJ_sha256
  0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02, // OBJ_sha384
  0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03, // OBJ_sha512
};

HASH_TABLE  mHash[] = {
 #ifndef DISABLE_SHA1_DEPRECATED_INTERFACES
  { L"SHA1",   20, &mHashOidValue[0],  5, Sha1GetContextSize,   Sha1Init,   Sha1Update,   Sha1Final   },
 #else
  { L"SHA1",   20, &mHashOidValue[0],  5, NULL,                 NULL,       NULL,         NULL        },
 #endif
  { L"SHA224", 28, &mHashOidValue[5],  9, NULL,                 NULL,       NULL,         NULL        },
  { L"SHA256", 32, &mHashOidValue[14], 9, Sha256GetContextSize, Sha256Init, Sha256Update, Sha256Final },
  { L"SHA384", 48, &mHashOidValue[23], 9, Sha384GetContextSize, Sha384Init, Sha384Update, Sha384Final },
  { L"SHA512", 64, &mHashOidValue[32], 9, Sha512GetContextSize, Sha512Init, Sha512Update, Sha512Final }
};

/**
  Get the length of the digest of given hash algorithm.

  @param[in]    HashAlg   Hash algorithm type.

  @retval UINTN           Length of the digest of given hash algorithm.

**/
UINTN
GetDigestLength (
  IN  UINT32  HashAlg
  )
{
  if (HashAlg >= HASHALG_MAX) {
    return 0;
  }

  return mHash[HashAlg].DigestLength;
}

/**
  SecureBoot Hook for processing image verification.

  @param[in] VariableName                 Name of Variable to be found.
  @param[in] VendorGuid                   Variable vendor GUID.
  @param[in] DataSize                     Size of Data found. If size is less than the
                                          data, this value contains the required size.
  @param[in] Data                         Data pointer.

**/
VOID
EFIAPI
SecureBootHook (
  IN CHAR16    *VariableName,
  IN EFI_GUID  *VendorGuid,
  IN UINTN     DataSize,
  IN VOID      *Data
  );

/**
  Reads contents of a PE/COFF image in memory buffer.

  Caution: This function may receive untrusted input.
  PE/COFF image is external input, so this function will make sure the PE/COFF image content
  read is within the image buffer.

  @param  FileHandle      Pointer to the file handle to read the PE/COFF image.
  @param  FileOffset      Offset into the PE/COFF image to begin the read operation.
  @param  ReadSize        On input, the size in bytes of the requested read operation.
                          On output, the number of bytes actually read.
  @param  Buffer          Output buffer that contains the data read from the PE/COFF image.

  @retval EFI_SUCCESS     The specified portion of the PE/COFF image was read and the size
**/
EFI_STATUS
EFIAPI
DxeImageVerificationLibImageRead (
  IN     VOID   *FileHandle,
  IN     UINTN  FileOffset,
  IN OUT UINTN  *ReadSize,
  OUT    VOID   *Buffer
  )
{
  UINTN  EndPosition;

  if ((FileHandle == NULL) || (ReadSize == NULL) || (Buffer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (MAX_ADDRESS - FileOffset < *ReadSize) {
    return EFI_INVALID_PARAMETER;
  }

  EndPosition = FileOffset + *ReadSize;
  if (EndPosition > LocalImageSize) {
    *ReadSize = (UINT32)(LocalImageSize - FileOffset);
  }

  if (FileOffset >= LocalImageSize) {
    *ReadSize = 0;
  }

  CopyMem (Buffer, (UINT8 *)((UINTN)FileHandle + FileOffset), *ReadSize);

  return EFI_SUCCESS;
}


EFI_STATUS
GetImagePeCoffOffset (
  IN     VOID                      *FileBase,
  IN     UINTN                     FileSize,
  OUT    UINT32                    *PeCoffHeaderOffset
  )
{
  EFI_IMAGE_DOS_HEADER             *DosHdr;
  PE_COFF_LOADER_IMAGE_CONTEXT     ImageContext;
  RETURN_STATUS                    PeCoffStatus;

  if (PeCoffHeaderOffset == NULL || FileBase == NULL || FileSize == 0) {
    return EFI_INVALID_PARAMETER;
  }

  LocalImageSize = FileSize;
  LocalImageBase = FileBase;

  ZeroMem (&ImageContext, sizeof (ImageContext));
  ImageContext.Handle    = (VOID *)FileBase;
  ImageContext.ImageRead = (PE_COFF_LOADER_READ_FILE)DxeImageVerificationLibImageRead;

  //
  // Get information about the image being loaded
  //
  PeCoffStatus = PeCoffLoaderGetImageInfo (&ImageContext);
  if (RETURN_ERROR (PeCoffStatus)) {
    LocalImageBase = NULL;
    LocalImageSize = 0;

    //
    // The information can't be got from the invalid PeImage
    //
    DEBUG ((DEBUG_INFO, "DxeImageVerificationLib: PeImage invalid. Cannot retrieve image information.\n"));
    return EFI_UNSUPPORTED;
  }

  LocalImageBase = NULL;
  LocalImageSize = 0;

  DosHdr = (EFI_IMAGE_DOS_HEADER *)FileBase;
  if (DosHdr->e_magic == EFI_IMAGE_DOS_SIGNATURE) {
    //
    // DOS image header is present,
    // so read the PE header after the DOS image header.
    //
    *PeCoffHeaderOffset = DosHdr->e_lfanew;
  } else {
    *PeCoffHeaderOffset = 0;
  }

  return EFI_SUCCESS;
}

EFI_IMAGE_OPTIONAL_HEADER_UNION *
GetImageNtHeader (
  IN     VOID                  *FileBase,
  IN     UINTN                 FileSize
  )
{
  EFI_STATUS                       Status;
  UINT32                           PeCoffHeaderOffset;
  EFI_IMAGE_OPTIONAL_HEADER_UNION  *NtHeader;

  Status = GetImagePeCoffOffset (FileBase, FileSize, &PeCoffHeaderOffset);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_INFO, "PE Coff offset not found\n"));
    return NULL;
  }

  NtHeader = (EFI_IMAGE_OPTIONAL_HEADER_UNION *)(FileBase + PeCoffHeaderOffset);
  //
  // Check PE/COFF image.
  //
  if (NtHeader->Pe32.Signature != EFI_IMAGE_NT_SIGNATURE) {
    //
    // It is not a valid Pe/Coff file.
    //
    DEBUG ((DEBUG_INFO, "DxeImageVerificationLib: Not a valid PE/COFF image.\n"));
    return NULL;
  }

  return NtHeader;
}

/**
  Calculate hash of Pe/Coff image based on the authenticode image hashing in
  PE/COFF Specification 8.0 Appendix A

  Caution: This function may receive untrusted input.
  PE/COFF image is external input, so this function will validate its data structure
  within this image buffer before use.

  @param[in]    ImageBase        Address to the file content.
  @param[in]    ImageSize        File size.
  @param[in]    HashAlg          Hash algorithm type.
  @param[out]   ImageDigest      Buffer containing digest of the file.
  @param[out]   ImageDigestSize  Size of the digest buffer.
  @param[out]   CertType         GUID representing the hash type

  @retval TRUE            Successfully hash image.
  @retval FALSE           Fail in hash image.

**/
BOOLEAN
HashPeImage (
  IN  VOID*    ImageBase,
  IN  UINTN    ImageSize,
  IN  UINT32   HashAlg,
  OUT UINT8    *ImageDigest,
  OUT UINTN    *ImageDigestSize,
  OUT EFI_GUID *CertType
  )
{
  BOOLEAN                          Status;
  EFI_IMAGE_SECTION_HEADER         *Section;
  VOID                             *HashCtx;
  UINTN                            CtxSize;
  UINT8                            *HashBase;
  UINTN                            HashSize;
  UINTN                            SumOfBytesHashed;
  EFI_IMAGE_SECTION_HEADER         *SectionHeader;
  UINTN                            Index;
  UINTN                            Pos;
  UINT32                           CertSize;
  UINT32                           NumberOfRvaAndSizes;
  EFI_IMAGE_OPTIONAL_HEADER_UNION  *NtHeader;

  HashCtx       = NULL;
  SectionHeader = NULL;
  Status        = FALSE;

  if ((HashAlg >= HASHALG_MAX)) {
    return FALSE;
  }

  if ((ImageDigest == NULL) || (ImageDigestSize == NULL) || (CertType == NULL)) {
    return FALSE;
  }


  if ((mHash[HashAlg].GetContextSize == NULL) ||
      (mHash[HashAlg].HashInit == NULL) ||
      (mHash[HashAlg].HashUpdate == NULL) ||
      (mHash[HashAlg].HashFinal == NULL)) {
    return FALSE;
  }

  //
  // Initialize context of hash.
  //
  ZeroMem (ImageDigest, MAX_DIGEST_SIZE);

  switch (HashAlg) {
 #ifndef DISABLE_SHA1_DEPRECATED_INTERFACES
    case HASHALG_SHA1:
      *ImageDigestSize = SHA1_DIGEST_SIZE;
      CopyGuid (CertType, &gEfiCertSha1Guid);
      break;
 #endif

    case HASHALG_SHA256:
      *ImageDigestSize = SHA256_DIGEST_SIZE;
      CopyGuid (CertType, &gEfiCertSha256Guid);
      break;

    case HASHALG_SHA384:
      *ImageDigestSize = SHA384_DIGEST_SIZE;
      CopyGuid (CertType, &gEfiCertSha384Guid);
      break;

    case HASHALG_SHA512:
      *ImageDigestSize = SHA512_DIGEST_SIZE;
      CopyGuid (CertType, &gEfiCertSha512Guid);
      break;

    default:
      return FALSE;
  }

  CtxSize      = mHash[HashAlg].GetContextSize ();

  HashCtx = AllocatePool (CtxSize);
  if (HashCtx == NULL) {
    return FALSE;
  }

  // 1.  Load the image header into memory.

  // 2.  Initialize a SHA hash context.
  Status = mHash[HashAlg].HashInit (HashCtx);

  if (!Status) {
    goto Done;
  }

  NtHeader = GetImageNtHeader (ImageBase, ImageSize);
  if (NtHeader == NULL) {
    goto Done;
  }
  //
  // Measuring PE/COFF Image Header;
  // But CheckSum field and SECURITY data directory (certificate) are excluded
  //

  //
  // 3.  Calculate the distance from the base of the image header to the image checksum address.
  // 4.  Hash the image header from its base to beginning of the image checksum.
  //
  HashBase = ImageBase;
  if (NtHeader->Pe32.OptionalHeader.Magic == EFI_IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
    //
    // Use PE32 offset.
    //
    HashSize            = (UINTN)(&NtHeader->Pe32.OptionalHeader.CheckSum) - (UINTN)HashBase;
    NumberOfRvaAndSizes = NtHeader->Pe32.OptionalHeader.NumberOfRvaAndSizes;
  } else if (NtHeader->Pe32.OptionalHeader.Magic == EFI_IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
    //
    // Use PE32+ offset.
    //
    HashSize            = (UINTN)(&NtHeader->Pe32Plus.OptionalHeader.CheckSum) - (UINTN)HashBase;
    NumberOfRvaAndSizes = NtHeader->Pe32Plus.OptionalHeader.NumberOfRvaAndSizes;
  } else {
    //
    // Invalid header magic number.
    //
    Status = FALSE;
    goto Done;
  }

  Status = mHash[HashAlg].HashUpdate (HashCtx, HashBase, HashSize);
  if (!Status) {
    goto Done;
  }

  //
  // 5.  Skip over the image checksum (it occupies a single ULONG).
  //
  if (NumberOfRvaAndSizes <= EFI_IMAGE_DIRECTORY_ENTRY_SECURITY) {
    //
    // 6.  Since there is no Cert Directory in optional header, hash everything
    //     from the end of the checksum to the end of image header.
    //
    if (NtHeader->Pe32.OptionalHeader.Magic == EFI_IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
      //
      // Use PE32 offset.
      //
      HashBase = (UINT8 *)&NtHeader->Pe32.OptionalHeader.CheckSum + sizeof (UINT32);
      HashSize = NtHeader->Pe32.OptionalHeader.SizeOfHeaders - ((UINTN)HashBase - (UINTN)ImageBase);
    } else {
      //
      // Use PE32+ offset.
      //
      HashBase = (UINT8 *)&NtHeader->Pe32Plus.OptionalHeader.CheckSum + sizeof (UINT32);
      HashSize = NtHeader->Pe32Plus.OptionalHeader.SizeOfHeaders - ((UINTN)HashBase - (UINTN)ImageBase);
    }

    if (HashSize != 0) {
      Status = mHash[HashAlg].HashUpdate (HashCtx, HashBase, HashSize);
      if (!Status) {
        goto Done;
      }
    }
  } else {
    //
    // 7.  Hash everything from the end of the checksum to the start of the Cert Directory.
    //
    if (NtHeader->Pe32.OptionalHeader.Magic == EFI_IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
      //
      // Use PE32 offset.
      //
      HashBase = (UINT8 *)&NtHeader->Pe32.OptionalHeader.CheckSum + sizeof (UINT32);
      HashSize = (UINTN)(&NtHeader->Pe32.OptionalHeader.DataDirectory[EFI_IMAGE_DIRECTORY_ENTRY_SECURITY]) - (UINTN)HashBase;
    } else {
      //
      // Use PE32+ offset.
      //
      HashBase = (UINT8 *)&NtHeader->Pe32Plus.OptionalHeader.CheckSum + sizeof (UINT32);
      HashSize = (UINTN)(&NtHeader->Pe32Plus.OptionalHeader.DataDirectory[EFI_IMAGE_DIRECTORY_ENTRY_SECURITY]) - (UINTN)HashBase;
    }

    if (HashSize != 0) {
      Status = mHash[HashAlg].HashUpdate (HashCtx, HashBase, HashSize);
      if (!Status) {
        goto Done;
      }
    }

    //
    // 8.  Skip over the Cert Directory. (It is sizeof(IMAGE_DATA_DIRECTORY) bytes.)
    // 9.  Hash everything from the end of the Cert Directory to the end of image header.
    //
    if (NtHeader->Pe32.OptionalHeader.Magic == EFI_IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
      //
      // Use PE32 offset
      //
      HashBase = (UINT8 *)&NtHeader->Pe32.OptionalHeader.DataDirectory[EFI_IMAGE_DIRECTORY_ENTRY_SECURITY + 1];
      HashSize = NtHeader->Pe32.OptionalHeader.SizeOfHeaders - ((UINTN)HashBase - (UINTN)ImageBase);
    } else {
      //
      // Use PE32+ offset.
      //
      HashBase = (UINT8 *)&NtHeader->Pe32Plus.OptionalHeader.DataDirectory[EFI_IMAGE_DIRECTORY_ENTRY_SECURITY + 1];
      HashSize = NtHeader->Pe32Plus.OptionalHeader.SizeOfHeaders - ((UINTN)HashBase - (UINTN)ImageBase);
    }

    if (HashSize != 0) {
      Status = mHash[HashAlg].HashUpdate (HashCtx, HashBase, HashSize);
      if (!Status) {
        goto Done;
      }
    }
  }

  //
  // 10. Set the SUM_OF_BYTES_HASHED to the size of the header.
  //
  if (NtHeader->Pe32.OptionalHeader.Magic == EFI_IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
    //
    // Use PE32 offset.
    //
    SumOfBytesHashed = NtHeader->Pe32.OptionalHeader.SizeOfHeaders;
  } else {
    //
    // Use PE32+ offset
    //
    SumOfBytesHashed = NtHeader->Pe32Plus.OptionalHeader.SizeOfHeaders;
  }

  Section = (EFI_IMAGE_SECTION_HEADER *)(
                                         (UINTN)(NtHeader) +
                                         sizeof (UINT32) +
                                         sizeof (EFI_IMAGE_FILE_HEADER) +
                                         NtHeader->Pe32.FileHeader.SizeOfOptionalHeader
                                         );

  //
  // 11. Build a temporary table of pointers to all the IMAGE_SECTION_HEADER
  //     structures in the image. The 'NumberOfSections' field of the image
  //     header indicates how big the table should be. Do not include any
  //     IMAGE_SECTION_HEADERs in the table whose 'SizeOfRawData' field is zero.
  //
  SectionHeader = (EFI_IMAGE_SECTION_HEADER *)AllocateZeroPool (sizeof (EFI_IMAGE_SECTION_HEADER) * NtHeader->Pe32.FileHeader.NumberOfSections);
  if (SectionHeader == NULL) {
    Status = FALSE;
    goto Done;
  }

  //
  // 12.  Using the 'PointerToRawData' in the referenced section headers as
  //      a key, arrange the elements in the table in ascending order. In other
  //      words, sort the section headers according to the disk-file offset of
  //      the section.
  //
  for (Index = 0; Index < NtHeader->Pe32.FileHeader.NumberOfSections; Index++) {
    Pos = Index;
    while ((Pos > 0) && (Section->PointerToRawData < SectionHeader[Pos - 1].PointerToRawData)) {
      CopyMem (&SectionHeader[Pos], &SectionHeader[Pos - 1], sizeof (EFI_IMAGE_SECTION_HEADER));
      Pos--;
    }

    CopyMem (&SectionHeader[Pos], Section, sizeof (EFI_IMAGE_SECTION_HEADER));
    Section += 1;
  }

  //
  // 13.  Walk through the sorted table, bring the corresponding section
  //      into memory, and hash the entire section (using the 'SizeOfRawData'
  //      field in the section header to determine the amount of data to hash).
  // 14.  Add the section's 'SizeOfRawData' to SUM_OF_BYTES_HASHED .
  // 15.  Repeat steps 13 and 14 for all the sections in the sorted table.
  //
  for (Index = 0; Index < NtHeader->Pe32.FileHeader.NumberOfSections; Index++) {
    Section = &SectionHeader[Index];
    if (Section->SizeOfRawData == 0) {
      continue;
    }

    HashBase = ImageBase + Section->PointerToRawData;
    HashSize = (UINTN)Section->SizeOfRawData;

    Status = mHash[HashAlg].HashUpdate (HashCtx, HashBase, HashSize);
    if (!Status) {
      goto Done;
    }

    SumOfBytesHashed += HashSize;
  }

  //
  // 16.  If the file size is greater than SUM_OF_BYTES_HASHED, there is extra
  //      data in the file that needs to be added to the hash. This data begins
  //      at file offset SUM_OF_BYTES_HASHED and its length is:
  //             FileSize  -  (CertDirectory->Size)
  //
  if (ImageSize > SumOfBytesHashed) {
    HashBase = ImageBase + SumOfBytesHashed;

    if (NumberOfRvaAndSizes <= EFI_IMAGE_DIRECTORY_ENTRY_SECURITY) {
      CertSize = 0;
    } else {
      if (NtHeader->Pe32.OptionalHeader.Magic == EFI_IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        //
        // Use PE32 offset.
        //
        CertSize = NtHeader->Pe32.OptionalHeader.DataDirectory[EFI_IMAGE_DIRECTORY_ENTRY_SECURITY].Size;
      } else {
        //
        // Use PE32+ offset.
        //
        CertSize = NtHeader->Pe32Plus.OptionalHeader.DataDirectory[EFI_IMAGE_DIRECTORY_ENTRY_SECURITY].Size;
      }
    }

    if (ImageSize > CertSize + SumOfBytesHashed) {
      HashSize = (UINTN)(ImageSize - CertSize - SumOfBytesHashed);

      Status = mHash[HashAlg].HashUpdate (HashCtx, HashBase, HashSize);
      if (!Status) {
        goto Done;
      }
    } else if (ImageSize < CertSize + SumOfBytesHashed) {
      Status = FALSE;
      goto Done;
    }
  }

  Status = mHash[HashAlg].HashFinal (HashCtx, ImageDigest);

Done:
  if (HashCtx != NULL) {
    FreePool (HashCtx);
  }

  if (SectionHeader != NULL) {
    FreePool (SectionHeader);
  }

  return Status;
}

/**
  Recognize the Hash algorithm in PE/COFF Authenticode and calculate hash of
  Pe/Coff image based on the authenticode image hashing in PE/COFF Specification
  8.0 Appendix A

  Caution: This function may receive untrusted input.
  PE/COFF image is external input, so this function will validate its data structure
  within this image buffer before use.

  @param[in]  ImageBase           Pointer to the file content.
  @param[in]  ImageSize           Size of the file.
  @param[in]  AuthData            Pointer to the Authenticode Signature retrieved from signed image.
  @param[in]  AuthDataSize        Size of the Authenticode Signature in bytes.
  @param[out] ImageDigest         Buffer with the file digest.
  @param[out] ImageDigestSize     Size of the file digest.
  @param[out] CertType            GUID representing the type of hash.

  @retval EFI_UNSUPPORTED             Hash algorithm is not supported.
  @retval EFI_SUCCESS                 Hash successfully.

**/
EFI_STATUS
HashPeImageByType (
  IN  VOID     *ImageBase,
  IN  UINTN    ImageSize,
  IN  UINT8    *AuthData,
  IN  UINTN    AuthDataSize,
  OUT UINT8    *ImageDigest,
  OUT UINTN    *ImageDigestSize,
  OUT EFI_GUID *CertType
  )
{
  UINT8  Index;

  if ((ImageBase == NULL) || (ImageSize == 0) ||
      (AuthData == NULL) || (AuthDataSize == 0) ||
      (ImageDigest == NULL) || (ImageDigestSize == NULL) ||
      (CertType == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  for (Index = 0; Index < HASHALG_MAX; Index++) {
    //
    // Check the Hash algorithm in PE/COFF Authenticode.
    //    According to PKCS#7 Definition:
    //        SignedData ::= SEQUENCE {
    //            version Version,
    //            digestAlgorithms DigestAlgorithmIdentifiers,
    //            contentInfo ContentInfo,
    //            .... }
    //    The DigestAlgorithmIdentifiers can be used to determine the hash algorithm in PE/COFF hashing
    //    This field has the fixed offset (+32) in final Authenticode ASN.1 data.
    //    Fixed offset (+32) is calculated based on two bytes of length encoding.
    //
    if ((*(AuthData + 1) & TWO_BYTE_ENCODE) != TWO_BYTE_ENCODE) {
      //
      // Only support two bytes of Long Form of Length Encoding.
      //
      continue;
    }

    if (AuthDataSize < 32 + mHash[Index].OidLength) {
      return EFI_UNSUPPORTED;
    }

    if (CompareMem (AuthData + 32, mHash[Index].OidValue, mHash[Index].OidLength) == 0) {
      break;
    }
  }

  if (Index == HASHALG_MAX) {
    return EFI_UNSUPPORTED;
  }

  //
  // HASH PE Image based on Hash algorithm in PE/COFF Authenticode.
  //
  if (!HashPeImage (ImageBase, ImageSize, Index, ImageDigest, ImageDigestSize, CertType)) {
    return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}

/**
  Calculate the hash of a certificate data with the specified hash algorithm.

  @param[in]    CertData  The certificate data to be hashed.
  @param[in]    CertSize  The certificate size in bytes.
  @param[in]    HashAlg   The specified hash algorithm.
  @param[out]   CertHash  The output digest of the certificate

  @retval TRUE            Successfully got the hash of the CertData.
  @retval FALSE           Failed to get the hash of CertData.

**/
BOOLEAN
CalculateCertHash (
  IN  UINT8   *CertData,
  IN  UINTN   CertSize,
  IN  UINT32  HashAlg,
  OUT UINT8   *CertHash
  )
{
  BOOLEAN  Status;
  VOID     *HashCtx;
  UINTN    CtxSize;
  UINT8    *TBSCert;
  UINTN    TBSCertSize;

  HashCtx = NULL;
  Status  = FALSE;

  if (HashAlg >= HASHALG_MAX) {
    return FALSE;
  }

  if (CertHash == NULL || CertData == NULL || CertSize == 0) {
    return FALSE;
  }

  //
  // Calculate the hash value of current TBSCertificate for comparision.
  //
  if ((mHash[HashAlg].GetContextSize == NULL) ||
      (mHash[HashAlg].HashInit == NULL) ||
      (mHash[HashAlg].HashUpdate == NULL) ||
      (mHash[HashAlg].HashFinal == NULL)) {
    return FALSE;
  }

  //
  // Retrieve the TBSCertificate for Hash Calculation.
  //
  if (!X509GetTBSCert (CertData, CertSize, &TBSCert, &TBSCertSize)) {
    return FALSE;
  }

  //
  // 1. Initialize context of hash.
  //
  CtxSize = mHash[HashAlg].GetContextSize ();
  HashCtx = AllocatePool (CtxSize);
  if (HashCtx == NULL) {
    return FALSE;
  }

  //
  // 2. Initialize a hash context.
  //
  Status = mHash[HashAlg].HashInit (HashCtx);
  if (!Status) {
    goto Done;
  }

  //
  // 3. Calculate the hash.
  //
  Status = mHash[HashAlg].HashUpdate (HashCtx, TBSCert, TBSCertSize);
  if (!Status) {
    goto Done;
  }

  //
  // 4. Get the hash result.
  //
  ZeroMem (CertHash, mHash[HashAlg].DigestLength);
  Status = mHash[HashAlg].HashFinal (HashCtx, CertHash);

Done:
  if (HashCtx != NULL) {
    FreePool (HashCtx);
  }

  return Status;
}


/**
  Check whether the hash of an given X.509 certificate is in forbidden database (DBX).

  @param[in]  Certificate       Pointer to X.509 Certificate that is searched for.
  @param[in]  CertSize          Size of X.509 Certificate.
  @param[in]  SignatureList     Pointer to the Signature List in forbidden database.
  @param[in]  SignatureListSize Size of Signature List.
  @param[out] RevocationTime    Return the time that the certificate was revoked.
  @param[out] IsFound           Search result. Only valid if EFI_SUCCESS returned.

  @retval EFI_SUCCESS           Finished the search without any error.
  @retval Others                Error occurred in the search of database.

**/
EFI_STATUS
IsCertHashFoundInDbx (
  IN  UINT8               *Certificate,
  IN  UINTN               CertSize,
  IN  EFI_SIGNATURE_LIST  *SignatureList, OPTIONAL
  IN  UINTN               SignatureListSize,
  OUT EFI_TIME            *RevocationTime,
  OUT BOOLEAN             *IsFound
  )
{
  EFI_STATUS          Status;
  EFI_SIGNATURE_LIST  *DbxList;
  EFI_SIGNATURE_LIST  *DbxWalker;
  UINTN               DbxSize;
  EFI_SIGNATURE_DATA  *CertHash;
  UINTN               CertHashCount;
  UINTN               Index;
  UINT32              HashAlg;
  UINT8               CertDigest[MAX_DIGEST_SIZE];
  UINT8               *DbxCertHash;
  UINTN               SiglistHeaderSize;

  Status   = EFI_ABORTED;
  *IsFound = FALSE;
  DbxList  = SignatureList;
  DbxSize  = SignatureListSize;
  HashAlg  = HASHALG_MAX;

  if (DbxList == NULL) {
    //
    // Read signature database variable.
    //
    DbxSize = 0;
    Status   = gRT->GetVariable (EFI_IMAGE_SECURITY_DATABASE1, &gEfiImageSecurityDatabaseGuid, NULL, &DbxSize, NULL);
    if (Status != EFI_BUFFER_TOO_SMALL) {
      return EFI_SUCCESS;
    }

    DbxList = (EFI_SIGNATURE_LIST *)AllocateZeroPool (DbxSize);
    if (DbxList == NULL) {
      return EFI_SUCCESS;
    }

    Status = gRT->GetVariable (EFI_IMAGE_SECURITY_DATABASE1, &gEfiImageSecurityDatabaseGuid, NULL, &DbxSize, DbxList);
    if (EFI_ERROR (Status)) {
      goto Done;
    }

    if (SignatureListSize == 0) {
      SignatureListSize = DbxSize;
    }
  }

  DbxWalker = DbxList;
  while ((DbxSize > 0) && (SignatureListSize >= DbxWalker->SignatureListSize)) {
    //
    // Determine Hash Algorithm of Certificate in the forbidden database.
    //
    if (CompareGuid (&DbxWalker->SignatureType, &gEfiCertX509Sha256Guid)) {
      HashAlg = HASHALG_SHA256;
    } else if (CompareGuid (&DbxWalker->SignatureType, &gEfiCertX509Sha384Guid)) {
      HashAlg = HASHALG_SHA384;
    } else if (CompareGuid (&DbxWalker->SignatureType, &gEfiCertX509Sha512Guid)) {
      HashAlg = HASHALG_SHA512;
    } else {
      DbxSize -= DbxWalker->SignatureListSize;
      DbxWalker  = (EFI_SIGNATURE_LIST *)((UINT8 *)DbxWalker + DbxWalker->SignatureListSize);
      continue;
    }
    //
    // Calculate the hash value of current TBSCertificate for comparision.
    //
    if (!CalculateCertHash (Certificate, CertSize, HashAlg, CertDigest)) {
      goto Done;
    }

    SiglistHeaderSize = sizeof (EFI_SIGNATURE_LIST) + DbxWalker->SignatureHeaderSize;
    CertHash          = (EFI_SIGNATURE_DATA *)((UINT8 *)DbxWalker + SiglistHeaderSize);
    CertHashCount     = (DbxWalker->SignatureListSize - SiglistHeaderSize) / DbxWalker->SignatureSize;
    for (Index = 0; Index < CertHashCount; Index++) {
      //
      // Iterate each Signature Data Node within this CertList for verify.
      //
      DbxCertHash = CertHash->SignatureData;
      if (CompareMem (DbxCertHash, CertDigest, mHash[HashAlg].DigestLength) == 0) {
        //
        // Hash of Certificate is found in forbidden database.
        //
        Status   = EFI_SUCCESS;
        *IsFound = TRUE;

        if (RevocationTime != NULL) {
          //
          // Return the revocation time.
          //
          CopyMem (RevocationTime, (EFI_TIME *)(DbxCertHash + mHash[HashAlg].DigestLength), sizeof (EFI_TIME));
        }
        goto Done;
      }

      CertHash = (EFI_SIGNATURE_DATA *)((UINT8 *)CertHash + DbxWalker->SignatureSize);
    }

    DbxSize -= DbxWalker->SignatureListSize;
    DbxWalker  = (EFI_SIGNATURE_LIST *)((UINT8 *)DbxWalker + DbxWalker->SignatureListSize);
  }

  Status = EFI_SUCCESS;

Done:
  if (SignatureList == NULL && DbxList != NULL) {
    FreePool (DbxList);
  }

  return Status;
}

/**
  Check whether signature is in specified database.

  @param[in]  VariableName        Name of database variable that is searched in.
  @param[in]  Signature           Pointer to signature that is searched for.
  @param[in]  CertType            Pointer to hash algorithm.
  @param[in]  SignatureSize       Size of Signature.
  @param[out] IsFound             Search result. Only valid if EFI_SUCCESS returned

  @retval EFI_SUCCESS             Finished the search without any error.
  @retval Others                  Error occurred in the search of database.

**/
EFI_STATUS
IsSignatureFoundInDatabase (
  IN  CHAR16    *VariableName,
  IN  UINT8     *Signature,
  IN  EFI_GUID  *CertType,
  IN  UINTN     SignatureSize,
  OUT BOOLEAN   *IsFound
  )
{
  EFI_STATUS          Status;
  EFI_SIGNATURE_LIST  *CertList;
  EFI_SIGNATURE_DATA  *Cert;
  UINTN               DataSize;
  UINT8               *Data;
  UINTN               Index;
  UINTN               CertCount;

  //
  // Read signature database variable.
  //
  *IsFound = FALSE;
  Data     = NULL;
  DataSize = 0;
  Status   = gRT->GetVariable (VariableName, &gEfiImageSecurityDatabaseGuid, NULL, &DataSize, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    if (Status == EFI_NOT_FOUND) {
      //
      // No database, no need to search.
      //
      Status = EFI_SUCCESS;
    }

    return Status;
  }

  Data = (UINT8 *)AllocateZeroPool (DataSize);
  if (Data == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gRT->GetVariable (VariableName, &gEfiImageSecurityDatabaseGuid, NULL, &DataSize, Data);
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  //
  // Enumerate all signature data in SigDB to check if signature exists for executable.
  //
  CertList = (EFI_SIGNATURE_LIST *)Data;
  while ((DataSize > 0) && (DataSize >= CertList->SignatureListSize)) {
    CertCount = (CertList->SignatureListSize - sizeof (EFI_SIGNATURE_LIST) - CertList->SignatureHeaderSize) / CertList->SignatureSize;
    Cert      = (EFI_SIGNATURE_DATA *)((UINT8 *)CertList + sizeof (EFI_SIGNATURE_LIST) + CertList->SignatureHeaderSize);
    if ((CertList->SignatureSize == sizeof (EFI_SIGNATURE_DATA) - 1 + SignatureSize) && (CompareGuid (&CertList->SignatureType, CertType))) {
      for (Index = 0; Index < CertCount; Index++) {
        if (CompareMem (Cert->SignatureData, Signature, SignatureSize) == 0) {
          //
          // Find the signature in database.
          //
          *IsFound = TRUE;
          //
          // Entries in UEFI_IMAGE_SECURITY_DATABASE that are used to validate image should be measured
          //
          if (StrCmp (VariableName, EFI_IMAGE_SECURITY_DATABASE) == 0) {
            SecureBootHook (VariableName, &gEfiImageSecurityDatabaseGuid, CertList->SignatureSize, Cert);
          }

          break;
        }

        Cert = (EFI_SIGNATURE_DATA *)((UINT8 *)Cert + CertList->SignatureSize);
      }

      if (*IsFound) {
        break;
      }
    }

    DataSize -= CertList->SignatureListSize;
    CertList  = (EFI_SIGNATURE_LIST *)((UINT8 *)CertList + CertList->SignatureListSize);
  }

Done:
  if (Data != NULL) {
    FreePool (Data);
  }

  return Status;
}

/**
  Check whether the timestamp is valid by comparing the signing time and the revocation time.

  @param SigningTime         A pointer to the signing time.
  @param RevocationTime      A pointer to the revocation time.

  @retval  TRUE              The SigningTime is not later than the RevocationTime.
  @retval  FALSE             The SigningTime is later than the RevocationTime.

**/
BOOLEAN
IsValidSignatureByTimestamp (
  IN EFI_TIME  *SigningTime,
  IN EFI_TIME  *RevocationTime
  )
{
  if (SigningTime->Year != RevocationTime->Year) {
    return (BOOLEAN)(SigningTime->Year < RevocationTime->Year);
  } else if (SigningTime->Month != RevocationTime->Month) {
    return (BOOLEAN)(SigningTime->Month < RevocationTime->Month);
  } else if (SigningTime->Day != RevocationTime->Day) {
    return (BOOLEAN)(SigningTime->Day < RevocationTime->Day);
  } else if (SigningTime->Hour != RevocationTime->Hour) {
    return (BOOLEAN)(SigningTime->Hour < RevocationTime->Hour);
  } else if (SigningTime->Minute != RevocationTime->Minute) {
    return (BOOLEAN)(SigningTime->Minute < RevocationTime->Minute);
  }

  return (BOOLEAN)(SigningTime->Second <= RevocationTime->Second);
}

/**
  Check if the given time value is zero.

  @param[in]  Time      Pointer of a time value.

  @retval     TRUE      The Time is Zero.
  @retval     FALSE     The Time is not Zero.

**/
BOOLEAN
IsTimeZero (
  IN EFI_TIME  *Time
  )
{
  if ((Time->Year == 0) && (Time->Month == 0) &&  (Time->Day == 0) &&
      (Time->Hour == 0) && (Time->Minute == 0) && (Time->Second == 0))
  {
    return TRUE;
  }

  return FALSE;
}

/**
  Check whether the timestamp signature is valid and the signing time is also earlier than
  the revocation time.

  @param[in]  AuthData        Pointer to the Authenticode signature retrieved from signed image.
  @param[in]  AuthDataSize    Size of the Authenticode signature in bytes.
  @param[in]  RevocationTime  The time that the certificate was revoked.

  @retval TRUE      Timestamp signature is valid and signing time is no later than the
                    revocation time.
  @retval FALSE     Timestamp signature is not valid or the signing time is later than the
                    revocation time.

**/
BOOLEAN
PassTimestampCheck (
  IN UINT8     *AuthData,
  IN UINTN     AuthDataSize,
  IN EFI_TIME  *RevocationTime
  )
{
  EFI_STATUS          Status;
  BOOLEAN             VerifyStatus;
  EFI_SIGNATURE_LIST  *CertList;
  EFI_SIGNATURE_DATA  *Cert;
  UINT8               *DbtData;
  UINTN               DbtDataSize;
  UINT8               *RootCert;
  UINTN               RootCertSize;
  UINTN               Index;
  UINTN               CertCount;
  EFI_TIME            SigningTime;

  //
  // Variable Initialization
  //
  VerifyStatus = FALSE;
  DbtData      = NULL;
  CertList     = NULL;
  Cert         = NULL;
  RootCert     = NULL;
  RootCertSize = 0;

  //
  // If RevocationTime is zero, the certificate shall be considered to always be revoked.
  //
  if (IsTimeZero (RevocationTime)) {
    return FALSE;
  }

  //
  // RevocationTime is non-zero, the certificate should be considered to be revoked from that time and onwards.
  // Using the dbt to get the trusted TSA certificates.
  //
  DbtDataSize = 0;
  Status      = gRT->GetVariable (EFI_IMAGE_SECURITY_DATABASE2, &gEfiImageSecurityDatabaseGuid, NULL, &DbtDataSize, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    goto Done;
  }

  DbtData = (UINT8 *)AllocateZeroPool (DbtDataSize);
  if (DbtData == NULL) {
    goto Done;
  }

  Status = gRT->GetVariable (EFI_IMAGE_SECURITY_DATABASE2, &gEfiImageSecurityDatabaseGuid, NULL, &DbtDataSize, (VOID *)DbtData);
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  CertList = (EFI_SIGNATURE_LIST *)DbtData;
  while ((DbtDataSize > 0) && (DbtDataSize >= CertList->SignatureListSize)) {
    if (CompareGuid (&CertList->SignatureType, &gEfiCertX509Guid)) {
      Cert      = (EFI_SIGNATURE_DATA *)((UINT8 *)CertList + sizeof (EFI_SIGNATURE_LIST) + CertList->SignatureHeaderSize);
      CertCount = (CertList->SignatureListSize - sizeof (EFI_SIGNATURE_LIST) - CertList->SignatureHeaderSize) / CertList->SignatureSize;
      for (Index = 0; Index < CertCount; Index++) {
        //
        // Iterate each Signature Data Node within this CertList for verify.
        //
        RootCert     = Cert->SignatureData;
        RootCertSize = CertList->SignatureSize - sizeof (EFI_GUID);
        //
        // Get the signing time if the timestamp signature is valid.
        //
        if (ImageTimestampVerify (AuthData, AuthDataSize, RootCert, RootCertSize, &SigningTime)) {
          //
          // The signer signature is valid only when the signing time is earlier than revocation time.
          //
          if (IsValidSignatureByTimestamp (&SigningTime, RevocationTime)) {
            VerifyStatus = TRUE;
            goto Done;
          }
        }

        Cert = (EFI_SIGNATURE_DATA *)((UINT8 *)Cert + CertList->SignatureSize);
      }
    }

    DbtDataSize -= CertList->SignatureListSize;
    CertList     = (EFI_SIGNATURE_LIST *)((UINT8 *)CertList + CertList->SignatureListSize);
  }

Done:
  if (DbtData != NULL) {
    FreePool (DbtData);
  }

  return VerifyStatus;
}

/**
  Check whether the image signature is forbidden by the forbidden database (dbx).
  The image is forbidden to load if any certificates for signing are revoked before signing time.

  @param[in]  AuthData      Pointer to the Authenticode signature retrieved from the signed image.
  @param[in]  AuthDataSize  Size of the Authenticode signature in bytes.
  @param[in]  ImageDigest      Buffer containing digest of the file.
  @param[in]  ImageDigestSize  Size of the digest buffer.

  @retval TRUE              Image is forbidden by dbx.
  @retval FALSE             Image is not forbidden by dbx.

**/
BOOLEAN
IsForbiddenByDbx (
  IN UINT8  *AuthData,
  IN UINTN  AuthDataSize,
  IN UINT8  *ImageDigest,
  IN UINTN  ImageDigestSize
  )
{
  EFI_STATUS          Status;
  BOOLEAN             IsForbidden;
  BOOLEAN             IsFound;
  UINT8               *Data;
  UINTN               DataSize;
  EFI_SIGNATURE_LIST  *CertList;
  UINTN               CertListSize;
  EFI_SIGNATURE_DATA  *CertData;
  UINT8               *RootCert;
  UINTN               RootCertSize;
  UINTN               CertCount;
  UINTN               Index;
  UINT8               *CertBuffer;
  UINTN               BufferLength;
  UINT8               *TrustedCert;
  UINTN               TrustedCertLength;
  UINT8               CertNumber;
  UINT8               *CertPtr;
  UINT8               *Cert;
  UINTN               CertSize;
  EFI_TIME            RevocationTime;

  //
  // Variable Initialization
  //
  IsForbidden       = TRUE;
  Data              = NULL;
  CertList          = NULL;
  CertData          = NULL;
  RootCert          = NULL;
  RootCertSize      = 0;
  Cert              = NULL;
  CertBuffer        = NULL;
  BufferLength      = 0;
  TrustedCert       = NULL;
  TrustedCertLength = 0;

  //
  // The image will not be forbidden if dbx can't be got.
  //
  DataSize = 0;
  Status   = gRT->GetVariable (EFI_IMAGE_SECURITY_DATABASE1, &gEfiImageSecurityDatabaseGuid, NULL, &DataSize, NULL);
  ASSERT (EFI_ERROR (Status));
  if (Status != EFI_BUFFER_TOO_SMALL) {
    if (Status == EFI_NOT_FOUND) {
      //
      // Evidently not in dbx if the database doesn't exist.
      //
      IsForbidden = FALSE;
    }

    return IsForbidden;
  }

  Data = (UINT8 *)AllocateZeroPool (DataSize);
  if (Data == NULL) {
    return IsForbidden;
  }

  Status = gRT->GetVariable (EFI_IMAGE_SECURITY_DATABASE1, &gEfiImageSecurityDatabaseGuid, NULL, &DataSize, (VOID *)Data);
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  //
  // Verify image signature with RAW X509 certificates in DBX database.
  // If passed, the image will be forbidden.
  //
  CertList     = (EFI_SIGNATURE_LIST *)Data;
  CertListSize = DataSize;
  while ((CertListSize > 0) && (CertListSize >= CertList->SignatureListSize)) {
    if (CompareGuid (&CertList->SignatureType, &gEfiCertX509Guid)) {
      CertData  = (EFI_SIGNATURE_DATA *)((UINT8 *)CertList + sizeof (EFI_SIGNATURE_LIST) + CertList->SignatureHeaderSize);
      CertCount = (CertList->SignatureListSize - sizeof (EFI_SIGNATURE_LIST) - CertList->SignatureHeaderSize) / CertList->SignatureSize;

      for (Index = 0; Index < CertCount; Index++) {
        //
        // Iterate each Signature Data Node within this CertList for verify.
        //
        RootCert     = CertData->SignatureData;
        RootCertSize = CertList->SignatureSize - sizeof (EFI_GUID);

        //
        // Call AuthenticodeVerify library to Verify Authenticode struct.
        //
        IsForbidden = AuthenticodeVerify (
                        AuthData,
                        AuthDataSize,
                        RootCert,
                        RootCertSize,
                        ImageDigest,
                        ImageDigestSize
                        );
        if (IsForbidden) {
          DEBUG ((DEBUG_INFO, "DxeImageVerificationLib: Image is signed but signature is forbidden by DBX.\n"));
          goto Done;
        }

        CertData = (EFI_SIGNATURE_DATA *)((UINT8 *)CertData + CertList->SignatureSize);
      }
    }

    CertListSize -= CertList->SignatureListSize;
    CertList      = (EFI_SIGNATURE_LIST *)((UINT8 *)CertList + CertList->SignatureListSize);
  }

  //
  // Check X.509 Certificate Hash & Possible Timestamp.
  //

  //
  // Retrieve the certificate stack from AuthData
  // The output CertStack format will be:
  //       UINT8  CertNumber;
  //       UINT32 Cert1Length;
  //       UINT8  Cert1[];
  //       UINT32 Cert2Length;
  //       UINT8  Cert2[];
  //       ...
  //       UINT32 CertnLength;
  //       UINT8  Certn[];
  //
  Pkcs7GetSigners (AuthData, AuthDataSize, &CertBuffer, &BufferLength, &TrustedCert, &TrustedCertLength);
  if ((BufferLength == 0) || (CertBuffer == NULL) || ((*CertBuffer) == 0)) {
    IsForbidden = TRUE;
    goto Done;
  }

  //
  // Check if any hash of certificates embedded in AuthData is in the forbidden database.
  //
  CertNumber = (UINT8)(*CertBuffer);
  CertPtr    = CertBuffer + 1;
  for (Index = 0; Index < CertNumber; Index++) {
    CertSize = (UINTN)ReadUnaligned32 ((UINT32 *)CertPtr);
    Cert     = (UINT8 *)CertPtr + sizeof (UINT32);
    //
    // Advance CertPtr to the next cert in image signer's cert list
    //
    CertPtr = CertPtr + sizeof (UINT32) + CertSize;

    Status = IsCertHashFoundInDbx (Cert, CertSize, (EFI_SIGNATURE_LIST *)Data, DataSize, &RevocationTime, &IsFound);
    if (EFI_ERROR (Status)) {
      //
      // Error in searching dbx. Consider it as 'found'. RevocationTime might
      // not be valid in such situation.
      //
      IsForbidden = TRUE;
    } else if (IsFound) {
      //
      // Found Cert in dbx successfully. Check the timestamp signature and
      // signing time to determine if the image can be trusted.
      //
      if (PassTimestampCheck (AuthData, AuthDataSize, &RevocationTime)) {
        IsForbidden = FALSE;
        //
        // Pass DBT check. Continue to check other certs in image signer's cert list against DBX, DBT
        //
        continue;
      } else {
        IsForbidden = TRUE;
        DEBUG ((DEBUG_INFO, "DxeImageVerificationLib: Image is signed but signature failed the timestamp check.\n"));
        goto Done;
      }
    }
  }

  IsForbidden = FALSE;

Done:
  if (Data != NULL) {
    FreePool (Data);
  }

  Pkcs7FreeSigners (CertBuffer);
  Pkcs7FreeSigners (TrustedCert);

  return IsForbidden;
}

/**
  Check whether the image signature can be verified by the trusted certificates in DB database.

  @param[in]  AuthData         Pointer to the Authenticode signature retrieved from signed image.
  @param[in]  AuthDataSize     Size of the Authenticode signature in bytes.
  @param[in]  ImageDigest      Buffer containing digest of the file.
  @param[in]  ImageDigestSize  Size of the digest buffer.

  @retval TRUE         Image passed verification using certificate in db.
  @retval FALSE        Image didn't pass verification using certificate in db.

**/
BOOLEAN
IsAllowedByDb (
  IN UINT8  *AuthData,
  IN UINTN  AuthDataSize,
  IN UINT8  *ImageDigest,
  IN UINTN  ImageDigestSize
  )
{
  EFI_STATUS          Status;
  BOOLEAN             VerifyStatus;
  BOOLEAN             IsFound;
  EFI_SIGNATURE_LIST  *CertList;
  EFI_SIGNATURE_DATA  *CertData;
  UINTN               DataSize;
  UINT8               *Data;
  UINT8               *RootCert;
  UINTN               RootCertSize;
  UINTN               Index;
  UINTN               CertCount;
  UINTN               DbxDataSize;
  UINT8               *DbxData;
  EFI_TIME            RevocationTime;

  Data         = NULL;
  CertList     = NULL;
  CertData     = NULL;
  RootCert     = NULL;
  DbxData      = NULL;
  RootCertSize = 0;
  VerifyStatus = FALSE;

  //
  // Fetch 'db' content. If 'db' doesn't exist or encounters problem to get the
  // data, return not-allowed-by-db (FALSE).
  //
  DataSize = 0;
  Status   = gRT->GetVariable (EFI_IMAGE_SECURITY_DATABASE, &gEfiImageSecurityDatabaseGuid, NULL, &DataSize, NULL);
  ASSERT (EFI_ERROR (Status));
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return VerifyStatus;
  }

  Data = (UINT8 *)AllocateZeroPool (DataSize);
  if (Data == NULL) {
    return VerifyStatus;
  }

  Status = gRT->GetVariable (EFI_IMAGE_SECURITY_DATABASE, &gEfiImageSecurityDatabaseGuid, NULL, &DataSize, (VOID *)Data);
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  //
  // Fetch 'dbx' content. If 'dbx' doesn't exist, continue to check 'db'.
  // If any other errors occurred, no need to check 'db' but just return
  // not-allowed-by-db (FALSE) to avoid bypass.
  //
  DbxDataSize = 0;
  Status      = gRT->GetVariable (EFI_IMAGE_SECURITY_DATABASE1, &gEfiImageSecurityDatabaseGuid, NULL, &DbxDataSize, NULL);
  ASSERT (EFI_ERROR (Status));
  if (Status != EFI_BUFFER_TOO_SMALL) {
    if (Status != EFI_NOT_FOUND) {
      goto Done;
    }

    //
    // 'dbx' does not exist. Continue to check 'db'.
    //
  } else {
    //
    // 'dbx' exists. Get its content.
    //
    DbxData = (UINT8 *)AllocateZeroPool (DbxDataSize);
    if (DbxData == NULL) {
      goto Done;
    }

    Status = gRT->GetVariable (EFI_IMAGE_SECURITY_DATABASE1, &gEfiImageSecurityDatabaseGuid, NULL, &DbxDataSize, (VOID *)DbxData);
    if (EFI_ERROR (Status)) {
      goto Done;
    }
  }

  //
  // Find X509 certificate in Signature List to verify the signature in pkcs7 signed data.
  //
  CertList = (EFI_SIGNATURE_LIST *)Data;
  while ((DataSize > 0) && (DataSize >= CertList->SignatureListSize)) {
    if (CompareGuid (&CertList->SignatureType, &gEfiCertX509Guid)) {
      CertData  = (EFI_SIGNATURE_DATA *)((UINT8 *)CertList + sizeof (EFI_SIGNATURE_LIST) + CertList->SignatureHeaderSize);
      CertCount = (CertList->SignatureListSize - sizeof (EFI_SIGNATURE_LIST) - CertList->SignatureHeaderSize) / CertList->SignatureSize;

      for (Index = 0; Index < CertCount; Index++) {
        //
        // Iterate each Signature Data Node within this CertList for verify.
        //
        RootCert     = CertData->SignatureData;
        RootCertSize = CertList->SignatureSize - sizeof (EFI_GUID);

        //
        // Call AuthenticodeVerify library to Verify Authenticode struct.
        //
        VerifyStatus = AuthenticodeVerify (
                         AuthData,
                         AuthDataSize,
                         RootCert,
                         RootCertSize,
                         ImageDigest,
                         ImageDigestSize
                         );
        if (VerifyStatus) {
          //
          // The image is signed and its signature is found in 'db'.
          //
          if (DbxData != NULL) {
            //
            // Here We still need to check if this RootCert's Hash is revoked
            //
            Status = IsCertHashFoundInDbx (RootCert, RootCertSize, (EFI_SIGNATURE_LIST *)DbxData, DbxDataSize, &RevocationTime, &IsFound);
            if (EFI_ERROR (Status)) {
              //
              // Error in searching dbx. Consider it as 'found'. RevocationTime might
              // not be valid in such situation.
              //
              VerifyStatus = FALSE;
            } else if (IsFound) {
              //
              // Check the timestamp signature and signing time to determine if the RootCert can be trusted.
              //
              VerifyStatus = PassTimestampCheck (AuthData, AuthDataSize, &RevocationTime);
              if (!VerifyStatus) {
                DEBUG ((DEBUG_INFO, "DxeImageVerificationLib: Image is signed and signature is accepted by DB, but its root cert failed the timestamp check.\n"));
              }
            }
          }

          //
          // There's no 'dbx' to check revocation time against (must-be pass),
          // or, there's revocation time found in 'dbx' and checked againt 'dbt'
          // (maybe pass or fail, depending on timestamp compare result). Either
          // way the verification job has been completed at this point.
          //
          goto Done;
        }

        CertData = (EFI_SIGNATURE_DATA *)((UINT8 *)CertData + CertList->SignatureSize);
      }
    }

    DataSize -= CertList->SignatureListSize;
    CertList  = (EFI_SIGNATURE_LIST *)((UINT8 *)CertList + CertList->SignatureListSize);
  }

Done:

  if (VerifyStatus) {
    SecureBootHook (EFI_IMAGE_SECURITY_DATABASE, &gEfiImageSecurityDatabaseGuid, CertList->SignatureSize, CertData);
  }

  if (Data != NULL) {
    FreePool (Data);
  }

  if (DbxData != NULL) {
    FreePool (DbxData);
  }

  return VerifyStatus;
}

/**
  Check whether signature is in specified database.

  @param[in]  FileBase            Pointer to the file content.
  @param[in]  FileSize            Size of the file.
  @param[inout] SecDataDir        POinter to a pointer to the Image Security Directory.

  @retval EFI_SUCCESS             Parsed the image without any error.
  @retval Others                  Error occurred in the image parsing.

**/
EFI_STATUS
GetImageSecDataDir (
  IN     VOID                      *FileBase,
  IN     UINTN                     FileSize,
  IN OUT EFI_IMAGE_DATA_DIRECTORY  **SecDataDir
  )
{
  UINT32                          NumberOfRvaAndSizes;
  EFI_IMAGE_OPTIONAL_HEADER_UNION *NtHeader;

  if (SecDataDir == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  NtHeader = GetImageNtHeader (FileBase, FileSize);
  if (NtHeader == NULL) {
    return EFI_NOT_FOUND;
  }

  if (NtHeader->Pe32.OptionalHeader.Magic == EFI_IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
    //
    // Use PE32 offset.
    //
    NumberOfRvaAndSizes = NtHeader->Pe32.OptionalHeader.NumberOfRvaAndSizes;
    if (NumberOfRvaAndSizes > EFI_IMAGE_DIRECTORY_ENTRY_SECURITY) {
      *SecDataDir = (EFI_IMAGE_DATA_DIRECTORY *)&NtHeader->Pe32.OptionalHeader.DataDirectory[EFI_IMAGE_DIRECTORY_ENTRY_SECURITY];
    }
  } else {
    //
    // Use PE32+ offset.
    //
    NumberOfRvaAndSizes = NtHeader->Pe32Plus.OptionalHeader.NumberOfRvaAndSizes;
    if (NumberOfRvaAndSizes > EFI_IMAGE_DIRECTORY_ENTRY_SECURITY) {
      *SecDataDir = (EFI_IMAGE_DATA_DIRECTORY *)&NtHeader->Pe32Plus.OptionalHeader.DataDirectory[EFI_IMAGE_DIRECTORY_ENTRY_SECURITY];
    }
  }

  return EFI_SUCCESS;
}
