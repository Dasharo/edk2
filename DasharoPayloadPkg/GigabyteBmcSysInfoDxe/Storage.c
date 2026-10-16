/** @file
  Disk inventory for the Gigabyte BMC: _HD_ payload entries and SMBIOS OEM
  type 0x81 records for SATA ports and NVMe controllers.

  Mirrors what the vendor BIOS (SendIpmiSystemInfo.efi) does:
   - One 256-byte _HD_ entry and one type 0x81 record per implemented SATA
     port (AHCI PI register), whether a disk is attached or not. Ports are
     numbered continuously across controllers (controller base + AHCI
     port). On the MZ33-AR1 the SATA0/SATA1 ports are shut down by coreboot,
     which leaves 16 ports (0-15) on SATA2/SATA3, as many as the vendor
     BIOS reports.
   - One type 0x81 record (interface 3 = PCIe) per NVMe controller, numbered
     from 1 like the vendor BIOS and linked to its SMBIOS type 9 slot. NVMe
     drives get no _HD_ entry.

  Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <IndustryStandard/Atapi.h>
#include <IndustryStandard/Nvme.h>
#include <IndustryStandard/Pci.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/DiskInfo.h>
#include <Protocol/NvmExpressPassthru.h>
#include <Protocol/PciIo.h>

#include "GigabyteBmcSysInfo.h"

#define AHCI_BAR_INDEX       5
#define AHCI_CAP_OFFSET      0x00
#define AHCI_CAP_NP_MASK     0x1F
#define AHCI_PI_OFFSET       0x0C
#define AHCI_MAX_PORTS       32
#define NVME_IDENTIFY_SIZE   4096
#define NVME_CNS_NAMESPACE   0x00
#define NVME_CNS_CONTROLLER  0x01
#define NVME_FIRST_NAMESPACE 0xFFFFFFFF   // GetNextNamespace() start value
#define PCI_IF_AHCI          0x01
#define NVME_TIMEOUT         EFI_TIMER_PERIOD_SECONDS (5)

typedef struct {
  EFI_HANDLE          Handle;
  GBT_PCI_LOCATION    Loc;
  UINT16              VendorId;
  UINT16              OnboardIndex;
  UINTN               FirstPort;
  UINTN               NumPorts;
} AHCI_CONTROLLER;

STATIC
BOOLEAN
IsClass (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN UINT8                BaseClass,
  IN INTN                 SubClass,       // -1: any
  IN INTN                 ProgIf          // -1: any
  )
{
  UINT8  ClassCode[3];

  if (EFI_ERROR (PciIo->Pci.Read (PciIo, EfiPciIoWidthUint8, PCI_CLASSCODE_OFFSET, 3, ClassCode))) {
    return FALSE;
  }

  return (ClassCode[2] == BaseClass) &&
         ((SubClass < 0) || (ClassCode[1] == SubClass)) &&
         ((ProgIf < 0) || (ClassCode[0] == ProgIf));
}

STATIC
INTN
CompareLocation (
  IN CONST GBT_PCI_LOCATION  *A,
  IN CONST GBT_PCI_LOCATION  *B
  )
{
  UINT64  KeyA;
  UINT64  KeyB;

  KeyA = LShiftU64 (A->Segment, 24) | (A->Bus << 16) | (A->Device << 8) | A->Function;
  KeyB = LShiftU64 (B->Segment, 24) | (B->Bus << 16) | (B->Device << 8) | B->Function;
  return (KeyA < KeyB) ? -1 : (KeyA > KeyB) ? 1 : 0;
}

/**
  With fast boot the BDS does not connect all controllers, so do it for
  mass storage controllers to get DiskInfo and NVMe pass-through instances.
**/
STATIC
VOID
ConnectStorageControllers (
  VOID
  )
{
  EFI_HANDLE           *Handles;
  UINTN                NumHandles;
  UINTN                Index;
  EFI_PCI_IO_PROTOCOL  *PciIo;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiPciIoProtocolGuid, NULL, &NumHandles, &Handles))) {
    return;
  }

  for (Index = 0; Index < NumHandles; Index++) {
    if (!EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiPciIoProtocolGuid, (VOID **)&PciIo)) &&
        IsClass (PciIo, PCI_CLASS_MASS_STORAGE, -1, -1))
    {
      gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
    }
  }

  FreePool (Handles);
}

