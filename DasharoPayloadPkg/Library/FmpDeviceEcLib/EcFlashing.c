/** @file
  Provides functions for communicating with Dasharo EC.

  Based on the corresponding driver in coreboot.

  Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.

  SPDX-License-Identifier: GPL-2.0-only
**/

#include "EcFlashing.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>

#include "EcCommands.h"

//
// This is the command region for Dasharo EC firmware. It must be
// enabled for LPC in the mainboard.
//
#define DASHARO_EC_BASE  0x0E00
#define DASHARO_EC_SIZE  256

#define SPI_SECTOR_SIZE  1024

#define REG_CMD     0
#define REG_RESULT  1
#define REG_DATA    2  // Start of command data

// When command register is 0, command is complete
#define CMD_FINISHED  0

#define SPI_WREN       0x06  // Write Enable
#define SPI_WRDI       0x04  // Write Disable
#define SPI_RDSR       0x05  // Read Status Register
#define SPI_FAST_READ  0x0b  // Read Data Bytes at Higher Speed
#define SPI_AAI_WP     0xad  // Auto Address Increment Word Program
#define SPI_BE         0xd7  // 1K Block Erase

#define SPI_SR_WIP  (1 << 0)  // Write-in-Progress
#define SPI_SR_WEL  (1 << 1)  // Write enable

#define SPI_TIMEOUT_US  10000

#define SPI_ACCESS_SIZE  252

#define MAX_WRITE_RETRY  3

#define EC_FLASH_SIZE  FixedPcdGet32 (PcdEcFlashSize)

//
// Weight constants for progress calculation.
// Reading 8 blocks takes approximately the same time as 1 erase+write operation.
//
#define READ_SECTOR_WEIGHT   1
#define ERASE_SECTOR_WEIGHT  4
#define WRITE_SECTOR_WEIGHT  4

typedef enum {
  EcUpdateErrNoAc,      // AC adapter is not connected.
  EcUpdateErrScratch,   // EC did not jump to scratch ROM
  EcUpdateErrErase,     // EC erase failed
  EcUpdateErrProgram,   // Programming EC failed
} EC_UPDATE_ERROR;

STATIC
UINT8
DasharoEcRead (
  UINT8  Addr
  )
{
  return IoRead8 (DASHARO_EC_BASE + Addr);
}

STATIC
VOID
DasharoEcWrite (
  UINT8  Addr,
  UINT8  Data
  )
{
  IoWrite8 (DASHARO_EC_BASE + Addr, Data);
}

//
// Wait for command completion with a test period of 1 microsecond.
//
RETURN_STATUS
WaitForCmdFinish (
  INTN  TimeoutUs
  )
{
  while ((TimeoutUs > 0) && (DasharoEcRead (REG_CMD) != CMD_FINISHED)) {
    MicroSecondDelay (1);
    --TimeoutUs;
  }

	return TimeoutUs > 0 ? RETURN_SUCCESS : RETURN_TIMEOUT;
}

RETURN_STATUS
RunCmd (
  UINT8  Cmd
  )
{
  // Write command register, which starts the command, and wait for it to finish
  DasharoEcWrite (REG_CMD, Cmd);
  return WaitForCmdFinish (SPI_TIMEOUT_US);
}

STATIC
UINT8
DasharoEcSmfiCmd (
  UINT8  Cmd,
  UINT8  Len,
  UINT8  *Data
  )
{
  UINTN  Index;

  if (Len > DASHARO_EC_SIZE - REG_DATA) {
    DEBUG ((DEBUG_ERROR, "%a(): Invalid command length\n", __func__));
    return -1;
  }

  if (WaitForCmdFinish (SPI_TIMEOUT_US) != RETURN_SUCCESS) {
    DEBUG ((DEBUG_ERROR, "%a(): Failed waiting for previous command\n", __func__));
    return -1;
  }

  // Write data first
  for (Index = 0; Index < Len; ++Index) {
    DasharoEcWrite (REG_DATA + Index, Data[Index]);
  }

  if (RunCmd (Cmd) != RETURN_SUCCESS) {
    DEBUG ((DEBUG_ERROR, "%a(): Failed waiting for a command (%d)\n", __func__, Cmd));
    return -1;
  }

  return DasharoEcRead (REG_RESULT);
}

