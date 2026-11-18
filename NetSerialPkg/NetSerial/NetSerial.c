/**
 * NetSerial - UEFI Telnet Serial Server Driver
 *
 * This driver creates a virtual serial port that is accessible via Telnet,
 * allowing remote console access to UEFI Shell and firmware setup.
 *
 * Key implementation details:
 * - Implements EFI_SERIAL_IO_PROTOCOL for TerminalDxe attachment
 * - TCP4 protocol for network communication on port 23
 * - Circular buffers (32KB) for TX/RX with non-blocking writes
 * - Continuous TX streaming with proper in-flight tracking
 * - Aggressive polling in TX callbacks to prevent RX starvation
 * - ConIn keyboard hook polls network on every keyboard check
 * - Small TCP buffers (256 bytes) for low latency
 *
 * Note: Network polling happens via the ConIn keyboard hook, which
 * intercepts keyboard reads and polls TCP before checking the keyboard.
 * This works reliably in both Shell and Setup menu, providing fast
 * input response without relying on UEFI timers.
 */

#include <Uefi.h>
#include <Protocol/SerialIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/Tcp4.h>
#include <Protocol/Ip4Config2.h>
#include <Protocol/ServiceBinding.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Guid/GlobalVariable.h>

#define TELNET_PORT 23
#define BUFFER_SIZE 32768  // 32KB buffer for heavy console output
#define MAX_CLIENTS 1

//
// Telnet protocol commands
//
#define TELNET_IAC  255  // Interpret As Command
#define TELNET_WILL 251
#define TELNET_WONT 252
#define TELNET_DO   253
#define TELNET_DONT 254
#define TELNET_SB   250  // Subnegotiation begin
#define TELNET_SE   240  // Subnegotiation end

//
// Telnet options
//
#define TELNET_ECHO         1
#define TELNET_SUPPRESS_GA  3
#define TELNET_LINEMODE     34

//
// Device context structure
//
typedef struct {
  UINT32                        Signature;
  EFI_HANDLE                    Handle;
  EFI_SERIAL_IO_PROTOCOL        SerialIo;
  EFI_DEVICE_PATH_PROTOCOL      *DevicePath;

  // Network components
  EFI_HANDLE                    Tcp4ChildHandle;      // Listener handle
  EFI_TCP4_PROTOCOL             *Tcp4;                // Listener protocol
  EFI_HANDLE                    Tcp4ConnectionHandle; // Active connection handle
  EFI_TCP4_PROTOCOL             *Tcp4Connection;      // Active connection protocol
  EFI_TCP4_CONFIG_DATA          Tcp4ConfigData;
  EFI_TCP4_LISTEN_TOKEN         ListenToken;
  EFI_TCP4_IO_TOKEN             TxToken;
  EFI_TCP4_IO_TOKEN             RxToken;

  // Buffers
  UINT8                         RxBuffer[BUFFER_SIZE];
  UINT8                         TxBuffer[BUFFER_SIZE];
  UINT8                         SerialRxBuffer[BUFFER_SIZE];
  UINT8                         SerialTxBuffer[BUFFER_SIZE];
  UINT8                         TelnetCmdBuffer[16];  // For Telnet negotiation commands
  UINTN                         RxBufferHead;
  UINTN                         RxBufferTail;
  UINTN                         TxBufferHead;
  UINTN                         TxBufferTail;

  // State
  BOOLEAN                       ClientConnected;
  BOOLEAN                       RxPending;
  BOOLEAN                       TxPending;
  BOOLEAN                       NetworkInitialized;
  UINTN                         TxInFlight;      // Bytes currently being transmitted
  EFI_EVENT                     NetworkRetryTimer;
  EFI_SERIAL_IO_MODE            Mode;
} NETSERIAL_DEVICE;

#define NETSERIAL_SIGNATURE SIGNATURE_32('N','S','R','L')
#define NETSERIAL_FROM_SERIALIO(a) CR(a, NETSERIAL_DEVICE, SerialIo, NETSERIAL_SIGNATURE)

//
// Global variables
//
STATIC NETSERIAL_DEVICE *gNetSerialDevice = NULL;
STATIC EFI_HANDLE gDriverBindingHandle = NULL;

// ConIn wrapper for Setup menu polling
STATIC EFI_SIMPLE_TEXT_INPUT_PROTOCOL *gOriginalConIn = NULL;
STATIC EFI_INPUT_READ_KEY gOriginalReadKeyStroke = NULL;

//
// Device Path: VenHw + UART
// Uses vendor hardware device path to identify this as NetSerial virtual device
//
// NetSerial GUID: {3C9F23F0-8F5A-4E3D-9A7B-2D6C8B3E4A1F}
#define NETSERIAL_DEVICE_GUID \
  { 0x3C9F23F0, 0x8F5A, 0x4E3D, { 0x9A, 0x7B, 0x2D, 0x6C, 0x8B, 0x3E, 0x4A, 0x1F } }

#pragma pack(1)
typedef struct {
  VENDOR_DEVICE_PATH            VenHw;
  UART_DEVICE_PATH              Uart;
  EFI_DEVICE_PATH_PROTOCOL      End;
} NETSERIAL_UART_DEVICE_PATH;
#pragma pack()