/**
  Number of port slots of an AHCI controller: up to the highest implemented
  port (PI register), 0 if no port is implemented, e.g. all ports disabled.
**/
STATIC
UINTN
GetAhciPortCount (
  IN EFI_PCI_IO_PROTOCOL  *PciIo
  )
{
  UINT32  Cap;
  UINT32  Pi;

  if (!EFI_ERROR (PciIo->Mem.Read (PciIo, EfiPciIoWidthUint32, AHCI_BAR_INDEX, AHCI_PI_OFFSET, 1, &Pi)) &&
      (Pi != 0xFFFFFFFF))
  {
    return (Pi == 0) ? 0 : (UINTN)HighBitSet32 (Pi) + 1;
  }

  if (!EFI_ERROR (PciIo->Mem.Read (PciIo, EfiPciIoWidthUint32, AHCI_BAR_INDEX, AHCI_CAP_OFFSET, 1, &Cap)) &&
      (Cap != 0xFFFFFFFF))
  {
    return (Cap & AHCI_CAP_NP_MASK) + 1;
  }

  // The vendor BIOS assumes 8 ports per controller on this board family
  return 8;
}

/**
  Enumerate the AHCI controllers sorted by PCI location and assign each one
  a range of _HD_ port numbers.
**/
STATIC
EFI_STATUS
GetAhciControllers (
  OUT AHCI_CONTROLLER  **Controllers,
  OUT UINTN            *NumControllers,
  OUT UINTN            *TotalPorts
  )
{
  EFI_HANDLE           *Handles;
  UINTN                NumHandles;
  UINTN                Index;
  UINTN                Count;
  UINTN                Ports;
  UINTN                I;
  UINTN                J;
  EFI_PCI_IO_PROTOCOL  *PciIo;
  AHCI_CONTROLLER      *List;
  AHCI_CONTROLLER      Tmp;
  EFI_STATUS           Status;

  *Controllers    = NULL;
  *NumControllers = 0;
  *TotalPorts     = 0;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiPciIoProtocolGuid, NULL, &NumHandles, &Handles);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  List = AllocateZeroPool (NumHandles * sizeof (*List));
  if (List == NULL) {
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }

  Count = 0;
  for (Index = 0; Index < NumHandles; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiPciIoProtocolGuid, (VOID **)&PciIo)) ||
        !IsClass (PciIo, PCI_CLASS_MASS_STORAGE, PCI_CLASS_MASS_STORAGE_SATADPA, PCI_IF_AHCI))
    {
      continue;
    }

    List[Count].Handle = Handles[Index];
    PciIo->GetLocation (PciIo, &List[Count].Loc.Segment, &List[Count].Loc.Bus,
                        &List[Count].Loc.Device, &List[Count].Loc.Function);
    PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, PCI_VENDOR_ID_OFFSET, 1, &List[Count].VendorId);
    List[Count].NumPorts     = GetAhciPortCount (PciIo);
    if (List[Count].NumPorts == 0) {
      DEBUG ((DEBUG_INFO, "GbtSysInfo: AHCI %02x:%02x.%x has no ports\n", (UINT32)List[Count].Loc.Bus,
              (UINT32)List[Count].Loc.Device, (UINT32)List[Count].Loc.Function));
      continue;
    }

    List[Count].OnboardIndex = GbtFindOnboardInstance (PciIo, FALSE);
    if (List[Count].OnboardIndex == 0xFFFF) {
      // Vendor BIOS: SATA disks are always reported as onboard, index 0
      List[Count].OnboardIndex = 0;
    }

    Count++;
  }

  FreePool (Handles);

  for (I = 1; I < Count; I++) {
    for (J = I; J > 0 && CompareLocation (&List[J - 1].Loc, &List[J].Loc) > 0; J--) {
      Tmp         = List[J - 1];
      List[J - 1] = List[J];
      List[J]     = Tmp;
    }
  }

  Ports = 0;
  for (I = 0; I < Count; I++) {
    List[I].FirstPort = Ports;
    Ports            += List[I].NumPorts;
    DEBUG ((
      DEBUG_INFO,
      "GbtSysInfo: AHCI %02x:%02x.%x, %u ports, BMC ports %u-%u\n",
      (UINT32)List[I].Loc.Bus,
      (UINT32)List[I].Loc.Device,
      (UINT32)List[I].Loc.Function,
      (UINT32)List[I].NumPorts,
      (UINT32)List[I].FirstPort,
      (UINT32)(Ports - 1)
      ));
  }

  // Port numbers are a UINT8 in both the _HD_ entries and type 0x81
  if (Ports > GBT_HDD_PORT_NOT_SPECIFIED) {
    Ports = GBT_HDD_PORT_NOT_SPECIFIED;
  }

  *Controllers    = List;
  *NumControllers = Count;
  *TotalPorts     = Ports;
  return EFI_SUCCESS;
}