STATIC
UINT8
EcReadStr (
  UINT8  Cmd,
  CHAR8  *Buf,
  UINTN  BufSize
  )
{
  UINTN  Index;
  UINT8  Result;
  UINT8  Data;

  if ((Buf == NULL) || (BufSize == 0)) {
    return -1;
  }

  if (WaitForCmdFinish (SPI_TIMEOUT_US) != RETURN_SUCCESS) {
    DEBUG ((DEBUG_ERROR, "%a(): Failed waiting for previous command\n", __func__));
    return -1;
  }

  if (RunCmd (Cmd) != RETURN_SUCCESS) {
    DEBUG ((DEBUG_ERROR, "%a(): Failed waiting for command %d\n", __func__, Cmd));
    return -1;
  }

  Result = DasharoEcRead (REG_RESULT);
  if (Result != 0) {
    return Result;
  }

  for (Index = 0; Index < DASHARO_EC_SIZE - REG_DATA; Index++) {
    Data = DasharoEcRead (REG_DATA + Index);

    if (Index < BufSize) {
      Buf[Index] = Data;
    }

    if (Data == 0) {
      break;
    }
  }

  Buf[BufSize - 1] = '\0';

  return 0;
}

UINT8
EFIAPI
EcReadVersion (
  CHAR8  *Buf,
  UINTN  BufSize
  )
{
  return EcReadStr (CMD_VERSION, Buf, BufSize);
}

// Ported from system76_ectool
STATIC
UINTN
FirmwareStr (
  CONST CHAR8  *Data,
  UINTN        DataLen,
  CONST CHAR8  *Key,
  CHAR8        *Dest,
  UINTN        DestLen
  )
{
  UINTN  DataIdx;
  UINTN  KeyIdx;
  UINTN  RetIdx;
  UINTN  KeyLen;

  KeyLen  = AsciiStrLen (Key);
  DataIdx = 0;
  KeyIdx  = 0;
  RetIdx  = 0;

  // Locate the key
  while (DataIdx < DataLen && KeyIdx < KeyLen) {
    if (Data[DataIdx] == Key[KeyIdx]) {
      KeyIdx += 1;
    } else {
      KeyIdx = 0;
    }

    DataIdx += 1;
  }

  if (KeyIdx < KeyLen) {
    return 0;
  }

  while (DataIdx < DataLen && RetIdx < DestLen - 1 && Data[DataIdx] != 0) {
    Dest[RetIdx++] = Data[DataIdx++];
  }

  Dest[RetIdx] = '\0';

  return RetIdx;
}

// Reset the EC SPI bus
STATIC
UINT8
EcSpiReset (
  VOID
  )
{
  UINT8  ResetCmd[2];

  ResetCmd[0] = CMD_SPI_FLAG_DISABLE | CMD_SPI_FLAG_SCRATCH;
  ResetCmd[1] = 0;

  return DasharoEcSmfiCmd (CMD_SPI, sizeof (ResetCmd), ResetCmd);
}

//
// Read Len bytes from EC SPI bus into Dest.
// Returns 0 on success, <0 on error.
//
STATIC
INTN
EcSpiBusRead (
  UINT8   *Dest,
  UINT32  Len
  )
{
  UINT32  Addr;
  UINT32  Index;
  UINT32  Rv;
  UINT8   ReadCmd[2];

  ReadCmd[0] = CMD_SPI_FLAG_READ | CMD_SPI_FLAG_SCRATCH;
  ReadCmd[1] = 0;

  for (Addr = 0; Addr + SPI_ACCESS_SIZE < Len; Addr += SPI_ACCESS_SIZE) {
    ReadCmd[1] = SPI_ACCESS_SIZE;

    if ((Rv = DasharoEcSmfiCmd (CMD_SPI, sizeof (ReadCmd), ReadCmd))) {
      DEBUG ((DEBUG_ERROR, "%a(): Failed to send read SPI bus command\n", __func__));
      return -Rv;
    }

    if (DasharoEcRead (REG_DATA + 1) != ReadCmd[1]) {
      DEBUG ((DEBUG_ERROR, "%a(): SPI bus read insufficient bytes\n", __func__));
      return -1;
    }

    for (Index = 0; Index < ReadCmd[1]; Index++) {
      Dest[Addr + Index] = DasharoEcRead (REG_DATA + 2 + Index);
    }
  }

  if (Addr == Len) {
    return 0;
  }

  ReadCmd[1] = Len % SPI_ACCESS_SIZE;

  if ((Rv = DasharoEcSmfiCmd (CMD_SPI, sizeof (ReadCmd), ReadCmd))) {
    DEBUG ((DEBUG_ERROR, "%a(): Failed to send read SPI bus command (remainder)\n", __func__));
    return -Rv;
  }

  if (DasharoEcRead (REG_DATA + 1) != ReadCmd[1]) {
    DEBUG ((DEBUG_ERROR, "%a(): SPI bus read remainder insufficient bytes\n", __func__));
    return -1;
  }

  for (Index = 0; Index < ReadCmd[1]; Index++) {
    Dest[Addr + Index] = DasharoEcRead (REG_DATA + 2 + Index);
  }

  return 0;
}

