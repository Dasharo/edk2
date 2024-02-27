/** @file
  Definition for structure & defines exported by APU Configuration UI

  Copyright (c) 2024, 3mdeb All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef APU_CONFIGURATION_UI_H_
#define APU_CONFIGURATION_UI_H_

#define APU_CONFIGURATION_FORMSET_GUID  {0x6f4e051b, 0x1c10, 0x422a, { 0x98, 0xcf, 0x96, 0x2e, 0x78, 0x36, 0x5c, 0x74 } }

#define APU_CONFIGURATION_VAR L"ApuConfig"

#pragma pack(push,1)
typedef struct {
  BOOLEAN CorePerfBoost;
  BOOLEAN WatchdogEnable;
  UINT16 WatchdogTimeout;
  BOOLEAN PciePwrMgmt;
} APU_CONFIGURATION_VARSTORE_DATA;
#pragma pack(pop)

#endif
