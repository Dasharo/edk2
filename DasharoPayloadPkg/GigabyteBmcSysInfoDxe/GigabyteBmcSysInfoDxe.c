/** @file
  Sends the host system information (SMBIOS tables plus disk, NIC and CPU
  data) to Gigabyte's AMI MegaRAC based BMCs, e.g. on the MZ33-AR1, so that
  the BMC web interface and Redfish show the BIOS version, disks, NICs etc.

  Re-implementation of the vendor BIOS driver SendIpmiSystemInfo.efi. At
  ReadyToBoot it:

   1. Adds the SMBIOS OEM records the BMC parses:
       - type 0x81 for every SATA port and NVMe controller (Storage.c), which
         needs the UEFI storage stack;
       - 0x80 version, 0x84 CPU, 0x85 DIMM and 0x83 NIC records, the same
         as coreboot's mainboard code writes, but only where coreboot did
         not; NICs coreboot had no MAC for get one from SNP (SmbiosOem.c).
      The _LAN_ entries are built from all 0x83 records (Network.c).
   2. Asks the BMC for its shared memory address:
        IPMI NetFn 0x2E Cmd 0x21, data 0A 3C 00 0A
        response: CC, 0A 3C 00, UINT32 address, 01
   3. Writes the payload there through the ASPEED P2A bridge of the BMC VGA
      function (PCI 1A03:2000, BAR1): BAR1+0xF000 = 1, BAR1+0xF004 = 64 KiB
      window base, data at BAR1+0x10000.
        SMBIOS 3.0 entry point (0x18) | SMBIOS table | _HD_ + entries |
        _CPU_ | _MEM_ | _LAN_ + entries | _MDATA_ | unnamed | _HEADER_
   4. Tells the BMC the payload is complete:
        IPMI NetFn 0x2E Cmd 0x20, data 0A 3C 00 0B, UINT16 total size,
        UINT16 total size - 34, UINT16 0
   5. Signals that POST is complete, as GbtBiosReady.efi does, also when the
      steps above failed: drives the "POST complete" GPIO from the coreboot
      table to its active level and sends
        IPMI NetFn 0x2E Cmd 0x20, data 0A 3C 00 57 00
      which makes the BMC start its MCTP and I3C device discovery.

  The BMC (firmware 13.06.29) validates the SMBIOS entry point checksum and
  that a type 127 terminated table follows it, then stores the SMBIOS data;
  a failure is answered with completion code 0xC7. The trailing blocks are
  sent in the vendor layout but are not used by this BMC firmware.

  Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <IndustryStandard/Pci.h>
#include <IndustryStandard/SmBios.h>
#include <Guid/SmBios.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BlParseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/IpmiTransportProtocol.h>
#include <Protocol/PciIo.h>
#include <Coreboot.h>

#include "GigabyteBmcSysInfo.h"

#define P2A_POLL_COUNT       1000
#define GBT_MAX_PAYLOAD      0xFFFF   // Sizes are UINT16 in the IPMI request

STATIC EFI_EVENT  mReadyToBootEvent;

STATIC
EFI_STATUS
SendOemCommand (
  IN  UINT8   Command,
  IN  VOID    *Request,
  IN  UINT32  RequestSize,
  OUT UINT8   *Response,
  IN  UINT32  ResponseBufferSize,
  OUT UINT32  *ResponseSize
  )
{
  IPMI_TRANSPORT  *Ipmi;
  EFI_STATUS      Status;
  UINTN           Try;

  Status = gBS->LocateProtocol (&gIpmiTransportProtocolGuid, NULL, (VOID **)&Ipmi);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Like the vendor BIOS, retry once
  for (Try = 0; Try < 2; Try++) {
    // The transport prepends the completion code, so leave a byte for it
    *ResponseSize = ResponseBufferSize - 1;
    Status        = Ipmi->IpmiSubmitCommand (
                            Ipmi,
                            GBT_IPMI_NETFN_OEM_GROUP,
                            0,
                            Command,
                            Request,
                            RequestSize,
                            Response,
                            ResponseSize
                            );
    if (!EFI_ERROR (Status)) {
      break;
    }
  }

  if (!EFI_ERROR (Status) && ((*ResponseSize < 1) || (Response[0] != 0))) {
    Status = EFI_DEVICE_ERROR;
  }

  return Status;
}

STATIC
EFI_STATUS
GetShmAddress (
  OUT UINT32  *Address
  )
{
  GBT_OEM_REQUEST  Request;
  UINT8            Response[32];
  UINT32           ResponseSize;
  EFI_STATUS       Status;

  Request.Iana[0] = GBT_IANA_BYTE0;
  Request.Iana[1] = GBT_IANA_BYTE1;
  Request.Iana[2] = GBT_IANA_BYTE2;
  Request.SubCmd  = GBT_SUBCMD_GET_SHM_ADDR;

  Status = SendOemCommand (GBT_IPMI_CMD_OEM_GET, &Request, sizeof (Request), Response, sizeof (Response), &ResponseSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // CC, IANA[3], UINT32 address, 01
  if (ResponseSize < 8) {
    return EFI_PROTOCOL_ERROR;
  }

  *Address = ReadUnaligned32 ((UINT32 *)&Response[4]);
  return EFI_SUCCESS;
}

/**
  Tells the BMC the payload is in place.

  The BMC answers with the number of bytes of SMBIOS data it stored: the
  entry point plus the structures up to and including the type 127 one. The
  blocks after the table are not counted.
**/
STATIC
EFI_STATUS
SetSmbiosReady (
  IN UINT16  TotalSize,
  IN UINTN   SmbiosSize
  )
{
  UINT16                        Accepted;
  GBT_SET_SMBIOS_READY_REQUEST  Request;
  UINT8                         Response[32];
  UINT32                        ResponseSize;
  EFI_STATUS                    Status;

  Request.Hdr.Iana[0] = GBT_IANA_BYTE0;
  Request.Hdr.Iana[1] = GBT_IANA_BYTE1;
  Request.Hdr.Iana[2] = GBT_IANA_BYTE2;
  Request.Hdr.SubCmd  = GBT_SUBCMD_SET_SMBIOS_READY;
  Request.TotalSize   = TotalSize;
  Request.DataSize    = TotalSize - sizeof (GBT_HEADER_BLOCK);
  Request.Reserved    = 0;

  Status = SendOemCommand (GBT_IPMI_CMD_OEM_SET, &Request, sizeof (Request), Response, sizeof (Response), &ResponseSize);
  if (!EFI_ERROR (Status) && (ResponseSize >= 6)) {
    Accepted = ReadUnaligned16 ((UINT16 *)&Response[4]);
    DEBUG ((
      (Accepted == SmbiosSize) ? DEBUG_INFO : DEBUG_WARN,
      "GbtSysInfo: BMC accepted %u of %u bytes of SMBIOS data (entry point and table)\n",
      Accepted,
      (UINT32)SmbiosSize
      ));
  }

  return Status;
}