STATIC NETSERIAL_UART_DEVICE_PATH gUartDevicePathTemplate = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8)(sizeof(VENDOR_DEVICE_PATH)),
        (UINT8)((sizeof(VENDOR_DEVICE_PATH)) >> 8)
      }
    },
    NETSERIAL_DEVICE_GUID
  },
  {
    {
      MESSAGING_DEVICE_PATH,
      MSG_UART_DP,
      {
        (UINT8)(sizeof(UART_DEVICE_PATH)),
        (UINT8)((sizeof(UART_DEVICE_PATH)) >> 8)
      }
    },
    0,        // Reserved
    115200,   // BaudRate
    8,        // DataBits
    1,        // Parity (No Parity)
    1         // StopBits (One stop bit)
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      sizeof(EFI_DEVICE_PATH_PROTOCOL),
      0
    }
  }
};
//
// Forward declarations
//
EFI_STATUS SetupNetworkStack(NETSERIAL_DEVICE *Device);
EFI_STATUS InitializeNetworkStack(NETSERIAL_DEVICE *Device);
EFI_STATUS EFIAPI NetSerialReset(IN EFI_SERIAL_IO_PROTOCOL *This);
EFI_STATUS EFIAPI NetSerialSetAttributes(IN EFI_SERIAL_IO_PROTOCOL *This, IN UINT64 BaudRate, IN UINT32 ReceiveFifoDepth, IN UINT32 Timeout, IN EFI_PARITY_TYPE Parity, IN UINT8 DataBits, IN EFI_STOP_BITS_TYPE StopBits);
EFI_STATUS EFIAPI NetSerialSetControl(IN EFI_SERIAL_IO_PROTOCOL *This, IN UINT32 Control);
EFI_STATUS EFIAPI NetSerialGetControl(IN EFI_SERIAL_IO_PROTOCOL *This, OUT UINT32 *Control);
EFI_STATUS EFIAPI NetSerialWrite(IN EFI_SERIAL_IO_PROTOCOL *This, IN OUT UINTN *BufferSize, IN VOID *Buffer);
EFI_STATUS EFIAPI NetSerialRead(IN EFI_SERIAL_IO_PROTOCOL *This, IN OUT UINTN *BufferSize, OUT VOID *Buffer);

//
// Telnet protocol handling
//
VOID
SendTelnetCommand(
  NETSERIAL_DEVICE *Device,
  UINT8            Command,
  UINT8            Option
  )
{
  EFI_STATUS Status;

  if (!Device->ClientConnected || Device->Tcp4Connection == NULL) {
    return;
  }

  if (Device->TxPending) {
    DEBUG((DEBUG_VERBOSE, "NetSerial: Skipping Telnet command, TX busy\n"));
    return;
  }

  Device->TelnetCmdBuffer[0] = TELNET_IAC;
  Device->TelnetCmdBuffer[1] = Command;
  Device->TelnetCmdBuffer[2] = Option;

  EFI_TCP4_TRANSMIT_DATA TxData;

  TxData.Push = TRUE;
  TxData.Urgent = FALSE;
  TxData.DataLength = 3;
  TxData.FragmentCount = 1;
  TxData.FragmentTable[0].FragmentLength = 3;
  TxData.FragmentTable[0].FragmentBuffer = Device->TelnetCmdBuffer;

  Device->TxToken.Packet.TxData = &TxData;
  Device->TxPending = TRUE;
  Status = Device->Tcp4Connection->Transmit(Device->Tcp4Connection, &Device->TxToken);

  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN, "NetSerial: Telnet command transmit failed: %r\n", Status));
    Device->TxPending = FALSE;
  }
}

VOID
InitializeTelnetSession(
  NETSERIAL_DEVICE *Device
  )
{
  DEBUG((DEBUG_INFO, "NetSerial: Initializing Telnet session\n"));

  SendTelnetCommand(Device, TELNET_WILL, TELNET_ECHO);
  gBS->Stall(5000);
  Device->TxPending = FALSE;

  SendTelnetCommand(Device, TELNET_WILL, TELNET_SUPPRESS_GA);
  gBS->Stall(5000);
  Device->TxPending = FALSE;

  SendTelnetCommand(Device, TELNET_DO, TELNET_SUPPRESS_GA);
  gBS->Stall(5000);
  Device->TxPending = FALSE;

  SendTelnetCommand(Device, TELNET_DONT, TELNET_LINEMODE);
  gBS->Stall(5000);
  Device->TxPending = FALSE;

  SendTelnetCommand(Device, TELNET_WONT, TELNET_LINEMODE);
  gBS->Stall(5000);
  Device->TxPending = FALSE;

  DEBUG((DEBUG_INFO, "NetSerial: Telnet character mode negotiated\n"));
  Print(L"\n========================================\n");
  Print(L"Telnet console ready\n");
  Print(L"========================================\n\n");
}