//
// Write Len bytes from Data to EC SPI bus with Flags.
// Returns 0 on success, <0 on error.
//
STATIC
INTN
EcSpiBusWriteWithFlags (
  UINT8  *Data,
  UINT8  Len,
  UINT8  Flags
  )
{
  UINT32  Addr;
  UINT32  Index;
  UINT32  Rv;
  UINT8   WriteCmd[2];

  WriteCmd[0] = CMD_SPI_FLAG_SCRATCH | Flags;
  WriteCmd[1] = 0;

  for (Addr = 0; Addr + SPI_ACCESS_SIZE < Len; Addr += SPI_ACCESS_SIZE) {
    WriteCmd[1] = SPI_ACCESS_SIZE;

    for (Index = 0; Index < WriteCmd[1]; ++Index) {
      DasharoEcWrite (REG_DATA + sizeof (WriteCmd) + Index, Data[Addr + Index]);
    }

    Rv = DasharoEcSmfiCmd (CMD_SPI, sizeof (WriteCmd), WriteCmd);
    if (Rv) {
      DEBUG ((DEBUG_ERROR, "%a(): Failed to send write SPI bus command\n", __func__));
      return -Rv;
    }

    if (DasharoEcRead (REG_DATA + 1) != WriteCmd[1]) {
      DEBUG ((DEBUG_ERROR, "%a(): SPI bus write insufficient bytes\n", __func__));
      return -1;
    }
  }

  if (Addr == Len) {
    return 0;
  }

  WriteCmd[1] = Len % SPI_ACCESS_SIZE;

  for (Index = 0; Index < WriteCmd[1]; Index++) {
    DasharoEcWrite (REG_DATA + sizeof (WriteCmd) + Index, Data[Addr + Index]);
  }

  Rv = DasharoEcSmfiCmd (CMD_SPI, sizeof (WriteCmd), WriteCmd);
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): Failed to send write SPI bus command (remainder)\n", __func__));
    return -Rv;
  }

  if (DasharoEcRead (REG_DATA + 1) != WriteCmd[1]) {
    DEBUG ((DEBUG_ERROR, "%a(): SPI bus write remainder insufficient bytes\n", __func__));
    return -1;
  }

  return 0;
}

//
// Write Len bytes from Data to EC SPI bus.
// Returns 0 on success, <0 on error.
//
STATIC
INTN
EcSpiBusWrite (
  UINT8  *Data,
  UINT8  Len
  )
{
  return EcSpiBusWriteWithFlags (Data, Len, 0);
}

STATIC
INTN
EcSpiWaitStatus (
  UINT8        Mask,
  UINT8        Value,
  CONST CHAR8  *Func,
  UINTN        Line
  )
{
  UINT64  StartTimeUs;
  UINT64  CurrentTimeUs;
  UINT8   Status;
  INTN    Rv;
  UINT8   Cmd;

  Cmd = SPI_RDSR;

  Rv = EcSpiReset ();
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiReset() failed\n", __func__));
    return Rv;
  }

  Rv = EcSpiBusWrite (&Cmd, sizeof (Cmd));
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiBusWrite() failed\n", __func__));
    return Rv;
  }

  StartTimeUs = GetTimeInNanoSecond (GetPerformanceCounter ()) / 1000;

  Rv = EcSpiBusRead (&Status, sizeof (Status));
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiBusRead() failed\n", __func__));
    return Rv;
  }

  while ((Status & Mask) != Value) {
    CurrentTimeUs = GetTimeInNanoSecond (GetPerformanceCounter ()) / 1000;
    if (CurrentTimeUs - StartTimeUs >= SPI_TIMEOUT_US) {
      DEBUG ((DEBUG_ERROR, "%a(): Timeout at %a():%u\n", __func__, Func, Line));
      EcSpiReset ();
      return -1;
    }

    //
    // Status Register can be constantly read from SPI bus.
    // No need to start new RDSR command.
    //
    Rv = EcSpiBusRead (&Status, sizeof (Status));
    if (Rv) {
      DEBUG ((DEBUG_ERROR, "%a(): EcSpiBusRead() failed\n", __func__));
      EcSpiReset ();
      return Rv;
    }
  }

  Rv = EcSpiReset ();
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiReset() failed\n", __func__));
    return Rv;
  }

  return 0;
}

