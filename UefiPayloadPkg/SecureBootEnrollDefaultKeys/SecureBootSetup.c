/** @file
   Enroll default PK, KEK, DB and DBX

   Copyright (C) 2014, Red Hat, Inc.

   This program and the accompanying materials are licensed and made available
   under the terms and conditions of the BSD License which accompanies this
   distribution. The full text of the license may be found at
   http://opensource.org/licenses/bsd-license.

   THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS, WITHOUT
   WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
 **/

#include <Guid/AuthenticatedVariableFormat.h>
#include <Guid/GlobalVariable.h>
#include <Guid/ImageAuthentication.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

// PK file
#define EFI_PK_SYSTEM76_GUID {0x76939896, 0x813a, 0x48ac, {0xad, 0x94, 0x47, 0xc2, 0xcb, 0xe1, 0xfd, 0xad}}
// KEK files
#define EFI_KEK_MICROSOFT_2011_GUID {0x54fbf0d1, 0x687c, 0x4ede, {0xac, 0xa4, 0x86, 0x5e, 0xe5, 0x7b, 0x41, 0x5a}}
#define EFI_KEK_MICROSOFT_2023_GUID {0x392a8937, 0xa832, 0x476d, {0xae, 0xe9, 0x85, 0x57, 0x90, 0x8c, 0x38, 0x47}}
#define EFI_KEK_SYSTEM76_GUID {0xf9fd279e, 0x5222, 0x46d7, {0x81, 0xf3, 0x6a, 0xd8, 0xd5, 0xdd, 0xfb, 0x62}}
// DB files
#define EFI_CA_WINDOWS_2011_GUID {0x9c823f53, 0x7cc4, 0x4fa9, {0x82, 0xb5, 0x0d, 0xeb, 0x68, 0xe8, 0x55, 0xa5}}
#define EFI_CA_WINDOWS_2023_GUID {0x90fc49dd, 0x13e0, 0x43a9, {0xbf, 0x31, 0x35, 0xe3, 0x08, 0xb4, 0xe0, 0x7d}}
#define EFI_CA_MICROSOFT_2011_GUID {0xb4bf62a6, 0x0f18, 0x48de, {0xbd, 0xdd, 0xba, 0xa5, 0xea, 0x50, 0xbe, 0x1a}}
#define EFI_CA_MICROSOFT_2023_GUID {0x10779d38, 0x00cb, 0x4c02, {0xba, 0x08, 0xa4, 0xe4, 0xc4, 0xc9, 0x98, 0x37}}
#define EFI_CA_SYSTEM76_GUID {0xf953eb13, 0xe915, 0x46d8, {0xaf, 0xcd, 0xa9, 0xd7, 0x52, 0x81, 0x9a, 0xe6}}
// DBX file
#define EFI_UEFI_DBX_GUID {0xa226f3fb, 0x0ba3, 0x481c, {0x9d, 0x1a, 0x6f, 0x21, 0x46, 0x94, 0x4e, 0x2a}}
// Signature Owner
#define EFI_MICROSOFT_OWNER_GUID { 0x77FA9ABD, 0x0359, 0x4D32, {0xBD, 0x60, 0x28, 0xF4, 0xE7, 0x8F, 0x78, 0x4B} }

EFI_GUID mPkSystem76PkGuid = EFI_PK_SYSTEM76_GUID;
EFI_GUID mKekMicrosoft2011Guid = EFI_KEK_MICROSOFT_2011_GUID;
EFI_GUID mKekMicrosoft2023Guid = EFI_KEK_MICROSOFT_2023_GUID;
EFI_GUID mKekSystem76 = EFI_KEK_SYSTEM76_GUID;
EFI_GUID mDbWindows2011Guid = EFI_CA_WINDOWS_2011_GUID;
EFI_GUID mDbWindows2023Guid = EFI_CA_WINDOWS_2023_GUID;
EFI_GUID mDbMicrosoft2011Guid = EFI_CA_MICROSOFT_2011_GUID;
EFI_GUID mDbMicrosoft2023Guid = EFI_CA_MICROSOFT_2023_GUID;
EFI_GUID mDbSystem76Guid = EFI_CA_SYSTEM76_GUID;
EFI_GUID mUefiDbxGuid = EFI_UEFI_DBX_GUID;
EFI_GUID mMicrosoftOwnerGuid = EFI_MICROSOFT_OWNER_GUID;

