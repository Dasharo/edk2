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

#define SOVEREIGN_BOOT_WIZARD_FORM_DATA_VARSTORE_ID  0x0001

#define SOVEREIGN_BOOT_WIZARD_WELCOME_FORM_ID            1
#define SOVEREIGN_BOOT_WIZARD_CONFIG_FORM_ID             2
#define SOVEREIGN_BOOT_WIZARD_MS_SECURE_BOOT_FORM_ID     3
#define SOVEREIGN_BOOT_WIZARD_INTERACTIVE_MODE_FORM_ID   9

// Question IDs
// Each form will reserve 0x100 IDs
#define SOVEREIGN_BOOT_WIZARD_FORM_QUESTION_ID_BASE      0x1000
// Welcome form
#define WELCOME_FORM_QUESTION_ID_BASE                   0x1100
#define SELECT_SOVEREIGN_BOOT_QUESTION_ID                0x1101
#define SELECT_DEFAULT_SECURE_BOOT_QUESTION_ID          0x1102

// Configuration form
#define CONFIG_FORM_QUESTION_ID_BASE                    0x1200
#define DO_NOT_TRUST_KEY_FORM2_QUESTION_ID              0x1201
#define TRUST_KEY_AND_BOOT_FORM2_QUESTION_ID            0x1202
#define TRUST_KEY_FORM2_QUESTION_ID                     0x1203
#define SHOW_KEY_DETAILS_FORM2_QUESTION_ID              0x1204

#define EXIT_FORM_QUESTION_ID_BASE                      0x1F00
#define EXIT_FORM1_QUESTION_ID                          0x1F01
#define EXIT_FORM2_QUESTION_ID                          0x1F02
#define EXIT_FORM3_QUESTION_ID                          0x1F03
#define EXIT_FORM9_QUESTION_ID                          0x1F09


#pragma pack(1)

// Form Data
typedef struct {
  UINT8               Unused;
} SOVEREIGN_BOOT_WIZARD_FORM_DATA;

#pragma pack()

#endif
