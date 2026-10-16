/** @file
  PCI and SMBIOS helpers for the Gigabyte BMC system information driver.

  Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/PciIo.h>
#include <Protocol/Smbios.h>

#include "GigabyteBmcSysInfo.h"

/**
  Find the PCI I/O instance a device path belongs to (the device itself or
  the PCI controller a child device, e.g. a disk, hangs off).
**/
EFI_STATUS
GbtGetPciIoForDevicePath (
  IN  EFI_DEVICE_PATH_PROTOCOL  *DevicePath,
  OUT EFI_PCI_IO_PROTOCOL       **PciIo,
  OUT EFI_HANDLE                *PciHandle OPTIONAL
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Remaining;
  EFI_HANDLE                Handle;
  EFI_STATUS                Status;

  Remaining = DevicePath;
  Status    = gBS->LocateDevicePath (&gEfiPciIoProtocolGuid, &Remaining, &Handle);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (PciHandle != NULL) {
    *PciHandle = Handle;
  }

  return gBS->HandleProtocol (Handle, &gEfiPciIoProtocolGuid, (VOID **)PciIo);
}

/**
  Return the PCI I/O handle of the bridge directly above a PCI device, by
  dropping the last PCI node of its device path.
**/
STATIC
EFI_HANDLE
GetParentBridge (
  IN EFI_HANDLE  PciHandle
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  EFI_DEVICE_PATH_PROTOCOL  *Parent;
  EFI_DEVICE_PATH_PROTOCOL  *Node;
  EFI_DEVICE_PATH_PROTOCOL  *LastPci;
  EFI_DEVICE_PATH_PROTOCOL  *Remaining;
  EFI_HANDLE                Handle;
  EFI_STATUS                Status;

  DevicePath = DevicePathFromHandle (PciHandle);
  if (DevicePath == NULL) {
    return NULL;
  }

  Parent = DuplicateDevicePath (DevicePath);
  if (Parent == NULL) {
    return NULL;
  }

  LastPci = NULL;
  for (Node = Parent; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if ((DevicePathType (Node) == HARDWARE_DEVICE_PATH) &&
        (DevicePathSubType (Node) == HW_PCI_DP))
    {
      LastPci = Node;
    }
  }

  Handle = NULL;
  if (LastPci != NULL) {
    SetDevicePathEndNode (LastPci);
    Remaining = Parent;
    Status    = gBS->LocateDevicePath (&gEfiPciIoProtocolGuid, &Remaining, &Handle);
    if (EFI_ERROR (Status) || !IsDevicePathEnd (Remaining)) {
      Handle = NULL;
    }
  }

  FreePool (Parent);
  return Handle;
}

STATIC
EFI_STATUS
GetLocation (
  IN  EFI_HANDLE        PciHandle,
  OUT GBT_PCI_LOCATION  *Loc
  )
{
  EFI_PCI_IO_PROTOCOL  *PciIo;
  EFI_STATUS           Status;

  Status = gBS->HandleProtocol (PciHandle, &gEfiPciIoProtocolGuid, (VOID **)&PciIo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return PciIo->GetLocation (PciIo, &Loc->Segment, &Loc->Bus, &Loc->Device, &Loc->Function);
}

/**
  Find the SMBIOS type 9 (system slot) record for a device. coreboot
  describes a slot by its root port, so the device (unless AncestorsOnly)
  and every bridge above it are checked.

  @return  Handle of the type 9 record, or 0xFFFF if none matches.
**/
UINT16
GbtFindSlotHandle (
  IN EFI_HANDLE  PciHandle,
  IN BOOLEAN     AncestorsOnly
  )
{
  EFI_SMBIOS_PROTOCOL      *Smbios;
  EFI_SMBIOS_HANDLE        SmbiosHandle;
  EFI_SMBIOS_TYPE          Type;
  EFI_SMBIOS_TABLE_HEADER  *Record;
  SMBIOS_TABLE_TYPE9       *Slot;
  GBT_PCI_LOCATION         Loc;
  EFI_HANDLE               Handle;
  UINTN                    Depth;

  if (EFI_ERROR (gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL, (VOID **)&Smbios))) {
    return 0xFFFF;
  }

  Handle = AncestorsOnly ? GetParentBridge (PciHandle) : PciHandle;
  for (Depth = 0; Handle != NULL && Depth < 8; Depth++) {
    if (EFI_ERROR (GetLocation (Handle, &Loc))) {
      break;
    }

    SmbiosHandle = SMBIOS_HANDLE_PI_RESERVED;
    Type         = EFI_SMBIOS_TYPE_SYSTEM_SLOTS;
    while (!EFI_ERROR (Smbios->GetNext (Smbios, &SmbiosHandle, &Type, &Record, NULL))) {
      Slot = (SMBIOS_TABLE_TYPE9 *)Record;
      if ((Record->Length >= OFFSET_OF (SMBIOS_TABLE_TYPE9, DevFuncNum) + 1) &&
          (Slot->SegmentGroupNum == Loc.Segment) &&
          (Slot->BusNum == Loc.Bus) &&
          (Slot->DevFuncNum == GBT_DEVFN (&Loc)))
      {
        return Record->Handle;
      }
    }

    Handle = GetParentBridge (Handle);
  }

  return 0xFFFF;
}