//
// The most important thing about the variable payload is that it is a list of
// lists, where the element size of any given *inner* list is constant.
//
// Since X509 certificates vary in size, each of our *inner* lists will contain
// one element only (one X.509 certificate). This is explicitly mentioned in
// the UEFI specification, in "28.4.1 Signature Database", in a Note.
//
// The list structure looks as follows:
//
// struct EFI_VARIABLE_AUTHENTICATION_2 {                           |
//   struct EFI_TIME {                                              |
//     UINT16 Year;                                                 |
//     UINT8  Month;                                                |
//     UINT8  Day;                                                  |
//     UINT8  Hour;                                                 |
//     UINT8  Minute;                                               |
//     UINT8  Second;                                               |
//     UINT8  Pad1;                                                 |
//     UINT32 Nanosecond;                                           |
//     INT16  TimeZone;                                             |
//     UINT8  Daylight;                                             |
//     UINT8  Pad2;                                                 |
//   } TimeStamp;                                                   |
//                                                                  |
//   struct WIN_CERTIFICATE_UEFI_GUID {                           | |
//     struct WIN_CERTIFICATE {                                   | |
//       UINT32 dwLength; ----------------------------------------+ |
//       UINT16 wRevision;                                        | |
//       UINT16 wCertificateType;                                 | |
//     } Hdr;                                                     | +- DataSize
//                                                                | |
//     EFI_GUID CertType;                                         | |
//     UINT8    CertData[1] = { <--- "struct hack"                | |
//       struct EFI_SIGNATURE_LIST {                            | | |
//         EFI_GUID SignatureType;                              | | |
//         UINT32   SignatureListSize; -------------------------+ | |
//         UINT32   SignatureHeaderSize;                        | | |
//         UINT32   SignatureSize; ---------------------------+ | | |
//         UINT8    SignatureHeader[SignatureHeaderSize];     | | | |
//                                                            v | | |
//         struct EFI_SIGNATURE_DATA {                        | | | |
//           EFI_GUID SignatureOwner;                         | | | |
//           UINT8    SignatureData[1] = { <--- "struct hack" | | | |
//             X.509 payload                                  | | | |
//           }                                                | | | |
//         } Signatures[];                                      | | |
//       } SigLists[];                                            | |
//     };                                                         | |
//   } AuthInfo;                                                  | |
// };                                                               |
//
// Given that the "struct hack" invokes undefined behavior (which is why C99
// introduced the flexible array member), and because subtracting those pesky
// sizes of 1 is annoying, and because the format is fully specified in the
// UEFI specification, we'll introduce two matching convenience structures that
// are customized for our X.509 purposes.
//

#pragma pack(1)
typedef struct {
  EFI_TIME TimeStamp;

  //
  // dwLength covers data below
  //
  UINT32 dwLength;
  UINT16 wRevision;
  UINT16 wCertificateType;
  EFI_GUID CertType;
} SINGLE_HEADER;

typedef struct {
  //
  // SignatureListSize covers data below
  //
  EFI_GUID SignatureType;
  UINT32 SignatureListSize;
  UINT32 SignatureHeaderSize; // constant 0
  UINT32 SignatureSize;

  //
  // SignatureSize covers data below
  //
  EFI_GUID SignatureOwner;

  //
  // X.509 certificate follows
  //
} REPEATING_HEADER;
#pragma pack()

