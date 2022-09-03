/** @file
The Dasharo system features implementation

Copyright (c) 2022, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause

**/

#ifndef _DASHARO_SYSTEM_FEATURES_HII_H_
#define _DASHARO_SYSTEM_FEATURES_HII_H_

#define DASHARO_SYSTEM_FEATURES_GUID  \
  { 0xd15b327e, 0xff2d, 0x4fc1, {0xab, 0xf6, 0xc1, 0x2b, 0xd0, 0x8c, 0x13, 0x59} }

#define DASHARO_SYSTEM_FEATURES_FORM_ID    0x1000
#define DASHARO_SECURITY_OPTIONS_FORM_ID   0x1001
#define DASHARO_NETWORK_OPTIONS_FORM_ID    0x1002

#define DASHARO_FEATURES_DATA_VARSTORE_ID  0x0001

typedef struct {
  BOOLEAN LockBios;
  BOOLEAN SmmBwp;
  BOOLEAN NetworkBoot;
} DASHARO_FEATURES_DATA;

#define LOCK_BIOS_QUESTION_ID              0x8000
#define NETWORK_BOOT_QUESTION_ID           0x8001

extern EFI_GUID gDasharoSystemFeaturesGuid;

#endif
