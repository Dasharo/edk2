/** @file
  NIC inventory for the Gigabyte BMC payload: _LAN_ entries.

  The SMBIOS OEM type 0x83 NIC records the BMC parses are written by coreboot
  (MAC addresses from the PCIe Device Serial Number capability). This builds
  the 160-byte _LAN_ payload entries from those records, one per NIC port,
  so that no UEFI network driver needs to be bound.

  Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/Smbios.h>

#include "GigabyteBmcSysInfo.h"

typedef struct {
  UINT32         Id;              // DeviceId << 16 | VendorId, or VendorId only
  CONST CHAR8    *Name;
} NIC_NAME;

//
// Same table and matching rules as the vendor BIOS: an exact
// vendor/device match, else the first vendor-only match.
//
STATIC CONST NIC_NAME  mNicNames[] = {
  { 0x15288086, "Intel X540"       },
  { 0x15638086, "Intel X550"       },
  { 0x15D18086, "Intel X550 AT"    },
  { 0x15578086, "Intel 82599"      },
  { 0x10FB8086, "Intel 82599"      },
  { 0x15AB8086, "Intel X552 Dual"  },
  { 0x15AC8086, "Intel X552"       },
  { 0x15AD8086, "Intel X557"       },
  { 0x15218086, "Intel i350"       },
  { 0x15FF8086, "Intel X710"       },
  { 0x15728086, "Intel X710"       },
  { 0x159B8086, "Intel E810"       },
  { 0x816810EC, "Realtek 8168"     },
  { 0x000010EC, "Realtek"          },
  { 0x000014E4, "Broadcom"         },
  { 0x101515B3, "Mellanox"         },
  { 0x101315B3, "Mellanox"         },
  { 0x000015B3, "Mellanox"         },
  { 0x00008086, "Intel LAN Device" },
  { 0x00001924, "Solarflare Lan"   },
  { 0x00001077, "QLogic"           },
};

STATIC
CONST CHAR8 *
GetNicName (
  IN UINT32  VendorDevice
  )
{
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (mNicNames); Index++) {
    if ((VendorDevice == mNicNames[Index].Id) || ((VendorDevice & 0xFFFF) == mNicNames[Index].Id)) {
      return mNicNames[Index].Name;
    }
  }

  return "Unknown Networking Device";
}

EFI_STATUS
GbtCollectNetwork (
  IN OUT GBT_SYS_INFO  *Info
  )
{
  EFI_SMBIOS_PROTOCOL      *Smbios;
  EFI_SMBIOS_HANDLE        Handle;
  EFI_SMBIOS_TYPE          Type;
  EFI_SMBIOS_TABLE_HEADER  *Record;
  GBT_SMBIOS_TYPE83        *Nic;
  GBT_LAN_ENTRY            *Entries;
  GBT_LAN_ENTRY            *Entry;
  CONST CHAR8              *Name;
  UINTN                    Count;
  EFI_STATUS               Status;

  Status = gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL, (VOID **)&Smbios);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Count  = 0;
  Handle = SMBIOS_HANDLE_PI_RESERVED;
  Type   = GBT_SMBIOS_TYPE_NETWORK;
  while (!EFI_ERROR (Smbios->GetNext (Smbios, &Handle, &Type, &Record, NULL))) {
    Count++;
  }

  // The _LAN_ block holds at most 255 entries
  Count = MIN (Count, MAX_UINT8);
  if (Count == 0) {
    DEBUG ((DEBUG_INFO, "GbtSysInfo: no SMBIOS type 0x83 NIC records\n"));
    return EFI_SUCCESS;
  }

  Entries = AllocateZeroPool (Count * sizeof (GBT_LAN_ENTRY));
  if (Entries == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Handle = SMBIOS_HANDLE_PI_RESERVED;
  while ((Info->NumLanEntries < Count) &&
         !EFI_ERROR (Smbios->GetNext (Smbios, &Handle, &Type, &Record, NULL)))
  {
    if (Record->Length < sizeof (GBT_SMBIOS_TYPE83)) {
      continue;
    }

    Nic   = (GBT_SMBIOS_TYPE83 *)Record;
    Name  = GetNicName (((UINT32)Nic->DeviceId << 16) | Nic->VendorId);
    Entry = &Entries[Info->NumLanEntries++];
    SetMem (Entry->Name, sizeof (Entry->Name), ' ');
    CopyMem (Entry->Name, Name, MIN (AsciiStrLen (Name), sizeof (Entry->Name)));
    Entry->Bus      = Nic->Bus;
    Entry->Device   = Nic->DevFn >> 3;
    Entry->Function = Nic->DevFn & 7;
    Entry->OnBoard  = (Nic->OnboardIndex != 0xFFFF) ? 1 : 0;
    CopyMem (Entry->MacAddress, Nic->MacAddress, sizeof (Entry->MacAddress));

    DEBUG ((
      DEBUG_INFO,
      "GbtSysInfo: NIC %02x:%02x.%x %a %02x:%02x:%02x:%02x:%02x:%02x %a\n",
      (UINT32)Entry->Bus,
      (UINT32)Entry->Device,
      (UINT32)Entry->Function,
      Name,
      Entry->MacAddress[0],
      Entry->MacAddress[1],
      Entry->MacAddress[2],
      Entry->MacAddress[3],
      Entry->MacAddress[4],
      Entry->MacAddress[5],
      Entry->OnBoard ? "onboard" : "add-in"
      ));
  }

  Info->LanEntries = Entries;
  return EFI_SUCCESS;
}
