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

#define NAME_VALUE_NAME_NUMBER  3

#define DEFAULT_CLASS_MANUFACTURING_VALUE  0xFF
#define DEFAULT_CLASS_STANDARD_VALUE       0x0

#define SOVEREIGN_BOOT_PRIVATE_SIGNATURE          SIGNATURE_32 ('S', 'B', 'p', 's')
#define SOVEREIGN_BOOT_MENU_OPTION_SIGNATURE      SIGNATURE_32 ('m', 'e', 'n', 'u')
#define SOVEREIGN_BOOT_MENU_ENTRY_SIGNATURE       SIGNATURE_32 ('e', 'n', 't', 'r')
#define SOVEREIGN_BOOT_CERT_ENTRY_SIGNATURE       SIGNATURE_32 ('c', 'e', 'r', 't')

#define SOVEREIGN_BOOT_LOAD_CONTEXT_SELECT      0x0
#define SOVEREIGN_BOOT_FILE_CONTEXT_SELECT      0x2

typedef struct {
  UINTN                                  Signature;

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

#pragma pack()

typedef struct {
  UINTN         Signature;
  LIST_ENTRY    Head;
  UINTN         MenuNumber;
} BM_MENU_OPTION;

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
  VOID             *VariableContext; // BM_LOAD_CONTEXT
  VOID             *SecurityContext; // BM_SECURITY_CONTEXT
} BM_MENU_ENTRY;

typedef struct {
  BOOLEAN                     IsBootNext;
  BOOLEAN                     NeedsPathExpansion;

  BOOLEAN                     IsLegacy;
  BOOLEAN                     IsFvOption;

  UINT32                      Attributes;
  UINT16                      FilePathLength;
  UINT16                      *Description;
  EFI_DEVICE_PATH_PROTOCOL    *FilePath;
} BM_LOAD_CONTEXT;

typedef struct {
  UINT32                      Signature;

  BOOLEAN                     CertIsInDbx;
  BOOLEAN                     CertIsInDb;
  BOOLEAN                     ImageIsVerified;

  UINTN                       CertDataSize;
  UINT8                       *CertData;

  UINTN                       CertDigestSize;
  UINT8                       *CertDigest;

  EFI_GUID                    CertType;

  LIST_ENTRY                  CertLink;
} BM_CERT_ENTRY;

typedef struct {
  BOOLEAN                     ImageIsInDbx;
  BOOLEAN                     ImageIsInDb;
  BOOLEAN                     ImageIsSigned;

  UINT32                      AuthenticationStatus;
  UINT32                      NumCertificates;

  UINTN                       ImageDigestSize;
  UINT8                       *ImageDigest;

  LIST_ENTRY                  Certs;
} BM_SECURITY_CONTEXT;

typedef struct {
  EFI_HANDLE                      Handle;
  EFI_DEVICE_PATH_PROTOCOL        *DevicePath;
  EFI_FILE_HANDLE                 FHandle;
  UINT16                          *FileName;
  EFI_FILE_SYSTEM_VOLUME_LABEL    *Info;

  BOOLEAN                         IsRoot;
  BOOLEAN                         IsDir;
  BOOLEAN                         IsRemovableMedia;
  BOOLEAN                         IsLoadFile;
  BOOLEAN                         IsBootLegacy;
} BM_FILE_CONTEXT;

extern BM_MENU_OPTION            BootOptionMenu;
extern UINTN                     mBootloaderIndex;
extern UINTN                     mCertIndex;

EFI_STATUS
GetBootOptions (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private
  );

BM_MENU_ENTRY *
GetMenuEntry (
  BM_MENU_OPTION  *MenuOption,
  UINTN           MenuNumber
  );

EFI_STATUS
UpdateBootloaderPage (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private
  );

EFI_STATUS
FillSecurityContext (
  IN  BM_MENU_ENTRY  *Entry
  );

BM_CERT_ENTRY *
GetCertEntry (
  BM_MENU_ENTRY  *MenuEntry,
  UINTN           CertNumber
  );

EFI_STATUS
UpdateCertInfo (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN  UINTN                               OptionNumber,
  IN  UINTN                               CertNumber
  );

#endif