STATIC
EFI_STATUS
SetBiosReady (
  VOID
  )
{
  GBT_SET_BIOS_READY_REQUEST  Request;
  UINT8                       Response[32];
  UINT32                      ResponseSize;

  Request.Hdr.Iana[0] = GBT_IANA_BYTE0;
  Request.Hdr.Iana[1] = GBT_IANA_BYTE1;
  Request.Hdr.Iana[2] = GBT_IANA_BYTE2;
  Request.Hdr.SubCmd  = GBT_SUBCMD_SET_BIOS_READY;
  Request.Data        = 0;

  return SendOemCommand (GBT_IPMI_CMD_OEM_SET, &Request, sizeof (Request), Response, sizeof (Response), &ResponseSize);
}

/**
  Drives the POST complete GPIO coreboot passed in its table to its active
  level and tells the BMC that POST is complete.
**/
STATIC
VOID
SignalPostComplete (
  VOID
  )
{
  UINT32         Register;
  UINT32         Polarity;
  UINT32         Bits;
  RETURN_STATUS  ParseStatus;
  EFI_STATUS     Status;

  ParseStatus = ParseGpio (GBT_POST_COMPLETE_GPIO_NAME, &Register, &Polarity, &Bits);
  if (RETURN_ERROR (ParseStatus)) {
    DEBUG ((DEBUG_WARN, "GbtSysInfo: no \"%a\" GPIO from coreboot\n", GBT_POST_COMPLETE_GPIO_NAME));
  } else {
    if (Polarity == CB_GPIO_ACTIVE_HIGH) {
      MmioOr32 (Register, Bits);
    } else {
      MmioAnd32 (Register, ~Bits);
    }

    DEBUG ((DEBUG_INFO, "GbtSysInfo: POST complete GPIO asserted (0x%08x bits 0x%08x)\n", Register, Bits));
  }

  Status = SetBiosReady ();
  DEBUG ((
    EFI_ERROR (Status) ? DEBUG_ERROR : DEBUG_INFO,
    "GbtSysInfo: BIOS ready: %r\n",
    Status
    ));
}

