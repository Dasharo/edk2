/** @file
This file include all platform action which can be customized by IBV/OEM.

Copyright (c) 2016, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "PlatformBootManager.h"
#include "PlatformConsole.h"
#include <Guid/SerialPortLibVendor.h>
#include <Guid/PcAnsi.h>
#include <Guid/TtyTerm.h>

#define PCI_DEVICE_PATH_NODE(Func, Dev) \
  { \
    { \
      HARDWARE_DEVICE_PATH, \
      HW_PCI_DP, \
      { \
        (UINT8) (sizeof (PCI_DEVICE_PATH)), \
        (UINT8) ((sizeof (PCI_DEVICE_PATH)) >> 8) \
      } \
    }, \
    (Func), \
    (Dev) \
  }

#define PNPID_DEVICE_PATH_NODE(PnpId) \
  { \
    { \
      ACPI_DEVICE_PATH, \
      ACPI_DP, \
      { \
        (UINT8) (sizeof (ACPI_HID_DEVICE_PATH)), \
        (UINT8) ((sizeof (ACPI_HID_DEVICE_PATH)) >> 8) \
      }, \
    }, \
    EISA_PNP_ID((PnpId)), \
    0 \
  }

#define gUartVendor \
  { \
    { \
      HARDWARE_DEVICE_PATH, \
      HW_VENDOR_DP, \
      { \
        (UINT8) (sizeof (VENDOR_DEVICE_PATH)), \
        (UINT8) ((sizeof (VENDOR_DEVICE_PATH)) >> 8) \
      } \
    }, \
    EDKII_SERIAL_PORT_LIB_VENDOR_GUID \
  }

#define gUart \
  { \
    { \
      MESSAGING_DEVICE_PATH, \
      MSG_UART_DP, \
      { \
        (UINT8) (sizeof (UART_DEVICE_PATH)), \
        (UINT8) ((sizeof (UART_DEVICE_PATH)) >> 8) \
      } \
    }, \
    0, \
    115200, \
    8, \
    1, \
    1 \
  }

#define gPcAnsiTerminal \
  { \
    { \
      MESSAGING_DEVICE_PATH, \
      MSG_VENDOR_DP, \
      { \
        (UINT8) (sizeof (VENDOR_DEVICE_PATH)), \
        (UINT8) ((sizeof (VENDOR_DEVICE_PATH)) >> 8) \
      } \
    }, \
    DEVICE_PATH_MESSAGING_PC_ANSI \
  }

#define gPciRootBridge \
  PNPID_DEVICE_PATH_NODE(0x0A03)

#define gPnp16550ComPort \
  PNPID_DEVICE_PATH_NODE(0x0501)

#define gPnpPs2Keyboard \
  PNPID_DEVICE_PATH_NODE(0x0303)

#define KEYBOARD_8042_DATA_REGISTER     0x60
#define KEYBOARD_8042_STATUS_REGISTER   0x64

#define KBC_INPBUF_VIA60_KBECHO         0xEE
#define KEYBOARD_CMDECHO_ACK            0xFA
#define KEYBOARD_CMD_RESEND             0xFE

#define KEYBOARD_STATUS_REGISTER_HAS_OUTPUT_DATA     BIT0
#define KEYBOARD_STATUS_REGISTER_HAS_INPUT_DATA      BIT1
#define KEYBOARD_STATUS_REGISTER_RECEIVE_TIMEOUT     BIT6

#define KEYBOARD_TIMEOUT                65536   // 0.07s
#define KEYBOARD_WAITFORVALUE_TIMEOUT   1000000 // 1s

typedef enum _TYPE_OF_TERMINAL {
  TerminalTypePcAnsi                = 0,
  TerminalTypeVt100,
  TerminalTypeVt100Plus,
  TerminalTypeVtUtf8,
  TerminalTypeTtyTerm,
  TerminalTypeLinux,
  TerminalTypeXtermR6,
  TerminalTypeVt400,
  TerminalTypeSCO
} TYPE_OF_TERMINAL;

ACPI_HID_DEVICE_PATH       gPnpPs2KeyboardDeviceNode  = gPnpPs2Keyboard;
UART_DEVICE_PATH           gUartDeviceNode            = gUart;
VENDOR_DEVICE_PATH         gTerminalTypeDeviceNode    = gPcAnsiTerminal;
VENDOR_DEVICE_PATH         gUartDeviceVendorNode      = gUartVendor;

BOOLEAN  mDetectDisplayOnly;
/**
  Check if PS2 keyboard is conntected, by sending ECHO command.
  @param                        none
  @retval TRUE                  connected
  @retvar FALSE                 unconnected
**/
BOOLEAN
DetectPs2Keyboard (
  VOID
  )
{
  UINT32                TimeOut;
  UINT32                RegEmptied;
  UINT8                 Data;
  UINT8                 Status;
  UINT32                SumTimeOut;
  UINT32                GotIt;

  TimeOut     = 0;
  RegEmptied  = 0;

  if (PcdGetBool (PcdSkipPs2Detect))
    return TRUE;

  //
  // Wait for input buffer empty
  //
  for (TimeOut = 0; TimeOut < KEYBOARD_TIMEOUT; TimeOut += 30) {
    if ((IoRead8 (KEYBOARD_8042_STATUS_REGISTER) & KEYBOARD_STATUS_REGISTER_HAS_INPUT_DATA) == 0) {
      RegEmptied = 1;
      break;
    }
    MicroSecondDelay (30);
  }

  if (RegEmptied == 0) {
    DEBUG ((EFI_D_INFO, "PS2 reg not emptied\n"));
    return FALSE;
  }

  //
  // Write it
  //
  IoWrite8 (KEYBOARD_8042_DATA_REGISTER, KBC_INPBUF_VIA60_KBECHO);

  //
  // wait for 1s
  //
  GotIt       = 0;
  TimeOut     = 0;
  SumTimeOut  = 0;
  Data        = 0;
  Status      = 0;

  //
  // Read from 8042 (multiple times if needed)
  // until the expected value appears
  // use SumTimeOut to control the iteration
  //
  while (1) {

    //
    // Perform a read
    //
    for (TimeOut = 0; TimeOut < KEYBOARD_TIMEOUT; TimeOut += 30) {
      Status = IoRead8 (KEYBOARD_8042_STATUS_REGISTER);
      Data = IoRead8 (KEYBOARD_8042_DATA_REGISTER);
      MicroSecondDelay (30);
    }

    SumTimeOut += TimeOut;

    if (PcdGetBool (PcdDetectPs2KbOnCmdAck)) {
      if(Data == KEYBOARD_CMDECHO_ACK) {
        GotIt = 1;
        break;
      }
    }

    // If keyboard not connected, the timeout will occurr
    if (Status & KEYBOARD_STATUS_REGISTER_RECEIVE_TIMEOUT || Data == KEYBOARD_CMD_RESEND) {
      DEBUG ((EFI_D_INFO, "PS/2 receive timeout, keyboard not connected\n"));
      GotIt = 0;
      break;
    }

    if (SumTimeOut >= KEYBOARD_WAITFORVALUE_TIMEOUT || PcdGetBool (PcdFastPS2Detection)) {
      // Some PS/2 controllers may not respond to echo command.
      // Assume keybaord connected if no timeout has been detected
      DEBUG ((EFI_D_INFO, "PS/2 detect timeout\n"));
      if (Data == KBC_INPBUF_VIA60_KBECHO) {
        GotIt = 1;
        break;
      }
      break;
    }
  }

  //
  // Check results
  //
  if (GotIt == 1) {
    return TRUE;
  } else {
    return FALSE;
  }
}

