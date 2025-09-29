/** @file

Copyright (c) 2025, 3mdeb Sp z o.o. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

Module Name:

  SovereignBootWizard.h

Abstract:


Revision History


**/

#ifndef _SV_BOOT_WIZARD_H_
#define _SV_BOOT_WIZARD_H_

#include <Uefi.h>

#include <PiDxe.h>
#include <Pi/PiBootMode.h>
#include <Pi/PiHob.h>

#include <Protocol/HiiConfigRouting.h>
#include <Protocol/FormBrowser2.h>
#include <Protocol/HiiConfigAccess.h>
#include <Protocol/HiiDatabase.h>
#include <Protocol/HiiString.h>
#include <Protocol/FormBrowserEx2.h>
#include <Protocol/HiiConfigKeyword.h>
#include <Protocol/HiiPopup.h>
#include <Protocol/DevicePathToText.h>

#include <Guid/MdeModuleHii.h>
#include <Guid/ImageAuthentication.h>
#include <Guid/FileSystemVolumeLabelInfo.h>
#include <Guid/AuthenticatedVariableFormat.h>

#include <UefiSecureBoot.h>

#include <Library/DebugLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseCryptLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/HobLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/HiiLib.h>
#include <Library/DevicePathLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiLib.h>
#include <Library/PeCoffLib.h>
#include <Library/SecureBootVariableLib.h>
#include <Library/SecureBootVariableProvisionLib.h>
#include <Library/DxeImageVerificationLib.h>

#include "SovereignBootWizardHii.h"

extern UINT8  SovereignBootWizardVfrBin[];
extern UINT8  SovereignBootWizardStrings[];

typedef struct {
  CONST UINT32  CertLength;
  CONST UINT8   *CertData;
  CONST UINT8   CertHash[SHA256_DIGEST_SIZE];
} CERT_PTR;

extern CONST UINTN MicrosoftCertificatesArraySize;
extern CONST CERT_PTR MicrosoftCertificates[];

#define NAME_VALUE_NAME_NUMBER  3

#define DEFAULT_CLASS_MANUFACTURING_VALUE  0xFF
#define DEFAULT_CLASS_STANDARD_VALUE       0x0

#define SOVEREIGN_BOOT_PRIVATE_SIGNATURE          SIGNATURE_32 ('S', 'B', 'p', 's')
#define SOVEREIGN_BOOT_MENU_OPTION_SIGNATURE      SIGNATURE_32 ('m', 'e', 'n', 'u')
#define SOVEREIGN_BOOT_MENU_ENTRY_SIGNATURE       SIGNATURE_32 ('e', 'n', 't', 'r')
#define SOVEREIGN_BOOT_CERT_ENTRY_SIGNATURE       SIGNATURE_32 ('c', 'e', 'r', 't')

#define SOVEREIGN_BOOT_LOAD_CONTEXT_SELECT      0x0
#define SOVEREIGN_BOOT_FILE_CONTEXT_SELECT      0x2

