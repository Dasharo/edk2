/** @file
  Scan the entire PCI bus for root bridges to support coreboot UEFI payload.

  Copyright (c) 2016, Intel Corporation. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Base.h>
#include <PiDxe.h>
#include <IndustryStandard/Pci.h>
#include <Protocol/PciHostBridgeResourceAllocation.h>
#include <Protocol/PciRootBridgeIo.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PciHostBridgeLib.h>
#include <Library/PciLib.h>
#include <Library/DxeServicesTableLib.h>
#include "PciHostBridge.h"

/**
  Adjust the collected PCI resource.

  @param[in]  Io               IO aperture.

  @param[in]  Mem              MMIO aperture.

  @param[in]  MemAbove4G       MMIO aperture above 4G.

  @param[in]  PMem             Prefetchable MMIO aperture.

  @param[in]  PMemAbove4G      Prefetchable MMIO aperture above 4G.
**/
VOID
AdjustRootBridgeResource (
  IN  PCI_ROOT_BRIDGE_APERTURE *Io,
  IN  PCI_ROOT_BRIDGE_APERTURE *Mem,
  IN  PCI_ROOT_BRIDGE_APERTURE *MemAbove4G,
  IN  PCI_ROOT_BRIDGE_APERTURE *PMem,
  IN  PCI_ROOT_BRIDGE_APERTURE *PMemAbove4G
)
{
  UINT64  Mask;

  //
  // For now try to downgrade everything into MEM32 since
  // - coreboot does not assign resource above 4GB
  // - coreboot might allocate interleaved MEM32 and PMEM32 resource
  //   in some cases
  //
  if (PMem->Base < Mem->Base) {
    Mem->Base = PMem->Base;
  }

  if (PMem->Limit > Mem->Limit) {
    Mem->Limit = PMem->Limit;
  }

  PMem->Base  = MAX_UINT64;
  PMem->Limit = 0;

  if (MemAbove4G->Base < 0x100000000ULL) {
    if (MemAbove4G->Base < Mem->Base) {
      Mem->Base  = MemAbove4G->Base;
    }
    if (MemAbove4G->Limit > Mem->Limit) {
      Mem->Limit = MemAbove4G->Limit;
    }
    MemAbove4G->Base  = MAX_UINT64;
    MemAbove4G->Limit = 0;
  }

  if (PMemAbove4G->Base < 0x100000000ULL) {
    if (PMemAbove4G->Base < Mem->Base) {
      Mem->Base  = PMemAbove4G->Base;
    }
    if (PMemAbove4G->Limit > Mem->Limit) {
      Mem->Limit = PMemAbove4G->Limit;
    }
    PMemAbove4G->Base  = MAX_UINT64;
    PMemAbove4G->Limit = 0;
  }

  //
  // Align IO  resource at 4K  boundary
  //
  Mask        = 0xFFFULL;
  Io->Limit   = ((Io->Limit + Mask) & ~Mask) - 1;
  if (Io->Base != MAX_UINT64) {
    Io->Base &= ~Mask;
  }

  //
  // Align MEM resource at 1MB boundary
  //
  Mask        = 0xFFFFFULL;
  Mem->Limit  = ((Mem->Limit + Mask) & ~Mask) - 1;
  if (Mem->Base != MAX_UINT64) {
    Mem->Base &= ~Mask;
  }
}

/**
  Probe a bar is existed or not.

  @param[in]    Address           PCI address for the BAR.
  @param[out]   OriginalValue     The original bar value returned.
  @param[out]   Value             The probed bar value returned.
**/
STATIC
VOID
PcatPciRootBridgeBarExisted (
  IN  UINT64                         Address,
  OUT UINT32                         *OriginalValue,
  OUT UINT32                         *Value
)
{
  UINTN   PciAddress;

  PciAddress = (UINTN)Address;

  //
  // Preserve the original value
  //
  *OriginalValue = PciRead32 (PciAddress);

  //
  // Disable timer interrupt while the BAR is probed
  //
  DisableInterrupts ();

  PciWrite32 (PciAddress, 0xFFFFFFFF);
  *Value = PciRead32 (PciAddress);
  PciWrite32 (PciAddress, *OriginalValue);

  //
  // Enable interrupt
  //
  EnableInterrupts ();
}

