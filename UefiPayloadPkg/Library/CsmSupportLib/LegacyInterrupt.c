/** @file
  Legacy Interrupt Support

  Copyright (c) 2006 - 2011, Intel Corporation. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "LegacyInterrupt.h"

//
// Handle for the Legacy Interrupt Protocol instance produced by this driver
//
STATIC EFI_HANDLE mLegacyInterruptHandle = NULL;

//
// Legacy Interrupt Device number and function
//
STATIC UINT8      mLegacyInterruptDevice;
STATIC UINT8      mLegacyInterruptDeviceFunction;

//
// Variables holding the processor vendor
//
STATIC BOOLEAN ProcIsIntel = FALSE;
STATIC BOOLEAN ProcIsAMD = FALSE;

//
// The Legacy Interrupt Protocol instance produced by this driver
//
STATIC EFI_LEGACY_INTERRUPT_PROTOCOL mLegacyInterrupt = {
  GetNumberPirqs,
  GetLocation,
  ReadPirq,
  WritePirq
};

STATIC UINT8 PirqReg[MAX_PIRQ_NUMBER] = { PIRQA, PIRQB, PIRQC, PIRQD, PIRQE, PIRQF, PIRQG, PIRQH };

/* FCH index/data registers */
#define PCI_INTR_INDEX  0xc00
#define PCI_INTR_DATA   0xc01
#define MODE_PIC        0

/*
 * Read the FCH PCI_INTR registers 0xC00/0xC01 at a
 * given index and a given PIC (0) or IOAPIC (1) mode
 */
STATIC
UINT8
ReadPciIntIndex(
  IN UINT8 Index,
  IN UINT8 Mode)
{
	IoWrite8((UINTN)PCI_INTR_INDEX, (Mode << 7) | Index);
	return IoRead8((UINTN)PCI_INTR_DATA);
}

/*
 * Write a value to the FCH PCI_INTR registers 0xC00/0xC01
 * at a given index and PIC (0) or IOAPIC (1) mode
 */
STATIC
VOID
WritePciIntIndex(
  IN UINT8 Index, 
  IN UINT8 Mode,
  IN UINT8 Data)
{
	IoWrite8((UINTN)PCI_INTR_INDEX, (Mode << 7) | Index);
	IoWrite8((UINTN)PCI_INTR_DATA, Data);
}

/* Intel sideband related definitions */
STATIC UINT8 PidItss;
STATIC UINTN PcrBaseAddress = 0;
STATIC UINT8 PirqsOnSideband = FALSE;
#define PCI_DEVICE_ID_INTEL_GLK_LPC 	0x31E8
#define PCI_DEVICE_ID_INTEL_APL_LPC		0x5ae8
#define PCR_ITSS_PIRQA_ROUT	          0x3100
#define PCR_PORTID_SHIFT              16
#define ALIGN_DOWN(x, a)              ((x) & ~((__typeof__(x))(a)-1UL))
#define IS_ALIGNED(x, a)              (((x) & ((__typeof__(x))(a)-1UL)) == 0)

STATIC
UINTN
PcrRegAddress(
  IN UINT8 Pid,
  IN UINT8 Offset
  )
{
	UINTN RegAddr;

	/* Create an address based off of port id and offset. */
	RegAddr = PcrBaseAddress;
	RegAddr += ((UINTN)Pid) << PCR_PORTID_SHIFT;
	RegAddr += (UINTN)Offset;

	return RegAddr;
}

STATIC
UINT32
PcrRead32(
  IN UINT8  Pid,
  IN UINT16 Offset
  )
{
	/* Ensure the PCR offset is correctly aligned. */
	ASSERT(IS_ALIGNED(Offset, sizeof(UINT32)));

	return MmioRead32(PcrRegAddress(Pid, Offset));
}

/*
 * After every write one needs to perform a read an innocuous register to
 * ensure the writes are completed for certain ports. This is done for
 * all ports so that the callers don't need the per-port knowledge for
 * each transaction.
 */
STATIC
VOID
WriteCompletion(
  IN UINT8  Pid,
  IN UINT16 Offset
  )
{
	MmioRead32(PcrRegAddress(Pid, ALIGN_DOWN(Offset, sizeof(UINT32))));
}

STATIC
VOID
PcrWrite32(
  IN UINT8  Pid,
  IN UINT16 Offset,
  IN UINT32 Indata
  )
{
	/* Ensure the PCR offset is correctly aligned. */
	ASSERT(IS_ALIGNED(Offset, sizeof(Indata)));

	MmioWrite32(PcrRegAddress(Pid, Offset), Indata);
	/* Ensure the writes complete. */
	WriteCompletion(Pid, Offset);
}


