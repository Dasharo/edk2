/*++
Copyright (c)  1999  - 2014, Intel Corporation. All rights reserved
                                                                                   
  SPDX-License-Identifier: BSD-2-Clause-Patent
                                                                                   
--*/

/** @file
**/

#ifndef _NETWORK_BOOT_POLICY_PROTOCOL_H_
#define _NETWORK_BOOT_POLICY_PROTOCOL_H_

#define EFI_NETWORK_BOOT_POLICY_PROTOCOL_GUID \
  { 0xdef83d91, 0x4613, 0x474c, 0xa7, 0xad, 0xf7, 0x79, 0x10, 0x11, 0x43, 0xf2 }

#define NETWORK_BOOT_POLICY_PROTOCOL_REVISION_01 0x01

typedef struct _NETWORK_BOOT_POLICY_PROTOCOL {
  UINT32           Revision;
  BOOLEAN          NetworkBootEnabled;
} NETWORK_BOOT_POLICY_PROTOCOL;

//
// Extern the GUID for protocol users.
//
extern EFI_GUID  gDasharoNetworkBootPolicyGuid;

#endif