STATIC
EFI_STATUS
PcatPciRootBridgeWarnAboutBrokenFW (
  IN  UINT64            Start,
  IN  UINT64            Limit
)
{
  EFI_STATUS                            Status;
  UINTN                                 Index;
  UINT64                                IntersectionBase;
  UINT64                                IntersectionEnd;
  UINTN                                 NumberOfDescriptors;
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR       *Map;

  Status = gDS->GetMemorySpaceMap (&NumberOfDescriptors, &Map);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: %a: GetMemorySpaceMap(): %r\n",
      gEfiCallerBaseName, __FUNCTION__, Status));
    return Status;
  }

  for (Index = 0; Index < NumberOfDescriptors; Index++) {
    if (Map[Index].GcdMemoryType == EfiGcdMemoryTypeNonExistent) {
      //
      // Ignore non existent entries
      //
      continue;
    }

    IntersectionBase = MAX (Start, Map[Index].BaseAddress);
    IntersectionEnd = MIN (Limit, Map[Index].BaseAddress + Map[Index].Length);
    if (IntersectionBase >= IntersectionEnd) {
      //
      // The descriptor and the aperture don't overlap.
      //
      continue;
    }

    if (Map[Index].GcdMemoryType == EfiGcdMemoryTypeReserved) {
      DEBUG ((EFI_D_ERROR, "%a: PCI aperature overlaps reserved memory region\n",
        __FUNCTION__));
      DEBUG ((EFI_D_ERROR, "%a: Please consider fixing EDK2 or your firmware\n",
        __FUNCTION__));
      return EFI_ACCESS_DENIED;
    }
  }
  return EFI_SUCCESS;
}