/**
  Add IsaKeyboard to ConIn; add IsaSerial to ConOut, ConIn, ErrOut.

  @param[in] DeviceHandle  Handle of the LPC Bridge device.

  @retval EFI_SUCCESS  Console devices on the LPC bridge have been added to
                       ConOut, ConIn, and ErrOut.

  @return              Error codes, due to EFI_DEVICE_PATH_PROTOCOL missing
                       from DeviceHandle.
**/
EFI_STATUS
PrepareLpcBridgeDevicePath (
  IN EFI_HANDLE                DeviceHandle
)
{
  EFI_STATUS                Status;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  EFI_DEVICE_PATH_PROTOCOL  *TempDevicePath;
  EFI_GUID                  TerminalTypeGuid;
  BOOLEAN                   Ps2Enabled;
  UINTN                     VarSize;

  DevicePath = NULL;
  Status = gBS->HandleProtocol (
             DeviceHandle,
             &gEfiDevicePathProtocolGuid,
             (VOID*)&DevicePath
           );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  TempDevicePath = DevicePath;
  DevicePath = AppendDevicePathNode (DevicePath, (EFI_DEVICE_PATH_PROTOCOL *)&gPnpPs2KeyboardDeviceNode);

  VarSize = sizeof (Ps2Enabled);
  Status = gRT->GetVariable (
      L"Ps2Controller",
      &gDasharoSystemFeaturesGuid,
      NULL,
      &VarSize,
      &Ps2Enabled
      );

  if ((Status == EFI_SUCCESS) && (VarSize == sizeof(Ps2Enabled))) {
    if (Ps2Enabled) {
      DEBUG ((DEBUG_INFO, "PS/2 controller enabled\n"));
      if (DetectPs2Keyboard()) {
        //
        // Register Keyboard
        //
        DEBUG ((DEBUG_INFO, "PS/2 keyboard connected\n"));
        EfiBootManagerUpdateConsoleVariable (ConIn, DevicePath, NULL);
      } else {
        // Remove PS/2 Keyboard from ConIn
        DEBUG ((DEBUG_INFO, "PS/2 keyboard not connected\n"));
        EfiBootManagerUpdateConsoleVariable (ConIn, NULL, DevicePath);
      }
    } else {
      DEBUG ((DEBUG_INFO, "PS/2 controller disabled\n"));
      // Remove PS/2 Keyboard from ConIn
      EfiBootManagerUpdateConsoleVariable (ConIn, NULL, DevicePath);
    }
  } else {
    DEBUG ((DEBUG_INFO, "PS/2 controller variable status %r\n", Status));
    if (DetectPs2Keyboard()) {
      //
      // Register Keyboard
      //
      DEBUG ((DEBUG_INFO, "PS/2 keyboard connected\n"));
      EfiBootManagerUpdateConsoleVariable (ConIn, DevicePath, NULL);
    }
  }
  //
  // Register COM1
  //
  DevicePath = TempDevicePath;
  DevicePath = AppendDevicePathNode ((EFI_DEVICE_PATH_PROTOCOL *)NULL, (EFI_DEVICE_PATH_PROTOCOL *)&gUartDeviceVendorNode);
  DevicePath = AppendDevicePathNode (DevicePath, (EFI_DEVICE_PATH_PROTOCOL *)&gUartDeviceNode);

  switch (PcdGet8 (PcdDefaultTerminalType)) {
  case TerminalTypePcAnsi:    TerminalTypeGuid = gEfiPcAnsiGuid;      break;
  case TerminalTypeVt100:     TerminalTypeGuid = gEfiVT100Guid;       break;
  case TerminalTypeVt100Plus: TerminalTypeGuid = gEfiVT100PlusGuid;   break;
  case TerminalTypeVtUtf8:    TerminalTypeGuid = gEfiVTUTF8Guid;      break;
  case TerminalTypeTtyTerm:   TerminalTypeGuid = gEfiTtyTermGuid;     break;
  case TerminalTypeLinux:     TerminalTypeGuid = gEdkiiLinuxTermGuid; break;
  case TerminalTypeXtermR6:   TerminalTypeGuid = gEdkiiXtermR6Guid;   break;
  case TerminalTypeVt400:     TerminalTypeGuid = gEdkiiVT400Guid;     break;
  case TerminalTypeSCO:       TerminalTypeGuid = gEdkiiSCOTermGuid;   break;
  default:                    TerminalTypeGuid = gEfiPcAnsiGuid;      break;
  }

  CopyGuid (&gTerminalTypeDeviceNode.Guid, &TerminalTypeGuid);

  DevicePath = AppendDevicePathNode (DevicePath, (EFI_DEVICE_PATH_PROTOCOL *)&gTerminalTypeDeviceNode);

  EfiBootManagerUpdateConsoleVariable (ConOut, DevicePath, NULL);
  EfiBootManagerUpdateConsoleVariable (ConIn, DevicePath, NULL);
  EfiBootManagerUpdateConsoleVariable (ErrOut, DevicePath, NULL);

  return EFI_SUCCESS;
}

