/** @file
  Implementation for a generic i801 SMBus driver.

Copyright (c) 2016, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent


**/

#include "SMBusi801Dxe.h"
#include <Bus/Pci/PciBusDxe/PciBus.h>
#include <Library/TimerLib.h>
#include <Library/IoLib.h>

EFI_HANDLE      mDriverHandle = NULL;
EFI_HANDLE      mPciDevice = NULL;

/* SMBus register offsets. */
#define SMBHSTSTAT		0x0
#define SMBHSTCTL		0x2
#define SMBHSTCMD		0x3
#define SMBXMITADD		0x4
#define SMBHSTDAT0		0x5
#define SMBHSTDAT1		0x6
#define SMBBLKDAT		0x7
#define SMBTRNSADD		0x9
#define SMBSLVDATA		0xa
#define SMLINK_PIN_CTL		0xe
#define SMBUS_PIN_CTL		0xf
#define SMBSLVCMD		0x11

/* I801 command constants */
#define I801_QUICK		(0 << 2)
#define I801_BYTE		(1 << 2)
#define I801_BYTE_DATA		(2 << 2)
#define I801_WORD_DATA		(3 << 2)
#define I801_PROCESS_CALL	(4 << 2)
#define I801_BLOCK_DATA		(5 << 2)
#define I801_I2C_BLOCK_DATA	(6 << 2) /* ICH5 and later */

/* I801 Host Control register bits */
#define SMBHSTCNT_INTREN	(1 << 0)
#define SMBHSTCNT_KILL		(1 << 1)
#define SMBHSTCNT_LAST_BYTE	(1 << 5)
#define SMBHSTCNT_START		(1 << 6)
#define SMBHSTCNT_PEC_EN	(1 << 7) /* ICH3 and later */

/* I801 Hosts Status register bits */
#define SMBHSTSTS_BYTE_DONE	(1 << 7)
#define SMBHSTSTS_INUSE_STS	(1 << 6)
#define SMBHSTSTS_SMBALERT_STS	(1 << 5)
#define SMBHSTSTS_FAILED	(1 << 4)
#define SMBHSTSTS_BUS_ERR	(1 << 3)
#define SMBHSTSTS_DEV_ERR	(1 << 2)
#define SMBHSTSTS_INTR		(1 << 1)
#define SMBHSTSTS_HOST_BUSY	(1 << 0)

/* For SMBXMITADD register. */
#define XMIT_WRITE(dev)		(((dev) << 1) | 0)
#define XMIT_READ(dev)		(((dev) << 1) | 1)

STATIC
UINT16
EFIAPI
SmbusGetSMBaseAddress (
  IN VOID
  )
{
  EFI_STATUS                           Status;
  IN EFI_PCI_IO_PROTOCOL               *PciIo;
  UINT32                               Reg32;
  UINT16                               IoBase;

  IoBase = 0;

  Status = gBS->HandleProtocol (
                  mPciDevice,
                  &gEfiPciIoProtocolGuid,
                  (VOID **) &PciIo
                  );
  if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_INFO, "%a: Failed to open gEfiPciIoProtocolGuid\n", __FUNCTION__));

    return 0;
  }

  //
  // Read SMBUS I/O base address
  //
  Status = PciIo->Pci.Read(
                          PciIo,
                          EfiPciWidthUint32,
                          0x20,
                          1,
                          &Reg32
                          );
  if (EFI_ERROR (Status)) {
    goto CloseAndReturn;
  }

  if (!(Reg32 & 1) || !(Reg32 & 0xfffc) || ((Reg32 & 0xfffc) == 0xfffc)) {
    goto CloseAndReturn;
  }

  IoBase = Reg32 & 0xfffc;

  //
  // Read SMBUS I/O base address
  //
  Status = PciIo->Pci.Read(
                          PciIo,
                          EfiPciWidthUint32,
                          0x40,
                          1,
                          &Reg32
                          );
  if (EFI_ERROR (Status)) {
    goto CloseAndReturn;
  }

CloseAndReturn:
  //
  // Close the I/O Abstraction(s) used to perform the supported test
  //
  gBS->CloseProtocol (
        mPciDevice,
        &gEfiPciIoProtocolGuid,
        PciIo,
        mPciDevice
        );

  return IoBase;
}