STATIC
EFI_PCI_IO_PROTOCOL *
FindAspeedVga (
  VOID
  )
{
  EFI_HANDLE           *Handles;
  UINTN                NumHandles;
  UINTN                Index;
  EFI_PCI_IO_PROTOCOL  *PciIo;
  EFI_PCI_IO_PROTOCOL  *Found;
  UINT16               Id[2];

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiPciIoProtocolGuid, NULL, &NumHandles, &Handles))) {
    return NULL;
  }

  Found = NULL;
  for (Index = 0; Index < NumHandles && Found == NULL; Index++) {
    if (!EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiPciIoProtocolGuid, (VOID **)&PciIo)) &&
        !EFI_ERROR (PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, PCI_VENDOR_ID_OFFSET, 2, Id)) &&
        (Id[0] == ASPEED_VENDOR_ID) && (Id[1] == ASPEED_VGA_DEVICE_ID))
    {
      Found = PciIo;
    }
  }

  FreePool (Handles);
  return Found;
}

/**
  Copy a buffer to the BMC's AHB address space through the P2A bridge.
  Size must be a multiple of 4 and Address 4-byte aligned.
**/
STATIC
EFI_STATUS
P2aWrite (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN UINT32               Address,
  IN UINT8                *Buffer,
  IN UINTN                Size
  )
{
  UINT32      Enable;
  UINT32      Base;
  UINT32      ReadBack;
  UINT32      Offset;
  UINTN       Chunk;
  UINTN       Poll;
  EFI_STATUS  Status;

  Enable = 1;
  while (Size > 0) {
    Base   = Address & ~(UINT32)(ASPEED_P2A_WINDOW_SIZE - 1);
    Offset = Address - Base;
    Chunk  = MIN (Size, ASPEED_P2A_WINDOW_SIZE - Offset);

    Status = PciIo->Mem.Write (PciIo, EfiPciIoWidthUint32, ASPEED_P2A_BAR_INDEX, ASPEED_P2A_CTRL, 1, &Enable);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Status = PciIo->Mem.Write (PciIo, EfiPciIoWidthUint32, ASPEED_P2A_BAR_INDEX, ASPEED_P2A_ADDR, 1, &Base);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    for (Poll = 0; Poll < P2A_POLL_COUNT; Poll++) {
      PciIo->Mem.Read (PciIo, EfiPciIoWidthUint32, ASPEED_P2A_BAR_INDEX, ASPEED_P2A_ADDR, 1, &ReadBack);
      if (ReadBack == Base) {
        break;
      }
    }

    if (Poll == P2A_POLL_COUNT) {
      DEBUG ((DEBUG_ERROR, "GbtSysInfo: P2A window 0x%08x not accepted\n", Base));
      return EFI_TIMEOUT;
    }

    Status = PciIo->Mem.Write (
                          PciIo,
                          EfiPciIoWidthUint32,
                          ASPEED_P2A_BAR_INDEX,
                          ASPEED_P2A_WINDOW + Offset,
                          Chunk / sizeof (UINT32),
                          Buffer
                          );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Address += (UINT32)Chunk;
    Buffer  += Chunk;
    Size    -= Chunk;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
WriteToBmc (
  IN UINT32  Address,
  IN UINT8   *Buffer,
  IN UINTN   Size
  )
{
  EFI_PCI_IO_PROTOCOL  *PciIo;
  UINT64               Attributes;
  EFI_STATUS           Status;

  PciIo = FindAspeedVga ();
  if (PciIo == NULL) {
    DEBUG ((DEBUG_ERROR, "GbtSysInfo: ASPEED VGA device not found\n"));
    return EFI_NOT_FOUND;
  }

  Status = PciIo->Attributes (PciIo, EfiPciIoAttributeOperationGet, 0, &Attributes);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((Attributes & EFI_PCI_IO_ATTRIBUTE_MEMORY) == 0) {
    Status = PciIo->Attributes (PciIo, EfiPciIoAttributeOperationEnable, EFI_PCI_IO_ATTRIBUTE_MEMORY, NULL);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Status = P2aWrite (PciIo, Address, Buffer, ALIGN_VALUE (Size, sizeof (UINT32)));

  if ((Attributes & EFI_PCI_IO_ATTRIBUTE_MEMORY) == 0) {
    PciIo->Attributes (PciIo, EfiPciIoAttributeOperationDisable, EFI_PCI_IO_ATTRIBUTE_MEMORY, NULL);
  }

  return Status;
}

STATIC
UINT16
CpuFeature (
  IN BOOLEAN  Enabled
  )
{
  return Enabled ? GBT_CPU_FEATURE_ON : 0;
}

/**
  The vendor BIOS fills both socket entries with the BSP's CPUID data.
**/
STATIC
VOID
FillCpuEntry (
  OUT GBT_CPU_ENTRY  *Entry
  )
{
  UINT32  Eax;
  UINT32  Ebx;
  UINT32  Ecx;
  UINT32  Edx;

  AsmCpuidEx (0x0B, 0, NULL, &Ebx, NULL, NULL);
  Entry->Smt = CpuFeature ((Ebx & 0xFF) > 1);

  AsmCpuid (0x01, NULL, NULL, &Ecx, NULL);
  Entry->Vmx = CpuFeature ((Ecx & BIT5) != 0);

  AsmCpuid (0x80000001, NULL, NULL, NULL, &Edx);
  Entry->Nx = CpuFeature ((Edx & BIT20) != 0);

  AsmCpuid (0x06, &Eax, NULL, NULL, NULL);
  Entry->Turbo = CpuFeature ((Eax & BIT1) != 0);
}

STATIC
VOID
AppendBlock (
  IN OUT UINT8   **Ptr,
  IN     VOID    *Data,
  IN     UINTN   Size
  )
{
  if (Size > 0) {
    CopyMem (*Ptr, Data, Size);
    *Ptr += Size;
  }
}

/**
  Build the payload. Returns NULL if it does not fit the 16-bit size field.
**/
STATIC
UINT8 *
BuildPayload (
  IN  SMBIOS_TABLE_3_0_ENTRY_POINT  *EntryPoint,
  IN  GBT_SYS_INFO                  *Info,
  OUT UINTN                         *PayloadSize
  )
{
  UINTN              TableSize;
  UINTN              HdSize;
  UINTN              LanSize;
  UINTN              Total;
  UINTN              Index;
  UINT8              *Payload;
  UINT8              *Ptr;
  GBT_HD_BLOCK       Hd;
  GBT_CPU_BLOCK      Cpu;
  GBT_MEM_BLOCK      Mem;
  GBT_LAN_BLOCK      Lan;
  GBT_MDATA_BLOCK    MData;
  GBT_UNNAMED_BLOCK  Unnamed;
  GBT_HEADER_BLOCK   Header;

  TableSize = EntryPoint->TableMaximumSize;
  HdSize    = Info->NumHdEntries * sizeof (GBT_HD_ENTRY);
  LanSize   = Info->NumLanEntries * sizeof (GBT_LAN_ENTRY);
  Total     = sizeof (*EntryPoint) + TableSize + sizeof (Hd) + HdSize + sizeof (Cpu) + sizeof (Mem) +
              sizeof (Lan) + LanSize + sizeof (MData) + sizeof (Unnamed) + sizeof (Header);
  if (Total > GBT_MAX_PAYLOAD) {
    return NULL;
  }

  ZeroMem (&Hd, sizeof (Hd));
  CopyMem (Hd.Signature, "_HD_", sizeof (Hd.Signature));
  CopyMem (Hd.Revision, "RV11", sizeof (Hd.Revision));
  Hd.NumEntries    = (UINT8)Info->NumHdEntries;
  Hd.NumEntriesDup = (UINT8)Info->NumHdEntries;

  ZeroMem (&Cpu, sizeof (Cpu));
  CopyMem (Cpu.Signature, "_CPU_", sizeof (Cpu.Signature));
  for (Index = 0; Index < GBT_CPU_SOCKETS; Index++) {
    FillCpuEntry (&Cpu.Cpu[Index]);
  }

  ZeroMem (&Mem, sizeof (Mem));
  CopyMem (Mem.Signature, "_MEM_", sizeof (Mem.Signature));
  Mem.Unknown0 = 0x1800;
  Mem.Unknown1 = 0x18;

  ZeroMem (&Lan, sizeof (Lan));
  CopyMem (Lan.Signature, "_LAN_", sizeof (Lan.Signature));
  Lan.Unknown    = 1;
  Lan.NumEntries = (UINT8)Info->NumLanEntries;

  ZeroMem (&MData, sizeof (MData));
  CopyMem (MData.Signature, "_MDATA_", sizeof (MData.Signature));

  ZeroMem (&Unnamed, sizeof (Unnamed));

  ZeroMem (&Header, sizeof (Header));
  CopyMem (Header.Signature, "_HEADER_", sizeof (Header.Signature));
  Header.NumBlocks    = GBT_HEADER_NUM_BLOCKS;
  Header.BlockSize[0] = (UINT16)TableSize;
  Header.BlockSize[1] = (UINT16)(sizeof (Hd) + HdSize);
  Header.BlockSize[2] = sizeof (Cpu);
  Header.BlockSize[3] = sizeof (Mem);
  Header.BlockSize[4] = (UINT16)(sizeof (Lan) + LanSize);
  Header.BlockSize[5] = sizeof (MData);
  Header.BlockSize[6] = sizeof (Unnamed);

  // Round up for the 32-bit P2A writes
  Payload = AllocateZeroPool (ALIGN_VALUE (Total, sizeof (UINT32)));
  if (Payload == NULL) {
    return NULL;
  }

  Ptr = Payload;
  AppendBlock (&Ptr, EntryPoint, sizeof (*EntryPoint));
  AppendBlock (&Ptr, (VOID *)(UINTN)EntryPoint->TableAddress, TableSize);
  AppendBlock (&Ptr, &Hd, sizeof (Hd));
  AppendBlock (&Ptr, Info->HdEntries, HdSize);
  AppendBlock (&Ptr, &Cpu, sizeof (Cpu));
  AppendBlock (&Ptr, &Mem, sizeof (Mem));
  AppendBlock (&Ptr, &Lan, sizeof (Lan));
  AppendBlock (&Ptr, Info->LanEntries, LanSize);
  AppendBlock (&Ptr, &MData, sizeof (MData));
  AppendBlock (&Ptr, &Unnamed, sizeof (Unnamed));
  AppendBlock (&Ptr, &Header, sizeof (Header));
  ASSERT ((UINTN)(Ptr - Payload) == Total);

  *PayloadSize = Total;
  return Payload;
}

STATIC
VOID
EFIAPI
OnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  GBT_SYS_INFO                  Info;
  SMBIOS_TABLE_3_0_ENTRY_POINT  *EntryPoint;
  UINT8                         *Payload;
  UINTN                         PayloadSize;
  UINT32                        ShmAddress;
  EFI_STATUS                    Status;

  // ReadyToBoot is signalled for every boot attempt; send once, otherwise
  // the SMBIOS OEM records would be added again.
  gBS->CloseEvent (Event);

  ZeroMem (&Info, sizeof (Info));
  GbtCollectStorage (&Info);
  GbtAddMissingOemRecords ();
  GbtCollectNetwork (&Info);

  Status = EfiGetSystemConfigurationTable (&gEfiSmbios3TableGuid, (VOID **)&EntryPoint);
  if (EFI_ERROR (Status) || (EntryPoint == NULL)) {
    DEBUG ((DEBUG_ERROR, "GbtSysInfo: no SMBIOS 3.0 table\n"));
    goto Exit;
  }

  Payload = BuildPayload (EntryPoint, &Info, &PayloadSize);
  if ((Payload == NULL) && ((Info.NumHdEntries != 0) || (Info.NumLanEntries != 0))) {
    // The BMC does not use the disk/NIC blocks, the SMBIOS part matters
    DEBUG ((DEBUG_WARN, "GbtSysInfo: payload too large, dropping _HD_/_LAN_ entries\n"));
    Info.NumHdEntries  = 0;
    Info.NumLanEntries = 0;
    Payload            = BuildPayload (EntryPoint, &Info, &PayloadSize);
  }

  if (Payload == NULL) {
    DEBUG ((DEBUG_ERROR, "GbtSysInfo: SMBIOS table too large (%u bytes)\n", EntryPoint->TableMaximumSize));
    goto Exit;
  }

  Status = GetShmAddress (&ShmAddress);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "GbtSysInfo: get shared memory address failed: %r\n", Status));
    goto FreePayload;
  }

  DEBUG ((DEBUG_INFO, "GbtSysInfo: writing %u bytes to BMC address 0x%08x\n", (UINT32)PayloadSize, ShmAddress));
  Status = WriteToBmc (ShmAddress, Payload, PayloadSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "GbtSysInfo: P2A write failed: %r\n", Status));
    goto FreePayload;
  }

  Status = SetSmbiosReady ((UINT16)PayloadSize, sizeof (*EntryPoint) + EntryPoint->TableMaximumSize);
  DEBUG ((
    EFI_ERROR (Status) ? DEBUG_ERROR : DEBUG_INFO,
    "GbtSysInfo: SMBIOS ready: %r (%u HD, %u LAN entries)\n",
    Status,
    (UINT32)Info.NumHdEntries,
    (UINT32)Info.NumLanEntries
    ));

FreePayload:
  FreePool (Payload);
Exit:
  if (Info.HdEntries != NULL) {
    FreePool (Info.HdEntries);
  }

  if (Info.LanEntries != NULL) {
    FreePool (Info.LanEntries);
  }

  SignalPostComplete ();
}

EFI_STATUS
EFIAPI
GigabyteBmcSysInfoEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  //
  // ReadyToBoot notifications of the same TPL run newest first: being
  // dispatched after SmbiosMeasurementDxe (see the FDF) makes the OEM
  // records part of the measured SMBIOS table.
  //
  return EfiCreateEventReadyToBootEx (TPL_CALLBACK, OnReadyToBoot, NULL, &mReadyToBootEvent);
}
