/** @file
  Library instance of PciHostBridgeLib library class for coreboot.

  Copyright (C) 2016, Red Hat, Inc.
  Copyright (c) 2016, Intel Corporation. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/
#include <PiDxe.h>

#include <IndustryStandard/Pci.h>
#include <Protocol/PciHostBridgeResourceAllocation.h>
#include <Protocol/PciRootBridgeIo.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PciHostBridgeLib.h>
#include <Library/PciLib.h>
#include <Library/PcdLib.h>

#include "PciHostBridge.h"

STATIC
CONST
CB_PCI_ROOT_BRIDGE_DEVICE_PATH mRootBridgeDevicePathTemplate = {
  {
    {
      ACPI_DEVICE_PATH,
      ACPI_DP,
      {
        (UINT8) (sizeof(ACPI_HID_DEVICE_PATH)),
        (UINT8) ((sizeof(ACPI_HID_DEVICE_PATH)) >> 8)
      }
    },
    EISA_PNP_ID(0x0A03), // HID
    0                    // UID
  },

  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      END_DEVICE_PATH_LENGTH,
      0
    }
  }
};


EFI_STATUS
GetTopOfMemory (UINT64 *TopOfMemory)
{
  UINTN                           NumberOfDescriptors;
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR *MemorySpaceMap;
  UINTN                           Index;
  UINT64                          TopOfRam = 0;

  if (!TopOfMemory)
    return EFI_INVALID_PARAMETER;

  gDS->GetMemorySpaceMap (&NumberOfDescriptors, &MemorySpaceMap);

  for (Index = 0; Index < NumberOfDescriptors; Index++) {
   DEBUG ((EFI_D_INFO, "MemEntry: Base %llx Length %llx Type %x\n",
           MemorySpaceMap[Index].BaseAddress, MemorySpaceMap[Index].Length,
          MemorySpaceMap[Index].GcdMemoryType));
    if ((MemorySpaceMap[Index].GcdMemoryType == EfiGcdMemoryTypeSystemMemory)) {
      if (MemorySpaceMap[Index].BaseAddress >= 0x100000000ULL &&
          MemorySpaceMap[Index].Length != 0) {
        if (MemorySpaceMap[Index].BaseAddress + MemorySpaceMap[Index].Length > TopOfRam)
          TopOfRam = MemorySpaceMap[Index].BaseAddress + MemorySpaceMap[Index].Length;
      }
    }
  }

  *TopOfMemory = TopOfRam;
  DEBUG ((EFI_D_INFO, "TopOfRam: %llx \n", TopOfRam));

  return EFI_SUCCESS;
}

/**
  Get physical address bits.

  @return Physical address bits.

**/
UINT8
GetPhysicalAddressBits (
  VOID
  )
{
  UINT32                        RegEax;
  UINT8                         PhysicalAddressBits;
  VOID                          *Hob;

  //
  // Get physical address bits supported.
  //
  Hob = GetFirstHob (EFI_HOB_TYPE_CPU);
  if (Hob != NULL) {
    PhysicalAddressBits = ((EFI_HOB_CPU *) Hob)->SizeOfMemorySpace;
  } else {
    AsmCpuid (0x80000000, &RegEax, NULL, NULL, NULL);
    if (RegEax >= 0x80000008) {
      AsmCpuid (0x80000008, &RegEax, NULL, NULL, NULL);
      PhysicalAddressBits = (UINT8) RegEax;
    } else {
      PhysicalAddressBits = 36;
    }
  }

  //
  // IA-32e paging translates 48-bit linear addresses to 52-bit physical addresses.
  //
  ASSERT (PhysicalAddressBits <= 52);
  if (PhysicalAddressBits > 48) {
    PhysicalAddressBits = 48;
  }

  return PhysicalAddressBits;
}