STATIC
INTN
EcSpiCmdWriteEnable (
  VOID
  )
{
  INTN   Rv;
  UINT8  Cmd;

  Cmd = SPI_WREN;

  Rv = EcSpiWaitStatus (SPI_SR_WIP, 0, __func__, __LINE__);
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): flash not ready\n", __func__));
    return Rv;
  }

  Rv = EcSpiBusWrite (&Cmd, sizeof (Cmd));
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiBusWrite() failed\n", __func__));
    return Rv;
  }

  return EcSpiWaitStatus (SPI_SR_WIP | SPI_SR_WEL, SPI_SR_WEL, __func__, __LINE__);
}

STATIC
INTN
EcSpiCmdWriteDisable (
  VOID
  )
{
  INTN   Rv;
  UINT8  Cmd;

  Cmd = SPI_WRDI;

  Rv = EcSpiReset ();
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiReset() failed\n", __func__));
    return Rv;
  }

  Rv = EcSpiBusWrite (&Cmd, sizeof (Cmd));
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiBusWrite() failed\n", __func__));
    return Rv;
  }

  return EcSpiWaitStatus (SPI_SR_WIP | SPI_SR_WEL, 0, __func__, __LINE__);
}

// Erase a sector at Addr. Returns 0 on success, <0 on error.
STATIC
INTN
EcSpiEraseSector (
  UINT32  Addr
  )
{
  INTN   Rv;
  UINT8  Buf[4];

  Buf[0] = SPI_BE;
  Buf[1] = (Addr >> 16) & 0xFF;
  Buf[2] = (Addr >> 8) & 0xFF;
  Buf[3] = Addr & 0xFF;

  Rv = EcSpiCmdWriteEnable ();
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiCmdWriteEnable() failed\n", __func__));
    return Rv;
  }

  Rv = EcSpiReset ();
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiReset() failed\n", __func__));
    return Rv;
  }

  Rv = EcSpiBusWrite (Buf, sizeof (Buf));
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiBusWrite() failed\n", __func__));
    return Rv;
  }

  Rv = EcSpiWaitStatus (SPI_SR_WIP, 0, __func__, __LINE__);
  if (Rv) {
    return Rv;
  }

  Rv = EcSpiCmdWriteDisable ();
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiCmdWriteDisable() failed\n", __func__));
    return Rv;
  }

  return 0;
}

// Read a sector into Dest. Returns 0 on success and <0 on error.
STATIC
INTN
EcSpiReadSector (
  UINT8   *Dest,
  UINT32  Addr
  )
{
  INTN   Rv;
  UINT8  Buf[5];

  Buf[0] = SPI_FAST_READ;
  Buf[1] = (Addr >> 16) & 0xFF;
  Buf[2] = (Addr >> 8) & 0xFF;
  Buf[3] = Addr & 0xFF;
  Buf[4] = 0;

  Rv = EcSpiWaitStatus (SPI_SR_WIP, 0, __func__, __LINE__);
  if (Rv) {
    return Rv;
  }

  Rv = EcSpiBusWrite (Buf, sizeof (Buf));
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiBusWrite() failed\n", __func__));
    return Rv;
  }

  Rv = EcSpiBusRead (Dest, SPI_SECTOR_SIZE);
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiBusRead() failed\n", __func__));
    return Rv;
  }

  Rv = EcSpiReset ();
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiReset() failed\n", __func__));
    return Rv;
  }

  return 0;
}