/**
   Enroll a set of certificates in a global variable, overwriting it.

   The variable will be rewritten with NV+BS+RT+AT attributes.

   @param[in] VariableName  The name of the variable to overwrite.

   @param[in] VendorGuid    The namespace (ie. vendor GUID) of the variable to
                           overwrite.

   @param[in] CertType      The GUID determining the type of all the
                           certificates in the set that is passed in. For
                           example, gEfiCertX509Guid stands for DER-encoded
                           X.509 certificates, while gEfiCertSha256Guid stands
                           for SHA256 image hashes.

   @param[in] ...           A list of

                             IN CONST UINT8    *Cert,
                             IN UINTN          CertSize,
                             IN CONST EFI_GUID *OwnerGuid

                           triplets. If the first component of a triplet is
                           NULL, then the other two components are not
                           accessed, and processing is terminated. The list of
                           certificates is enrolled in the variable specified,
                           overwriting it. The OwnerGuid component identifies
                           the agent installing the certificate.

   @retval EFI_INVALID_PARAMETER  The triplet list is empty (ie. the first Cert
                                 value is NULL), or one of the CertSize values
                                 is 0, or one of the CertSize values would
                                 overflow the accumulated UINT32 data size.

   @retval EFI_OUT_OF_RESOURCES   Out of memory while formatting variable
                                 payload.

   @retval EFI_SUCCESS            Enrollment successful; the variable has been
                                 overwritten (or created).

   @return                        Error codes from gRT->GetTime() and
                                 gRT->SetVariable().
 **/
STATIC
EFI_STATUS
EFIAPI
EnrollListOfCerts (
  IN CHAR16   *VariableName,
  IN EFI_GUID *VendorGuid,
  IN EFI_GUID *CertType,
  ...
  )
{
  UINTN DataSize;
  SINGLE_HEADER    *SingleHeader;
  REPEATING_HEADER *RepeatingHeader;
  VA_LIST Marker;
  CONST UINT8      *Cert;
  EFI_STATUS Status;
  UINT8            *Data;
  UINT8            *Position;

  Status = EFI_SUCCESS;

  //
  // compute total size first, for UINT32 range check, and allocation
  //
  DataSize = sizeof *SingleHeader;
  VA_START (Marker, CertType);
  for (Cert = VA_ARG (Marker, CONST UINT8 *);
       Cert != NULL;
       Cert = VA_ARG (Marker, CONST UINT8 *)) {
    UINTN CertSize;

    CertSize = VA_ARG (Marker, UINTN);
    (VOID)VA_ARG (Marker, CONST EFI_GUID *);

    if (CertSize == 0 ||
        CertSize > MAX_UINT32 - sizeof *RepeatingHeader ||
        DataSize > MAX_UINT32 - sizeof *RepeatingHeader - CertSize) {
      Status = EFI_INVALID_PARAMETER;
      break;
    }
    DataSize += sizeof *RepeatingHeader + CertSize;
  }
  VA_END (Marker);

  if (DataSize == sizeof *SingleHeader) {
    Status = EFI_INVALID_PARAMETER;
  }
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SecureBootSetup: Invalid certificate parameters\n"));
    goto Out;
  }

  Data = AllocatePool (DataSize);
  if (Data == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Out;
  }

  Position = Data;

  SingleHeader = (SINGLE_HEADER *)Position;
  Status = gRT->GetTime (&SingleHeader->TimeStamp, NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_INFO, "SecureBootSetup: GetTime failed\n"));
    // Fill in dummy values
    SingleHeader->TimeStamp.Year       = 2018;
    SingleHeader->TimeStamp.Month      = 1;
    SingleHeader->TimeStamp.Day        = 1;
    SingleHeader->TimeStamp.Hour       = 0;
    SingleHeader->TimeStamp.Minute     = 0;
    SingleHeader->TimeStamp.Second     = 0;
    Status = EFI_SUCCESS;
  }
  SingleHeader->TimeStamp.Pad1       = 0;
  SingleHeader->TimeStamp.Nanosecond = 0;
  SingleHeader->TimeStamp.TimeZone   = 0;
  SingleHeader->TimeStamp.Daylight   = 0;
  SingleHeader->TimeStamp.Pad2       = 0;

  //
  // This looks like a bug in edk2. According to the UEFI specification,
  // dwLength is "The length of the entire certificate, including the length of
  // the header, in bytes". That shouldn't stop right after CertType -- it
  // should include everything below it.
  //
  SingleHeader->dwLength         = sizeof *SingleHeader - sizeof SingleHeader->TimeStamp;
  SingleHeader->wRevision        = 0x0200;
  SingleHeader->wCertificateType = WIN_CERT_TYPE_EFI_GUID;
  CopyGuid (&SingleHeader->CertType, &gEfiCertPkcs7Guid);
  Position += sizeof *SingleHeader;

  VA_START (Marker, CertType);
  for (Cert = VA_ARG (Marker, CONST UINT8 *);
       Cert != NULL;
       Cert = VA_ARG (Marker, CONST UINT8 *)) {
    UINTN CertSize;
    CONST EFI_GUID   *OwnerGuid;

    CertSize  = VA_ARG (Marker, UINTN);
    OwnerGuid = VA_ARG (Marker, CONST EFI_GUID *);

    RepeatingHeader = (REPEATING_HEADER *)Position;
    CopyGuid (&RepeatingHeader->SignatureType, CertType);
    RepeatingHeader->SignatureListSize   =
      (UINT32)(sizeof *RepeatingHeader + CertSize);
    RepeatingHeader->SignatureHeaderSize = 0;
    RepeatingHeader->SignatureSize       =
      (UINT32)(sizeof RepeatingHeader->SignatureOwner + CertSize);
    CopyGuid (&RepeatingHeader->SignatureOwner, OwnerGuid);
    Position += sizeof *RepeatingHeader;

    CopyMem (Position, Cert, CertSize);
    Position += CertSize;
  }
  VA_END (Marker);

  ASSERT (Data + DataSize == Position);

  Status = gRT->SetVariable (VariableName, VendorGuid,
           (EFI_VARIABLE_NON_VOLATILE |
            EFI_VARIABLE_RUNTIME_ACCESS |
            EFI_VARIABLE_BOOTSERVICE_ACCESS |
            EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS),
           DataSize, Data);

  FreePool (Data);

