/** @file
  Gigabyte BMC "system information" handoff: wire format and SMBIOS OEM records.

  Reverse engineered from the Gigabyte MZ33-AR1 stock BIOS driver
  SendIpmiSystemInfo.efi and the BMC firmware 13.06.29 (libgbt.so,
  libgbtmisc.so). See the comment in GigabyteBmcSysInfoDxe.c for the
  protocol itself.

  Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef GIGABYTE_BMC_SYS_INFO_H_
#define GIGABYTE_BMC_SYS_INFO_H_

#include <Uefi.h>
#include <IndustryStandard/SmBios.h>
#include <Protocol/DevicePath.h>
#include <Protocol/PciIo.h>

//
// IPMI: OEM group NetFn, data starts with the Gigabyte IANA (15370) and a
// sub-command. Even command = "set" handler, odd command = "get" handler.
//
#define GBT_IPMI_NETFN_OEM_GROUP       0x2E
#define GBT_IPMI_CMD_OEM_SET           0x20
#define GBT_IPMI_CMD_OEM_GET           0x21
#define GBT_IANA_BYTE0                 0x0A
#define GBT_IANA_BYTE1                 0x3C
#define GBT_IANA_BYTE2                 0x00
#define GBT_SUBCMD_GET_SHM_ADDR        0x0A
#define GBT_SUBCMD_SET_SMBIOS_READY    0x0B
#define GBT_SUBCMD_SET_BIOS_READY      0x57

//
// POST complete ("BIOS ready") GPIO, active low, read by the BMC through the
// CPLD. coreboot configures it as an output and passes it in its table under
// this name: port = address of the GPIO control register, value = the bits
// in it to clear (active low) or set (active high) to assert the GPIO.
//
#define GBT_POST_COMPLETE_GPIO_NAME    "POST complete"

//
// ASPEED BMC VGA function and its P2A (PCI-to-AHB) bridge, all in BAR1.
//
#define ASPEED_VENDOR_ID               0x1A03
#define ASPEED_VGA_DEVICE_ID           0x2000
#define ASPEED_P2A_BAR_INDEX           1
#define ASPEED_P2A_CTRL                0xF000
#define ASPEED_P2A_ADDR                0xF004
#define ASPEED_P2A_WINDOW              0x10000
#define ASPEED_P2A_WINDOW_SIZE         0x10000

#pragma pack(1)

typedef struct {
  UINT8    Iana[3];
  UINT8    SubCmd;
} GBT_OEM_REQUEST;

typedef struct {
  GBT_OEM_REQUEST    Hdr;
  UINT8              Data;            // 0: start the BMC's device discovery
} GBT_SET_BIOS_READY_REQUEST;

typedef struct {
  GBT_OEM_REQUEST    Hdr;
  UINT16             TotalSize;       // Whole payload including _HEADER_
  UINT16             DataSize;        // TotalSize without _HEADER_
  UINT16             Reserved;
} GBT_SET_SMBIOS_READY_REQUEST;

//
// Payload blocks following the SMBIOS 3.0 entry point and structure table.
//
#define GBT_HD_ENTRY_SIZE      256
#define GBT_LAN_ENTRY_SIZE     160
#define GBT_CPU_SOCKETS        2
#define GBT_CPU_FEATURE_ON     0x0100
#define GBT_HEADER_NUM_BLOCKS  11

typedef struct {
  CHAR8    Signature[4];          // "_HD_"
  CHAR8    Revision[4];           // "RV11"
  UINT8    NumEntries;            // 256-byte entries (SATA ports)
  UINT8    Reserved0[5];
  UINT8    NumEntriesDup;         // Same as NumEntries
  UINT8    NumLargeEntries;       // 512-byte entries, never used
} GBT_HD_BLOCK;

typedef struct {
  UINT32    Reserved0;
  UINT8     Port;                 // Controller base + AHCI port
  CHAR8     SerialNumber[20];
  CHAR8     FirmwareRevision[8];
  CHAR8     Model[40];            // NUL terminated if shorter
  UINT16    SataCapabilities;     // IDENTIFY word 76
  UINT16    SecurityStatus;       // IDENTIFY word 128
  UINT64    SizeMB;               // 10^6 bytes
  UINT8     Reserved1[GBT_HD_ENTRY_SIZE - 0x55];
} GBT_HD_ENTRY;

typedef struct {
  UINT16    Smt;                  // CPUID.0B.0:EBX[7:0] > 1
  UINT16    Vmx;                  // CPUID.1:ECX[5]
  UINT16    Nx;                   // CPUID.80000001:EDX[20]
  UINT16    Turbo;                // CPUID.6:EAX[1]
  UINT8     Reserved[24];
} GBT_CPU_ENTRY;

typedef struct {
  CHAR8            Signature[5];  // "_CPU_"
  UINT8            Reserved[3];
  GBT_CPU_ENTRY    Cpu[GBT_CPU_SOCKETS];
} GBT_CPU_BLOCK;

typedef struct {
  CHAR8     Signature[5];         // "_MEM_"
  UINT8     Reserved0[3];
  UINT32    Unknown0;             // Always 0x1800
  UINT32    Unknown1;             // Always 0x18
  UINT8     Reserved1[48];
} GBT_MEM_BLOCK;

typedef struct {
  CHAR8    Signature[5];          // "_LAN_"
  UINT8    Reserved0[5];
  UINT8    Unknown;               // Always 1
  UINT8    NumEntries;            // 160-byte entries
  UINT8    Reserved1[20];
} GBT_LAN_BLOCK;

typedef struct {
  CHAR8    Name[64];              // Space padded, not NUL terminated
  UINT8    Reserved0[2];
  UINT8    Bus;
  UINT8    Device;
  UINT8    Function;
  UINT8    OnBoard;
  UINT8    MacAddress[6];
  UINT8    Reserved1[GBT_LAN_ENTRY_SIZE - 0x4C];
} GBT_LAN_ENTRY;

typedef struct {
  CHAR8    Signature[7];          // "_MDATA_"
  UINT8    Reserved0;
  UINT8    NumEntries;            // 128-byte entries, always 0
  UINT8    Reserved1[23];
} GBT_MDATA_BLOCK;

typedef struct {
  UINT8    Reserved0[8];
  UINT8    NumEntries;            // 320-byte entries, always 0
  UINT8    Reserved1[23];
} GBT_UNNAMED_BLOCK;

typedef struct {
  CHAR8     Signature[8];         // "_HEADER_"
  UINT32    NumBlocks;            // GBT_HEADER_NUM_BLOCKS
  // [0] SMBIOS table (without entry point), then each block incl. entries:
  // [1] _HD_, [2] _CPU_, [3] _MEM_, [4] _LAN_, [5] _MDATA_, [6] unnamed
  UINT16    BlockSize[GBT_HEADER_NUM_BLOCKS];
} GBT_HEADER_BLOCK;

//
// SMBIOS OEM type 0x81: one per SATA port (present or not) and per NVMe
// controller. Parsed by the BMC (SmbiosInterfaceParseHDDJson).
//
#define GBT_SMBIOS_TYPE_HDD           0x81
#define GBT_HDD_INTERFACE_SATA        0
#define GBT_HDD_INTERFACE_PCIE        3
#define GBT_HDD_PORT_NOT_SPECIFIED    0xFF

typedef struct {
  SMBIOS_STRUCTURE    Hdr;
  UINT32              Interface;          // GBT_HDD_INTERFACE_*
  UINT8               Port;               // iPortNumber
  SMBIOS_TABLE_STRING SerialNumber;       // strSN
  SMBIOS_TABLE_STRING FirmwareVersion;    // strFwVersion
  SMBIOS_TABLE_STRING Model;              // 0 means "no disk" (iExist = 0)
  UINT64              SizeMB;             // iSize
  UINT16              SataCapabilities;   // hexCapability, IDENTIFY word 76
  UINT16              SecurityStatus;     // hexSecurityStatus, IDENTIFY word 128
  UINT16              VendorId;           // NVMe: PCI vendor, looked up by the BMC
  UINT16              Reserved0;
  UINT8               Bus;                // BusNumber
  UINT8               DevFn;
  UINT16              SlotHandle;         // hexSlotHandle, type 9 handle or 0xFFFF
  UINT16              OnboardIndex;       // hexOnboardIndex, 0xFFFF = not onboard
  UINT32              Reserved1;          // Vendor BIOS: 0xFFFFFFFF
  SMBIOS_TABLE_STRING Locator;            // Only used for Interface 0xFFFF
  SMBIOS_TABLE_STRING SlotDesignation;
} GBT_SMBIOS_TYPE81;

//
// SMBIOS OEM types 0x80 (version), 0x84 (CPU), 0x85 (DIMM), as written by the
// vendor GbtUpdateSMBIOS.efi. coreboot writes them; SmbiosOem.c writes them
// only if coreboot did not.
//
#define GBT_SMBIOS_TYPE_VERSION       0x80
#define GBT_SMBIOS_TYPE_CPU           0x84
#define GBT_SMBIOS_TYPE_DIMM          0x85
#define GBT_SMBIOS_VERSION            0x0001    // Read by the BMC as 0x0100
#define GBT_CPU_RECORDS               2         // Vendor BIOS: always two
#define GBT_CPU_CHAR_VMX              BIT0
#define GBT_CPU_CHAR_SMT              BIT1
#define GBT_CPU_CHAR_NX               BIT2
#define GBT_CPU_CHAR_TURBO            BIT3

typedef struct {
  SMBIOS_STRUCTURE    Hdr;
  UINT16              Version;
} GBT_SMBIOS_TYPE80;

typedef struct {
  SMBIOS_STRUCTURE    Hdr;
  UINT32              Characteristics;    // hexGbtChars, GBT_CPU_CHAR_*
  UINT32              Status;             // hexGbtStatus, always 0
} GBT_SMBIOS_TYPE84;

typedef struct {
  SMBIOS_STRUCTURE    Hdr;
  UINT32              MaxSizeGiB;         // iMaxSize
  UINT32              TotalSlots;         // iTotalSlot
} GBT_SMBIOS_TYPE85;

//
// SMBIOS OEM type 0x82: one per PCI device in a slot or onboard, as written
// by the vendor GbtUpdateSMBIOS.efi. The BMC links disks (0x81) and NICs
// (0x83) to them by SlotHandle / OnboardIndex and takes the slot name from
// the type 9 record SlotHandle points to. Only written by EDK2 (coreboot
// writes its OEM records before the type 9 handles are known).
//
#define GBT_SMBIOS_TYPE_PCIE          0x82

typedef struct {
  SMBIOS_STRUCTURE    Hdr;
  UINT16              DeviceId;           // hexDeviceID
  UINT16              VendorId;           // hexVendorID
  UINT32              RevisionClass;      // hexClass: PCI config dword 0x08
  UINT16              SlotHandle;         // hexSlotHandle: type 9 handle or 0xFFFF
  UINT16              PortNumber;         // iPortNumber, vendor BIOS: 0xFF
  SMBIOS_TABLE_STRING FirmwareVersion;    // strFwVersion
  UINT16              OnboardIndex;       // hexOnboardIndex, 0xFFFF = in a slot
  UINT8               LinkWidth;          // hexLinkWidth: PCIe Link Status width
  UINT8               LinkSpeed;          // hexLinkSpeed: PCIe Link Status speed
  UINT8               Reserved0[4];
  UINT8               Unknown;            // Vendor BIOS: 1 if it adds strings
  UINT16              Segment;            // hexSegmentGroupNumber
  UINT8               Bus;                // hexBusNumber
  UINT8               DevFn;              // hexDeviceNum
  UINT16              SubsystemVendorId;
  UINT16              SubsystemId;
  SMBIOS_TABLE_STRING Unknown1;           // Vendor BIOS: string number or 0
} GBT_SMBIOS_TYPE82;

//
// SMBIOS OEM type 0x83: one per NIC port, onboard or add-in. Written by
// coreboot (mainboard code, MAC from the PCIe DSN); SmbiosOem.c adds the
// ones coreboot could not write. Parsed by the BMC
// (SmbiosInterfaceParseNetworkJson, SmbiosInterfaceGetNetWorkMAC) and used
// here for the _LAN_ entries.
//
#define GBT_SMBIOS_TYPE_NETWORK       0x83

typedef struct {
  SMBIOS_STRUCTURE    Hdr;
  UINT16              DeviceId;
  UINT16              VendorId;
  UINT8               Unknown0;           // Always 1
  UINT8               MacAddress[6];      // strMAC
  UINT8               Reserved0[18];
  UINT32              Unknown1;           // Always 0xEE000000
  UINT16              SlotHandle;         // hexSlotHandle, type 9 handle or 0xFFFF
  UINT16              Port;               // iPortNumber, PCI function number
  UINT8               Reserved1[40];
  UINT8               Speed;              // iSpeed, vendor BIOS: 0
  UINT16              OnboardIndex;       // hexOnboardIndex, 0xFFFF = add-in card
  UINT16              Segment;
  UINT8               Bus;
  UINT8               DevFn;
  UINT16              SubsystemVendorId;
  UINT16              SubsystemId;
  // Followed by one string: the device name.
} GBT_SMBIOS_TYPE83;

#pragma pack()

STATIC_ASSERT (sizeof (GBT_HD_BLOCK) == 16, "Wrong _HD_ block size");
STATIC_ASSERT (sizeof (GBT_HD_ENTRY) == GBT_HD_ENTRY_SIZE, "Wrong _HD_ entry size");
STATIC_ASSERT (OFFSET_OF (GBT_HD_ENTRY, SizeMB) == 0x4D, "Wrong _HD_ entry layout");
STATIC_ASSERT (sizeof (GBT_CPU_BLOCK) == 72, "Wrong _CPU_ block size");
STATIC_ASSERT (sizeof (GBT_MEM_BLOCK) == 64, "Wrong _MEM_ block size");
STATIC_ASSERT (sizeof (GBT_LAN_BLOCK) == 32, "Wrong _LAN_ block size");
STATIC_ASSERT (sizeof (GBT_LAN_ENTRY) == GBT_LAN_ENTRY_SIZE, "Wrong _LAN_ entry size");
STATIC_ASSERT (OFFSET_OF (GBT_LAN_ENTRY, MacAddress) == 0x46, "Wrong _LAN_ entry layout");
STATIC_ASSERT (sizeof (GBT_MDATA_BLOCK) == 32, "Wrong _MDATA_ block size");
STATIC_ASSERT (sizeof (GBT_UNNAMED_BLOCK) == 32, "Wrong unnamed block size");
STATIC_ASSERT (sizeof (GBT_HEADER_BLOCK) == 34, "Wrong _HEADER_ block size");
STATIC_ASSERT (sizeof (GBT_SMBIOS_TYPE81) == 0x28, "Wrong type 0x81 size");
STATIC_ASSERT (OFFSET_OF (GBT_SMBIOS_TYPE81, SlotHandle) == 0x1E, "Wrong type 0x81 layout");
STATIC_ASSERT (sizeof (GBT_SMBIOS_TYPE80) == 6, "Wrong type 0x80 size");
STATIC_ASSERT (sizeof (GBT_SMBIOS_TYPE84) == 12, "Wrong type 0x84 size");
STATIC_ASSERT (sizeof (GBT_SMBIOS_TYPE85) == 12, "Wrong type 0x85 size");
STATIC_ASSERT (sizeof (GBT_SMBIOS_TYPE82) == 0x23, "Wrong type 0x82 size");
STATIC_ASSERT (OFFSET_OF (GBT_SMBIOS_TYPE82, OnboardIndex) == 0x11, "Wrong type 0x82 layout");
STATIC_ASSERT (OFFSET_OF (GBT_SMBIOS_TYPE82, Bus) == 0x1C, "Wrong type 0x82 layout");
STATIC_ASSERT (sizeof (GBT_SMBIOS_TYPE83) == 0x5C, "Wrong type 0x83 size");
STATIC_ASSERT (OFFSET_OF (GBT_SMBIOS_TYPE83, SlotHandle) == 0x25, "Wrong type 0x83 layout");
STATIC_ASSERT (OFFSET_OF (GBT_SMBIOS_TYPE83, OnboardIndex) == 0x52, "Wrong type 0x83 layout");

//
// Collected inventory, filled by Storage.c / Network.c.
//
typedef struct {
  GBT_HD_ENTRY     *HdEntries;     // NumHdEntries entries
  UINTN            NumHdEntries;
  GBT_LAN_ENTRY    *LanEntries;    // NumLanEntries entries
  UINTN            NumLanEntries;
} GBT_SYS_INFO;

//
// PciUtil.c
//
typedef struct {
  UINTN    Segment;
  UINTN    Bus;
  UINTN    Device;
  UINTN    Function;
} GBT_PCI_LOCATION;

#define GBT_DEVFN(Loc)  ((UINT8)(((Loc)->Device << 3) | (Loc)->Function))

EFI_STATUS
GbtGetPciIoForDevicePath (
  IN  EFI_DEVICE_PATH_PROTOCOL  *DevicePath,
  OUT EFI_PCI_IO_PROTOCOL       **PciIo,
  OUT EFI_HANDLE                *PciHandle OPTIONAL
  );

UINT16
GbtFindSlotHandle (
  IN EFI_HANDLE  PciHandle,
  IN BOOLEAN     AncestorsOnly
  );

UINT16
GbtFindOnboardInstance (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN BOOLEAN              AnyFunction
  );

EFI_STATUS
GbtAddSmbiosRecord (
  IN SMBIOS_STRUCTURE  *Record,
  IN CHAR8             **Strings,
  IN UINTN             NumStrings
  );

VOID
GbtCopyAtaString (
  OUT CHAR8        *Dest,
  IN  CONST UINT8  *Src,
  IN  UINTN        Length,
  IN  BOOLEAN      SwapBytes
  );

//
// Storage.c / SmbiosOem.c / Network.c
//
EFI_STATUS
GbtAddMissingOemRecords (
  VOID
  );

EFI_STATUS
GbtCollectStorage (
  IN OUT GBT_SYS_INFO  *Info
  );

EFI_STATUS
GbtCollectNetwork (
  IN OUT GBT_SYS_INFO  *Info
  );

#endif