// Read a sector and verify if it has been erased. Returns 0 on success and <0 on error.
STATIC
INTN
EcSpiVerifyErasedSector (
  UINT32  Addr
  )
{
  INTN   Rv;
  UINTN  Index;
  UINT8  Buf[5];
  UINT8  Data;

  Buf[0] = SPI_FAST_READ;  // SPI Read
  Buf[1] = (Addr >> 16) & 0xFF;
  Buf[2] = (Addr >> 8) & 0xFF;
  Buf[3] = Addr & 0xFF;
  Buf[4] = 0;

  Rv = EcSpiWaitStatus (SPI_SR_WIP, 0, __func__, __LINE__);
  if (Rv) {
    return Rv;
  }

  Rv = EcSpiBusWrite (Buf, sizeof (Buf));
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiBusWrite() failed\n", __func__));
    return Rv;
  }

  for (Index = 0; Index < SPI_SECTOR_SIZE; Index++) {
    // Read the sector byte after byte and compare.
    Rv = EcSpiBusRead (&Data, sizeof (Data));
    if (Rv) {
      DEBUG ((DEBUG_ERROR, "%a(): EcSpiBusRead() failed\n", __func__));
      return Rv;
    }

    if (Data != 0xff) {
      DEBUG ((DEBUG_ERROR, "%a(): sector at %x is not erased\n", __func__, Addr));
      return -1;
    }
  }

  Rv = EcSpiReset ();
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiReset() failed\n", __func__));
    return Rv;
  }

  return 0;
}

//
// Program a chip with a given image at given address and size of data.
// Returns written byte count on success and <0 on error.
//
STATIC
INTN
EcSpiWriteAt (
  UINT32       Start,
  CONST UINT8  *Image,
  UINTN        Size
  )
{
  INTN    Rv;
  UINT32  Addr;
  UINT8   Buf[6];

  Buf[0] = SPI_AAI_WP;
  Buf[1] = (Start >> 16) & 0xff;
  Buf[2] = (Start >> 8) & 0xff;
  Buf[3] = Start & 0xff;
  Buf[4] = 0;
  Buf[5] = 0;

  Rv = EcSpiCmdWriteEnable ();
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiCmdWriteEnable() failed\n", __func__));
    return Rv;
  }

  for (Addr = Start; Addr < Start + Size; Addr += 2) {
    //
    // EcSpiCmdWriteEnable ends with EcSpiWaitStatus which always calls
    // EcSpiReset. No need to do it here again. We also reset the SPI
    // (make the CS# go high) after AAI WP command by passing the
    // CMD_SPI_FLAG_DISABLE to EcSpiBusWriteWithFlags.
    //

    if (Addr == Start) {
      // 1st cmd bytes 1,2,3 are the address
      Buf[4] = Image[Addr];
      Buf[5] = Image[Addr + 1];

      Rv = EcSpiBusWriteWithFlags (Buf, 6, CMD_SPI_FLAG_DISABLE);
    } else {
      Buf[1] = Image[Addr];
      Buf[2] = Image[Addr + 1];

      Rv = EcSpiBusWriteWithFlags (Buf, 3, CMD_SPI_FLAG_DISABLE);
    }

    if (Rv) {
      DEBUG ((DEBUG_ERROR, "%a(): EcSpiBusWriteWithFlags() failed, addr 0x%06x\n", __func__, Addr));
      return Rv;
    }

    //
    // From the experiments it looked like the busy bit is never set.
    // It is still dangerous to not probe the bit, however, AAI programming
    // is quite picky and stops after first 2 bytes if we interrupt the
    // process with different command than AAI word program.
    //
    // Rv = EcSpiWaitStatus (SPI_SR_WIP, 0, __func__, __LINE__);
    // if (Rv) {
    //   return Rv;
    // }
    //
  }

  Rv = EcSpiCmdWriteDisable ();
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiCmdWriteDisable() failed\n", __func__));
    return Rv;
  }

  return Addr - Start;
}