/**
  Find the SMBIOS type 41 (onboard device) record of a PCI device. With
  AnyFunction and no record for the function itself, a record for another
  function of the same device matches, which covers multi-port NICs
  described once (coreboot "Onboard LAN" on 00.0).

  @return  The record's device type instance, or 0xFFFF if none matches.
**/
UINT16
GbtFindOnboardInstance (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN BOOLEAN              AnyFunction
  )
{
  EFI_SMBIOS_PROTOCOL      *Smbios;
  EFI_SMBIOS_HANDLE        SmbiosHandle;
  EFI_SMBIOS_TYPE          Type;
  EFI_SMBIOS_TABLE_HEADER  *Record;
  SMBIOS_TABLE_TYPE41      *Onboard;
  GBT_PCI_LOCATION         Loc;
  UINT16                   SameDevice;

  if (EFI_ERROR (PciIo->GetLocation (PciIo, &Loc.Segment, &Loc.Bus, &Loc.Device, &Loc.Function))) {
    return 0xFFFF;
  }

  if (EFI_ERROR (gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL, (VOID **)&Smbios))) {
    return 0xFFFF;
  }

  SameDevice   = 0xFFFF;
  SmbiosHandle = SMBIOS_HANDLE_PI_RESERVED;
  Type         = EFI_SMBIOS_TYPE_ONBOARD_DEVICES_EXTENDED_INFORMATION;
  while (!EFI_ERROR (Smbios->GetNext (Smbios, &SmbiosHandle, &Type, &Record, NULL))) {
    Onboard = (SMBIOS_TABLE_TYPE41 *)Record;
    if ((Record->Length < sizeof (SMBIOS_TABLE_TYPE41)) ||
        (Onboard->SegmentGroupNum != Loc.Segment) || (Onboard->BusNum != Loc.Bus) ||
        ((Onboard->DevFuncNum >> 3) != Loc.Device))
    {
      continue;
    }

    if (Onboard->DevFuncNum == GBT_DEVFN (&Loc)) {
      return Onboard->DeviceTypeInstance;
    }

    if (AnyFunction && (SameDevice == 0xFFFF)) {
      SameDevice = Onboard->DeviceTypeInstance;
    }
  }

  return SameDevice;
}

/**
  Add an SMBIOS record whose formatted area is Record->Length bytes, with
  the given strings (string numbers 1..NumStrings) appended.
**/
EFI_STATUS
GbtAddSmbiosRecord (
  IN SMBIOS_STRUCTURE  *Record,
  IN CHAR8             **Strings,
  IN UINTN             NumStrings
  )
{
  EFI_SMBIOS_PROTOCOL  *Smbios;
  EFI_SMBIOS_HANDLE    Handle;
  EFI_STATUS           Status;
  UINTN                Size;
  UINTN                Index;
  UINT8                *Buffer;
  UINT8                *Ptr;

  Status = gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL, (VOID **)&Smbios);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Size = Record->Length + 1;
  for (Index = 0; Index < NumStrings; Index++) {
    Size += AsciiStrSize (Strings[Index]);
  }

  // At least a double NUL terminator
  Buffer = AllocateZeroPool (MAX (Size, Record->Length + 2u));
  if (Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  CopyMem (Buffer, Record, Record->Length);
  Ptr = Buffer + Record->Length;
  for (Index = 0; Index < NumStrings; Index++) {
    CopyMem (Ptr, Strings[Index], AsciiStrSize (Strings[Index]));
    Ptr += AsciiStrSize (Strings[Index]);
  }

  Handle = SMBIOS_HANDLE_PI_RESERVED;
  Status = Smbios->Add (Smbios, NULL, &Handle, (EFI_SMBIOS_TABLE_HEADER *)Buffer);
  FreePool (Buffer);
  return Status;
}

/**
  Copy an ATA/NVMe identify string: optionally swap the bytes of each word
  (ATA), replace non-printable characters with spaces, drop trailing spaces
  and NUL terminate. Dest must hold Length + 1 bytes.
**/
VOID
GbtCopyAtaString (
  OUT CHAR8        *Dest,
  IN  CONST UINT8  *Src,
  IN  UINTN        Length,
  IN  BOOLEAN      SwapBytes
  )
{
  UINTN  Index;
  CHAR8  Char;

  for (Index = 0; Index < Length; Index++) {
    Char = (CHAR8)Src[SwapBytes ? (Index ^ 1) : Index];
    if ((Char < 0x20) || (Char > 0x7E)) {
      Char = ' ';
    }

    Dest[Index] = Char;
  }

  Dest[Length] = '\0';
  while ((Length > 0) && (Dest[Length - 1] == ' ')) {
    Dest[--Length] = '\0';
  }

  // Leading spaces are common in ATA model strings
  for (Index = 0; Dest[Index] == ' '; Index++) {
  }

  if (Index > 0) {
    CopyMem (Dest, Dest + Index, Length - Index + 1);
  }
}
