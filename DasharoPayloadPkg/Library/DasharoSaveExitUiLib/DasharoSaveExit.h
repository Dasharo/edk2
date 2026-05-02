/** @file
  Dasharo Save & Exit UI library — header shared between the VFR and the C
  glue. Defines the formset GUID, form / question IDs, and the local
  HII_VENDOR_DEVICE_PATH used to publish the formset.

  Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef _DASHARO_SAVE_EXIT_H_
#define _DASHARO_SAVE_EXIT_H_

#define DASHARO_SAVE_EXIT_GUID \
  { 0x5b7f9a21, 0x3c44, 0x4e89, { 0xb1, 0xa6, 0xd8, 0xc7, 0xf2, 0xe5, 0xa9, 0x01 } }

#define DASHARO_SAVE_EXIT_FORM_ID  0x1

// Question IDs for the action items. Browser invokes our config-access
// callback with these on user activation.
#define KEY_SAVE_AND_EXIT      0x1001
#define KEY_DISCARD_AND_EXIT   0x1002
#define KEY_SAVE               0x1003
#define KEY_DISCARD            0x1004
#define KEY_RESET_SYSTEM       0x1005

#ifndef VFRCOMPILE
#include <Protocol/DevicePath.h>

extern UINT8  DasharoSaveExitVfrBin[];

typedef struct {
  VENDOR_DEVICE_PATH          VendorDevicePath;
  EFI_DEVICE_PATH_PROTOCOL    End;
} HII_VENDOR_DEVICE_PATH;
#endif

#endif