STATIC
EFI_STATUS
SmbusSetupCommand (
  IN UINT16 IoBase,
  IN UINT8 Ctrl,
  IN UINT8 Xmitadd
  )
{
	UINTN Loops = 10000;
	UINT8 host_busy;

	do {
    MicroSecondDelay (100);
		host_busy = IoRead8 (IoBase + SMBHSTSTAT) & SMBHSTSTS_HOST_BUSY;
	} while (--Loops && host_busy);

	if (Loops == 0)
		return EFI_TIMEOUT;

	/* Clear any lingering errors, so the transaction will run. */
	IoWrite8 (IoBase + SMBHSTSTAT, IoRead8(IoBase + SMBHSTSTAT));

	/* Set up transaction */
	/* Disable interrupts */
	IoWrite8 (IoBase + SMBHSTCTL, Ctrl);

	/* Set the device I'm talking to. */
	IoWrite8 (IoBase + SMBXMITADD, Xmitadd);

	return EFI_SUCCESS;
}

STATIC
EFI_STATUS
SmbusExecuteCommand (
  IN UINT16 IoBase
  )
{
	UINTN Loops = 10000;
	UINT8 Status;

	/* Start the command. */
  IoWrite8 (IoBase + SMBHSTCTL, IoRead8(IoBase + SMBHSTCTL) | SMBHSTCNT_START);
		Status = IoRead8 (IoBase + SMBHSTSTAT);

	/* Poll for it to start. */
	do {
    MicroSecondDelay (100);

		/* If we poll too slow, we could miss HOST_BUSY flag
		 * set and detect INTR or x_ERR flags instead here.
		 */
		Status = IoRead8 (IoBase + SMBHSTSTAT);

		Status &= ~(SMBHSTSTS_SMBALERT_STS | SMBHSTSTS_INUSE_STS);
	} while (--Loops && Status == 0);

	if (Loops == 0)
		return EFI_TIMEOUT;

	return EFI_SUCCESS;
}

STATIC
BOOLEAN
SmbusHostCompleted (
  IN UINT8 Status
) {
  if (Status & SMBHSTSTS_HOST_BUSY)
		return FALSE;

	/* These status bits do not imply completion of transaction. */
	Status &= ~(SMBHSTSTS_BYTE_DONE | SMBHSTSTS_INUSE_STS |
		    SMBHSTSTS_SMBALERT_STS);
	return Status != 0;
}

STATIC
EFI_STATUS
SmbusCompleteCommand (
  IN UINT16 IoBase
  )
{
	UINTN Loops = 10000;
	UINT8 Status;

	do {
    MicroSecondDelay(100);
		Status = IoRead8 (IoBase + SMBHSTSTAT);
	} while (--Loops && !SmbusHostCompleted(Status));

	if (Loops == 0)
		return EFI_TIMEOUT;

	/* These status bits do not imply errors. */
	Status &= ~(SMBHSTSTS_BYTE_DONE | SMBHSTSTS_INUSE_STS |
		    SMBHSTSTS_SMBALERT_STS);

	if (Status == SMBHSTSTS_INTR)
	  return EFI_SUCCESS;

	return EFI_NO_RESPONSE;
}

STATIC
EFI_STATUS
EFIAPI
SmbusProcessCallOperation (
  IN CONST  UINT16                    IoBase,
  IN CONST  EFI_SMBUS_HC_PROTOCOL     *This,
  IN        EFI_SMBUS_DEVICE_ADDRESS  SlaveAddress,
  IN        EFI_SMBUS_DEVICE_COMMAND  Command,
  IN        BOOLEAN                   PecCheck,
  IN OUT    UINTN                     *Length,
  IN OUT    VOID                      *Buffer
  )
{
  EFI_STATUS                           Status;
	UINT16                               ResultBuffer;
	UINT16                               AddrBuffer;

  if (!Buffer || !Length) {
    return RETURN_INVALID_PARAMETER;
  }

  if (*Length != 2) {
    return RETURN_INVALID_PARAMETER;
  }

  CopyMem (&AddrBuffer, Buffer, sizeof(AddrBuffer));

	/* Set up for process call */
	Status = SmbusSetupCommand (IoBase, I801_PROCESS_CALL, XMIT_WRITE(SlaveAddress.SmbusDeviceAddress));
  if (EFI_ERROR (Status)) {
    return Status;
  }

	/* cmd will only be send if I2C_EN is zero */
  IoWrite8 (IoBase + SMBHSTCMD, Command);

  IoWrite8 (IoBase + SMBHSTDAT0, AddrBuffer & 0x00ff);
  IoWrite8 (IoBase + SMBHSTDAT1, (AddrBuffer & 0xff00) >> 8);

	/* Start the command */
	Status = SmbusExecuteCommand (IoBase);
  if (EFI_ERROR (Status)) {
    return Status;
  }

	/* Poll for transaction completion */
	Status = SmbusCompleteCommand (IoBase);
  if (EFI_ERROR (Status)) {
    return Status;
  }

	/* Read results of transaction */
	ResultBuffer = IoRead8 (IoBase + SMBHSTDAT0);
	ResultBuffer |= (IoRead8 (IoBase + SMBHSTDAT1) << 8);

  CopyMem (Buffer, &ResultBuffer, sizeof(ResultBuffer));
  *Length = sizeof(ResultBuffer);

  return EFI_SUCCESS;
}