#define FREE_NON_NULL(Pointer)  \
  do {                                \
    if ((Pointer) != NULL) {          \
      DEBUG ((DEBUG_INFO, "Freeing " #Pointer "\n"));  \
      FreePool((Pointer));            \
      (Pointer) = NULL;               \
    }                                 \
  } while(FALSE)

typedef struct {
  UINT32                                 Signature;

  EFI_HANDLE                             AppHandle;
  EFI_HII_HANDLE                         HiiHandle;
  SOVEREIGN_BOOT_WIZARD_CONFIG_DATA      ConfigData;
  SOVEREIGN_BOOT_WIZARD_NV_CONFIG        NvConfig;
  SOVEREIGN_BOOT_WIZARD_FORM_DATA        FormData;

  EFI_STRING_ID                          NameStringId[NAME_VALUE_NAME_NUMBER];
  EFI_STRING                             NameValueName[NAME_VALUE_NAME_NUMBER];

  EFI_HII_DATABASE_PROTOCOL              *HiiDatabase;
  EFI_HII_STRING_PROTOCOL                *HiiString;
  EFI_HII_CONFIG_ROUTING_PROTOCOL        *HiiConfigRouting;
  EFI_CONFIG_KEYWORD_HANDLER_PROTOCOL    *HiiKeywordHandler;
  EFI_HII_POPUP_PROTOCOL                 *HiiPopup;

  EFI_FORM_BROWSER2_PROTOCOL             *FormBrowser2;
  EDKII_FORM_BROWSER_EXTENSION2_PROTOCOL *FormBrowserEx2;

  EFI_HII_CONFIG_ACCESS_PROTOCOL         ConfigAccess;

  EFI_DEVICE_PATH_TO_TEXT_PROTOCOL       *DevPathToText;
} SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA;

#define SOVEREIGN_BOOT_WIZARD_PRIVATE_FROM_THIS(a)  CR (a, SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA, ConfigAccess, SOVEREIGN_BOOT_PRIVATE_SIGNATURE)

#pragma pack(1)

typedef struct {
  VENDOR_DEVICE_PATH          VendorDevicePath;
  EFI_DEVICE_PATH_PROTOCOL    End;
} HII_VENDOR_DEVICE_PATH;

// YYMMDDHHMMSS Z
typedef struct {
  CHAR8 Year[2];
  CHAR8 Month[2];
  CHAR8 Day[2];
  CHAR8 Hour[2];
  CHAR8 Minute[2];
  CHAR8 Seconds[2];
} ASN1_UTC_TIME;

typedef struct {
  CHAR8 Year[4];
  CHAR8 Month[2];
  CHAR8 Day[2];
  CHAR8 Hour[2];
  CHAR8 Minute[2];
  CHAR8 Seconds[2];
} ASN1_GENERALIZED_TIME;

typedef struct {
  CHAR8 Sign;
  CHAR8 Hour[2];
  CHAR8 Minute[2];
} ASN1_TIMEZONE;

#pragma pack()

#define ASN1_TYPE_UTC_TIME 0x17
#define ASN1_TYPE_GENERALIZED_TIME 0x18

typedef union {
  ASN1_UTC_TIME *UtcTime;
  ASN1_GENERALIZED_TIME *GeneralizedTime;
} ASN1_TIME_PTR_UNION;

#define ASN1_FLAG_MSTRING 0x40
typedef struct {
    INT32 Length;
    INT32 Type;
    ASN1_TIME_PTR_UNION Data;
    /*
     * The value of the following field depends on the type being held.  It
     * is mostly being used for BIT_STRING so if the input data has a
     * non-zero 'unused bits' value, it will be handled correctly
     */
    UINT64 Flags;
} OPENSSL_ASN1_TIME;

typedef struct {
  UINTN         Signature;
  LIST_ENTRY    Head;
  UINTN         MenuNumber;
} SV_MENU_OPTION;

typedef struct {
  UINTN            Signature;
  LIST_ENTRY       Link;
  UINTN            OptionNumber;
  UINT16           *DisplayString;
  UINT16           *DevicePathString;
  UINT16           *FilePathString;
  EFI_STRING_ID    DisplayStringToken;
  EFI_STRING_ID    DevicePathStringToken;
  EFI_STRING_ID    FilePathStringToken;
  UINTN            ContextSelection;
  VOID             *VariableContext; // SV_LOAD_CONTEXT
  VOID             *SecurityContext; // SV_SECURITY_CONTEXT
} SV_MENU_ENTRY;

typedef struct {
  BOOLEAN                     IsBootNext;
  BOOLEAN                     NeedsPathExpansion;

  BOOLEAN                     IsLegacy;
  BOOLEAN                     IsFvOption;

  UINT32                      Attributes;
  UINT16                      FilePathLength;
  UINT16                      *Description;
  EFI_DEVICE_PATH_PROTOCOL    *FilePath;
  UINT8                       *OptionalData;
  UINTN                       OptionalDataSize;
} SV_LOAD_CONTEXT;

typedef struct {
  UINT32                      Signature;

  BOOLEAN                     CertIsInDbx;
  BOOLEAN                     CertIsInDb;
  BOOLEAN                     SignatureValid;
  BOOLEAN                     CertIsValid;
  BOOLEAN                     CertIsMicrosoft;
  BOOLEAN                     CertIsCA;

  UINT8                       CertDigest[MAX_DIGEST_SIZE];
  UINTN                       CertDigestSize;

  UINTN                       CertDataSize;
  UINT8                       *CertData;

  EFI_GUID                    CertType;

  LIST_ENTRY                  CertLink;
} SV_CERT_ENTRY;

typedef struct {
  UINT8                       ImageDigest[MAX_DIGEST_SIZE];
  UINTN                       ImageDigestSize;

  BOOLEAN                     ImageIsInDbx;
  BOOLEAN                     ImageIsInDb;
  BOOLEAN                     ImageIsSigned;
  BOOLEAN                     ImageIsVerified;

  UINT32                      AuthenticationStatus;
  UINT32                      NumCertificates;

  EFI_GUID                    HashType;

  LIST_ENTRY                  Certs;
} SV_SECURITY_CONTEXT;

extern SV_MENU_OPTION            BootOptionMenu;
extern UINTN                     mBootloaderIndex;
extern UINTN                     mCertIndex;
extern INTN                      mFirstTrustedBootloader;

EFI_STATUS
GetBootOptions (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private
  );

EFI_STATUS
ScanFileSystemsForBootOptions (
  IN     SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN OUT SV_MENU_OPTION                      *MenuOption
  );

EFI_STATUS
FillMenuEntryFromDevicePath (
  IN     SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN     EFI_HANDLE                          DeviceHandle,
  IN     EFI_DEVICE_PATH_PROTOCOL            *DevicePath,
  IN OUT SV_MENU_ENTRY                       **MenuEntry
  );

VOID
ToLowerString (
  IN CHAR16  *String
  );

BOOLEAN
CheckIfEntryIsDuplicate (
  IN SV_MENU_ENTRY *MenuEntry
  );

SV_MENU_ENTRY *
GetMenuEntry (
  SV_MENU_OPTION  *MenuOption,
  UINTN           MenuNumber
  );

VOID
FreeBootMenuEntry (
  SV_MENU_ENTRY    *BootloaderEntry
  );

EFI_STATUS
UpdateBootloaderPage (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private
  );

EFI_STATUS
FillSecurityContext (
  IN  SV_MENU_ENTRY  *Entry
  );

SV_CERT_ENTRY *
GetCertEntry (
  SV_MENU_ENTRY  *MenuEntry,
  UINTN           CertNumber
  );

EFI_STATUS
UpdateCertInfo (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN  UINTN                               OptionNumber
  );

EFI_STATUS
UpdateCertDetails (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private
  );

EFI_STATUS
RestoreSecureBootDefaults (
  VOID
  );

EFI_STATUS
PrepareSbVariablesForSvBoot (
  VOID
  );

EFI_STATUS
AddKeyOrHashAsTrustedOrUntrusted (
  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA   *PrivateData,
  BOOLEAN                              Trust
  );

EFI_STATUS
FinalizeSvBootProvisioning (
  VOID
  );

BOOLEAN
Asn1TimeToEfiTime (
  IN     OPENSSL_ASN1_TIME *Asn1Time,
  IN OUT EFI_TIME          *EfiTime
  );

OPENSSL_ASN1_TIME *
EfiTimeToAsn1Time (
  IN EFI_TIME              *EfiTime
  );

VOID
FormatAsn1Time (
  IN     OPENSSL_ASN1_TIME *Time,
  IN OUT CHAR16            *DateBuffer,
  IN     UINTN             DateBufferSize
  );

EFI_STATUS
GetCurrentTime (
  IN EFI_TIME  *Time
  );

VOID
FreeBootMenuEntries (
  VOID
  );

VOID
FreeSecurityContext (
  SV_SECURITY_CONTEXT  *SecCtx
  );

#endif