//
// Program an entire chip with a given image.
// Returns written byte count on success and <0 on error.
//
STATIC
INTN
EcSpiImageWrite (
  CONST UINT8          *Image,
  UINTN                Size,
  FW_PROGRESS_CONTROL  ProgressCtl,
  VOID                 *ProgressContext
  )
{
  UINT8   *Sector;
  UINT32  Addr;
  UINT32  EraseAddr;
  UINTN   Length;
  INTN    Rv;

  Sector = AllocatePool (SPI_SECTOR_SIZE);
  if (Sector == NULL) {
    return -1;
  }

  Rv   = 0;
  Addr = 0;

  while ((Addr < EC_FLASH_SIZE) && (Addr + SPI_SECTOR_SIZE < Size)) {
    Rv = EcSpiReadSector (Sector, Addr);
    ProgressCtl (PROGRESS_INC_CURRENT, READ_SECTOR_WEIGHT, ProgressContext);
    if (Rv) {
      DEBUG ((DEBUG_ERROR, "%a(): EcSpiReadSector() failed, addr 0x%06x\n", __func__, Addr));
      goto Cleanup;
    }

    if (!CompareMem (Sector, Image + Addr, SPI_SECTOR_SIZE)) {
      DEBUG ((DEBUG_VERBOSE, "%a(): Skipping identical sector, addr 0x%06x\n", __func__, Addr));
      Addr += SPI_SECTOR_SIZE;
      ProgressCtl (
        PROGRESS_INC_CURRENT,
        ERASE_SECTOR_WEIGHT + READ_SECTOR_WEIGHT + WRITE_SECTOR_WEIGHT,
        ProgressContext
        );
      continue;
    }

    Rv = EcSpiEraseSector (Addr);
    ProgressCtl (PROGRESS_INC_CURRENT, ERASE_SECTOR_WEIGHT, ProgressContext);
    if (Rv) {
      DEBUG ((DEBUG_ERROR, "%a(): EcSpiEraseSector() failed, addr 0x%06x\n", __func__, Addr));
      goto Cleanup;
    }

    Rv = EcSpiVerifyErasedSector (Addr);
    ProgressCtl (PROGRESS_INC_CURRENT, READ_SECTOR_WEIGHT, ProgressContext);
    if (Rv) {
      DEBUG ((DEBUG_ERROR, "%a(): EcSpiVerifyErasedSector() failed, addr 0x%06x\n", __func__, Addr));
      goto Cleanup;
    }

    Rv = EcSpiWriteAt (Addr, Image, SPI_SECTOR_SIZE);
    ProgressCtl (PROGRESS_INC_CURRENT, WRITE_SECTOR_WEIGHT, ProgressContext);
    if (Rv < 0) {
      DEBUG ((DEBUG_ERROR, "%a(): EcSpiWriteAt() failed, addr 0x%06x (%d)\n", __func__, Addr, Rv));
      goto Cleanup;
    }

    Addr += SPI_SECTOR_SIZE;
  }

  // Update any remainder bytes.
  Rv = EcSpiReadSector (Sector, Addr);
  ProgressCtl (PROGRESS_INC_CURRENT, READ_SECTOR_WEIGHT, ProgressContext);
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiReadSector() failed, last addr 0x%06x\n", __func__, Addr));
    goto Cleanup;
  }

  // Save EraseAddr before Addr is updated.
  EraseAddr = Addr + SPI_SECTOR_SIZE;

  if (Size % SPI_SECTOR_SIZE == 0 && Addr < EC_FLASH_SIZE) {
    Length = SPI_SECTOR_SIZE;
  } else {
    Length = Size % SPI_SECTOR_SIZE;
  }

  if (CompareMem (Sector, Image + Addr, Length)) {
    Rv = EcSpiEraseSector (Addr);
    ProgressCtl (PROGRESS_INC_CURRENT, ERASE_SECTOR_WEIGHT, ProgressContext);
    if (Rv) {
      DEBUG ((DEBUG_ERROR, "%a(): EcSpiEraseSector() failed, addr 0x%06x\n", __func__, Addr));
      goto Cleanup;
    }

    Rv = EcSpiVerifyErasedSector (Addr);
    ProgressCtl (PROGRESS_INC_CURRENT, READ_SECTOR_WEIGHT, ProgressContext);
    if (Rv) {
      DEBUG ((DEBUG_ERROR, "%a(): EcSpiVerifyErasedSector() failed, addr 0x%06x\n", __func__, Addr));
      goto Cleanup;
    }

    Rv = EcSpiWriteAt (Addr, Image, Length);
    ProgressCtl (PROGRESS_INC_CURRENT, WRITE_SECTOR_WEIGHT, ProgressContext);
    if (Rv < 0) {
      DEBUG ((DEBUG_ERROR, "%a(): EcSpiWriteAt() failed, addr 0x%06x (%d)\n", __func__, Addr, Rv));
      goto Cleanup;
    }

    Addr += Length;
  } else {
    DEBUG ((DEBUG_VERBOSE, "%a(): Skipping identical sector, addr 0x%06x\n", __func__, Addr));
    Addr += Length;
  }

  // Erase remaining sectors if any.
  while (EraseAddr < EC_FLASH_SIZE) {
    Rv = EcSpiEraseSector (EraseAddr);
    ProgressCtl (PROGRESS_INC_CURRENT, ERASE_SECTOR_WEIGHT, ProgressContext);
    if (Rv) {
      DEBUG ((DEBUG_ERROR, "%a(): EcSpiEraseSector() failed, addr 0x%06x\n", __func__, EraseAddr));
      goto Cleanup;
    }

    EraseAddr += SPI_SECTOR_SIZE;
  }

  // If we got here, it's a success.
  Rv = 0;