UINTN
ProcessTelnetData(
  NETSERIAL_DEVICE *Device,
  UINT8            *Data,
  UINTN            Length
  )
{
  UINTN i, OutLen = 0;
  UINTN State = 0;
  UINT8 Command = 0;

  for (i = 0; i < Length; i++) {
    if (State == 0) {
      if (Data[i] == TELNET_IAC) {
        State = 1;
      } else {
        Device->SerialRxBuffer[Device->RxBufferTail] = Data[i];
        Device->RxBufferTail = (Device->RxBufferTail + 1) % BUFFER_SIZE;
        OutLen++;
      }
    } else if (State == 1) {
      if (Data[i] == TELNET_IAC) {
        Device->SerialRxBuffer[Device->RxBufferTail] = TELNET_IAC;
        Device->RxBufferTail = (Device->RxBufferTail + 1) % BUFFER_SIZE;
        OutLen++;
        State = 0;
      } else if (Data[i] >= 251 && Data[i] <= 254) {
        Command = Data[i];
        State = 2;
      } else {
        State = 0;
      }
    } else if (State == 2) {
      DEBUG((DEBUG_VERBOSE, "NetSerial: Telnet negotiation - Command=%d Option=%d\n", Command, Data[i]));

      if (Command == TELNET_DO) {
        if (Data[i] == TELNET_ECHO || Data[i] == TELNET_SUPPRESS_GA) {
          SendTelnetCommand(Device, TELNET_WILL, Data[i]);
          DEBUG((DEBUG_INFO, "NetSerial: Accepted DO %d\n", Data[i]));
        } else if (Data[i] == TELNET_LINEMODE) {
          SendTelnetCommand(Device, TELNET_WONT, Data[i]);
          DEBUG((DEBUG_INFO, "NetSerial: Refused DO LINEMODE\n"));
        } else {
          SendTelnetCommand(Device, TELNET_WONT, Data[i]);
        }
      } else if (Command == TELNET_DONT) {
        SendTelnetCommand(Device, TELNET_WONT, Data[i]);
        DEBUG((DEBUG_VERBOSE, "NetSerial: Acknowledged DONT %d\n", Data[i]));
      } else if (Command == TELNET_WILL) {
        if (Data[i] == TELNET_SUPPRESS_GA) {
          SendTelnetCommand(Device, TELNET_DO, Data[i]);
          DEBUG((DEBUG_INFO, "NetSerial: Accepted WILL SUPPRESS_GA\n"));
        } else if (Data[i] == TELNET_ECHO || Data[i] == TELNET_LINEMODE) {
          SendTelnetCommand(Device, TELNET_DONT, Data[i]);
          DEBUG((DEBUG_INFO, "NetSerial: Refused WILL %d\n", Data[i]));
        } else {
          SendTelnetCommand(Device, TELNET_DONT, Data[i]);
        }
      } else if (Command == TELNET_WONT) {
        SendTelnetCommand(Device, TELNET_DONT, Data[i]);
        DEBUG((DEBUG_VERBOSE, "NetSerial: Acknowledged WONT %d\n", Data[i]));
      }
      State = 0;
    }
  }

  DEBUG((DEBUG_VERBOSE, "NetSerial: Processed %d bytes -> %d data bytes\n", Length, OutLen));
  return OutLen;
}
//
// Network callbacks
//
VOID EFIAPI TcpListenCallback(IN EFI_EVENT Event, IN VOID *Context)
{
  NETSERIAL_DEVICE *Device = (NETSERIAL_DEVICE *)Context;
  EFI_STATUS Status;
  EFI_TCP4_CONNECTION_STATE ConnState;

  DEBUG((DEBUG_INFO, "NetSerial: ListenCallback status=%r\n", Device->ListenToken.CompletionToken.Status));

  if (Device->ListenToken.CompletionToken.Status == EFI_SUCCESS) {
    Print(L"\nTelnet client connected\n");

    EFI_HANDLE *NewHandlePtr = (EFI_HANDLE*)((UINT8*)&Device->ListenToken + 16);
    Device->Tcp4ConnectionHandle = *NewHandlePtr;

    if (Device->Tcp4ConnectionHandle != NULL) {
      Status = gBS->HandleProtocol(Device->Tcp4ConnectionHandle, &gEfiTcp4ProtocolGuid, (VOID**)&Device->Tcp4Connection);

      if (!EFI_ERROR(Status) && Device->Tcp4Connection != NULL) {
        Status = Device->Tcp4Connection->GetModeData(Device->Tcp4Connection, &ConnState, NULL, NULL, NULL, NULL);
        if (!EFI_ERROR(Status) && ConnState == Tcp4StateEstablished) {
          DEBUG((DEBUG_INFO, "NetSerial: Connection state: Established\n"));
        }

        // Note: Can't reconfigure an already-established connection
        // Options must be set on the listener before Accept()

        Device->ClientConnected = TRUE;
        InitializeTelnetSession(Device);

        Device->RxToken.Packet.RxData->DataLength = 256;
        Device->RxToken.Packet.RxData->FragmentCount = 1;
        Device->RxToken.Packet.RxData->FragmentTable[0].FragmentLength = 256;
        Device->RxToken.Packet.RxData->FragmentTable[0].FragmentBuffer = Device->RxBuffer;
        Device->RxPending = TRUE;

        Status = Device->Tcp4Connection->Receive(Device->Tcp4Connection, &Device->RxToken);

        if (EFI_ERROR(Status)) {
          DEBUG((DEBUG_ERROR, "NetSerial: Failed to start receive: %r\n", Status));
          Print(L"Failed to start receive: %r\n", Status);
          Device->RxPending = FALSE;
        } else {
          DEBUG((DEBUG_INFO, "NetSerial: Receive started on connection\n"));
          Print(L"Console ready\n\n");
        }
      } else {
        Print(L"Failed to get TCP4 protocol: %r\n", Status);
      }

      *NewHandlePtr = NULL;

      Device->ListenToken.CompletionToken.Status = EFI_NOT_READY;
      Status = Device->Tcp4->Accept(Device->Tcp4, &Device->ListenToken);
      if (EFI_ERROR(Status)) {
        DEBUG((DEBUG_WARN, "NetSerial: Failed to restart Accept: %r\n", Status));
      }
    } else {
      Print(L"Connection failed: NewChildHandle is NULL\n");
    }
  } else {
    DEBUG((DEBUG_WARN, "NetSerial: Listen failed: %r\n", Device->ListenToken.CompletionToken.Status));
  }
}

