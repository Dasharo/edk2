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
#define SOVEREIGN_BOOT_WIZARD_HASH_DETAILS_FORM_ID      0xd
#define FORMID_SOVEREIGN_BOOT_BL_OPTION_FORM            0xe
#define SOVEREIGN_BOOT_WIZARD_BL_DETAILS_FORM_ID        0xf

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

#define KEY_REMOVE_KEY_FROM_DATABASE                    0x1D01
#define KEY_REMOVE_HASH_FROM_DATABASE                   0x1D02
#define KEY_REMOVE_CERT_FROM_DATABASE                   0x1D03

#define SIGNATURE_TYPE_QUESTION_ID                      0x1D10

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
#define KEY_ENROLL_SIGNATURE_TO_DB                      0x130d
#define KEY_ENROLL_SIGNATURE_TO_DBX                     0x130e
#define KEY_SOVEREIGN_BOOT_BL_OPTION                    0x130f

#define LABEL_DB_DELETE                                 0x1401
#define LABEL_SIGNATURE_LIST_START                      0x1402
#define LABEL_SIGNATURE_DATA_START                      0x1403
#define LABEL_DELETE_ALL_LIST_BUTTON                    0x1500
#define LABEL_DB_CERTS_DATA_START                       0x1600
#define LABEL_DBX_CERTS_DATA_START                      0x1700
#define LABEL_BOOTLOADER_LIST_START                     0x2000
#define LABEL_BOOTLOADER_CERT_LIST_START                0x3000
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
//
// Question ID 0x7000 ~ 0x7FFF is for DB list
//
#define OPTION_DB_LIST_QUESTION_ID                      0x7000
//
// Question ID 0x8000 ~ 0x8FFF is for DBX list
//
#define OPTION_DBX_LIST_QUESTION_ID                     0x8000
//
// Question ID 0x7000 ~ 0x7FFF is for DB list entries
//
#define OPTION_DB_ENTRIES_QUESTION_ID                   0x9000
//
// Question ID 0x8000 ~ 0x8FFF is for DBX list entries
//
#define OPTION_DBX_ENTRIES_QUESTION_ID                  0xA000

#define SIGNATURE_TYPE_RSA2048_SHA256                   0
#define SIGNATURE_TYPE_RSA2048                          1
#define SIGNATURE_TYPE_X509                             2
#define SIGNATURE_TYPE_SHA1                             3
#define SIGNATURE_TYPE_SHA224                           4
#define SIGNATURE_TYPE_SHA256                           5
#define SIGNATURE_TYPE_SHA384                           6
#define SIGNATURE_TYPE_SHA512                           7
#define SIGNATURE_TYPE_SM3                              8
#define SIGNATURE_TYPE_X509_SHA256                      9
#define SIGNATURE_TYPE_X509_SHA384                      10
#define SIGNATURE_TYPE_X509_SHA512                      11
#define SIGNATURE_TYPE_X509_SM3                         12
#define SIGNATURE_TYPE_UNKNOWN                          13

#define IMAGE_STATE_UNDECIDED                           0
#define IMAGE_STATE_UNTRUSTED                           1
#define IMAGE_STATE_TRUSTED                             2

// Keep the form data packed to workaround the storage size calculation
// difference in C and IFR for EFI_HII_TIME
#pragma pack(1)
// Form Data
typedef struct {
  UINT8           ImageUnsigned;        // If the image is unsigned.
  BOOLEAN         AlwaysRevocation;     // If the certificate is always revoked. Revocation time is hidden
  UINT8           CertificateFormat;    // The type of the certificate
  EFI_HII_DATE    RevocationDate;       // The revocation date of the certificate
  EFI_HII_TIME    RevocationTime;       // The revocation time of the certificate
  UINT8           FileEnrollType;       // File type of signature enroll
  UINT32          ListCount;            // The count of signature list.
  UINT32          CheckedDataCount;     // The count of checked signature data.
  UINT8           SignatureType;        // Type of signature to be displayed
  BOOLEAN         IsCertHash;           // If the signature data is certificate hash
  UINT8           SignatureRemove;      // If the signature data should be modified
  UINT32          BootloaderCount;      // The count of bootloaders.
  UINT8           SignedByMs;           // If current bootloader is signed by MS certs.
  UINT8           SignedByMsOnly;       // If current bootloader is signed by MS certs only.
  UINT8           ImageTrusted;         // If current bootloader is trusted.
  UINT8           HasInvalidSignature;  // If current bootloader contains invalid signature.
} SOVEREIGN_BOOT_WIZARD_FORM_DATA;

#pragma pack()

#endif