STATIC
UINT64
AtaSizeMB (
  IN ATA_IDENTIFY_DATA  *Id
  )
{
  UINT64  Sectors;
  UINT32  SectorSize;

  if ((Id->config & BIT15) != 0) {
    return 0;                             // ATAPI
  }

  // Word 106: valid (bit 14 set, bit 15 clear) and logical sector > 256 words
  if ((Id->phy_logic_sector_support & (BIT15 | BIT14 | BIT12)) == (BIT14 | BIT12)) {
    SectorSize = (Id->logic_sector_size_lo | ((UINT32)Id->logic_sector_size_hi << 16)) * 2;
  } else {
    SectorSize = 512;
  }

  if ((Id->command_set_supported_83 & BIT10) != 0) {
    Sectors = Id->maximum_lba_for_48bit_addressing[0] |
              LShiftU64 (Id->maximum_lba_for_48bit_addressing[1], 16) |
              LShiftU64 (Id->maximum_lba_for_48bit_addressing[2], 32) |
              LShiftU64 (Id->maximum_lba_for_48bit_addressing[3], 48);
  } else {
    Sectors = Id->user_addressable_sectors_lo | ((UINT32)Id->user_addressable_sectors_hi << 16);
  }

  return DivU64x32 (MultU64x32 (Sectors, SectorSize), 1000000);
}

STATIC
VOID
AddEmptySataRecord (
  IN UINT8            Port,
  IN AHCI_CONTROLLER  *Ctrl
  )
{
  GBT_SMBIOS_TYPE81  Rec;

  // Same as the vendor BIOS, except that no placeholder strings are added
  SetMem (&Rec, sizeof (Rec), 0xFF);
  Rec.Hdr.Type        = GBT_SMBIOS_TYPE_HDD;
  Rec.Hdr.Length      = sizeof (Rec);
  Rec.Hdr.Handle      = SMBIOS_HANDLE_PI_RESERVED;
  Rec.Interface       = GBT_HDD_INTERFACE_SATA;
  Rec.Port            = Port;
  Rec.SerialNumber    = 0;
  Rec.FirmwareVersion = 0;
  Rec.Model           = 0;
  Rec.Reserved0       = 0;
  Rec.OnboardIndex    = Ctrl->OnboardIndex;
  Rec.Locator         = 0;
  Rec.SlotDesignation = 0;
  GbtAddSmbiosRecord (&Rec.Hdr, NULL, 0);
}