VOID EFIAPI TcpReceiveCallback(IN EFI_EVENT Event, IN VOID *Context)
{
  NETSERIAL_DEVICE *Device = (NETSERIAL_DEVICE *)Context;
  EFI_STATUS Status;

  Device->RxPending = FALSE;

  DEBUG((DEBUG_VERBOSE, "NetSerial: RxCallback status=%r\n", Device->RxToken.CompletionToken.Status));

  if (Device->RxToken.CompletionToken.Status == EFI_SUCCESS) {
    UINTN DataLength = Device->RxToken.Packet.RxData->FragmentTable[0].FragmentLength;

    DEBUG((DEBUG_VERBOSE, "NetSerial: Received %d bytes\n", DataLength));

    if (DataLength > 0) {
      UINTN Processed = ProcessTelnetData(Device, Device->RxBuffer, DataLength);
      DEBUG((DEBUG_VERBOSE, "NetSerial: Processed %d bytes (net %d data bytes)\n", DataLength, Processed));

      if (Device->ClientConnected && Device->Tcp4Connection != NULL) {
        // Properly reset RxData structure for next receive
        Device->RxToken.Packet.RxData->DataLength = 256;
        Device->RxToken.Packet.RxData->FragmentCount = 1;
        Device->RxToken.Packet.RxData->FragmentTable[0].FragmentLength = 256;
        Device->RxToken.Packet.RxData->FragmentTable[0].FragmentBuffer = Device->RxBuffer;
        Device->RxPending = TRUE;
        Status = Device->Tcp4Connection->Receive(Device->Tcp4Connection, &Device->RxToken);

        if (EFI_ERROR(Status)) {
          DEBUG((DEBUG_WARN, "NetSerial: Failed to restart receive: %r\n", Status));
          Print(L"Failed to restart receive: %r\n", Status);
          Device->RxPending = FALSE;
          Device->ClientConnected = FALSE;
        } else {
          DEBUG((DEBUG_VERBOSE, "NetSerial: Receive restarted\n"));
        }
      }
    }
  } else if (Device->RxToken.CompletionToken.Status == EFI_CONNECTION_FIN ||
             Device->RxToken.CompletionToken.Status == EFI_CONNECTION_RESET ||
             Device->RxToken.CompletionToken.Status == EFI_ABORTED) {
    DEBUG((DEBUG_INFO, "NetSerial: Connection closed: %r\n", Device->RxToken.CompletionToken.Status));
    Print(L"\nTelnet client disconnected\n");
    Print(L"Waiting for new connection...\n\n");
    Device->ClientConnected = FALSE;
    Device->RxBufferHead = 0;
    Device->RxBufferTail = 0;

    if (Device->Tcp4 != NULL && Device->NetworkInitialized) {
      Status = Device->Tcp4->Accept(Device->Tcp4, &Device->ListenToken);
      if (!EFI_ERROR(Status)) {
        DEBUG((DEBUG_INFO, "NetSerial: Listening for new connection\n"));
        Print(L"NetSerial: Ready to accept new connection on port 23\n");
      }
    }
  } else {
    DEBUG((DEBUG_WARN, "NetSerial: Receive error: %r\n", Device->RxToken.CompletionToken.Status));
    Print(L"NetSerial: Connection error: %r\n", Device->RxToken.CompletionToken.Status);
    Device->ClientConnected = FALSE;
    Device->RxBufferHead = 0;
    Device->RxBufferTail = 0;
  }
}

