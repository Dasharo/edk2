/** @file
  The library header file, defines functions used by ImageVerificationLib.

Copyright (c) 2009 - 2014, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef __DXEIMAGEVERIFICATIONLIB_H__
#define __DXEIMAGEVERIFICATIONLIB_H__

#include <Guid/ImageAuthentication.h>
#include <IndustryStandard/PeImage.h>
#include <Library/BaseCryptLib.h>
//
// Set max digest size as SHA512 Output (64 bytes) by far
//
#define MAX_DIGEST_SIZE  SHA512_DIGEST_SIZE

//
// Support hash types
//
#define HASHALG_SHA1    0x00000000
#define HASHALG_SHA224  0x00000001
#define HASHALG_SHA256  0x00000002
#define HASHALG_SHA384  0x00000003
#define HASHALG_SHA512  0x00000004
#define HASHALG_MAX     0x00000005
// Used only by SecureBootConfigDxe
#define HASHALG_RAW     0x00000006

//
//
// PKCS7 Certificate definition
//
typedef struct {
  WIN_CERTIFICATE    Hdr;
  UINT8              CertData[1];
} WIN_CERTIFICATE_EFI_PKCS;

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
  );

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
  );

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
  );

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
  );

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
  );

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
  );

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
  IN  EFI_SIGNATURE_LIST  *SignatureList,
  IN  UINTN               SignatureListSize,
  OUT EFI_TIME            *RevocationTime,
  OUT BOOLEAN             *IsFound
  );

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
  );

/**
  Get the length of the digest of given hash algorithm.

  @param[in]    HashAlg   Hash algorithm type.

  @retval UINTN           Length of the digest of given hash algorithm.

**/
UINTN
GetDigestLength (
  IN  UINT32  HashAlg
  );

#endif