Cleanup:
  FreePool (Sector);

  // Some functions return a positive value on error.
  if (Rv > 0) {
    Rv = -Rv;
  }

  return Rv ? Rv : Addr;
}

// Verify an image sector by sector. Returns 0 on success and <0 on error.
STATIC
INTN
EcSpiImageVerify (
  CONST UINT8          *Image,
  UINTN                ImageSz,
  FW_PROGRESS_CONTROL  ProgressCtl,
  VOID                 *ProgressContext
  )
{
  UINT8   *Sector;
  UINT32  Addr;
  INTN    Rv;
  INTN    Error;

  Sector = AllocatePool (SPI_SECTOR_SIZE);
  if (Sector == NULL) {
    return -1;
  }

  Rv    = 0;
  Addr  = 0;
  Error = 0;

  while ((Addr < EC_FLASH_SIZE) && (Addr + SPI_SECTOR_SIZE < ImageSz)) {
    Rv = EcSpiReadSector (Sector, Addr);
    ProgressCtl (PROGRESS_INC_CURRENT, READ_SECTOR_WEIGHT, ProgressContext);
    if (Rv) {
      DEBUG ((DEBUG_ERROR, "%a(): EcSpiReadSector() failed, addr 0x%06x\n", __func__, Addr));
      return Rv;
    }

    Rv = CompareMem (Sector, Image + Addr, SPI_SECTOR_SIZE) ? -1 : 0;
    if (Rv) {
      DEBUG ((DEBUG_ERROR, "%a(): failed to verify sector, addr 0x%06x\n", __func__, Addr));
      Error = Rv;
    }

    Addr += SPI_SECTOR_SIZE;
  }

  if (Addr == ImageSz) {
    goto Exit;
  }

  Rv = EcSpiReadSector (Sector, Addr);
  ProgressCtl (PROGRESS_INC_CURRENT, READ_SECTOR_WEIGHT, ProgressContext);
  if (Rv) {
    DEBUG ((DEBUG_ERROR, "%a(): EcSpiReadSector() failed, last addr 0x%06x\n", __func__, Addr));
    goto Exit;
  }

  Rv = CompareMem (Sector, Image + Addr, ImageSz % SPI_SECTOR_SIZE) ? -1 : 0;
  if (Rv) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to verify last sector, addr 0x%06x size 0x%lx\n",
      __func__,
      Addr,
      ImageSz % SPI_SECTOR_SIZE
      ));
    goto Exit;
  }

Exit:
  FreePool (Sector);

  if (Error) {
    return Error;
  }

  return Rv;
}

BOOLEAN
EFIAPI
EcImageIsValid (
  CONST VOID  *Image,
  UINTN       ImageSz
  )
{
  CHAR8  ImgBoardStr[64];
  CHAR8  ImgVersionStr[64];
  CHAR8  CurBoardStr[64];
  CHAR8  CurVersionStr[64];

  // Sanity checks.
  if (ImageSz % 2 || ImageSz > EC_FLASH_SIZE || ImageSz % SPI_SECTOR_SIZE != 0) {
    DEBUG ((DEBUG_ERROR, "%a(): incorrect update size.\n", __func__));
    return FALSE;
  }

  if (!FirmwareStr (Image, ImageSz, "76EC_BOARD=", ImgBoardStr, sizeof (ImgBoardStr))) {
    DEBUG ((DEBUG_ERROR, "%a(): could not determine update target board.\n", __func__));
    return FALSE;
  }

  if (!FirmwareStr (Image, ImageSz, "76EC_VERSION=", ImgVersionStr, sizeof (ImgVersionStr))) {
    DEBUG ((DEBUG_ERROR, "%a(): could not determine update version.\n", __func__));
    return FALSE;
  }

  DEBUG ((DEBUG_INFO, "%a(): update board: %a\n", __func__, ImgBoardStr));
  DEBUG ((DEBUG_INFO, "%a(): update version: %a\n", __func__, ImgVersionStr));

  EcReadStr (CMD_BOARD, CurBoardStr, sizeof (CurBoardStr));
  EcReadVersion (CurVersionStr, sizeof (CurVersionStr));

  DEBUG ((DEBUG_INFO, "%a(): current board: %a\n", __func__, CurBoardStr));
  DEBUG ((DEBUG_INFO, "%a(): current version: %a\n", __func__, CurVersionStr));

  if (AsciiStrCmp (ImgBoardStr, CurBoardStr) != 0) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): update target mismatch detected! Found '%a', expected '%a'\n",
      __func__,
      ImgBoardStr,
      CurBoardStr
      ));
    return FALSE;
  }

  return TRUE;
}