STATIC
EFI_STATUS
CollectSata (
  IN OUT GBT_SYS_INFO  *Info
  )
{
  AHCI_CONTROLLER      *Ctrls;
  UINTN                NumCtrls;
  UINTN                TotalPorts;
  EFI_HANDLE           *Handles;
  UINTN                NumHandles;
  UINTN                Index;
  UINTN                C;
  EFI_DISK_INFO_PROTOCOL  *DiskInfo;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  EFI_PCI_IO_PROTOCOL  *PciIo;
  EFI_HANDLE           PciHandle;
  ATA_IDENTIFY_DATA    *Id;
  UINT32               IdSize;
  UINT32               AhciPort;
  UINT32               PmPort;
  UINTN                Port;
  BOOLEAN              *Present;
  GBT_HD_ENTRY         *Entry;
  GBT_SMBIOS_TYPE81    Rec;
  CHAR8                Serial[sizeof (Id->SerialNo) + 1];
  CHAR8                Firmware[sizeof (Id->FirmwareVer) + 1];
  CHAR8                Model[sizeof (Id->ModelName) + 1];
  CHAR8                *Strings[3];
  EFI_STATUS           Status;

  Status = GetAhciControllers (&Ctrls, &NumCtrls, &TotalPorts);
  if (EFI_ERROR (Status) || (TotalPorts == 0)) {
    return Status;
  }

  Info->HdEntries = AllocateZeroPool (TotalPorts * sizeof (GBT_HD_ENTRY));
  Present         = AllocateZeroPool (TotalPorts * sizeof (BOOLEAN));
  Id              = AllocateZeroPool (sizeof (ATA_IDENTIFY_DATA));
  if ((Info->HdEntries == NULL) || (Present == NULL) || (Id == NULL)) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  Info->NumHdEntries = TotalPorts;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiDiskInfoProtocolGuid, NULL, &NumHandles, &Handles);
  if (EFI_ERROR (Status)) {
    NumHandles = 0;
    Handles    = NULL;
  }

  for (Index = 0; Index < NumHandles; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiDiskInfoProtocolGuid, (VOID **)&DiskInfo)) ||
        !CompareGuid (&DiskInfo->Interface, &gEfiDiskInfoAhciInterfaceGuid))
    {
      continue;
    }

    DevicePath = DevicePathFromHandle (Handles[Index]);
    if ((DevicePath == NULL) || EFI_ERROR (GbtGetPciIoForDevicePath (DevicePath, &PciIo, &PciHandle))) {
      continue;
    }

    for (C = 0; C < NumCtrls && Ctrls[C].Handle != PciHandle; C++) {
    }

    if ((C == NumCtrls) || EFI_ERROR (DiskInfo->WhichIde (DiskInfo, &AhciPort, &PmPort))) {
      continue;
    }

    // Disks behind a port multiplier (other than the first) have no slot
    if ((AhciPort >= Ctrls[C].NumPorts) || ((PmPort != 0xFFFF) && (PmPort != 0))) {
      continue;
    }

    Port = Ctrls[C].FirstPort + AhciPort;
    if ((Port >= TotalPorts) || Present[Port]) {
      continue;
    }

    IdSize = sizeof (ATA_IDENTIFY_DATA);
    ZeroMem (Id, IdSize);
    if (EFI_ERROR (DiskInfo->Identify (DiskInfo, Id, &IdSize)) || ((Id->config & BIT15) != 0)) {
      continue;
    }

    GbtCopyAtaString (Serial, (UINT8 *)Id->SerialNo, sizeof (Id->SerialNo), TRUE);
    GbtCopyAtaString (Firmware, (UINT8 *)Id->FirmwareVer, sizeof (Id->FirmwareVer), TRUE);
    GbtCopyAtaString (Model, (UINT8 *)Id->ModelName, sizeof (Id->ModelName), TRUE);

    Entry       = &Info->HdEntries[Port];
    Entry->Port = (UINT8)Port;
    CopyMem (Entry->SerialNumber, Serial, AsciiStrLen (Serial));
    CopyMem (Entry->FirmwareRevision, Firmware, AsciiStrLen (Firmware));
    CopyMem (Entry->Model, Model, AsciiStrLen (Model));
    Entry->SataCapabilities = Id->serial_ata_capabilities;
    Entry->SecurityStatus   = Id->security_status;
    Entry->SizeMB           = AtaSizeMB (Id);

    ZeroMem (&Rec, sizeof (Rec));
    Rec.Hdr.Type         = GBT_SMBIOS_TYPE_HDD;
    Rec.Hdr.Length       = sizeof (Rec);
    Rec.Hdr.Handle       = SMBIOS_HANDLE_PI_RESERVED;
    Rec.Interface        = GBT_HDD_INTERFACE_SATA;
    Rec.Port             = (UINT8)Port;
    Rec.SerialNumber     = 1;
    Rec.FirmwareVersion  = 2;
    Rec.Model            = 3;
    Rec.SizeMB           = Entry->SizeMB;
    Rec.SataCapabilities = Entry->SataCapabilities;
    Rec.SecurityStatus   = Entry->SecurityStatus;
    // The vendor BIOS reports the bus 0 bridge above the controller here;
    // report the SATA controller itself instead.
    Rec.VendorId     = Ctrls[C].VendorId;
    Rec.Bus          = (UINT8)Ctrls[C].Loc.Bus;
    Rec.DevFn        = GBT_DEVFN (&Ctrls[C].Loc);
    Rec.SlotHandle   = 0xFFFF;
    Rec.OnboardIndex = Ctrls[C].OnboardIndex;
    Rec.Reserved1    = 0xFFFFFFFF;

    Strings[0] = Serial;
    Strings[1] = Firmware;
    Strings[2] = Model;
    Status     = GbtAddSmbiosRecord (&Rec.Hdr, Strings, 3);
    DEBUG ((
      DEBUG_INFO,
      "GbtSysInfo: SATA port %u: %a SN %a FW %a, %lu MB: %r\n",
      (UINT32)Port,
      Model,
      Serial,
      Firmware,
      Entry->SizeMB,
      Status
      ));

    Present[Port] = TRUE;
  }

  if (Handles != NULL) {
    FreePool (Handles);
  }

  for (C = 0; C < NumCtrls; C++) {
    for (Port = Ctrls[C].FirstPort; Port < Ctrls[C].FirstPort + Ctrls[C].NumPorts && Port < TotalPorts; Port++) {
      if (!Present[Port]) {
        Info->HdEntries[Port].Port = (UINT8)Port;
        AddEmptySataRecord ((UINT8)Port, &Ctrls[C]);
      }
    }
  }

  Status = EFI_SUCCESS;