STATIC
EFI_STATUS
EFIAPI
SmbusReadOperation (
  IN CONST  UINT16                    IoBase,
  IN CONST  EFI_SMBUS_HC_PROTOCOL     *This,
  IN        EFI_SMBUS_DEVICE_ADDRESS  SlaveAddress,
  IN        EFI_SMBUS_DEVICE_COMMAND  Command,
  IN        BOOLEAN                   PecCheck,
  IN OUT    UINTN                     *Length,
  IN OUT    VOID                      *Buffer
  )
{
  EFI_STATUS                           Status;
	UINT8                                ResultBuffer;

  if (!Buffer || !Length) {
    return RETURN_INVALID_PARAMETER;
  }

	/* Set up for a byte data read. */
	Status = SmbusSetupCommand (IoBase, I801_BYTE_DATA, XMIT_READ(SlaveAddress.SmbusDeviceAddress));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  IoWrite8 (IoBase + SMBHSTCMD, Command);

  IoWrite8 (IoBase + SMBHSTDAT0, 0);
  IoWrite8 (IoBase + SMBHSTDAT1, 0);

	/* Start the command */
	Status = SmbusExecuteCommand (IoBase);
  if (EFI_ERROR (Status)) {
    return Status;
  }

	/* Poll for transaction completion */
	Status = SmbusCompleteCommand (IoBase);
  if (EFI_ERROR (Status)) {
    return Status;
  }

	/* Read results of transaction */
	ResultBuffer = IoRead8 (IoBase + SMBHSTDAT0);

  CopyMem (Buffer, &ResultBuffer, sizeof(ResultBuffer));
  *Length = sizeof(ResultBuffer);

  return EFI_SUCCESS;
}