Out:
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SecureBootSetup: %a(\"%s\", %g): %r\n", __FUNCTION__, VariableName,
      VendorGuid, Status));
  }
  return Status;
}


STATIC
EFI_STATUS
EFIAPI
GetExact (
  IN CHAR16   *VariableName,
  IN EFI_GUID *VendorGuid,
  OUT VOID    *Data,
  IN UINTN DataSize,
  IN BOOLEAN AllowMissing
  )
{
  UINTN Size;
  EFI_STATUS Status;

  Size = DataSize;
  Status = gRT->GetVariable (VariableName, VendorGuid, NULL, &Size, Data);
  if (EFI_ERROR (Status)) {
    if (Status == EFI_NOT_FOUND && AllowMissing) {
      ZeroMem (Data, DataSize);
      return EFI_SUCCESS;
    }

    DEBUG ((EFI_D_ERROR, "SecureBootSetup: GetVariable(\"%s\", %g): %r\n", VariableName,
      VendorGuid, Status));
    return Status;
  }

  if (Size != DataSize) {
    DEBUG ((EFI_D_INFO, "SecureBootSetup: GetVariable(\"%s\", %g): expected size 0x%Lx, "
      "got 0x%Lx\n", VariableName, VendorGuid, (UINT64)DataSize, (UINT64)Size));
    return EFI_PROTOCOL_ERROR;
  }

  return EFI_SUCCESS;
}

typedef struct {
  UINT8 SetupMode;
  UINT8 SecureBoot;
  UINT8 SecureBootEnable;
  UINT8 CustomMode;
  UINT8 VendorKeys;
} SETTINGS;

STATIC
EFI_STATUS
EFIAPI
GetSettings (
  OUT SETTINGS *Settings,
  BOOLEAN AllowMissing
  )
{
  EFI_STATUS Status;

  ZeroMem (Settings, sizeof(SETTINGS));

  Status = GetExact (EFI_SETUP_MODE_NAME, &gEfiGlobalVariableGuid,
         &Settings->SetupMode, sizeof Settings->SetupMode, AllowMissing);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = GetExact (EFI_SECURE_BOOT_MODE_NAME, &gEfiGlobalVariableGuid,
         &Settings->SecureBoot, sizeof Settings->SecureBoot, AllowMissing);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = GetExact (EFI_SECURE_BOOT_ENABLE_NAME,
         &gEfiSecureBootEnableDisableGuid, &Settings->SecureBootEnable,
         sizeof Settings->SecureBootEnable, AllowMissing);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = GetExact (EFI_CUSTOM_MODE_NAME, &gEfiCustomModeEnableGuid,
         &Settings->CustomMode, sizeof Settings->CustomMode, AllowMissing);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = GetExact (EFI_VENDOR_KEYS_VARIABLE_NAME, &gEfiGlobalVariableGuid,
         &Settings->VendorKeys, sizeof Settings->VendorKeys, AllowMissing);
  return Status;
}