Exit:
  if (EFI_ERROR (Status) && (Info->HdEntries != NULL)) {
    FreePool (Info->HdEntries);
    Info->HdEntries    = NULL;
    Info->NumHdEntries = 0;
  }

  if (Present != NULL) {
    FreePool (Present);
  }

  if (Id != NULL) {
    FreePool (Id);
  }

  FreePool (Ctrls);
  return Status;
}

STATIC
EFI_STATUS
NvmeIdentify (
  IN  EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL  *PassThru,
  IN  UINT32                              NamespaceId,
  IN  UINT32                              Cns,
  OUT VOID                                *Buffer
  )
{
  EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET  Packet;
  EFI_NVM_EXPRESS_COMMAND                   Command;
  EFI_NVM_EXPRESS_COMPLETION                Completion;

  ZeroMem (&Packet, sizeof (Packet));
  ZeroMem (&Command, sizeof (Command));
  ZeroMem (&Completion, sizeof (Completion));

  Command.Cdw0.Opcode = NVME_ADMIN_IDENTIFY_CMD;
  Command.Nsid        = NamespaceId;
  Command.Cdw10       = Cns;
  Command.Flags       = CDW10_VALID;

  Packet.NvmeCmd        = &Command;
  Packet.NvmeCompletion = &Completion;
  Packet.TransferBuffer = Buffer;
  Packet.TransferLength = NVME_IDENTIFY_SIZE;
  Packet.CommandTimeout = NVME_TIMEOUT;
  Packet.QueueType      = NVME_ADMIN_QUEUE;

  // NvmExpressDxe rejects a command whose Nsid differs from this argument
  return PassThru->PassThru (PassThru, NamespaceId, &Packet, NULL);
}

