/** @file

Copyright (c) 2025, 3mdeb Sp. z o.o. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef SOVEREIGN_BOOT_H_
#define SOVEREIGN_BOOT_H_

#define SOVEREIGN_BOOT_WIZARD_FORMSET_GUID \
  { \
    0xB57031B9, 0x1ABB, 0x45F8, {0xA9, 0xCB, 0xAC, 0x5A, 0xAD, 0x72, 0xAD, 0x31} \
  }

#define SV_BOOT_DATA_VAR      L"SvBootData"
#define SV_BOOT_CONFIG_VAR    L"SvBootConfig"

// Application launch causes. Determine the logic and screens showed
// when the application is launched.
// We want different screens and messages when:
// 1. SV Boot is provisioned but image fails to verify.
// 2. SV Boot is not yet provisioned or platform booting with default settings.
// 3. Application is launched from setup
#define SV_BOOT_LAUNCH_UNDEFINED                        0
#define SV_BOOT_LAUNCH_BOOT_WITH_DEFAULT_SETTINGS       1
#define SV_BOOT_LAUNCH_IMAGE_VERIFICATION_FAILED        2
#define SV_BOOT_LAUNCH_VIA_SETUP                        3
#define SV_BOOT_LAUNCH_MAX                              4

extern EFI_GUID gSovereignBootWizardFormSetGuid;

#pragma pack(1)

// Data passed from firmware via EFI variables (volatile, BS access)
typedef struct {
  UINT8               AppLaunchCause;
} SOVEREIGN_BOOT_WIZARD_CONFIG_DATA;

// State of SV Boot in EFI variables (non-volatile, BS access)
typedef struct {
  BOOLEAN             SvBootEnabled;
  BOOLEAN             SvBootProvisioned;
} SOVEREIGN_BOOT_WIZARD_NV_CONFIG;

#pragma pack()

#endif