/**
  Parse PCI bar and collect the assigned PCI resource information.

  @param[in]  Command          Supported attributes.

  @param[in]  Bus              PCI bus number.

  @param[in]  Device           PCI device number.

  @param[in]  Function         PCI function number.

  @param[in]  BarOffsetBase    PCI bar start offset.

  @param[in]  BarOffsetEnd     PCI bar end offset.

  @param[in]  Io               IO aperture.

  @param[in]  Mem              MMIO aperture.

  @param[in]  MemAbove4G       MMIO aperture above 4G.

  @param[in]  PMem             Prefetchable MMIO aperture.

  @param[in]  PMemAbove4G      Prefetchable MMIO aperture above 4G.
**/
STATIC
VOID
PcatPciRootBridgeParseBars (
  IN UINT16                         Command,
  IN UINTN                          Bus,
  IN UINTN                          Device,
  IN UINTN                          Function,
  IN UINTN                          BarOffsetBase,
  IN UINTN                          BarOffsetEnd,
  IN PCI_ROOT_BRIDGE_APERTURE       *Io,
  IN PCI_ROOT_BRIDGE_APERTURE       *Mem,
  IN PCI_ROOT_BRIDGE_APERTURE       *MemAbove4G,
  IN PCI_ROOT_BRIDGE_APERTURE       *PMem,
  IN PCI_ROOT_BRIDGE_APERTURE       *PMemAbove4G

)
{
  UINT32                            OriginalValue;
  UINT32                            Value;
  UINT32                            OriginalUpperValue;
  UINT32                            UpperValue;
  UINT64                            Mask;
  UINTN                             Offset;
  UINTN                             LowBit;
  UINT64                            Base;
  UINT64                            Length;
  UINT64                            Limit;
  PCI_ROOT_BRIDGE_APERTURE          *MemAperture;
  UINT64                            NewBase;
  UINT64                            NewLength;
  EFI_STATUS                        Status;

  for (Offset = BarOffsetBase; Offset < BarOffsetEnd; Offset += sizeof (UINT32)) {
    PcatPciRootBridgeBarExisted (
      PCI_LIB_ADDRESS (Bus, Device, Function, Offset),
      &OriginalValue, &Value
    );
    if (Value == 0) {
      continue;
    }
    if ((Value & BIT0) == BIT0) {
      //
      // IO Bar
      //
      if (Command & EFI_PCI_COMMAND_IO_SPACE) {
        Mask = 0xfffffffc;
        Base = OriginalValue & Mask;
        Length = ((~(Value & Mask)) & Mask) + 0x04;
        if (!(Value & 0xFFFF0000)) {
          Length &= 0x0000FFFF;
        }
        Limit = Base + Length - 1;

        if ((Base > 0) && (Base < Limit)) {
          if (Io->Base > Base) {
            Io->Base = Base;
          }
          if (Io->Limit < Limit) {
            Io->Limit = Limit;
          }
        }
      }
    } else {
      //
      // Mem Bar
      //
      if (Command & EFI_PCI_COMMAND_MEMORY_SPACE) {

        Mask = 0xfffffff0;
        Base = OriginalValue & Mask;
        Length = Value & Mask;

        if ((Value & (BIT1 | BIT2)) == BIT2) {
          //
          // 64bit
          //
          Offset += 4;
          PcatPciRootBridgeBarExisted (
            PCI_LIB_ADDRESS (Bus, Device, Function, Offset),
            &OriginalUpperValue,
            &UpperValue
          );

          Base = Base | LShiftU64 ((UINT64) OriginalUpperValue, 32);
          Length = Length | LShiftU64 ((UINT64) UpperValue, 32);

          if (Length != 0) {
            LowBit = LowBitSet64 (Length);
            Length = LShiftU64 (1ULL, LowBit);
          }

          DEBUG ((EFI_D_INFO, "%a: PCI %x:%x.%x 64bit%s BAR@%d %x %x \n",
              __FUNCTION__,
              Bus,
              Device,
              Function,
              (Value & BIT3) ? L" prefetchable" : L"",
              Offset - 4,
              Base,
              Length));

          /* Intel PCH SMBus can have length of 0x100 only which does not align
           * with setting UC attribute */
          if (Length != 0 && Length < 0x1000)
            Length = 0x1000;

          if (Length != 0 && Base != 0) {
            if ((Base < SIZE_4GB)) {
              MemAperture = Mem;
        // FIXME: interleaved Pmem and Mem is not supported 
        //       if (((Value & BIT3) == BIT3)) {
        //         MemAperture = PMem;
        //       } else {
        //         MemAperture = Mem;
        //       }
            } else {
              MemAperture = MemAbove4G;
        // FIXME: interleaved Pmem and Mem is not supported 
        //       if (((Value & BIT3) == BIT3)) {
        //         MemAperture = PMemAbove4G;
        //       } else {
        //         MemAperture = MemAbove4G;
        //       }
            }
          } else {
            continue;
          }

        } else if ((Value & (BIT1 | BIT2)) == 0) {
          //
          // 32bit
          //
          Length = ((~Length) + 1) & 0xffffffff;

          /* Intel PCH SMBus can have length of 0x100 only which does not align
           * with setting UC attribute */
          if (Length != 0 && Length < 0x1000)
            Length = 0x1000;


          if ((Value & BIT3) == BIT3) {
        // FIXME: interleaved Pmem and Mem is not supported 
        //     MemAperture = PMem;
            DEBUG ((EFI_D_INFO, "%a: PCI %x:%x.%x 32bit prefetchable BAR@%d %x %x\n",
              __FUNCTION__,
              Bus,
              Device,
              Function,
              Offset,
              Base,
              Length));
          } else {
            MemAperture = Mem;
            DEBUG ((EFI_D_INFO, "%a: PCI %x:%x.%x 32bit BAR@%d %x %x\n",
              __FUNCTION__,
              Bus,
              Device,
              Function,
              Offset,
              Base,
              Length));
          }

        } else {
            DEBUG ((EFI_D_ERROR, "%a: Not adding PCI %x:%x.%x BAR@%d %x %x to aperature, unsupported BAR type\n",
              __FUNCTION__,
              Bus,
              Device,
              Function,
              Offset,
              Base,
              Length));
            continue;
        }

        Limit = Base + Length - 1;
        if ((Base > 0) && (Base < Limit)) {
          NewBase = MemAperture->Base;
          if (MemAperture->Base > Base) {
            NewBase = Base;
          }
          NewLength = MemAperture->Base;
          if (MemAperture->Limit < Limit) {
            NewLength = Base;
          }

          //
          // In case the BAR is placed on "the other side" of the MMCONF window
          // this would cause the aperature to cover the MMCONF window, which
          // isn't supported by EDK2.
          //
          Status = PcatPciRootBridgeWarnAboutBrokenFW(NewBase, NewLength);
          if (EFI_ERROR (Status)) {
            DEBUG ((EFI_D_ERROR, "%a: Not adding PCI %x:%x.%x BAR@%d %x:%x to aperature\n",
              __FUNCTION__,
              Bus,
              Device,
              Function,
              Offset,
              Base,
              Limit));
            continue;
          }

          if (MemAperture->Base > Base) {
            MemAperture->Base = Base;
          }
          if (MemAperture->Limit < Limit) {
            MemAperture->Limit = Limit;
          }
        }
      }
    }
  }
}

