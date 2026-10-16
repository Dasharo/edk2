/** @file
  Gigabyte OEM SMBIOS records that coreboot's mainboard code normally writes
  (src/mainboard/gigabyte/mz33-ar1/smbios_oem.c), built here the same way
  when coreboot did not, so that the BMC always gets them:

   - 0x80 Gigabyte SMBIOS version
   - 0x84 CPU extras, two records from the BSP's CPUID
   - 0x85 DIMM extras, from the SMBIOS type 16 record (coreboot sets it to
     the board's maximum memory and DIMM slot count)
   - 0x83 one record per NIC port, onboard or add-in, with the MAC address
     from the PCIe Device Serial Number capability

  Beyond what coreboot can do, a NIC without a usable DSN gets its MAC from a
  bound Simple Network Protocol instance, and an add-in NIC gets the handle
  of its SMBIOS type 9 slot. The PCIe device records (0x82), through which
  the BMC finds the slot name of disks and NICs, are only written here:
  they reference the type 9 handles, which coreboot does not know yet when
  it writes its OEM records.

  Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <IndustryStandard/Pci.h>
#include <IndustryStandard/PciExpress21.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/PciIo.h>
#include <Protocol/SimpleNetwork.h>
#include <Protocol/Smbios.h>

#include "GigabyteBmcSysInfo.h"

#define PCI_VID_CHELSIO         0x1425
#define PCI_EXT_CAP_MAX_LOOPS   64

//
// Functions of multi-personality Chelsio adapters that are not NICs, skipped
// like in the vendor BIOS: device ID and a bit mask of PCI functions.
//
STATIC CONST struct {
  UINT16    DeviceId;
  UINT8     Functions;
} mChelsioSkip[] = {
  { 0x540E, 0x10 },
  { 0x6001, 0x0C }, { 0x6401, 0x10 },
  { 0x6002, 0x0C }, { 0x6402, 0x10 },
  { 0x6007, 0x0C }, { 0x6407, 0x10 },
  { 0x6008, 0x0C }, { 0x6408, 0x10 },
  { 0x600D, 0x0C }, { 0x640D, 0x10 },
};

STATIC
EFI_SMBIOS_TABLE_HEADER *
FindRecord (
  IN EFI_SMBIOS_PROTOCOL  *Smbios,
  IN EFI_SMBIOS_TYPE      Type
  )
{
  EFI_SMBIOS_HANDLE        Handle;
  EFI_SMBIOS_TABLE_HEADER  *Record;

  Handle = SMBIOS_HANDLE_PI_RESERVED;
  if (EFI_ERROR (Smbios->GetNext (Smbios, &Handle, &Type, &Record, NULL))) {
    return NULL;
  }

  return Record;
}

STATIC
VOID
AddVersionRecord (
  VOID
  )
{
  GBT_SMBIOS_TYPE80  Rec;

  ZeroMem (&Rec, sizeof (Rec));
  Rec.Hdr.Type   = GBT_SMBIOS_TYPE_VERSION;
  Rec.Hdr.Length = sizeof (Rec);
  Rec.Hdr.Handle = SMBIOS_HANDLE_PI_RESERVED;
  Rec.Version    = GBT_SMBIOS_VERSION;
  GbtAddSmbiosRecord (&Rec.Hdr, NULL, 0);
}

/**
  Same CPUID checks as the vendor BIOS (they target Intel; VMX and Turbo
  read 0 on AMD).
**/
STATIC
UINT32
CpuCharacteristics (
  VOID
  )
{
  UINT32  Eax;
  UINT32  Ebx;
  UINT32  Ecx;
  UINT32  Edx;
  UINT32  Chars;

  Chars = 0;
  AsmCpuid (0x01, NULL, NULL, &Ecx, NULL);
  if ((Ecx & BIT5) != 0) {
    Chars |= GBT_CPU_CHAR_VMX;
  }

  AsmCpuidEx (0x0B, 0, NULL, &Ebx, NULL, NULL);
  if ((Ebx & 0xFF) > 1) {
    Chars |= GBT_CPU_CHAR_SMT;
  }

  AsmCpuid (0x80000001, NULL, NULL, NULL, &Edx);
  if ((Edx & BIT20) != 0) {
    Chars |= GBT_CPU_CHAR_NX;
  }

  AsmCpuid (0x06, &Eax, NULL, NULL, NULL);
  if ((Eax & BIT1) != 0) {
    Chars |= GBT_CPU_CHAR_TURBO;
  }

  return Chars;
}