STATIC
VOID
EFIAPI
PrintSettings (
  IN CONST SETTINGS *Settings
  )
{
  DEBUG ((EFI_D_INFO, "SecureBootSetup: SetupMode=%d SecureBoot=%d SecureBootEnable=%d "
    "CustomMode=%d VendorKeys=%d\n", Settings->SetupMode, Settings->SecureBoot,
    Settings->SecureBootEnable, Settings->CustomMode, Settings->VendorKeys));
}

/**
  Install SecureBoot certificates once the VariableDriver is running.

  @param[in]  Event     Event whose notification function is being invoked
  @param[in]  Context   Pointer to the notification function's context
**/
VOID
EFIAPI
InstallSecureBootHook (
  IN EFI_EVENT                      Event,
  IN VOID                           *Context
  )
{
  EFI_STATUS  Status;
  VOID        *Protocol;
  SETTINGS Settings;

  UINT8 *DbWindows2011 = 0;
  UINTN DbWindows2011Size;
  UINT8 *DbWindows2023 = 0;
  UINTN DbWindows2023Size;
  UINT8 *DbMicrosoft2011 = 0;
  UINTN DbMicrosoft2011Size;
  UINT8 *DbMicrosoft2023 = 0;
  UINTN DbMicrosoft2023Size;
  UINT8 *DbSystem76 = 0;
  UINTN DbSystem76Size;

  UINT8 *KekMicrosoft2011 = 0;
  UINTN KekMicrosoft2011Size;
  UINT8 *KekMicrosoft2023 = 0;
  UINTN KekMicrosoft2023Size;
  UINT8 *KekSystem76 = 0;
  UINTN KekSystem76Size;

  UINT8 *PkSystem76 = 0;
  UINTN PkSystem76Size;

  UINT8 *UefiDbx = 0;
  UINTN UefiDbxSize;


  Status = gBS->LocateProtocol (&gEfiVariableWriteArchProtocolGuid, NULL, (VOID **)&Protocol);
  if (EFI_ERROR (Status)) {
    return;
  }

  Status = GetSettings (&Settings, TRUE);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SecureBootSetup: Failed to get current settings\n"));
    return;
  }

  if (Settings.SetupMode != SETUP_MODE) {
    DEBUG ((EFI_D_ERROR, "SecureBootSetup: already in User Mode\n"));
    return;
  }
  PrintSettings (&Settings);

  if (Settings.CustomMode != CUSTOM_SECURE_BOOT_MODE) {
    Settings.CustomMode = CUSTOM_SECURE_BOOT_MODE;
    Status = gRT->SetVariable (EFI_CUSTOM_MODE_NAME, &gEfiCustomModeEnableGuid,
             (EFI_VARIABLE_NON_VOLATILE |
              EFI_VARIABLE_BOOTSERVICE_ACCESS),
             sizeof Settings.CustomMode, &Settings.CustomMode);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "SecureBootSetup: SetVariable(\"%s\", %g): %r\n", EFI_CUSTOM_MODE_NAME,
        &gEfiCustomModeEnableGuid, Status));
      ASSERT_EFI_ERROR (Status);
    }
  }

  Status = GetSectionFromAnyFv(&mDbWindows2011Guid, EFI_SECTION_RAW, 0, (void **)&DbWindows2011, &DbWindows2011Size);
  ASSERT_EFI_ERROR (Status);
  Status = GetSectionFromAnyFv(&mDbWindows2023Guid, EFI_SECTION_RAW, 0, (void **)&DbWindows2023, &DbWindows2023Size);
  ASSERT_EFI_ERROR (Status);
  Status = GetSectionFromAnyFv(&mDbMicrosoft2011Guid, EFI_SECTION_RAW, 0, (void **)&DbMicrosoft2011, &DbMicrosoft2011Size);
  ASSERT_EFI_ERROR (Status);
  Status = GetSectionFromAnyFv(&mDbMicrosoft2023Guid, EFI_SECTION_RAW, 0, (void **)&DbMicrosoft2023, &DbMicrosoft2023Size);
  ASSERT_EFI_ERROR (Status);
  Status = GetSectionFromAnyFv(&mDbSystem76Guid, EFI_SECTION_RAW, 0, (void **)&DbSystem76, &DbSystem76Size);
  ASSERT_EFI_ERROR (Status);

  Status = GetSectionFromAnyFv(&mKekMicrosoft2011Guid, EFI_SECTION_RAW, 0, (void **)&KekMicrosoft2011, &KekMicrosoft2011Size);
  ASSERT_EFI_ERROR (Status);
  Status = GetSectionFromAnyFv(&mKekMicrosoft2023Guid, EFI_SECTION_RAW, 0, (void **)&KekMicrosoft2023, &KekMicrosoft2023Size);
  ASSERT_EFI_ERROR (Status);
  Status = GetSectionFromAnyFv(&mKekSystem76, EFI_SECTION_RAW, 0, (void **)&KekSystem76, &KekSystem76Size);
  ASSERT_EFI_ERROR (Status);

  Status = GetSectionFromAnyFv(&mPkSystem76PkGuid, EFI_SECTION_RAW, 0, (void **)&PkSystem76, &PkSystem76Size);
  ASSERT_EFI_ERROR (Status);

  Status = GetSectionFromAnyFv(&mUefiDbxGuid, EFI_SECTION_RAW, 0, (void **)&UefiDbx, &UefiDbxSize);
  ASSERT_EFI_ERROR (Status);

  Status = gRT->SetVariable (EFI_IMAGE_SECURITY_DATABASE1, &gEfiImageSecurityDatabaseGuid,
           (EFI_VARIABLE_NON_VOLATILE |
            EFI_VARIABLE_RUNTIME_ACCESS |
            EFI_VARIABLE_BOOTSERVICE_ACCESS |
            EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS),
           UefiDbxSize, UefiDbx);
  ASSERT_EFI_ERROR (Status);

  Status = EnrollListOfCerts (
    EFI_IMAGE_SECURITY_DATABASE,
    &gEfiImageSecurityDatabaseGuid,
    &gEfiCertX509Guid,
    DbWindows2011, DbWindows2011Size, &mMicrosoftOwnerGuid,
    DbWindows2023, DbWindows2023Size, &mMicrosoftOwnerGuid,
    DbMicrosoft2011, DbMicrosoft2011Size, &mMicrosoftOwnerGuid,
    DbMicrosoft2023, DbMicrosoft2023Size, &mMicrosoftOwnerGuid,
    DbSystem76, DbSystem76Size, &gEfiCallerIdGuid,
    NULL);
  ASSERT_EFI_ERROR (Status);

  Status = EnrollListOfCerts (
    EFI_KEY_EXCHANGE_KEY_NAME,
    &gEfiGlobalVariableGuid,
    &gEfiCertX509Guid,
    KekMicrosoft2011, KekMicrosoft2011Size, &mMicrosoftOwnerGuid,
    KekMicrosoft2023, KekMicrosoft2023Size, &mMicrosoftOwnerGuid,
    KekSystem76, KekSystem76Size, &gEfiCallerIdGuid,
    NULL);
  ASSERT_EFI_ERROR (Status);

  Status = EnrollListOfCerts (
    EFI_PLATFORM_KEY_NAME,
    &gEfiGlobalVariableGuid,
    &gEfiCertX509Guid,
    PkSystem76, PkSystem76Size, &gEfiGlobalVariableGuid,
    NULL);
  ASSERT_EFI_ERROR (Status);

  FreePool (DbWindows2011);
  FreePool (DbWindows2023);
  FreePool (DbMicrosoft2011);
  FreePool (DbMicrosoft2023);
  FreePool (DbSystem76);
  FreePool (KekMicrosoft2011);
  FreePool (KekMicrosoft2023);
  FreePool (KekSystem76);
  FreePool (PkSystem76);
  FreePool (UefiDbx);

  Settings.CustomMode = STANDARD_SECURE_BOOT_MODE;
  Status = gRT->SetVariable (EFI_CUSTOM_MODE_NAME, &gEfiCustomModeEnableGuid,
           EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
           sizeof Settings.CustomMode, &Settings.CustomMode);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SecureBootSetup: SetVariable(\"%s\", %g): %r\n", EFI_CUSTOM_MODE_NAME,
      &gEfiCustomModeEnableGuid, Status));
    ASSERT_EFI_ERROR (Status);
  }

  // FIXME: Force SecureBoot to ON. The AuthService will do this if authenticated variables
  // are supported, which aren't as the SMM handler isn't able to verify them.

  Settings.SecureBootEnable = SECURE_BOOT_DISABLE;
  Status = gRT->SetVariable (EFI_SECURE_BOOT_ENABLE_NAME, &gEfiSecureBootEnableDisableGuid,
           EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
           sizeof Settings.SecureBootEnable, &Settings.SecureBootEnable);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SecureBootSetup: SetVariable(\"%s\", %g): %r\n", EFI_SECURE_BOOT_ENABLE_NAME,
      &gEfiSecureBootEnableDisableGuid, Status));
    ASSERT_EFI_ERROR (Status);
  }

  Settings.SecureBoot = SECURE_BOOT_DISABLE;
  Status = gRT->SetVariable (EFI_SECURE_BOOT_MODE_NAME, &gEfiGlobalVariableGuid,
           EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
           sizeof Settings.SecureBoot, &Settings.SecureBoot);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SecureBootSetup: SetVariable(\"%s\", %g): %r\n", EFI_SECURE_BOOT_MODE_NAME,
      &gEfiGlobalVariableGuid, Status));
    ASSERT_EFI_ERROR (Status);
  }

  Status = GetSettings (&Settings, FALSE);
  ASSERT_EFI_ERROR (Status);

  //
  // Final sanity check:
  //
  //                                 [SetupMode]
  //                        (read-only, standardized by UEFI)
  //                                /                \_
  //                               0               1, default
  //                              /                    \_
  //                      PK enrolled                   no PK enrolled yet,
  //              (this is called "User Mode")          PK enrollment possible
  //                             |
  //                             |
  //                     [SecureBootEnable]
  //         (read-write, edk2-specific, boot service only)
  //                /                           \_
  //               0                         1, default
  //              /                               \_
  //       [SecureBoot]=0                     [SecureBoot]=1
  // (read-only, standardized by UEFI)  (read-only, standardized by UEFI)
  //     images are not verified         images are verified, platform is
  //                                      operating in Secure Boot mode
  //                                                 |
  //                                                 |
  //                                           [CustomMode]
  //                          (read-write, edk2-specific, boot service only)
  //                                /                           \_
  //                          0, default                         1
  //                              /                               \_
  //                      PK, KEK, db, dbx                PK, KEK, db, dbx
  //                    updates are verified          updates are not verified
  //

  PrintSettings (&Settings);

  if (Settings.SetupMode != 0 || Settings.SecureBoot != 1 ||
      Settings.SecureBootEnable != 1 || Settings.CustomMode != 0 ||
      Settings.VendorKeys != 0) {
    DEBUG ((EFI_D_ERROR, "SecureBootSetup: disabled\n"));
    return;
  }

  DEBUG ((EFI_D_INFO, "SecureBootSetup: SecureBoot enabled\n"));
}

EFI_STATUS
EFIAPI
DriverEntry (
  IN EFI_HANDLE ImageHandle,
  IN EFI_SYSTEM_TABLE            *SystemTable
  )
{
  EFI_STATUS Status;

  VOID *TcgProtocol;
  VOID *Registration;

  Status = gBS->LocateProtocol (&gEfiTcgProtocolGuid, NULL, (VOID **) &TcgProtocol);
  if (!EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "SecureBootSetup: Started too late."
      "TPM is already running!\n"));
    return EFI_DEVICE_ERROR;
  }

  //
  // Create event callback, because we need access variable on SecureBootPolicyVariable
  // We should use VariableWriteArch instead of VariableArch, because Variable driver
  // may update SecureBoot value based on last setting.
  //
  EfiCreateProtocolNotifyEvent (
    &gEfiVariableWriteArchProtocolGuid,
    TPL_CALLBACK,
    InstallSecureBootHook,
    NULL,
    &Registration);

  return EFI_SUCCESS;
}