/**
  Add PCI Serial to ConOut, ConIn, ErrOut.

  @param[in]  DeviceHandle - Handle of PciIo protocol.

  @retval EFI_SUCCESS  - PCI Serial is added to ConOut, ConIn, and ErrOut.
  @retval EFI_STATUS   - No PCI Serial device is added.

**/
EFI_STATUS
PreparePciSerialDevicePath (
  IN EFI_HANDLE                DeviceHandle
)
{
  EFI_STATUS                Status;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;

  DevicePath = NULL;
  Status = gBS->HandleProtocol (
             DeviceHandle,
             &gEfiDevicePathProtocolGuid,
             (VOID*)&DevicePath
           );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DevicePath = AppendDevicePathNode (DevicePath, (EFI_DEVICE_PATH_PROTOCOL *)&gUartDeviceNode);
  DevicePath = AppendDevicePathNode (DevicePath, (EFI_DEVICE_PATH_PROTOCOL *)&gTerminalTypeDeviceNode);

  EfiBootManagerUpdateConsoleVariable (ConOut, DevicePath, NULL);
  EfiBootManagerUpdateConsoleVariable (ConIn,  DevicePath, NULL);
  EfiBootManagerUpdateConsoleVariable (ErrOut, DevicePath, NULL);

  return EFI_SUCCESS;
}


