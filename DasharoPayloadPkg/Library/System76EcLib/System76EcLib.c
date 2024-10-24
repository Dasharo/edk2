/** @file
  System76 EC logging

  Copyright (c) 2020 System76, Inc.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/IoLib.h>

// From coreboot/src/drivers/system76_ec/system76_ec.c {
#define SYSTEM76_EC_BASE 0x0E00

static inline UINT8 system76_ec_read(UINT8 addr) {
    return IoRead8(SYSTEM76_EC_BASE + (UINT16)addr);
}

static inline void system76_ec_write(UINT8 addr, UINT8 data) {
    IoWrite8(SYSTEM76_EC_BASE + (UINT16)addr, data);
}

void system76_ec_init(void) {
    // Clear entire command region
    for (int i = 0; i < 256; i++) {
        system76_ec_write((UINT8)i, 0);
    }
}

void system76_ec_flush(void) {
    // Send command
    system76_ec_write(0, 4);

    // Wait for command completion
    while (system76_ec_read(0) != 0) {}

    // Clear length
    system76_ec_write(3, 0);
}

void system76_ec_print(UINT8 byte) {
    // Read length
    UINT8 len = system76_ec_read(3);
    // Write data at offset
    system76_ec_write(len + 4, byte);
    // Update length
    system76_ec_write(3, len + 1);

    // If we hit the end of the buffer, or were given a newline, flush
    if (byte == '\n' || len >= 128) {
        system76_ec_flush();
    }
}
// } From coreboot/src/drivers/system76_ec/system76_ec.c

// Implement SerialPortLib {
#include <Library/SerialPortLib.h>

RETURN_STATUS
EFIAPI
SerialPortInitialize (
  VOID
  )
{
    system76_ec_init();
    return RETURN_SUCCESS;
}

UINTN
EFIAPI
SerialPortWrite (
  IN UINT8     *Buffer,
  IN UINTN     NumberOfBytes
  )
{
    if (Buffer == NULL) {
        return 0;
    }

    if (NumberOfBytes == 0) {
        system76_ec_flush();
        return 0;
    }

    for(UINTN i = 0; i < NumberOfBytes; i++) {
        system76_ec_print(Buffer[i]);
    }

    return NumberOfBytes;
}

BOOLEAN
EFIAPI
SerialPortPoll (
  VOID
  )
{
    return FALSE;
}

RETURN_STATUS
EFIAPI
SerialPortGetControl (
  OUT UINT32 *Control
  )
{
    return RETURN_UNSUPPORTED;
}

RETURN_STATUS
EFIAPI
SerialPortSetControl (
  IN UINT32 Control
  )
{
    return RETURN_UNSUPPORTED;
}

RETURN_STATUS
EFIAPI
SerialPortSetAttributes (
  IN OUT UINT64             *BaudRate,
  IN OUT UINT32             *ReceiveFifoDepth,
  IN OUT UINT32             *Timeout,
  IN OUT EFI_PARITY_TYPE    *Parity,
  IN OUT UINT8              *DataBits,
  IN OUT EFI_STOP_BITS_TYPE *StopBits
  )
{
    return RETURN_UNSUPPORTED;
}
// } Implement SerialPortLib

// Implement PlatformHookLib {
#include <Library/PlatformHookLib.h>

RETURN_STATUS
EFIAPI
PlatformHookSerialPortInitialize (
  VOID
  )
{
    return RETURN_SUCCESS;
}
// } Implement PlatformHookLib
