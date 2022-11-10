/*++
Copyright (c)  1999  - 2014, Intel Corporation. All rights reserved
                                                                                   
  SPDX-License-Identifier: BSD-2-Clause-Patent
                                                                                   
--*/

/** @file
**/

#ifndef _DASHARO_BOOT_POLICIES_H_
#define _DASHARO_BOOT_POLICIES_H_

#define DASHARO_NETWORK_BOOT_POLICY_PROTOCOL_GUID \
  { 0xdef83d91, 0x4613, 0x474c, 0xa7, 0xad, 0xf7, 0x79, 0x10, 0x11, 0x43, 0xf2 }

#define DASHARO_USB_DRIVER_POLICY_PROTOCOL_GUID \
  { 0x808330b5, 0xbe46, 0x4a41, 0x97, 0x79, 0x84, 0xa3, 0xd1, 0x31, 0xbb, 0xb4 }

#define DASHARO_USB_MASS_STORAGE_POLICY_PROTOCOL_GUID \
  { 0xd7d1a290, 0x651a, 0x4c90, 0xbf, 0x09, 0x1b, 0x7c, 0x56, 0x7c, 0xd5, 0x9c }

#define DASHARO_PS2_CONTROLLER_POLICY_PROTOCOL_GUID \
  { 0x4885b96d, 0xbdf1, 0x496d, 0xbc, 0x3d, 0x3e, 0x79, 0x57, 0x74, 0xcd, 0x40 }

#define NETWORK_BOOT_POLICY_PROTOCOL_REVISION_01	0x01
#define USB_STACK_POLICY_PROTOCOL_REVISION_01		0x01
#define USB_MASS_STORAGE_POLICY_PROTOCOL_REVISION_01	0x01
#define PS2_CONTROLLER_POLICY_PROTOCOL_REVISION_01	0x01

typedef struct _NETWORK_BOOT_POLICY_PROTOCOL {
  UINT32           Revision;
  BOOLEAN          NetworkBootEnabled;
} NETWORK_BOOT_POLICY_PROTOCOL;

typedef struct _USB_STACK_POLICY_PROTOCOL {
  UINT32           Revision;
  BOOLEAN          UsbStackEnabled;
} USB_STACK_POLICY_PROTOCOL;

typedef struct _USB_MASS_STORAGE_POLICY_PROTOCOL {
  UINT32           Revision;
  BOOLEAN          UsbMassStorageEnabled;
} USB_MASS_STORAGE_POLICY_PROTOCOL;

typedef struct _PS2_CONTROLLER_POLICY_PROTOCOL {
  UINT32           Revision;
  BOOLEAN          Ps2ControllerEnabled;
} PS2_CONTROLLER_POLICY_PROTOCOL;

//
// Extern the GUID for protocol users.
//
extern EFI_GUID gDasharoNetworkBootPolicyGuid;
extern EFI_GUID gDasharoUsbDriverPolicyGuid;
extern EFI_GUID gDasharoUsbMassStoragePolicyGuid;
extern EFI_GUID gDasharoPs2ControllerPolicyGuid;

#endif
