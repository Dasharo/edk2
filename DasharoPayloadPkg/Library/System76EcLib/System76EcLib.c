/** @file
  System76 EC logging

  Copyright (c) 2020 System76, Inc.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/
#include <Uefi.h>
#include <Library/IoLib.h>

// From coreboot/src/drivers/system76_ec/system76_ec.c {
#define SYSTEM76_EC_BASE 0x0E00

static BOOLEAN flush_pending = FALSE;

static inline UINT8 system76_ec_read(UINT8 addr) {
    return IoRead8(SYSTEM76_EC_BASE + (UINT16)addr);
}

static inline void system76_ec_write(UINT8 addr, UINT8 data) {
    IoWrite8(SYSTEM76_EC_BASE + (UINT16)addr, data);
}

static void system76_ec_wait(void) {
    if (flush_pending) {
        while (system76_ec_read(0) != 0) {}
        system76_ec_write(3, 0);
        flush_pending = FALSE;
    }
}
void system76_ec_init(void) {
    // Clear entire command region
    for (int i = 0; i < 256; i++) {
        system76_ec_write((UINT8)i, 0);
    }
}

void system76_ec_flush(void) {
    // Send command
    system76_ec_wait();
    system76_ec_write(0, 4);
    flush_pending = TRUE;
}

void system76_ec_print(UINT8 byte) {
    system76_ec_wait();
    // Read length
    UINT8 len = system76_ec_read(3);
    // Write data at offset
    system76_ec_write(len + 4, byte);
    // Update length
    system76_ec_write(3, len + 1);

    // If we hit the end of the buffer, flush
    if (len >= 128 || byte == '\n') {
        system76_ec_flush();
    }
}
// } From coreboot/src/drivers/system76_ec/system76_ec.c

#include <Library/UefiBootServicesTableLib.h>

STATIC EFI_EVENT  mFlushTimer = NULL;

STATIC VOID EFIAPI
system76_ec_flush_tick (IN EFI_EVENT Event, IN VOID *Context) {
    if (system76_ec_read(3) != 0) {
        system76_ec_flush();
    }
}

STATIC VOID
system76_ec_arm_flush_timer (VOID) {
    if (mFlushTimer != NULL || gBS == NULL) {
        return;
    }
    if (EFI_ERROR (gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL,
            TPL_NOTIFY, system76_ec_flush_tick, NULL, &mFlushTimer))) {
        return;
    }
    gBS->SetTimer (mFlushTimer, TimerPeriodic, 1000000); // 100ms
}

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

    system76_ec_arm_flush_timer();
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