STATIC
VOID
WritePirqSideband(
  IN  UINT8                          PirqNumber,
  IN  UINT8                          PirqData
  )
{
  UINT8               Pirqs[8];
  UINT32              Regs[2];

  Regs[0] =	PcrRead32(PidItss, PCR_ITSS_PIRQA_ROUT);
  Regs[1] =	PcrRead32(PidItss, PCR_ITSS_PIRQA_ROUT + sizeof(UINT32));

  CopyMem(Pirqs, Regs, MAX_PIRQ_NUMBER);
  Pirqs[PirqNumber] = PirqData;
  CopyMem(Regs, Pirqs, MAX_PIRQ_NUMBER);

  PcrWrite32(PidItss,  PCR_ITSS_PIRQA_ROUT , Regs[0]);
  PcrWrite32(PidItss,  PCR_ITSS_PIRQA_ROUT + sizeof(UINT32), Regs[1]);
}

STATIC
UINT8
ReadPirqSideband(
  IN  UINT8                          PirqNumber
  )
{
  UINT8               Pirqs[8];
  UINT32              Regs[2];

  Regs[0] =	PcrRead32(PidItss, PCR_ITSS_PIRQA_ROUT);
  Regs[1] =	PcrRead32(PidItss, PCR_ITSS_PIRQA_ROUT + sizeof(UINT32));

  CopyMem(Pirqs, Regs, MAX_PIRQ_NUMBER);

  return (Pirqs[PirqNumber] & 0x7f);
}


/**
  Return the number of PIRQs supported by chipset.

  @param[in]  This         Pointer to LegacyInterrupt Protocol
  @param[out] NumberPirqs  The pointer to return the max IRQ number supported

  @retval EFI_SUCCESS   Max PIRQs successfully returned

**/
EFI_STATUS
EFIAPI
GetNumberPirqs (
  IN  EFI_LEGACY_INTERRUPT_PROTOCOL  *This,
  OUT UINT8                          *NumberPirqs
  )
{
  *NumberPirqs = MAX_PIRQ_NUMBER;

  return EFI_SUCCESS;
}


/**
  Return PCI location of this device.
  $PIR table requires this info.

  @param[in]   This                - Protocol instance pointer.
  @param[out]  Bus                 - PCI Bus
  @param[out]  Device              - PCI Device
  @param[out]  Function            - PCI Function

  @retval  EFI_SUCCESS   Bus/Device/Function returned

**/
EFI_STATUS
EFIAPI
GetLocation (
  IN  EFI_LEGACY_INTERRUPT_PROTOCOL  *This,
  OUT UINT8                          *Bus,
  OUT UINT8                          *Device,
  OUT UINT8                          *Function
  )
{
  *Bus      = LEGACY_INT_BUS;
  *Device   = mLegacyInterruptDevice;
  *Function = mLegacyInterruptDeviceFunction;

  return EFI_SUCCESS;
}


/**
  Builds the PCI configuration address for the register specified by PirqNumber

  @param[in]  PirqNumber - The PIRQ number to build the PCI configuration address for

  @return  The PCI Configuration address for the PIRQ
**/
UINTN
GetAddress (
  UINT8  PirqNumber
  )
{
  return PCI_LIB_ADDRESS(
          LEGACY_INT_BUS,
          mLegacyInterruptDevice,
          LEGACY_INT_FUNC,
          PirqReg[PirqNumber]
          );
}

/**
  Read the given PIRQ register

  @param[in]  This        Protocol instance pointer
  @param[in]  PirqNumber  The Pirq register 0 = A, 1 = B etc
  @param[out] PirqData    Value read

  @retval EFI_SUCCESS   Decoding change affected.
  @retval EFI_INVALID_PARAMETER   Invalid PIRQ number

**/
EFI_STATUS
EFIAPI
ReadPirq (
  IN  EFI_LEGACY_INTERRUPT_PROTOCOL  *This,
  IN  UINT8                          PirqNumber,
  OUT UINT8                          *PirqData
  )
{
  if (PirqNumber >= MAX_PIRQ_NUMBER) {
    return EFI_INVALID_PARAMETER;
  }

  if (ProcIsIntel) {
    if (PirqsOnSideband) {
      *PirqData = ReadPirqSideband(PirqNumber);
      *PirqData = (UINT8) (*PirqData & 0x7f);
      return EFI_SUCCESS;
    } else {
      *PirqData = PciRead8 (GetAddress (PirqNumber));
      *PirqData = (UINT8) (*PirqData & 0x7f);
      return EFI_SUCCESS;
    }
  }
  
  if (ProcIsAMD) {
    *PirqData = ReadPciIntIndex (PirqNumber, MODE_PIC);
    *PirqData = (UINT8) (*PirqData & 0x1f);
    /* Check if unused or not programmed */
    if (*PirqData == 0x1F) {
      *PirqData = 0;
    }
    return EFI_SUCCESS;
  }

  return EFI_DEVICE_ERROR;
}