STATIC
EFI_STATUS
EFIAPI
SmbusWriteOperation (
  IN CONST  UINT16                    IoBase,
  IN CONST  EFI_SMBUS_HC_PROTOCOL     *This,
  IN        EFI_SMBUS_DEVICE_ADDRESS  SlaveAddress,
  IN        EFI_SMBUS_DEVICE_COMMAND  Command,
  IN        BOOLEAN                   PecCheck,
  IN OUT    UINTN                     *Length,
  IN OUT    VOID                      *Buffer
  )
{
  EFI_STATUS                           Status;
	UINT8                                InputBuffer;

  if (!Buffer || !Length) {
    return RETURN_INVALID_PARAMETER;
  }

  if (*Length != 1) {
    return RETURN_INVALID_PARAMETER;
  }

  CopyMem (&InputBuffer, Buffer, sizeof(InputBuffer));

	/* Set up for a byte data read. */
	Status = SmbusSetupCommand (IoBase, I801_BYTE_DATA, XMIT_WRITE(SlaveAddress.SmbusDeviceAddress));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  IoWrite8 (IoBase + SMBHSTCMD, Command);

  IoWrite8 (IoBase + SMBHSTDAT0, InputBuffer);

	/* Start the command */
	Status = SmbusExecuteCommand (IoBase);
  if (EFI_ERROR (Status)) {
    return Status;
  }

	/* Poll for transaction completion */
	Status = SmbusCompleteCommand (IoBase);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SmbusExecuteOperation (
  IN CONST  EFI_SMBUS_HC_PROTOCOL     *This,
  IN        EFI_SMBUS_DEVICE_ADDRESS  SlaveAddress,
  IN        EFI_SMBUS_DEVICE_COMMAND  Command,
  IN        EFI_SMBUS_OPERATION       Operation,
  IN        BOOLEAN                   PecCheck,
  IN OUT    UINTN                     *Length,
  IN OUT    VOID                      *Buffer
  )
{
  UINT16                               IoBase;

  IoBase = SmbusGetSMBaseAddress();
  if (!IoBase) {
    return EFI_UNSUPPORTED;
  }

  if (Operation == EfiSmbusProcessCall) {
    return SmbusProcessCallOperation (
      IoBase,
      This,
      SlaveAddress,
      Command,
      PecCheck,
      Length,
      Buffer
    );
  } else if (Operation == EfiSmbusReadByte) {
    return SmbusReadOperation (
      IoBase,
      This,
      SlaveAddress,
      Command,
      PecCheck,
      Length,
      Buffer
    );
  } else if (Operation == EfiSmbusWriteByte) {
    return SmbusWriteOperation (
      IoBase,
      This,
      SlaveAddress,
      Command,
      PecCheck,
      Length,
      Buffer
    );
  }

  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
SmbusHcProtocolArpDevice (
 IN CONST  EFI_SMBUS_HC_PROTOCOL     *This,
  IN        BOOLEAN                   ArpAll,
  IN        EFI_SMBUS_UDID            *SmbusUdid,   OPTIONAL
  IN OUT    EFI_SMBUS_DEVICE_ADDRESS  *SlaveAddress OPTIONAL
  )
{
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
SmbusHcProtocolGetArpMap (
  IN CONST  EFI_SMBUS_HC_PROTOCOL   *This,
  IN OUT    UINTN                   *Length,
  IN OUT    EFI_SMBUS_DEVICE_MAP    **SmbusDeviceMap
  )
{
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
SmbusHcProtocolNotify (
  IN CONST  EFI_SMBUS_HC_PROTOCOL     *This,
  IN        EFI_SMBUS_DEVICE_ADDRESS  SlaveAddress,
  IN        UINTN                     Data,
  IN        EFI_SMBUS_NOTIFY_FUNCTION NotifyFunction
  )
{
  return EFI_UNSUPPORTED;
}

EFI_SMBUS_HC_PROTOCOL                     mSmbusProtocol = {
  SmbusExecuteOperation,
  SmbusHcProtocolArpDevice,
  SmbusHcProtocolGetArpMap,
  SmbusHcProtocolNotify
};

/**
  The Entry Point for SMBUS driver.

  It installs DriverBinding.

  @retval EFI_SUCCESS       The entry point is executed successfully.
  @retval other             Some error occurs when executing this entry point.

**/
EFI_STATUS
EFIAPI
InstallSmbusProtocol (
  IN EFI_HANDLE                        ImageHandle,
  IN EFI_SYSTEM_TABLE                  *SystemTable
  )
{
  EFI_STATUS                Status;
  UINTN                     HandleCount;
  EFI_HANDLE                *HandleBuffer;
  UINTN                     Index;
  EFI_PCI_IO_PROTOCOL       *PciIo;
  UINT16                    IoBase;

  //
  // Start to check all the PciIo to find all possible device
  //
  HandleCount = 0;
  HandleBuffer = NULL;
  Status = gBS->LocateHandleBuffer (
             ByProtocol,
             &gEfiPciIoProtocolGuid,
             NULL,
             &HandleCount,
             &HandleBuffer
           );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (HandleBuffer[Index], &gEfiPciIoProtocolGuid, (VOID **)&PciIo);
    if (EFI_ERROR (Status)) {
      continue;
    }

    PCI_TYPE00                Pci;

    //
    // Check for all PCI device
    //
    Status = PciIo->Pci.Read (
              PciIo,
              EfiPciIoWidthUint32,
              0,
              sizeof (Pci) / sizeof (UINT32),
              &Pci
            );
    if (EFI_ERROR (Status)) {
       continue;
    }

    //
    // Here we decide whether it is a i801 SMBUS controller
    //
    if ((IS_CLASS2 (&Pci, PCI_CLASS_SERIAL, PCI_CLASS_SERIAL_SMB)) &&
         (Pci.Hdr.VendorId == 0x8086) &&
         ((Pci.Hdr.Command & 1) == 1)
       ) {
        mPciDevice = HandleBuffer[Index];
        break;
    }
  }

  gBS->FreePool (HandleBuffer);

  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (mPciDevice == NULL) {
    return EFI_UNSUPPORTED;
  }

  IoBase = SmbusGetSMBaseAddress();
  if (!IoBase) {
    return EFI_UNSUPPORTED;
  }
  DEBUG ((EFI_D_INFO, "%a: Detected i801 SMBus controller with SMBASE 0x%x\n",
      __FUNCTION__, IoBase));

  Status = gBS->InstallProtocolInterface (
                  &mDriverHandle,
                  &gEfiSmbusHcProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mSmbusProtocol
                  );

  if (EFI_ERROR (Status)) {
      return Status;
  }

  return Status;
}