/**
  Initialize a PCI_ROOT_BRIDGE structure.

  @param[in]  Supports         Supported attributes.

  @param[in]  Attributes       Initial attributes.

  @param[in]  AllocAttributes  Allocation attributes.

  @param[in]  RootBusNumber    The bus number to store in RootBus.

  @param[in]  MaxSubBusNumber  The inclusive maximum bus number that can be
                               assigned to any subordinate bus found behind any
                               PCI bridge hanging off this root bus.

                               The caller is responsible for ensuring that
                               RootBusNumber <= MaxSubBusNumber. If
                               RootBusNumber equals MaxSubBusNumber, then the
                               root bus has no room for subordinate buses.

  @param[in]  Io               IO aperture.

  @param[in]  Mem              MMIO aperture.

  @param[in]  MemAbove4G       MMIO aperture above 4G.

  @param[in]  PMem             Prefetchable MMIO aperture.

  @param[in]  PMemAbove4G      Prefetchable MMIO aperture above 4G.

  @param[out] RootBus          The PCI_ROOT_BRIDGE structure (allocated by the
                               caller) that should be filled in by this
                               function.

  @retval EFI_SUCCESS           Initialization successful. A device path
                                consisting of an ACPI device path node, with
                                UID = RootBusNumber, has been allocated and
                                linked into RootBus.

  @retval EFI_OUT_OF_RESOURCES  Memory allocation failed.
**/
EFI_STATUS
InitRootBridge (
  IN  UINT64                   Supports,
  IN  UINT64                   Attributes,
  IN  UINT64                   AllocAttributes,
  IN  UINT8                    RootBusNumber,
  IN  UINT8                    MaxSubBusNumber,
  IN  PCI_ROOT_BRIDGE_APERTURE *Io,
  IN  PCI_ROOT_BRIDGE_APERTURE *Mem,
  IN  PCI_ROOT_BRIDGE_APERTURE *MemAbove4G,
  IN  PCI_ROOT_BRIDGE_APERTURE *PMem,
  IN  PCI_ROOT_BRIDGE_APERTURE *PMemAbove4G,
  OUT PCI_ROOT_BRIDGE          *RootBus
)
{
  CB_PCI_ROOT_BRIDGE_DEVICE_PATH *DevicePath;
  UINT64                         TopOfMemory;
  EFI_STATUS                     Status;

  TopOfMemory = 0;

  Status = GetTopOfMemory(&TopOfMemory);
  if (EFI_ERROR(Status))
    TopOfMemory = 0;

  //
  // Be safe if other fields are added to PCI_ROOT_BRIDGE later.
  //
  ZeroMem (RootBus, sizeof *RootBus);

  RootBus->Segment = 0;

  RootBus->Supports   = Supports;
  RootBus->Attributes = Attributes;

  RootBus->DmaAbove4G = TRUE;

  RootBus->AllocationAttributes = AllocAttributes;
  RootBus->Bus.Base  = RootBusNumber;
  RootBus->Bus.Limit = (PcdGet64(PcdPciExpressBaseSize) / SIZE_1MB) - 1ULL;
  Mem->Limit = PcdGet64(PcdPciExpressBaseAddress) - 1ULL;
  if (TopOfMemory > SIZE_4GB) {
    MemAbove4G->Base = TopOfMemory;
    MemAbove4G->Limit = LShiftU64(1ULL, GetPhysicalAddressBits()) - 1ULL;
  }

  Io->Base = 0x2000;
  Io->Limit = 0xFFFF;

  CopyMem (&RootBus->Io, Io, sizeof (*Io));
  CopyMem (&RootBus->Mem, Mem, sizeof (*Mem));
  CopyMem (&RootBus->MemAbove4G, MemAbove4G, sizeof (*MemAbove4G));
  CopyMem (&RootBus->PMem, PMem, sizeof (*PMem));
  CopyMem (&RootBus->PMemAbove4G, PMemAbove4G, sizeof (*PMemAbove4G));

  RootBus->NoExtendedConfigSpace = FALSE;

  DevicePath = AllocateCopyPool (sizeof (mRootBridgeDevicePathTemplate),
                                 &mRootBridgeDevicePathTemplate);
  if (DevicePath == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: %r\n", __FUNCTION__, EFI_OUT_OF_RESOURCES));
    return EFI_OUT_OF_RESOURCES;
  }
  DevicePath->AcpiDevicePath.UID = RootBusNumber;
  RootBus->DevicePath = (EFI_DEVICE_PATH_PROTOCOL *)DevicePath;

  DEBUG ((DEBUG_INFO,
          "%a: populated root bus %d, with room for %d subordinate bus(es)\n",
          __FUNCTION__, RootBusNumber, MaxSubBusNumber - RootBusNumber));
  return EFI_SUCCESS;
}


/**
  Return all the root bridge instances in an array.

  @param Count  Return the count of root bridge instances.

  @return All the root bridge instances in an array.
          The array should be passed into PciHostBridgeFreeRootBridges()
          when it's not used.
**/
PCI_ROOT_BRIDGE *
EFIAPI
PciHostBridgeGetRootBridges (
  UINTN *Count
)
{
  return ScanForRootBridges (Count);
}


/**
  Free the root bridge instances array returned from
  PciHostBridgeGetRootBridges().

  @param  The root bridge instances array.
  @param  The count of the array.
**/
VOID
EFIAPI
PciHostBridgeFreeRootBridges (
  PCI_ROOT_BRIDGE *Bridges,
  UINTN           Count
)
{
  if (Bridges == NULL && Count == 0) {
    return;
  }
  ASSERT (Bridges != NULL && Count > 0);

  do {
    --Count;
    FreePool (Bridges[Count].DevicePath);
  } while (Count > 0);

  FreePool (Bridges);
}


/**
  Inform the platform that the resource conflict happens.

  @param HostBridgeHandle Handle of the Host Bridge.
  @param Configuration    Pointer to PCI I/O and PCI memory resource
                          descriptors. The Configuration contains the resources
                          for all the root bridges. The resource for each root
                          bridge is terminated with END descriptor and an
                          additional END is appended indicating the end of the
                          entire resources. The resource descriptor field
                          values follow the description in
                          EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL
                          .SubmitResources().
**/
VOID
EFIAPI
PciHostBridgeResourceConflict (
  EFI_HANDLE                        HostBridgeHandle,
  VOID                              *Configuration
)
{
  //
  // coreboot UEFI Payload does not do PCI enumeration and should not call this
  // library interface.
  //
  ASSERT (FALSE);
}