/**
  Scan for all root bridges in platform.

  @param[out] NumberOfRootBridges  Number of root bridges detected

  @retval     Pointer to the allocated PCI_ROOT_BRIDGE structure array.
**/
PCI_ROOT_BRIDGE *
ScanForRootBridges (
  OUT UINTN      *NumberOfRootBridges
)
{
  UINTN      PrimaryBus;
  UINTN      SubBus;
  UINT8      Device;
  UINT8      Function;
  UINTN      NumberOfDevices;
  UINTN      Address;
  PCI_TYPE01 Pci;
  UINT64     Attributes;
  UINT64     Base;
  UINT64     Limit;
  UINT64     Value;
  PCI_ROOT_BRIDGE_APERTURE Io, Mem, MemAbove4G, PMem, PMemAbove4G, *MemAperture;
  PCI_ROOT_BRIDGE *RootBridges;
  UINTN      BarOffsetEnd;


  *NumberOfRootBridges = 0;
  RootBridges = NULL;

  //
  // After scanning all the PCI devices on the PCI root bridge's primary bus,
  // update the Primary Bus Number for the next PCI root bridge to be this PCI
  // root bridge's subordinate bus number + 1.
  //
  for (PrimaryBus = 0; PrimaryBus <= PCI_MAX_BUS; PrimaryBus = SubBus + 1) {
    SubBus = PrimaryBus;
    Attributes = 0;

    ZeroMem (&Io, sizeof (Io));
    ZeroMem (&Mem, sizeof (Mem));
    ZeroMem (&MemAbove4G, sizeof (MemAbove4G));
    ZeroMem (&PMem, sizeof (PMem));
    ZeroMem (&PMemAbove4G, sizeof (PMemAbove4G));
    Io.Base = Mem.Base = MemAbove4G.Base = PMem.Base = PMemAbove4G.Base = MAX_UINT64;
    //
    // Scan all the PCI devices on the primary bus of the PCI root bridge
    //
    for (Device = 0, NumberOfDevices = 0; Device <= PCI_MAX_DEVICE; Device++) {

      for (Function = 0; Function <= PCI_MAX_FUNC; Function++) {

        //
        // Compute the PCI configuration address of the PCI device to probe
        //
        Address = PCI_LIB_ADDRESS (PrimaryBus, Device, Function, 0);

        //
        // Read the Vendor ID from the PCI Configuration Header
        //
        if (PciRead16 (Address) == MAX_UINT16) {
          if (Function == 0) {
            //
            // If the PCI Configuration Read fails, or a PCI device does not
            // exist, then skip this entire PCI device
            //
            break;
          } else {
            //
            // If PCI function != 0, VendorId == 0xFFFF, we continue to search
            // PCI function.
            //
            continue;
          }
        }

        //
        // Read the entire PCI Configuration Header
        //
        PciReadBuffer (Address, sizeof (Pci), &Pci);

        //
        // Increment the number of PCI device found on the primary bus of the
        // PCI root bridge
        //
        NumberOfDevices++;

        //
        // Look for devices with the VGA Palette Snoop enabled in the COMMAND
        // register of the PCI Config Header
        //
        if ((Pci.Hdr.Command & EFI_PCI_COMMAND_VGA_PALETTE_SNOOP) != 0) {
          Attributes |= EFI_PCI_ATTRIBUTE_VGA_PALETTE_IO;
          Attributes |= EFI_PCI_ATTRIBUTE_VGA_PALETTE_IO_16;
        }

        BarOffsetEnd = 0;

        //
        // PCI-PCI Bridge
        //
        if (IS_PCI_BRIDGE (&Pci)) {
          //
          // Get the Bus range that the PPB is decoding
          //
          if (Pci.Bridge.SubordinateBus > SubBus) {
            //
            // If the subordinate bus number of the PCI-PCI bridge is greater
            // than the PCI root bridge's current subordinate bus number,
            // then update the PCI root bridge's subordinate bus number
            //
            SubBus = Pci.Bridge.SubordinateBus;
          }

          //
          // Get the I/O range that the PPB is decoding
          //
          Value = Pci.Bridge.IoBase & 0x0f;
          Base = ((UINT32) Pci.Bridge.IoBase & 0xf0) << 8;
          Limit = (((UINT32) Pci.Bridge.IoLimit & 0xf0) << 8) | 0x0fff;
          if (Value == BIT0) {
            Base |= ((UINT32) Pci.Bridge.IoBaseUpper16 << 16);
            Limit |= ((UINT32) Pci.Bridge.IoLimitUpper16 << 16);
          }
          if ((Base > 0) && (Base < Limit)) {
            if (Io.Base > Base) {
              Io.Base = Base;
            }
            if (Io.Limit < Limit) {
              Io.Limit = Limit;
            }
          }

          //
          // Get the Memory range that the PPB is decoding
          //
          Base = ((UINT32) Pci.Bridge.MemoryBase & 0xfff0) << 16;
          Limit = (((UINT32) Pci.Bridge.MemoryLimit & 0xfff0) << 16) | 0xfffff;
          if ((Base > 0) && (Base < Limit)) {
            if (Mem.Base > Base) {
              Mem.Base = Base;
            }
            if (Mem.Limit < Limit) {
              Mem.Limit = Limit;
            }
          }

          //
          // Get the Prefetchable Memory range that the PPB is decoding
          //
          Value = Pci.Bridge.PrefetchableMemoryBase & 0x0f;
          Base = ((UINT32) Pci.Bridge.PrefetchableMemoryBase & 0xfff0) << 16;
          Limit = (((UINT32) Pci.Bridge.PrefetchableMemoryLimit & 0xfff0)
                   << 16) | 0xfffff;
        // FIXME: interleaved Pmem and Mem is not supported 
        //   MemAperture = &PMem;
          MemAperture = &Mem;
          if (Value == BIT0) {
            Base |= LShiftU64 (Pci.Bridge.PrefetchableBaseUpper32, 32);
            Limit |= LShiftU64 (Pci.Bridge.PrefetchableLimitUpper32, 32);
            if (Base > SIZE_4GB)
        // FIXME: interleaved Pmem and Mem is not supported 
        //       MemAperture = &PMemAbove4G;
              MemAperture = &MemAbove4G;
          }
          if ((Base > 0) && (Base < Limit)) {
            if (MemAperture->Base > Base) {
              MemAperture->Base = Base;
            }
            if (MemAperture->Limit < Limit) {
              MemAperture->Limit = Limit;
            }
          }

          //
          // Look at the PPB Configuration for legacy decoding attributes
          //
          if ((Pci.Bridge.BridgeControl & EFI_PCI_BRIDGE_CONTROL_ISA)
              == EFI_PCI_BRIDGE_CONTROL_ISA) {
            Attributes |= EFI_PCI_ATTRIBUTE_ISA_IO;
            Attributes |= EFI_PCI_ATTRIBUTE_ISA_IO_16;
            Attributes |= EFI_PCI_ATTRIBUTE_ISA_MOTHERBOARD_IO;
          }
          if ((Pci.Bridge.BridgeControl & EFI_PCI_BRIDGE_CONTROL_VGA)
              == EFI_PCI_BRIDGE_CONTROL_VGA) {
            Attributes |= EFI_PCI_ATTRIBUTE_VGA_PALETTE_IO;
            Attributes |= EFI_PCI_ATTRIBUTE_VGA_MEMORY;
            Attributes |= EFI_PCI_ATTRIBUTE_VGA_IO;
            if ((Pci.Bridge.BridgeControl & EFI_PCI_BRIDGE_CONTROL_VGA_16)
                != 0) {
              Attributes |= EFI_PCI_ATTRIBUTE_VGA_PALETTE_IO_16;
              Attributes |= EFI_PCI_ATTRIBUTE_VGA_IO_16;
            }
          }

          BarOffsetEnd = OFFSET_OF (PCI_TYPE01, Bridge.Bar[2]);
        } else {
          //
          // Parse the BARs of the PCI device to get what I/O Ranges, Memory
          // Ranges, and Prefetchable Memory Ranges the device is decoding
          //
          if ((Pci.Hdr.HeaderType & HEADER_LAYOUT_CODE) == HEADER_TYPE_DEVICE) {
            BarOffsetEnd = OFFSET_OF (PCI_TYPE00, Device.Bar[6]);
          }
        }

        PcatPciRootBridgeParseBars (
          Pci.Hdr.Command,
          PrimaryBus,
          Device,
          Function,
          OFFSET_OF (PCI_TYPE00, Device.Bar),
          BarOffsetEnd,
          &Io,
          &Mem, &MemAbove4G,
          &PMem, &PMemAbove4G
        );

        //
        // See if the PCI device is an IDE controller
        //
        if (IS_CLASS2 (&Pci, PCI_CLASS_MASS_STORAGE,
                       PCI_CLASS_MASS_STORAGE_IDE)) {
          if (Pci.Hdr.ClassCode[0] & 0x80) {
            Attributes |= EFI_PCI_ATTRIBUTE_IDE_PRIMARY_IO;
            Attributes |= EFI_PCI_ATTRIBUTE_IDE_SECONDARY_IO;
          }
          if (Pci.Hdr.ClassCode[0] & 0x01) {
            Attributes |= EFI_PCI_ATTRIBUTE_IDE_PRIMARY_IO;
          }
          if (Pci.Hdr.ClassCode[0] & 0x04) {
            Attributes |= EFI_PCI_ATTRIBUTE_IDE_SECONDARY_IO;
          }
        }

        //
        // See if the PCI device is a legacy VGA controller or
        // a standard VGA controller
        //
        if (IS_CLASS2 (&Pci, PCI_CLASS_OLD, PCI_CLASS_OLD_VGA) ||
            IS_CLASS2 (&Pci, PCI_CLASS_DISPLAY, PCI_CLASS_DISPLAY_VGA)
           ) {
          Attributes |= EFI_PCI_ATTRIBUTE_VGA_PALETTE_IO;
          Attributes |= EFI_PCI_ATTRIBUTE_VGA_PALETTE_IO_16;
          Attributes |= EFI_PCI_ATTRIBUTE_VGA_MEMORY;
          Attributes |= EFI_PCI_ATTRIBUTE_VGA_IO;
          Attributes |= EFI_PCI_ATTRIBUTE_VGA_IO_16;
        }

        //
        // See if the PCI Device is a PCI - ISA or PCI - EISA
        // or ISA_POSITIVE_DECODE Bridge device
        //
        if (Pci.Hdr.ClassCode[2] == PCI_CLASS_BRIDGE) {
          if (Pci.Hdr.ClassCode[1] == PCI_CLASS_BRIDGE_ISA ||
              Pci.Hdr.ClassCode[1] == PCI_CLASS_BRIDGE_EISA ||
              Pci.Hdr.ClassCode[1] == PCI_CLASS_BRIDGE_ISA_PDECODE) {
            Attributes |= EFI_PCI_ATTRIBUTE_ISA_IO;
            Attributes |= EFI_PCI_ATTRIBUTE_ISA_IO_16;
            Attributes |= EFI_PCI_ATTRIBUTE_ISA_MOTHERBOARD_IO;
          }
        }

        //
        // If this device is not a multi function device, then skip the rest
        // of this PCI device
        //
        if (Function == 0 && !IS_PCI_MULTI_FUNC (&Pci)) {
          break;
        }
      }
    }

    //
    // If at least one PCI device was found on the primary bus of this PCI
    // root bridge, then the PCI root bridge exists.
    //
    if (NumberOfDevices > 0) {
      RootBridges = ReallocatePool (
                      (*NumberOfRootBridges) * sizeof (PCI_ROOT_BRIDGE),
                      (*NumberOfRootBridges + 1) * sizeof (PCI_ROOT_BRIDGE),
                      RootBridges
                    );
      ASSERT (RootBridges != NULL);

      if (0)
        AdjustRootBridgeResource (&Io, &Mem, &MemAbove4G, &PMem, &PMemAbove4G);

      InitRootBridge (
        Attributes, Attributes, 0,
        (UINT8) PrimaryBus, (UINT8) SubBus,
        &Io, &Mem, &MemAbove4G, &PMem, &PMemAbove4G,
        &RootBridges[*NumberOfRootBridges]
      );
      RootBridges[*NumberOfRootBridges].ResourceAssigned = TRUE;
      //
      // Increment the index for the next PCI Root Bridge
      //
      (*NumberOfRootBridges)++;
    }
  }

  return RootBridges;
}