VOID EFIAPI TcpTransmitCallback(IN EFI_EVENT Event, IN VOID *Context)
{
  NETSERIAL_DEVICE *Device = (NETSERIAL_DEVICE *)Context;
  EFI_STATUS Status;
  EFI_TCP4_TRANSMIT_DATA TxData;

  Device->TxPending = FALSE;

  // After TX completes, poll to process any pending RX
  // This prevents TX from blocking RX during output bursts
  if (Device->ClientConnected && Device->Tcp4Connection != NULL) {
    for (UINTN i = 0; i < 10; i++) {
      Device->Tcp4Connection->Poll(Device->Tcp4Connection);
    }
  }

  // Advance Head now that previous transmission completed
  if (Device->TxInFlight > 0) {
    Device->TxBufferHead = (Device->TxBufferHead + Device->TxInFlight) % BUFFER_SIZE;
    Device->TxInFlight = 0;
  }

  // Continue sending if more data in buffer (streaming)
  if (Device->ClientConnected && Device->Tcp4Connection != NULL &&
      Device->TxBufferHead != Device->TxBufferTail) {

    UINTN ToSend;
    if (Device->TxBufferTail > Device->TxBufferHead) {
      ToSend = Device->TxBufferTail - Device->TxBufferHead;
    } else {
      ToSend = BUFFER_SIZE - Device->TxBufferHead;
    }

    if (ToSend > 1024) ToSend = 1024;

    TxData.Push = TRUE;
    TxData.Urgent = FALSE;
    TxData.DataLength = (UINT32)ToSend;
    TxData.FragmentCount = 1;
    TxData.FragmentTable[0].FragmentLength = (UINT32)ToSend;
    TxData.FragmentTable[0].FragmentBuffer = &Device->SerialTxBuffer[Device->TxBufferHead];

    Device->TxToken.Packet.TxData = &TxData;
    Device->TxPending = TRUE;
    Device->TxInFlight = ToSend;  // Track new in-flight data
    Status = Device->Tcp4Connection->Transmit(Device->Tcp4Connection, &Device->TxToken);

    if (!EFI_ERROR(Status)) {
      DEBUG((DEBUG_VERBOSE, "NetSerial: TxCallback - continued TX of %d bytes\n", ToSend));
      // Poll again after starting TX to let any RX callbacks fire
      for (UINTN i = 0; i < 5; i++) {
        Device->Tcp4Connection->Poll(Device->Tcp4Connection);
      }
      // Don't advance Head here - wait for next callback
    } else {
      Device->TxPending = FALSE;
      Device->TxInFlight = 0;
      DEBUG((DEBUG_WARN, "NetSerial: TxCallback - transmit failed: %r\n", Status));
    }
  }
}
//
// SerialIO Protocol Implementation
//
EFI_STATUS EFIAPI NetSerialReset(IN EFI_SERIAL_IO_PROTOCOL *This)
{
  NETSERIAL_DEVICE *Device = NETSERIAL_FROM_SERIALIO(This);
  Device->RxBufferHead = 0;
  Device->RxBufferTail = 0;
  Device->TxBufferHead = 0;
  Device->TxBufferTail = 0;
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI NetSerialSetAttributes(IN EFI_SERIAL_IO_PROTOCOL *This, IN UINT64 BaudRate, IN UINT32 ReceiveFifoDepth, IN UINT32 Timeout, IN EFI_PARITY_TYPE Parity, IN UINT8 DataBits, IN EFI_STOP_BITS_TYPE StopBits)
{
  NETSERIAL_DEVICE *Device = NETSERIAL_FROM_SERIALIO(This);
  if (BaudRate != 0) Device->Mode.BaudRate = BaudRate;
  if (ReceiveFifoDepth != 0) Device->Mode.ReceiveFifoDepth = ReceiveFifoDepth;
  if (Timeout != 0) Device->Mode.Timeout = Timeout;
  if (Parity != DefaultParity) Device->Mode.Parity = Parity;
  if (DataBits != 0) Device->Mode.DataBits = DataBits;
  if (StopBits != DefaultStopBits) Device->Mode.StopBits = StopBits;
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI NetSerialSetControl(IN EFI_SERIAL_IO_PROTOCOL *This, IN UINT32 Control)
{
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI NetSerialGetControl(IN EFI_SERIAL_IO_PROTOCOL *This, OUT UINT32 *Control)
{
  NETSERIAL_DEVICE *Device = NETSERIAL_FROM_SERIALIO(This);

  if (Control == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *Control = EFI_SERIAL_CLEAR_TO_SEND | EFI_SERIAL_DATA_SET_READY | EFI_SERIAL_CARRIER_DETECT;

  if (Device->ClientConnected) {
    DEBUG((DEBUG_VERBOSE, "NetSerial: Client connected, all signals active\n"));
  }

  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI NetSerialWrite(IN EFI_SERIAL_IO_PROTOCOL *This, IN OUT UINTN *BufferSize, IN VOID *Buffer)
{
  NETSERIAL_DEVICE *Device = NETSERIAL_FROM_SERIALIO(This);
  EFI_STATUS Status;
  UINTN Count;
  UINT8 *DataPtr;
  EFI_TCP4_TRANSMIT_DATA TxData;

  if (BufferSize == NULL || Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (*BufferSize == 0) {
    return EFI_SUCCESS;
  }

  DEBUG((DEBUG_VERBOSE, "NetSerial: Write called, size=%d\n", *BufferSize));

  if (!Device->NetworkInitialized || Device->Tcp4Connection == NULL) {
    DEBUG((DEBUG_VERBOSE, "NetSerial: Write - network not ready\n"));
    return EFI_SUCCESS;
  }

  if (!Device->ClientConnected) {
    DEBUG((DEBUG_VERBOSE, "NetSerial: Write - no client\n"));
    return EFI_SUCCESS;
  }

  // Use circular buffer with queuing - never blocks
  DataPtr = (UINT8 *)Buffer;
  Count = 0;

  // Add ALL data to circular buffer
  while (Count < *BufferSize) {
    UINTN NextTail = (Device->TxBufferTail + 1) % BUFFER_SIZE;

    // Calculate effective head (accounting for in-flight data)
    UINTN EffectiveHead = (Device->TxBufferHead + Device->TxInFlight) % BUFFER_SIZE;

    // If buffer is full, try harder to drain it before dropping
    if (NextTail == EffectiveHead) {
      // Buffer completely full - try to drain by polling TCP multiple times
      UINTN Retries = 5; // Try 5 times with small delays

      while (Retries > 0 && NextTail == EffectiveHead) {
        if (Device->Tcp4Connection != NULL) {
          Device->Tcp4Connection->Poll(Device->Tcp4Connection);
        }

        // Recalculate after poll (TxInFlight may have changed)
        EffectiveHead = (Device->TxBufferHead + Device->TxInFlight) % BUFFER_SIZE;
        NextTail = (Device->TxBufferTail + 1) % BUFFER_SIZE;

        if (NextTail == EffectiveHead && Retries > 1) {
          // Still full - wait 1ms and try again
          gBS->Stall(1000); // 1ms
        }

        Retries--;
      }

      // If STILL full after polling and waiting, drop oldest QUEUED byte
      // Don't touch in-flight data!
      if (NextTail == EffectiveHead) {
        // Advance effective head (skip oldest queued byte)
        EffectiveHead = (EffectiveHead + 1) % BUFFER_SIZE;
        // Update TxBufferTail to point to new position
        Device->TxBufferTail = EffectiveHead;
        DEBUG((DEBUG_WARN, "NetSerial: TX buffer full after retries, dropping byte\n"));
        // This write will now succeed
      }
    }

    Device->SerialTxBuffer[Device->TxBufferTail] = DataPtr[Count];
    Device->TxBufferTail = (Device->TxBufferTail + 1) % BUFFER_SIZE;
    Count++;
  }

  DEBUG((DEBUG_VERBOSE, "NetSerial: Buffered %d bytes\n", Count));

  // If TX not currently active, kick off transmission from buffer
  if (!Device->TxPending) {
    // Calculate effective head (actual start of queued data)
    UINTN EffectiveHead = (Device->TxBufferHead + Device->TxInFlight) % BUFFER_SIZE;

    if (EffectiveHead != Device->TxBufferTail) {
      UINTN ToSend;
      if (Device->TxBufferTail > EffectiveHead) {
        ToSend = Device->TxBufferTail - EffectiveHead;
      } else {
        ToSend = BUFFER_SIZE - EffectiveHead;
      }

      if (ToSend > 1024) ToSend = 1024;

      TxData.Push = TRUE;
      TxData.Urgent = FALSE;
      TxData.DataLength = (UINT32)ToSend;
      TxData.FragmentCount = 1;
      TxData.FragmentTable[0].FragmentLength = (UINT32)ToSend;
      TxData.FragmentTable[0].FragmentBuffer = &Device->SerialTxBuffer[EffectiveHead];

      Device->TxToken.Packet.TxData = &TxData;
      Device->TxPending = TRUE;
      Device->TxInFlight = ToSend;  // Track how much data is in flight
      Status = Device->Tcp4Connection->Transmit(Device->Tcp4Connection, &Device->TxToken);

      if (!EFI_ERROR(Status)) {
        DEBUG((DEBUG_VERBOSE, "NetSerial: Started TX of %d bytes\n", ToSend));
        // Don't poll here - causes TPL violations
        // The background polling timer will drain the queue
        // DON'T advance TxBufferHead here - wait for callback to confirm
      } else {
        Device->TxPending = FALSE;
        Device->TxInFlight = 0;
        DEBUG((DEBUG_WARN, "NetSerial: Transmit failed: %r\n", Status));
      }
    }
  }

  *BufferSize = Count;
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI NetSerialRead(IN EFI_SERIAL_IO_PROTOCOL *This, IN OUT UINTN *BufferSize, OUT VOID *Buffer)
{
  NETSERIAL_DEVICE *Device = NETSERIAL_FROM_SERIALIO(This);
  UINTN Count;
  UINT8 *DataPtr;

  if (BufferSize == NULL || Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (*BufferSize == 0) {
    return EFI_SUCCESS;
  }

  if (!Device->NetworkInitialized) {
    DEBUG((DEBUG_VERBOSE, "NetSerial: Read - network not initialized yet\n"));
    *BufferSize = 0;
    return EFI_SUCCESS;
  }

  if (!Device->ClientConnected) {
    DEBUG((DEBUG_VERBOSE, "NetSerial: Read - no client connected\n"));
    *BufferSize = 0;
    return EFI_SUCCESS;
  }

  DataPtr = (UINT8 *)Buffer;
  Count = 0;

  while (Count < *BufferSize && Device->RxBufferHead != Device->RxBufferTail) {
    DataPtr[Count] = Device->SerialRxBuffer[Device->RxBufferHead];
    Device->RxBufferHead = (Device->RxBufferHead + 1) % BUFFER_SIZE;
    Count++;
  }

  if (Count == 0) {
    DEBUG((DEBUG_VERBOSE, "NetSerial: Read - buffer empty\n"));
    *BufferSize = 0;
    return EFI_SUCCESS;
  }

  DEBUG((DEBUG_VERBOSE, "NetSerial: Read %d bytes from buffer\n", Count));

  *BufferSize = Count;
  return EFI_SUCCESS;
}

VOID EFIAPI NetworkRetryTimerCallback(IN EFI_EVENT Event, IN VOID *Context)
{
  NETSERIAL_DEVICE *Device = (NETSERIAL_DEVICE *)Context;
  EFI_STATUS Status;

  if (Device->NetworkInitialized) {
    gBS->SetTimer(Device->NetworkRetryTimer, TimerCancel, 0);
    return;
  }

  DEBUG((DEBUG_INFO, "NetSerial: Attempting network initialization...\n"));

  Status = InitializeNetworkStack(Device);
  if (!EFI_ERROR(Status)) {
    Device->NetworkInitialized = TRUE;
    gBS->SetTimer(Device->NetworkRetryTimer, TimerCancel, 0);
    Print(L"NetSerial: Network initialized successfully!\n");
    DEBUG((DEBUG_INFO, "NetSerial: Listening on port %d\n", TELNET_PORT));
  } else {
    DEBUG((DEBUG_WARN, "NetSerial: Network init failed, will retry: %r\n", Status));
  }
}

//
// Network setup
//
EFI_STATUS SetupNetworkStack(NETSERIAL_DEVICE *Device)
{
  EFI_STATUS Status;
  EFI_SERVICE_BINDING_PROTOCOL *ServiceBinding;
  EFI_HANDLE *HandleBuffer;
  UINTN HandleCount;

  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiTcp4ServiceBindingProtocolGuid, NULL, &HandleCount, &HandleBuffer);

  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN, "NetSerial: TCP4 service binding not available yet\n"));
    return Status;
  }

  Status = gBS->HandleProtocol(HandleBuffer[0], &gEfiTcp4ServiceBindingProtocolGuid, (VOID **)&ServiceBinding);
  FreePool(HandleBuffer);

  if (EFI_ERROR(Status)) {
    return Status;
  }

  if (Device->Tcp4ChildHandle == NULL) {
    Status = ServiceBinding->CreateChild(ServiceBinding, &Device->Tcp4ChildHandle);

    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_WARN, "NetSerial: Failed to create TCP4 child\n"));
      return Status;
    }

    Status = gBS->HandleProtocol(Device->Tcp4ChildHandle, &gEfiTcp4ProtocolGuid, (VOID **)&Device->Tcp4);

    if (EFI_ERROR(Status)) {
      ServiceBinding->DestroyChild(ServiceBinding, Device->Tcp4ChildHandle);
      Device->Tcp4ChildHandle = NULL;
      return Status;
    }
  }

  return EFI_SUCCESS;
}
EFI_STATUS InitializeNetworkStack(NETSERIAL_DEVICE *Device)
{
  EFI_STATUS Status;
  EFI_TCP4_ACCESS_POINT AccessPoint;
  EFI_TCP4_OPTION Option;

  if (Device->Tcp4 == NULL) {
    Status = SetupNetworkStack(Device);
    if (EFI_ERROR(Status)) {
      return Status;
    }
  }

  ZeroMem(&Device->Tcp4ConfigData, sizeof(EFI_TCP4_CONFIG_DATA));
  ZeroMem(&AccessPoint, sizeof(EFI_TCP4_ACCESS_POINT));
  ZeroMem(&Option, sizeof(EFI_TCP4_OPTION));

  AccessPoint.UseDefaultAddress = TRUE;
  AccessPoint.StationPort = TELNET_PORT;
  AccessPoint.RemotePort = 0;
  AccessPoint.ActiveFlag = FALSE;

  Device->Tcp4ConfigData.TypeOfService = 0;
  Device->Tcp4ConfigData.TimeToLive = 64;
  Device->Tcp4ConfigData.AccessPoint = AccessPoint;
  Device->Tcp4ConfigData.ControlOption = &Option;

  Option.ReceiveBufferSize = 256;        // Very small for instant RX
  Option.SendBufferSize = 16384;         // Larger TX buffer for throughput
  Option.MaxSynBackLog = 1;
  Option.ConnectionTimeout = 3;
  Option.DataRetries = 1;                 // Fail fast
  Option.FinTimeout = 1;
  Option.KeepAliveProbes = 1;
  Option.KeepAliveTime = 30;
  Option.KeepAliveInterval = 5;
  Option.EnableNagle = FALSE;             // Critical for latency!
  Option.EnableTimeStamp = FALSE;
  Option.EnableWindowScaling = FALSE;     // Disable window scaling for simplicity

  Status = Device->Tcp4->Configure(Device->Tcp4, &Device->Tcp4ConfigData);

  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN, "NetSerial: TCP4 Configure failed (may need IP): %r\n", Status));
    return Status;
  }

  if (Device->ListenToken.CompletionToken.Event == NULL) {
    Status = gBS->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, TcpListenCallback, Device, &Device->ListenToken.CompletionToken.Event);
    if (EFI_ERROR(Status)) {
      Device->Tcp4->Configure(Device->Tcp4, NULL);
      return Status;
    }
  }

  if (Device->RxToken.CompletionToken.Event == NULL) {
    Status = gBS->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, TcpReceiveCallback, Device, &Device->RxToken.CompletionToken.Event);
    if (EFI_ERROR(Status)) {
      gBS->CloseEvent(Device->ListenToken.CompletionToken.Event);
      Device->ListenToken.CompletionToken.Event = NULL;
      Device->Tcp4->Configure(Device->Tcp4, NULL);
      return Status;
    }
  }

  if (Device->TxToken.CompletionToken.Event == NULL) {
    Status = gBS->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, TcpTransmitCallback, Device, &Device->TxToken.CompletionToken.Event);
    if (EFI_ERROR(Status)) {
      gBS->CloseEvent(Device->RxToken.CompletionToken.Event);
      Device->RxToken.CompletionToken.Event = NULL;
      gBS->CloseEvent(Device->ListenToken.CompletionToken.Event);
      Device->ListenToken.CompletionToken.Event = NULL;
      Device->Tcp4->Configure(Device->Tcp4, NULL);
      return Status;
    }
  }

  if (Device->RxToken.Packet.RxData == NULL) {
    Device->RxToken.Packet.RxData = AllocateZeroPool(sizeof(EFI_TCP4_RECEIVE_DATA) + sizeof(EFI_TCP4_FRAGMENT_DATA));
    if (Device->RxToken.Packet.RxData == NULL) {
      gBS->CloseEvent(Device->TxToken.CompletionToken.Event);
      Device->TxToken.CompletionToken.Event = NULL;
      gBS->CloseEvent(Device->RxToken.CompletionToken.Event);
      Device->RxToken.CompletionToken.Event = NULL;
      gBS->CloseEvent(Device->ListenToken.CompletionToken.Event);
      Device->ListenToken.CompletionToken.Event = NULL;
      Device->Tcp4->Configure(Device->Tcp4, NULL);
      return EFI_OUT_OF_RESOURCES;
    }
    Device->RxToken.Packet.RxData->FragmentCount = 1;
  }

  Status = Device->Tcp4->Accept(Device->Tcp4, &Device->ListenToken);

  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN, "NetSerial: Failed to start listening: %r\n", Status));
    Print(L"NetSerial: Failed to start listening: %r\n", Status);
    return Status;
  }

  DEBUG((DEBUG_INFO, "NetSerial: Listening on port %d\n", TELNET_PORT));
  Print(L"NetSerial: Accepting connections on port %d\n", TELNET_PORT);

  return EFI_SUCCESS;
}