STATIC
VOID
AddCpuRecords (
  VOID
  )
{
  GBT_SMBIOS_TYPE84  Rec;
  UINTN              Index;

  ZeroMem (&Rec, sizeof (Rec));
  Rec.Hdr.Type        = GBT_SMBIOS_TYPE_CPU;
  Rec.Hdr.Length      = sizeof (Rec);
  Rec.Characteristics = CpuCharacteristics ();
  Rec.Status          = 0;
  for (Index = 0; Index < GBT_CPU_RECORDS; Index++) {
    Rec.Hdr.Handle = SMBIOS_HANDLE_PI_RESERVED;
    GbtAddSmbiosRecord (&Rec.Hdr, NULL, 0);
  }
}

STATIC
VOID
AddDimmRecord (
  IN EFI_SMBIOS_PROTOCOL  *Smbios
  )
{
  SMBIOS_TABLE_TYPE16  *MemArray;
  GBT_SMBIOS_TYPE85    Rec;
  UINT64               MaxGiB;

  MemArray = (SMBIOS_TABLE_TYPE16 *)FindRecord (Smbios, EFI_SMBIOS_TYPE_PHYSICAL_MEMORY_ARRAY);
  if (MemArray == NULL) {
    DEBUG ((DEBUG_WARN, "GbtSysInfo: no SMBIOS type 16, no DIMM record\n"));
    return;
  }

  if ((MemArray->MaximumCapacity == 0x80000000) &&
      (MemArray->Hdr.Length >= sizeof (SMBIOS_TABLE_TYPE16)))
  {
    MaxGiB = RShiftU64 (MemArray->ExtendedMaximumCapacity, 30);    // Bytes
  } else {
    MaxGiB = MemArray->MaximumCapacity >> 20;                       // KiB
  }

  ZeroMem (&Rec, sizeof (Rec));
  Rec.Hdr.Type   = GBT_SMBIOS_TYPE_DIMM;
  Rec.Hdr.Length = sizeof (Rec);
  Rec.Hdr.Handle = SMBIOS_HANDLE_PI_RESERVED;
  Rec.MaxSizeGiB = (UINT32)MaxGiB;
  Rec.TotalSlots = MemArray->NumberOfMemoryDevices;
  GbtAddSmbiosRecord (&Rec.Hdr, NULL, 0);
}

STATIC
BOOLEAN
IsSkippedFunction (
  IN UINT16  VendorId,
  IN UINT16  DeviceId,
  IN UINTN   Function
  )
{
  UINTN  Index;

  if (VendorId != PCI_VID_CHELSIO) {
    return FALSE;
  }

  for (Index = 0; Index < ARRAY_SIZE (mChelsioSkip); Index++) {
    if ((mChelsioSkip[Index].DeviceId == DeviceId) && ((mChelsioSkip[Index].Functions & (1 << Function)) != 0)) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
  NICs put their MAC address in the PCIe Device Serial Number as an EUI-64
  built from the EUI-48 with ff:fe inserted, e.g. DSN 10-ff-e0-ff-fe-ab-c8-b1
  for MAC 10:ff:e0:ab:c8:b1. All functions of a multi-port NIC report the
  same DSN; the other ports follow in function number order. Same rule as
  coreboot.
**/
STATIC
BOOLEAN
MacFromDsn (
  IN  EFI_PCI_IO_PROTOCOL  *PciIo,
  IN  UINTN                Function,
  OUT UINT8                Mac[6]
  )
{
  UINT32  Offset;
  UINT32  Header;
  UINT32  Low;
  UINT32  High;
  UINT32  Nic;
  UINTN   Loops;

  Offset = EFI_PCIE_CAPABILITY_BASE_OFFSET;
  for (Loops = 0; Offset != 0 && Loops < PCI_EXT_CAP_MAX_LOOPS; Loops++) {
    if (EFI_ERROR (PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, Offset, 1, &Header)) ||
        (Header == 0) || (Header == MAX_UINT32))
    {
      return FALSE;
    }

    if ((Header & 0xFFFF) == PCI_EXPRESS_EXTENDED_CAPABILITY_SERIAL_NUMBER_ID) {
      if (EFI_ERROR (PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, Offset + 4, 1, &Low)) ||
          EFI_ERROR (PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, Offset + 8, 1, &High)) ||
          ((High & 0xFF) != 0xFF) || ((Low >> 24) != 0xFE))
      {
        return FALSE;
      }

      Nic    = (Low & 0xFFFFFF) + (UINT32)Function;
      Mac[0] = (UINT8)(High >> 24);
      Mac[1] = (UINT8)(High >> 16);
      Mac[2] = (UINT8)(High >> 8);
      Mac[3] = (UINT8)(Nic >> 16);
      Mac[4] = (UINT8)(Nic >> 8);
      Mac[5] = (UINT8)Nic;
      return TRUE;
    }

    Offset = (Header >> 20) & 0xFFC;
  }

  return FALSE;
}