EFI_STATUS
EFIAPI
EcFlashImage (
  CONST VOID           *Image,
  UINTN                ImageSz,
  FW_PROGRESS_CONTROL  ProgressCtl,
  VOID                 *ProgressContext
  )
{
  UINT8  SmfiCmd[2];
  INTN   Rv;
  UINTN  Index;
  UINTN  SectorCount;

  SmfiCmd[0] = 0;
  SmfiCmd[1] = 0;

  // An extra check just in case, shouldn't incur significant overhead.
  if (!EcImageIsValid (Image, ImageSz)) {
    return EFI_ABORTED;
  }

  SectorCount = (ImageSz + (SPI_SECTOR_SIZE - 1)) / SPI_SECTOR_SIZE;

  //
  // Write involves reading to skip writing sectors that don't need to be
  // updated and then another reading later to verify erasure before writing.
  // Flash image can be partial, the rest of the chip will be erased.
  //
  // Verification involves reading every sector once again.
  //
  ProgressCtl (
    PROGRESS_SET_TOTAL,
    (READ_SECTOR_WEIGHT + ERASE_SECTOR_WEIGHT + READ_SECTOR_WEIGHT + WRITE_SECTOR_WEIGHT) * SectorCount +
    ERASE_SECTOR_WEIGHT * ((EC_FLASH_SIZE - ImageSz) / SPI_SECTOR_SIZE) +
    READ_SECTOR_WEIGHT * SectorCount,
    ProgressContext
    );

  // Jump to Scratch ROM.
  SmfiCmd[0] = CMD_SPI_FLAG_SCRATCH;
  if (DasharoEcSmfiCmd (CMD_SPI, sizeof (SmfiCmd), SmfiCmd)) {
    DEBUG ((DEBUG_ERROR, "%a(): failed to jump to scratch ROM!\n", __func__));
    return EFI_DEVICE_ERROR;
  }

  for (Index = 0; Index < MAX_WRITE_RETRY; Index++) {
    Rv = EcSpiImageWrite (Image, ImageSz, ProgressCtl, ProgressContext);
    if (Rv < 0) {
      // EC is now in an unknown state. It may still boot from backup.
      DEBUG ((DEBUG_ERROR, "%a(): update failed!\n", __func__));
    } else {
      DEBUG ((DEBUG_INFO, "%a(): wrote 0x%x bytes\n", __func__, Rv));
    }

    Rv = EcSpiImageVerify (Image, ImageSz, ProgressCtl, ProgressContext);
    if (Rv < 0) {
      DEBUG ((DEBUG_ERROR, "%a(): update verification failed! (try: %d)\n", __func__, Index + 1));
    } else {
      DEBUG ((DEBUG_INFO, "%a(): update verified.\n", __func__));
      Rv = 0;
      break;
    }
  }

  if (Rv < 0) {
    // EC is now in an unknown state. It may still boot from backup.
    DEBUG ((DEBUG_ERROR, "%a(): update failed!\n", __func__));
    return EFI_DEVICE_ERROR;
  }

  SmfiCmd[0] = CMD_SPI_FLAG_DISABLE;
  if (DasharoEcSmfiCmd (CMD_SPI, sizeof (SmfiCmd), SmfiCmd)) {
    DEBUG ((DEBUG_ERROR, "%a(): failed to disable SPI bus!\n", __func__));
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
EcReset (
  VOID
  )
{
  UINT8  ResetCmd;

  ResetCmd = 0;
  if (DasharoEcSmfiCmd (CMD_RESET, sizeof (ResetCmd), &ResetCmd)) {
    DEBUG ((DEBUG_ERROR, "%a(): failed to trigger reset!\n", __func__));
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}