/**
  Write the given PIRQ register

  @param[in]  This        Protocol instance pointer
  @param[in]  PirqNumber  The Pirq register 0 = A, 1 = B etc
  @param[out] PirqData    Value to write

  @retval EFI_SUCCESS   Decoding change affected.
  @retval EFI_INVALID_PARAMETER   Invalid PIRQ number

**/
EFI_STATUS
EFIAPI
WritePirq (
  IN  EFI_LEGACY_INTERRUPT_PROTOCOL  *This,
  IN  UINT8                          PirqNumber,
  IN  UINT8                          PirqData
  )
{
  if (PirqNumber >= MAX_PIRQ_NUMBER) {
    return EFI_INVALID_PARAMETER;
  }

  if (ProcIsIntel) {
    if (PirqsOnSideband) {
      WritePirqSideband(PirqNumber, PirqData);
      return EFI_SUCCESS;
    } else {
      PciWrite8 (GetAddress (PirqNumber), PirqData);
      return EFI_SUCCESS;
    }
  }
  
  if (ProcIsAMD) {
    WritePciIntIndex (PirqNumber, MODE_PIC, PirqData);
    return EFI_SUCCESS;
  }

  return EFI_DEVICE_ERROR;
}

STATIC
VOID
CheckPirqsOnSideband (
  VOID
  )
{
  UINT8 Pirq;
  UINT8 PirqNumber;
  UINT8 PirqAllZeros = 0;

  for (PirqNumber = 0; PirqNumber < ARRAY_SIZE(PirqReg); PirqNumber++) {
    Pirq = PciRead8 (GetAddress (PirqReg[PirqNumber]));
    if (Pirq == 0) {
      PirqAllZeros |= (1 << PirqNumber);
    }
  }

  /* All PIRQ registers on LPC device are zeros, probably they are located on sideband */
  if (PirqAllZeros == 0xFF) {
    PirqsOnSideband = TRUE;
  }
}

/**
  Initialize Legacy Interrupt support

  @retval EFI_SUCCESS   Successfully initialized

**/
EFI_STATUS
LegacyInterruptInstall (
  VOID
  )
{
  UINT16      HostBridgeVenId;
  UINT16      ChipsetDevId;
  EFI_STATUS  Status;

  //
  // Make sure the Legacy Interrupt Protocol is not already installed in the system
  //
  ASSERT_PROTOCOL_ALREADY_INSTALLED(NULL, &gEfiLegacyInterruptProtocolGuid);

  //
  // Query Host Bridge DID to determine platform type, then set device number
  //
  HostBridgeVenId = PciRead16 (PCI_LIB_ADDRESS(0, 0, 0, 0));
  switch (HostBridgeVenId) {
    /* Intel */
    case 0x8086:
      DEBUG ((DEBUG_INFO, "%a: Intel Host Bridge Device found\n", __FUNCTION__));
      ProcIsIntel = TRUE;
      mLegacyInterruptDevice = 0x1f;
      mLegacyInterruptDeviceFunction = 0;
      CheckPirqsOnSideband();
      ChipsetDevId = PciRead16 (PCI_LIB_ADDRESS(0, 0x1f, 0, 0x02));
      if (PirqsOnSideband) {
        if (ChipsetDevId == PCI_DEVICE_ID_INTEL_APL_LPC ||
            ChipsetDevId == PCI_DEVICE_ID_INTEL_GLK_LPC) {
          /* Unhide PS2B */
          PciWrite8 (PCI_LIB_ADDRESS(0, 0x0d, 0, 0xe1), 0);
          /* Get PS2B BAR */
          PcrBaseAddress = (UINTN) PciRead32 (PCI_LIB_ADDRESS(0, 0x0d, 0, 0x10));
          /* Hide PS2SB */
          PciWrite8 (PCI_LIB_ADDRESS(0, 0x0d, 0, 0xe1), 1);
          PidItss = 0xD0;
        } else {
          /* Unhide PS2B */
          PciWrite8 (PCI_LIB_ADDRESS(0, 0x1f, 1, 0xe1), 0);
          /* Get PS2B BAR */
          PcrBaseAddress = (UINTN) PciRead32 (PCI_LIB_ADDRESS(0, 0x1f, 1, 0x10));
          /* Hide PS2SB */
          PciWrite8 (PCI_LIB_ADDRESS(0, 0x1f, 1, 0xe1), 1);
          PidItss = 0xC4;
        }
      }
      DEBUG ((DEBUG_INFO, "%a: Intel P2SB address %p\n", __FUNCTION__, PcrBaseAddress));
      break;
    /* AMD */
    case 0x1022:
      DEBUG ((DEBUG_INFO, "%a: AMD Host Bridge Device found\n", __FUNCTION__));
      ProcIsAMD = TRUE;
      mLegacyInterruptDevice = 0x14;
      mLegacyInterruptDeviceFunction = 3;
      break;
    default:
      DEBUG ((EFI_D_ERROR, "%a: Unknown Host Bridge Device Vendor ID: 0x%04x\n",
        __FUNCTION__, HostBridgeVenId));
      ASSERT (FALSE);
      return EFI_UNSUPPORTED;
  }

  //
  // Make a new handle and install the protocol
  //
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mLegacyInterruptHandle,
                  &gEfiLegacyInterruptProtocolGuid,
                  &mLegacyInterrupt,
                  NULL
                  );
  ASSERT_EFI_ERROR(Status);

  return Status;
}