/**
  For every PCI instance execute a callback function.

  @param[in]  Id                 - The protocol GUID for callback
  @param[in]  CallBackFunction   - The callback function
  @param[in]  Context    - The context of the callback

  @retval EFI_STATUS - Callback function failed.

**/
EFI_STATUS
EFIAPI
VisitAllInstancesOfProtocol (
  IN EFI_GUID                    *Id,
  IN PROTOCOL_INSTANCE_CALLBACK  CallBackFunction,
  IN VOID                        *Context
)
{
  EFI_STATUS                Status;
  UINTN                     HandleCount;
  EFI_HANDLE                *HandleBuffer;
  UINTN                     Index;
  VOID                      *Instance;

  //
  // Start to check all the PciIo to find all possible device
  //
  HandleCount = 0;
  HandleBuffer = NULL;
  Status = gBS->LocateHandleBuffer (
             ByProtocol,
             Id,
             NULL,
             &HandleCount,
             &HandleBuffer
           );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (HandleBuffer[Index], Id, &Instance);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = (*CallBackFunction) (
               HandleBuffer[Index],
               Instance,
               Context
             );
  }

  gBS->FreePool (HandleBuffer);

  return EFI_SUCCESS;
}


/**
  For every PCI instance execute a callback function.

  @param[in]  Handle     - The PCI device handle
  @param[in]  Instance   - The instance of the PciIo protocol
  @param[in]  Context    - The context of the callback

  @retval EFI_STATUS - Callback function failed.

**/
EFI_STATUS
EFIAPI
VisitingAPciInstance (
  IN EFI_HANDLE  Handle,
  IN VOID        *Instance,
  IN VOID        *Context
)
{
  EFI_STATUS                Status;
  EFI_PCI_IO_PROTOCOL       *PciIo;
  PCI_TYPE00                Pci;

  PciIo = (EFI_PCI_IO_PROTOCOL*) Instance;

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
    return Status;
  }

  return (*(VISIT_PCI_INSTANCE_CALLBACK)(UINTN) Context) (
           Handle,
           PciIo,
           &Pci
         );

}