/**
  Fallback beyond coreboot: the permanent MAC of the first SNP instance bound
  to this PCI function.
**/
STATIC
BOOLEAN
MacFromSnp (
  IN  EFI_HANDLE  PciHandle,
  OUT UINT8       Mac[6]
  )
{
  EFI_HANDLE                   *Handles;
  UINTN                        NumHandles;
  UINTN                        Index;
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_DEVICE_PATH_PROTOCOL     *DevicePath;
  EFI_PCI_IO_PROTOCOL          *PciIo;
  EFI_HANDLE                   Owner;
  BOOLEAN                      Found;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleNetworkProtocolGuid, NULL, &NumHandles, &Handles))) {
    return FALSE;
  }

  Found = FALSE;
  for (Index = 0; Index < NumHandles && !Found; Index++) {
    DevicePath = DevicePathFromHandle (Handles[Index]);
    if ((DevicePath != NULL) &&
        !EFI_ERROR (GbtGetPciIoForDevicePath (DevicePath, &PciIo, &Owner)) &&
        (Owner == PciHandle) &&
        !EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiSimpleNetworkProtocolGuid, (VOID **)&Snp)) &&
        (Snp->Mode != NULL) && (Snp->Mode->HwAddressSize >= 6))
    {
      CopyMem (Mac, &Snp->Mode->PermanentAddress, 6);
      Found = TRUE;
    }
  }

  FreePool (Handles);
  return Found;
}

STATIC
GBT_SMBIOS_TYPE83 *
FindNicRecord (
  IN EFI_SMBIOS_PROTOCOL  *Smbios,
  IN GBT_PCI_LOCATION     *Loc
  )
{
  EFI_SMBIOS_HANDLE        Handle;
  EFI_SMBIOS_TYPE          Type;
  EFI_SMBIOS_TABLE_HEADER  *Record;
  GBT_SMBIOS_TYPE83        *Nic;

  Handle = SMBIOS_HANDLE_PI_RESERVED;
  Type   = GBT_SMBIOS_TYPE_NETWORK;
  while (!EFI_ERROR (Smbios->GetNext (Smbios, &Handle, &Type, &Record, NULL))) {
    Nic = (GBT_SMBIOS_TYPE83 *)Record;
    if ((Record->Length >= sizeof (GBT_SMBIOS_TYPE83)) && (Nic->Segment == Loc->Segment) &&
        (Nic->Bus == Loc->Bus) && (Nic->DevFn == GBT_DEVFN (Loc)))
    {
      return Nic;
    }
  }

  return NULL;
}

/**
  coreboot cannot know the type 9 handles when it writes its 0x83 records,
  so an add-in NIC's record has no slot handle. Replace such a record with a
  copy that has it; the BMC needs it to find the slot (through 0x82).
**/
STATIC
VOID
AddSlotHandle (
  IN EFI_SMBIOS_PROTOCOL  *Smbios,
  IN GBT_SMBIOS_TYPE83    *Nic,
  IN EFI_HANDLE           PciHandle
  )
{
  GBT_SMBIOS_TYPE83  Rec;
  UINT16             SlotHandle;
  EFI_STATUS         Status;

  if ((Nic->OnboardIndex != 0xFFFF) || (Nic->SlotHandle != 0xFFFF)) {
    return;
  }

  SlotHandle = GbtFindSlotHandle (PciHandle, FALSE);
  if (SlotHandle == 0xFFFF) {
    return;
  }

  // coreboot's 0x83 records have no strings
  CopyMem (&Rec, Nic, sizeof (Rec));
  Rec.SlotHandle = SlotHandle;
  Rec.Hdr.Handle = SMBIOS_HANDLE_PI_RESERVED;
  Status         = Smbios->Remove (Smbios, Nic->Hdr.Handle);
  if (!EFI_ERROR (Status)) {
    Status = GbtAddSmbiosRecord (&Rec.Hdr, NULL, 0);
  }

  DEBUG ((DEBUG_INFO, "GbtSysInfo: NIC %02x:%02x.%x: slot handle 0x%04x added: %r\n",
          (UINT32)Rec.Bus, (UINT32)(Rec.DevFn >> 3), (UINT32)(Rec.DevFn & 7), (UINT32)SlotHandle, Status));
}

