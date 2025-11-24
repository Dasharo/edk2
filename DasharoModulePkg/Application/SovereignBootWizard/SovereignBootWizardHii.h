/** @file

Copyright (c) 2025, 3mdeb Sp. z o.o. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

Module Name:

  SovereignBootWizardHii.h

Abstract:

  HII Data used by the Sovereign Boot Wizard application

Revision History:


**/

#ifndef SOVEREIGN_BOOT_WIZARD_HII_H_
#define SOVEREIGN_BOOT_WIZARD_HII_H_

#include <Guid/HiiPlatformSetupFormset.h>
#include <Guid/HiiFormMapMethodGuid.h>
#include <Guid/SovereignBoot.h>
#include <Guid/ZeroGuid.h>

#define SOVEREIGN_BOOT_WIZARD_FORM_DATA_VARSTORE_ID     0x0001

#define SOVEREIGN_BOOT_WIZARD_WELCOME_FORM_ID           0x1
#define SOVEREIGN_BOOT_WIZARD_CONFIG_FORM_ID            0x2
#define SOVEREIGN_BOOT_WIZARD_MS_SECURE_BOOT_FORM_ID    0x3
#define SOVEREIGN_BOOT_WIZARD_KEY_DETAILS_FORM_ID       0x4
#define SOVEREIGN_BOOT_WIZARD_INTERACTIVE_MODE_FORM_ID  0x5
#define FORMID_SOVEREIGN_BOOT_DB_OPTION_FORM            0x6
#define FORMID_SOVEREIGN_BOOT_DBX_OPTION_FORM           0x7
#define SOVEREIGN_BOOT_ENROLL_SIGNATURE_TO_DB           0x8
#define SOVEREIGN_BOOT_DELETE_SIGNATURE_FROM_DB         0x9
#define SOVEREIGN_BOOT_ENROLL_SIGNATURE_TO_DBX          0xa
#define SOVEREIGN_BOOT_DELETE_SIGNATURE_LIST_FORM       0xb
#define SOVEREIGN_BOOT_DELETE_SIGNATURE_DATA_FORM       0xc

// Question IDs
// Each form will reserve 0x100 IDs
#define SOVEREIGN_BOOT_WIZARD_FORM_QUESTION_ID_BASE     0x1000

// Welcome form
#define WELCOME_FORM_QUESTION_ID_BASE                   0x1100
#define SELECT_SOVEREIGN_BOOT_QUESTION_ID               0x1101
#define SELECT_DEFAULT_SECURE_BOOT_QUESTION_ID          0x1102

#define EXIT_FORM_QUESTION_ID_BASE                      0x1F00
#define KEY_EXIT_FORM1                                  0x1F01
#define KEY_EXIT_FORM2                                  0x1F02
#define KEY_EXIT_FORM3                                  0x1F03
#define KEY_EXIT_FORM5                                  0x1F05

// Configuration form
#define CONFIG_FORM_QUESTION_ID_BASE                    0x1200
#define KEY_DO_NOT_TRUST_KEY_FORM2                      0x1201
#define KEY_TRUST_KEY_AND_BOOT_FORM2                    0x1202
#define KEY_TRUST_KEY_FORM2                             0x1203
#define KEY_SHOW_KEY_DETAILS_FORM2                      0x1204
#define KEY_SKIP_KEY_FORM2                              0x1205

// Interactive form
#define KEY_VALUE_SAVE_AND_EXIT_DB                      0x1F0A
#define KEY_VALUE_NO_SAVE_AND_EXIT_DB                   0x1F0B
#define KEY_VALUE_SAVE_AND_EXIT_DBX                     0x1F0C
#define KEY_VALUE_NO_SAVE_AND_EXIT_DBX                  0x1F0D
#define KEY_VALUE_FROM_DBX_TO_LIST_FORM                 0x1F0E

#define KEY_SOVEREIGN_BOOT_DB_OPTION                    0x1306
#define KEY_SOVEREIGN_BOOT_DBX_OPTION                   0x1307
#define KEY_SOVEREIGN_BOOT_SIGNATURE_GUID_DB            0x1308
#define KEY_SOVEREIGN_BOOT_SIGNATURE_GUID_DBX           0x1309
#define KEY_SOVEREIGN_BOOT_DELETE_ALL_LIST              0x130a
#define KEY_SOVEREIGN_BOOT_DELETE_ALL_DATA              0x130b
#define KEY_SOVEREIGN_BOOT_DELETE_CHECK_DATA            0x130c

#define LABEL_DB_DELETE                                 0x1401
#define LABEL_SIGNATURE_LIST_START                      0x1402
#define LABEL_SIGNATURE_DATA_START                      0x1403
#define LABEL_DELETE_ALL_LIST_BUTTON                    0x1500
#define LABEL_END                                       0xffff

#define OPTION_CONFIG_RANGE                             0x1000

//
// Question ID 0x2000 ~ 0x2FFF is for bootloaders
//
#define OPTION_BL_QUESTION_ID                           0x2000
//
// Question ID 0x3000 ~ 0x3FFF is for bootloader certificates
//
#define OPTION_BL_CERT_QUESTION_ID                      0x3000
//
// Question ID 0x4000 ~ 0x4FFF is for DB
//
#define OPTION_DEL_DB_QUESTION_ID                       0x4000
//
// Question ID 0x5000 ~ 0x5FFF is for signature list.
//
#define OPTION_SIGNATURE_LIST_QUESTION_ID               0x5000
//
// Question ID 0x6000 ~ 0x6FFF is for signature data.
//
#define OPTION_SIGNATURE_DATA_QUESTION_ID               0x6000

// Keep the form data packed to workaround the storage size calculation
// difference in C and IFR for EFI_HII_TIME
#pragma pack(1)
// Form Data
typedef struct {
  BOOLEAN         ImageUnsigned;     // If the image is unsigned.
  BOOLEAN         AlwaysRevocation;  // If the certificate is always revoked. Revocation time is hidden
  UINT8           CertificateFormat; // The type of the certificate
  EFI_HII_DATE    RevocationDate;    // The revocation date of the certificate
  EFI_HII_TIME    RevocationTime;    // The revocation time of the certificate
  UINT8           FileEnrollType;    // File type of signature enroll
  UINT32          ListCount;         // The count of signature list.
  UINT32          CheckedDataCount;  // The count of checked signature data.
} SOVEREIGN_BOOT_WIZARD_FORM_DATA;

#pragma pack()

#endif