//
// ConIn wrapper for polling in Setup menu
//
EFI_STATUS EFIAPI WrappedReadKeyStroke(
  IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This,
  OUT EFI_INPUT_KEY *Key
  )
{
  NETSERIAL_DEVICE *Device = gNetSerialDevice;

  // Poll network when Setup checks keyboard
  // This enables fast input in Setup menu where timers don't fire
  if (Device != NULL && Device->ClientConnected && Device->Tcp4Connection != NULL) {
    for (UINTN i = 0; i < 5; i++) {
      Device->Tcp4Connection->Poll(Device->Tcp4Connection);
    }
  }

  // Call original keyboard handler
  if (gOriginalReadKeyStroke != NULL) {
    return gOriginalReadKeyStroke(This, Key);
  }
  return EFI_NOT_READY;
}

VOID HookConIn(NETSERIAL_DEVICE *Device)
{
  if (gST->ConIn == NULL) {
    return;
  }

  gOriginalConIn = gST->ConIn;
  gOriginalReadKeyStroke = gST->ConIn->ReadKeyStroke;
  gST->ConIn->ReadKeyStroke = WrappedReadKeyStroke;

  Print(L"NetSerial: Hooked keyboard for fast Setup menu input\n");
}

EFI_STATUS EFIAPI NetSerialDriverEntryPoint(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  NETSERIAL_DEVICE *Device;

  DEBUG((DEBUG_INFO, "NetSerial: Driver loaded\n"));

  if (gNetSerialDevice != NULL) {
    DEBUG((DEBUG_WARN, "NetSerial: Driver already loaded, refusing second instance\n"));
    Print(L"NetSerial: Already running (only one instance allowed)\n");
    return EFI_SUCCESS;
  }

  Device = AllocateZeroPool(sizeof(NETSERIAL_DEVICE));
  if (Device == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Device->Signature = NETSERIAL_SIGNATURE;

  Device->SerialIo.Revision = EFI_SERIAL_IO_PROTOCOL_REVISION;
  Device->SerialIo.Reset = NetSerialReset;
  Device->SerialIo.SetAttributes = NetSerialSetAttributes;
  Device->SerialIo.SetControl = NetSerialSetControl;
  Device->SerialIo.GetControl = NetSerialGetControl;
  Device->SerialIo.Write = NetSerialWrite;
  Device->SerialIo.Read = NetSerialRead;
  Device->SerialIo.Mode = &Device->Mode;

  Device->Mode.ControlMask = EFI_SERIAL_CLEAR_TO_SEND | EFI_SERIAL_DATA_SET_READY | EFI_SERIAL_CARRIER_DETECT;
  Device->Mode.Timeout = 1000000;
  Device->Mode.BaudRate = 115200;
  Device->Mode.ReceiveFifoDepth = BUFFER_SIZE;
  Device->Mode.DataBits = 8;
  Device->Mode.Parity = NoParity;
  Device->Mode.StopBits = OneStopBit;

  // Start with UART device path
  Device->DevicePath = DuplicateDevicePath((EFI_DEVICE_PATH_PROTOCOL *)&gUartDevicePathTemplate);
  Device->NetworkInitialized = FALSE;
  Device->Tcp4 = NULL;
  Device->Tcp4ChildHandle = NULL;
  Device->ListenToken.CompletionToken.Event = NULL;
  Device->RxToken.CompletionToken.Event = NULL;
  Device->TxToken.CompletionToken.Event = NULL;
  Device->RxToken.Packet.RxData = NULL;

  Device->Handle = NULL;

  if (Device->DevicePath == NULL) {
    DEBUG((DEBUG_ERROR, "NetSerial: Failed to create device path\n"));
    FreePool(Device);
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gBS->InstallMultipleProtocolInterfaces(&Device->Handle, &gEfiSerialIoProtocolGuid, &Device->SerialIo, &gEfiDevicePathProtocolGuid, Device->DevicePath, NULL);

  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "NetSerial: Failed to install protocols: %r\n", Status));
    FreePool(Device->DevicePath);
    FreePool(Device);
    return Status;
  }

  gNetSerialDevice = Device;
  gDriverBindingHandle = ImageHandle;

  DEBUG((DEBUG_INFO, "NetSerial: SerialIO protocol installed\n"));
  Print(L"\n========================================\n");
  Print(L"NetSerial Network Console Driver\n");
  Print(L"========================================\n");
  Print(L"✓ SerialIO protocol installed\n");
  Print(L"✓ Device appears in Boot Maintenance Manager\n");
  Print(L"✓ Will auto-activate when network ready\n\n");

  Status = InitializeNetworkStack(Device);
  if (!EFI_ERROR(Status)) {
    Device->NetworkInitialized = TRUE;
    DEBUG((DEBUG_INFO, "NetSerial: Network initialized successfully\n"));
    Print(L"✓ Network ready! Telnet server on port %d\n", TELNET_PORT);

    // Hook ConIn to poll network when Setup checks keyboard
    HookConIn(Device);
  } else {
    DEBUG((DEBUG_WARN, "NetSerial: Initial network init failed: %r\n", Status));
    Print(L"⧗ Network not ready yet, will retry automatically\n");
    Print(L"  Configure with: ifconfig -s eth0 dhcp\n");
  }
  Print(L"\n");

  Status = gBS->CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, NetworkRetryTimerCallback, Device, &Device->NetworkRetryTimer);

  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "NetSerial: Failed to create retry timer: %r\n", Status));
  } else {
    if (!Device->NetworkInitialized) {
      Status = gBS->SetTimer(Device->NetworkRetryTimer, TimerPeriodic, EFI_TIMER_PERIOD_SECONDS(5));
      if (EFI_ERROR(Status)) {
        DEBUG((DEBUG_ERROR, "NetSerial: Failed to set retry timer: %r\n", Status));
        gBS->CloseEvent(Device->NetworkRetryTimer);
        Device->NetworkRetryTimer = NULL;
      } else {
        DEBUG((DEBUG_INFO, "NetSerial: Retry timer started (5 second interval)\n"));
      }
    }
  }

  return EFI_SUCCESS;
}
EFI_STATUS EFIAPI NetSerialDriverUnload(IN EFI_HANDLE ImageHandle)
{
  EFI_STATUS Status;
  NETSERIAL_DEVICE *Device = gNetSerialDevice;
  EFI_SERVICE_BINDING_PROTOCOL *ServiceBinding;
  EFI_HANDLE *HandleBuffer;
  UINTN HandleCount;

  if (Device == NULL) {
    return EFI_SUCCESS;
  }

  Status = gBS->UninstallMultipleProtocolInterfaces(Device->Handle, &gEfiSerialIoProtocolGuid, &Device->SerialIo, &gEfiDevicePathProtocolGuid, Device->DevicePath, NULL);

  if (Device->Tcp4 != NULL) {
    Device->Tcp4->Configure(Device->Tcp4, NULL);
  }

  if (Device->NetworkRetryTimer != NULL) {
    gBS->SetTimer(Device->NetworkRetryTimer, TimerCancel, 0);
    gBS->CloseEvent(Device->NetworkRetryTimer);
  }
  if (Device->ListenToken.CompletionToken.Event != NULL) {
    gBS->CloseEvent(Device->ListenToken.CompletionToken.Event);
  }
  if (Device->RxToken.CompletionToken.Event != NULL) {
    gBS->CloseEvent(Device->RxToken.CompletionToken.Event);
  }
  if (Device->TxToken.CompletionToken.Event != NULL) {
    gBS->CloseEvent(Device->TxToken.CompletionToken.Event);
  }

  if (Device->RxToken.Packet.RxData != NULL) {
    FreePool(Device->RxToken.Packet.RxData);
  }

  if (Device->Tcp4ChildHandle != NULL) {
    Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiTcp4ServiceBindingProtocolGuid, NULL, &HandleCount, &HandleBuffer);

    if (!EFI_ERROR(Status)) {
      Status = gBS->HandleProtocol(HandleBuffer[0], &gEfiTcp4ServiceBindingProtocolGuid, (VOID **)&ServiceBinding);

      if (!EFI_ERROR(Status)) {
        ServiceBinding->DestroyChild(ServiceBinding, Device->Tcp4ChildHandle);
      }

      FreePool(HandleBuffer);
    }
  }

  FreePool(Device->DevicePath);
  FreePool(Device);
  gNetSerialDevice = NULL;

  DEBUG((DEBUG_INFO, "NetSerial: Driver unloaded\n"));

  return EFI_SUCCESS;
}