STATIC
VOID
AddMissingNicRecords (
  IN EFI_SMBIOS_PROTOCOL  *Smbios
  )
{
  EFI_HANDLE           *Handles;
  UINTN                NumHandles;
  UINTN                Index;
  EFI_PCI_IO_PROTOCOL  *PciIo;
  GBT_PCI_LOCATION     Loc;
  UINT8                ClassCode[3];
  UINT16               Id[2];
  UINT16               Subsystem[2];
  UINT8                Mac[6];
  CONST CHAR8          *Source;
  GBT_SMBIOS_TYPE83    Rec;
  GBT_SMBIOS_TYPE83    *Existing;
  EFI_STATUS           Status;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiPciIoProtocolGuid, NULL, &NumHandles, &Handles))) {
    return;
  }

  for (Index = 0; Index < NumHandles; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiPciIoProtocolGuid, (VOID **)&PciIo)) ||
        EFI_ERROR (PciIo->Pci.Read (PciIo, EfiPciIoWidthUint8, PCI_CLASSCODE_OFFSET, 3, ClassCode)) ||
        (ClassCode[2] != PCI_CLASS_NETWORK) || (ClassCode[1] != PCI_CLASS_NETWORK_ETHERNET) ||
        EFI_ERROR (PciIo->GetLocation (PciIo, &Loc.Segment, &Loc.Bus, &Loc.Device, &Loc.Function)))
    {
      continue;
    }

    PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, PCI_VENDOR_ID_OFFSET, 2, Id);
    if (IsSkippedFunction (Id[0], Id[1], Loc.Function)) {
      continue;
    }

    Existing = FindNicRecord (Smbios, &Loc);
    if (Existing != NULL) {
      AddSlotHandle (Smbios, Existing, Handles[Index]);
      continue;
    }

    if (MacFromDsn (PciIo, Loc.Function, Mac)) {
      Source = "DSN";
    } else if (MacFromSnp (Handles[Index], Mac)) {
      Source = "SNP";
    } else {
      DEBUG ((
        DEBUG_INFO,
        "GbtSysInfo: NIC %02x:%02x.%x: no MAC address (no DSN, no SNP), skipping\n",
        (UINT32)Loc.Bus,
        (UINT32)Loc.Device,
        (UINT32)Loc.Function
        ));
      continue;
    }

    PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, PCI_SUBSYSTEM_VENDOR_ID_OFFSET, 2, Subsystem);

    ZeroMem (&Rec, sizeof (Rec));
    Rec.Hdr.Type          = GBT_SMBIOS_TYPE_NETWORK;
    Rec.Hdr.Length        = sizeof (Rec);
    Rec.Hdr.Handle        = SMBIOS_HANDLE_PI_RESERVED;
    Rec.DeviceId          = Id[1];
    Rec.VendorId          = Id[0];
    Rec.Unknown0          = 1;
    Rec.Unknown1          = 0xEE000000;
    Rec.Port              = (UINT16)Loc.Function;
    Rec.Speed             = 0;
    Rec.Segment           = (UINT16)Loc.Segment;
    Rec.Bus               = (UINT8)Loc.Bus;
    Rec.DevFn             = GBT_DEVFN (&Loc);
    Rec.SubsystemVendorId = Subsystem[0];
    Rec.SubsystemId       = Subsystem[1];
    CopyMem (Rec.MacAddress, Mac, sizeof (Rec.MacAddress));

    //
    // Onboard: described by a type 41 record (coreboot: "Onboard LAN" on
    // function 0 of the onboard NIC, all functions share it), like
    // coreboot's gpp_bridge_5_0_b rule. Add-in cards also get their slot.
    //
    Rec.OnboardIndex = GbtFindOnboardInstance (PciIo, TRUE);
    Rec.SlotHandle   = (Rec.OnboardIndex == 0xFFFF) ? GbtFindSlotHandle (Handles[Index], FALSE) : 0xFFFF;

    Status = GbtAddSmbiosRecord (&Rec.Hdr, NULL, 0);
    DEBUG ((
      DEBUG_INFO,
      "GbtSysInfo: NIC %02x:%02x.%x MAC %02x:%02x:%02x:%02x:%02x:%02x (%a), %a, slot 0x%04x: %r\n",
      (UINT32)Loc.Bus,
      (UINT32)Loc.Device,
      (UINT32)Loc.Function,
      Mac[0],
      Mac[1],
      Mac[2],
      Mac[3],
      Mac[4],
      Mac[5],
      Source,
      (Rec.OnboardIndex == 0xFFFF) ? "add-in" : "onboard",
      (UINT32)Rec.SlotHandle,
      Status
      ));
  }

  FreePool (Handles);
}

