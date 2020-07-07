/** @file
  This file defines the hob structure for board settings

  Copyright (c) 2020, 9elements GmbH. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef __BOARD_SETTINGS_GUID_H__
#define __BOARD_SETTINGS_GUID_H__

///
/// Board information GUID
///
extern EFI_GUID gEfiBoardSettingsVariableGuid;

#pragma pack(1)

typedef struct {
  UINT32 Signature;
  UINT8 SecureBoot;
  UINT8 PrimaryVideo;
} BOARD_SETTINGS;

#pragma pack()

#define BOARD_SETTINGS_NAME L"BoardSettings"

#endif // __BOARD_SETTINGS_GUID_H__