STATIC
EFI_STATUS
CollectNvme (
  VOID
  )
{
  EFI_HANDLE                          *Handles;
  UINTN                               NumHandles;
  UINTN                               Index;
  UINTN                               Instance;
  EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL  *PassThru;
  EFI_PCI_IO_PROTOCOL                 *PciIo;
  GBT_PCI_LOCATION                    Loc;
  NVME_ADMIN_CONTROLLER_DATA          *Ctrl;
  NVME_ADMIN_NAMESPACE_DATA           *Ns;
  UINT32                              NamespaceId;
  UINT64                              Bytes;
  UINT8                               Lbads;
  GBT_SMBIOS_TYPE81                   Rec;
  CHAR8                               Serial[sizeof (Ctrl->Sn) + 1];
  CHAR8                               Firmware[sizeof (Ctrl->Fr) + 1];
  CHAR8                               Model[sizeof (Ctrl->Mn) + 1];
  CHAR8                               *Strings[3];
  EFI_STATUS                          Status;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiNvmExpressPassThruProtocolGuid, NULL, &NumHandles, &Handles);
  if (EFI_ERROR (Status)) {
    return EFI_SUCCESS;
  }

  // PassThru needs IoAlign-ed buffers; page allocations satisfy any IoAlign
  Ctrl = AllocatePages (EFI_SIZE_TO_PAGES (NVME_IDENTIFY_SIZE));
  Ns   = AllocatePages (EFI_SIZE_TO_PAGES (NVME_IDENTIFY_SIZE));
  if ((Ctrl == NULL) || (Ns == NULL)) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  Instance = 0;
  for (Index = 0; Index < NumHandles; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiNvmExpressPassThruProtocolGuid, (VOID **)&PassThru)) ||
        EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiPciIoProtocolGuid, (VOID **)&PciIo)) ||
        EFI_ERROR (PciIo->GetLocation (PciIo, &Loc.Segment, &Loc.Bus, &Loc.Device, &Loc.Function)))
    {
      continue;
    }

    ZeroMem (Ctrl, NVME_IDENTIFY_SIZE);
    if (EFI_ERROR (NvmeIdentify (PassThru, 0, NVME_CNS_CONTROLLER, Ctrl))) {
      continue;
    }

    // Total capacity of all active namespaces
    Bytes       = 0;
    NamespaceId = NVME_FIRST_NAMESPACE;
    while (!EFI_ERROR (PassThru->GetNextNamespace (PassThru, &NamespaceId))) {
      ZeroMem (Ns, NVME_IDENTIFY_SIZE);
      if (EFI_ERROR (NvmeIdentify (PassThru, NamespaceId, NVME_CNS_NAMESPACE, Ns))) {
        continue;
      }

      Lbads = Ns->LbaFormat[Ns->Flbas & 0x0F].Lbads;
      if ((Lbads >= 9) && (Lbads < 32)) {
        Bytes += LShiftU64 (Ns->Nsze, Lbads);
      }
    }

    GbtCopyAtaString (Serial, Ctrl->Sn, sizeof (Ctrl->Sn), FALSE);
    GbtCopyAtaString (Firmware, Ctrl->Fr, sizeof (Ctrl->Fr), FALSE);
    GbtCopyAtaString (Model, Ctrl->Mn, sizeof (Ctrl->Mn), FALSE);

    // The rest of the formatted area is 0xFF, like the vendor BIOS
    SetMem (&Rec, sizeof (Rec), 0xFF);
    Rec.Hdr.Type         = GBT_SMBIOS_TYPE_HDD;
    Rec.Hdr.Length       = sizeof (Rec);
    Rec.Hdr.Handle       = SMBIOS_HANDLE_PI_RESERVED;
    Rec.Interface        = GBT_HDD_INTERFACE_PCIE;
    Rec.Port             = (UINT8)(Instance + 1);
    Rec.SerialNumber     = 1;
    Rec.FirmwareVersion  = 2;
    Rec.Model            = 3;
    Rec.SizeMB           = DivU64x32 (Bytes, 1000000);
    Rec.SataCapabilities = 0;
    Rec.SecurityStatus   = 0;
    Rec.VendorId         = Ctrl->Vid;
    Rec.Reserved0        = 0;
    Rec.Bus              = (UINT8)Loc.Bus;
    Rec.DevFn            = GBT_DEVFN (&Loc);
    Rec.SlotHandle       = GbtFindSlotHandle (Handles[Index], FALSE);
    Rec.OnboardIndex     = GbtFindOnboardInstance (PciIo, FALSE);
    Rec.Locator          = 0;
    Rec.SlotDesignation  = 0;

    Strings[0] = Serial;
    Strings[1] = Firmware;
    Strings[2] = Model;
    Status     = GbtAddSmbiosRecord (&Rec.Hdr, Strings, 3);
    DEBUG ((
      DEBUG_INFO,
      "GbtSysInfo: NVMe %02x:%02x.%x: %a SN %a FW %a, %lu MB, slot 0x%04x: %r\n",
      (UINT32)Loc.Bus,
      (UINT32)Loc.Device,
      (UINT32)Loc.Function,
      Model,
      Serial,
      Firmware,
      Rec.SizeMB,
      (UINT32)Rec.SlotHandle,
      Status
      ));
    Instance++;
  }

  Status = EFI_SUCCESS;

Exit:
  if (Ctrl != NULL) {
    FreePages (Ctrl, EFI_SIZE_TO_PAGES (NVME_IDENTIFY_SIZE));
  }

  if (Ns != NULL) {
    FreePages (Ns, EFI_SIZE_TO_PAGES (NVME_IDENTIFY_SIZE));
  }

  FreePool (Handles);
  return Status;
}

EFI_STATUS
GbtCollectStorage (
  IN OUT GBT_SYS_INFO  *Info
  )
{
  EFI_STATUS  Status;

  ConnectStorageControllers ();

  Status = CollectSata (Info);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "GbtSysInfo: SATA inventory failed: %r\n", Status));
  }

  Status = CollectNvme ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "GbtSysInfo: NVMe inventory failed: %r\n", Status));
  }

  return EFI_SUCCESS;
}