/**
  Current link width and speed from the PCIe capability's Link Status.
**/
STATIC
VOID
GetLinkStatus (
  IN  EFI_PCI_IO_PROTOCOL  *PciIo,
  OUT UINT8                *Width,
  OUT UINT8                *Speed
  )
{
  UINT8   Ptr;
  UINT8   CapId;
  UINT16  Status;
  UINT16  LinkStatus;
  UINTN   Loops;

  *Width = 0;
  *Speed = 0;
  if (EFI_ERROR (PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, PCI_PRIMARY_STATUS_OFFSET, 1, &Status)) ||
      ((Status & EFI_PCI_STATUS_CAPABILITY) == 0) ||
      EFI_ERROR (PciIo->Pci.Read (PciIo, EfiPciIoWidthUint8, PCI_CAPBILITY_POINTER_OFFSET, 1, &Ptr)))
  {
    return;
  }

  for (Loops = 0; Ptr >= 0x40 && Loops < 48; Loops++) {
    if (EFI_ERROR (PciIo->Pci.Read (PciIo, EfiPciIoWidthUint8, Ptr, 1, &CapId))) {
      return;
    }

    if (CapId == EFI_PCI_CAPABILITY_ID_PCIEXP) {
      if (!EFI_ERROR (PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, Ptr + 0x12, 1, &LinkStatus))) {
        *Speed = LinkStatus & 0x0F;
        *Width = (LinkStatus >> 4) & 0x3F;
      }

      return;
    }

    if (EFI_ERROR (PciIo->Pci.Read (PciIo, EfiPciIoWidthUint8, Ptr + 1, 1, &Ptr))) {
      return;
    }
  }
}

STATIC
BOOLEAN
HasPcieRecord (
  IN EFI_SMBIOS_PROTOCOL  *Smbios,
  IN GBT_PCI_LOCATION     *Loc
  )
{
  EFI_SMBIOS_HANDLE        Handle;
  EFI_SMBIOS_TYPE          Type;
  EFI_SMBIOS_TABLE_HEADER  *Record;
  GBT_SMBIOS_TYPE82        *Pcie;

  Handle = SMBIOS_HANDLE_PI_RESERVED;
  Type   = GBT_SMBIOS_TYPE_PCIE;
  while (!EFI_ERROR (Smbios->GetNext (Smbios, &Handle, &Type, &Record, NULL))) {
    Pcie = (GBT_SMBIOS_TYPE82 *)Record;
    if ((Record->Length >= sizeof (GBT_SMBIOS_TYPE82)) && (Pcie->Segment == Loc->Segment) &&
        (Pcie->Bus == Loc->Bus) && (Pcie->DevFn == GBT_DEVFN (Loc)))
    {
      return TRUE;
    }
  }

  return FALSE;
}