/**
  For every PCI instance execute a callback function.

  @param[in]  CallBackFunction - Callback function pointer

  @retval EFI_STATUS - Callback function failed.

**/
EFI_STATUS
EFIAPI
VisitAllPciInstances (
  IN VISIT_PCI_INSTANCE_CALLBACK CallBackFunction
)
{
  return VisitAllInstancesOfProtocol (
           &gEfiPciIoProtocolGuid,
           VisitingAPciInstance,
           (VOID*)(UINTN) CallBackFunction
         );
}


/**
  Do platform specific PCI Device check and add them to
  ConOut, ConIn, ErrOut.

  @param[in]  Handle - Handle of PCI device instance
  @param[in]  PciIo - PCI IO protocol instance
  @param[in]  Pci - PCI Header register block

  @retval EFI_SUCCESS - PCI Device check and Console variable update successfully.
  @retval EFI_STATUS - PCI Device check or Console variable update fail.

**/
EFI_STATUS
EFIAPI
DetectAndPreparePlatformPciDevicePath (
  IN EFI_HANDLE           Handle,
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN PCI_TYPE00           *Pci
)
{
  EFI_STATUS                Status;

  Status = PciIo->Attributes (
             PciIo,
             EfiPciIoAttributeOperationEnable,
             EFI_PCI_DEVICE_ENABLE,
             NULL
           );
  ASSERT_EFI_ERROR (Status);

  //
  // Here we decide whether it is LPC Bridge
  //
  if ((IS_PCI_LPC (Pci)) ||
      ((IS_PCI_ISA_PDECODE (Pci)) && (Pci->Hdr.VendorId == 0x8086))) {
    //
    // Add IsaKeyboard to ConIn,
    // add IsaSerial to ConOut, ConIn, ErrOut
    //
    DEBUG ((DEBUG_INFO, "Found LPC Bridge device\n"));
    PrepareLpcBridgeDevicePath (Handle);
    return EFI_SUCCESS;
  }
  //
  // Here we decide which Serial device to enable in PCI bus
  //
  if (IS_PCI_16550SERIAL (Pci)) {
    //
    // Add them to ConOut, ConIn, ErrOut.
    //
    DEBUG ((DEBUG_INFO, "Found PCI 16550 SERIAL device\n"));
    PreparePciSerialDevicePath (Handle);
    return EFI_SUCCESS;
  }

  return Status;
}


/**
  Do platform specific PCI Device check and add them to ConOut, ConIn, ErrOut

  @param[in]  DetectDisplayOnly - Only detect display device if it's TRUE.

  @retval EFI_SUCCESS - PCI Device check and Console variable update successfully.
  @retval EFI_STATUS - PCI Device check or Console variable update fail.

**/
EFI_STATUS
DetectAndPreparePlatformPciDevicePaths (
  BOOLEAN  DetectDisplayOnly
  )
{
  mDetectDisplayOnly = DetectDisplayOnly;

  EfiBootManagerUpdateConsoleVariable (
    ConIn,
    (EFI_DEVICE_PATH_PROTOCOL *) &gUsbClassKeyboardDevicePath,
    NULL
    );

  return VisitAllPciInstances (DetectAndPreparePlatformPciDevicePath);
}



/**
  Platform console init. Include the platform firmware vendor, revision
  and so crc check.

**/
VOID
EFIAPI
PlatformConsoleInit (
  VOID
)
{
  gUartDeviceNode.BaudRate = PcdGet64 (PcdUartDefaultBaudRate);
  gUartDeviceNode.DataBits = PcdGet8 (PcdUartDefaultDataBits);
  gUartDeviceNode.Parity   = PcdGet8 (PcdUartDefaultParity);
  gUartDeviceNode.StopBits = PcdGet8 (PcdUartDefaultStopBits);

  //
  // Do platform specific PCI Device check and add them to ConOut, ConIn, ErrOut
  //
  DetectAndPreparePlatformPciDevicePaths (FALSE);

}