/**
  One 0x82 record per PCI device behind a slot (a bridge above it has a
  type 9 record) or onboard (a type 41 record describes one of its
  functions), like the vendor BIOS does for its lists of slot and onboard
  root ports.
**/
STATIC
VOID
AddPcieDeviceRecords (
  IN EFI_SMBIOS_PROTOCOL  *Smbios
  )
{
  EFI_HANDLE           *Handles;
  UINTN                NumHandles;
  UINTN                Index;
  EFI_PCI_IO_PROTOCOL  *PciIo;
  GBT_PCI_LOCATION     Loc;
  UINT16               Id[2];
  UINT16               Subsystem[2];
  UINT32               RevisionClass;
  UINT16               SlotHandle;
  UINT16               OnboardIndex;
  GBT_SMBIOS_TYPE82    Rec;
  EFI_STATUS           Status;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiPciIoProtocolGuid, NULL, &NumHandles, &Handles))) {
    return;
  }

  for (Index = 0; Index < NumHandles; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiPciIoProtocolGuid, (VOID **)&PciIo)) ||
        EFI_ERROR (PciIo->GetLocation (PciIo, &Loc.Segment, &Loc.Bus, &Loc.Device, &Loc.Function)) ||
        HasPcieRecord (Smbios, &Loc))
    {
      continue;
    }

    SlotHandle   = GbtFindSlotHandle (Handles[Index], TRUE);
    OnboardIndex = (SlotHandle == 0xFFFF) ? GbtFindOnboardInstance (PciIo, TRUE) : 0xFFFF;
    if ((SlotHandle == 0xFFFF) && (OnboardIndex == 0xFFFF)) {
      continue;
    }

    PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, PCI_VENDOR_ID_OFFSET, 2, Id);
    PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, PCI_REVISION_ID_OFFSET, 1, &RevisionClass);
    PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, PCI_SUBSYSTEM_VENDOR_ID_OFFSET, 2, Subsystem);

    ZeroMem (&Rec, sizeof (Rec));
    Rec.Hdr.Type          = GBT_SMBIOS_TYPE_PCIE;
    Rec.Hdr.Length        = sizeof (Rec);
    Rec.Hdr.Handle        = SMBIOS_HANDLE_PI_RESERVED;
    Rec.DeviceId          = Id[1];
    Rec.VendorId          = Id[0];
    // Vendor BIOS: revision only for slot devices
    Rec.RevisionClass     = (SlotHandle != 0xFFFF) ? RevisionClass : (RevisionClass & 0xFFFFFF00);
    Rec.SlotHandle        = SlotHandle;
    Rec.PortNumber        = 0xFF;
    Rec.OnboardIndex      = OnboardIndex;
    Rec.Segment           = (UINT16)Loc.Segment;
    Rec.Bus               = (UINT8)Loc.Bus;
    Rec.DevFn             = GBT_DEVFN (&Loc);
    Rec.SubsystemVendorId = Subsystem[0];
    Rec.SubsystemId       = Subsystem[1];
    GetLinkStatus (PciIo, &Rec.LinkWidth, &Rec.LinkSpeed);

    Status = GbtAddSmbiosRecord (&Rec.Hdr, NULL, 0);
    DEBUG ((
      DEBUG_INFO,
      "GbtSysInfo: PCIe %02x:%02x.%x %04x:%04x x%u Gen%u, slot 0x%04x, onboard 0x%04x: %r\n",
      (UINT32)Loc.Bus,
      (UINT32)Loc.Device,
      (UINT32)Loc.Function,
      (UINT32)Rec.VendorId,
      (UINT32)Rec.DeviceId,
      (UINT32)Rec.LinkWidth,
      (UINT32)Rec.LinkSpeed,
      (UINT32)Rec.SlotHandle,
      (UINT32)Rec.OnboardIndex,
      Status
      ));
  }

  FreePool (Handles);
}

EFI_STATUS
GbtAddMissingOemRecords (
  VOID
  )
{
  EFI_SMBIOS_PROTOCOL  *Smbios;
  EFI_STATUS           Status;

  Status = gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL, (VOID **)&Smbios);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (FindRecord (Smbios, GBT_SMBIOS_TYPE_VERSION) == NULL) {
    DEBUG ((DEBUG_INFO, "GbtSysInfo: adding the version record (0x80)\n"));
    AddVersionRecord ();
  }

  if (FindRecord (Smbios, GBT_SMBIOS_TYPE_CPU) == NULL) {
    DEBUG ((DEBUG_INFO, "GbtSysInfo: adding the CPU records (0x84)\n"));
    AddCpuRecords ();
  }

  if (FindRecord (Smbios, GBT_SMBIOS_TYPE_DIMM) == NULL) {
    DEBUG ((DEBUG_INFO, "GbtSysInfo: adding the DIMM record (0x85)\n"));
    AddDimmRecord (Smbios);
  }

  AddMissingNicRecords (Smbios);
  AddPcieDeviceRecords (Smbios);
  return EFI_SUCCESS;
}
