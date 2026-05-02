/** @file
  Tpm2DeviceLib implementation for AMD fTPM.

  AMD's PSP exposes a fTPM through a reduced CRB-style register block in
  PSP MMIO space. The layout differs from the standard TCG CRB:

    - No InterfaceId / Locality control / Cancel / IdleByPass registers.
    - Only STATUS (1 bit Error) at 0x44 and START (1 bit Go) at 0x4C.
    - Command / response data live in RAM buffers pointed to by the
      CMD_ADDR / RESP_ADDR registers (programmed by coreboot at boot).

  Coreboot's reference implementation lives in src/drivers/amd/ftpm/tpm.c
  (function crb_tpm_process_command). This library is a port of that
  protocol to EDK2's Tpm2DeviceLib API.

  The PSP MMIO base comes from PSP_ADDR_MSR (0xC00110A2), which coreboot
  syncs across all CPUs via SOC_AMD_COMMON_BLOCK_CPU_SYNC_PSP_ADDR_MSR.
  The fTPM CRB block sits at psp_base + PSPV2_MBOX_CMD_OFFSET - 0xA0
  (offset comes from PcdAmdPspv2MboxCmdOffset, set per-SoC by coreboot's
  Kconfig).

  Notes on the register layout that surprised us:
    - CMD_ADDR / RESP_ADDR are 64-bit logical registers but live at
      offsets 0x5C and 0x68. 0x5C is *not* 8-byte aligned, so we access
      the high/low halves as separate UINT32 words to avoid an EDK2
      MmioRead64/MmioWrite64 alignment ASSERT.
    - CMD_SIZE / RESP_SIZE are sticky in the sense that the TPM rewrites
      RESP_SIZE with the actual response length on completion. We must
      cache the maximum at init and restore it before each command.
    - SecurityPkg's stock Tcg2Dxe clobbers CMD_ADDR / RESP_ADDR when it
      builds its TPM2 ACPI table (it assumes the TCG-standard layout
      where the data buffer lives at CRB+0x80). We restore them on every
      command so the TPM still sees coreboot's RAM buffers.

  Copyright (c) 2026, 3mdeb.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiPei.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/Tpm2DeviceLib.h>

//
// AMD fTPM CRB register offsets, relative to the CRB base.
// See src/drivers/amd/ftpm/tpm.h for ground truth.
//
#define AMD_FTPM_CRB_STATUS         0x44
#define   AMD_FTPM_CRB_STATUS_ERROR BIT0
#define AMD_FTPM_CRB_START          0x4C
#define   AMD_FTPM_CRB_START_GO     BIT0
#define AMD_FTPM_CRB_CMD_SIZE       0x58
#define AMD_FTPM_CRB_CMD_ADDR_LO    0x5C
#define AMD_FTPM_CRB_CMD_ADDR_HI    0x60
#define AMD_FTPM_CRB_RESP_SIZE      0x64
#define AMD_FTPM_CRB_RESP_ADDR_LO   0x68
#define AMD_FTPM_CRB_RESP_ADDR_HI   0x6C

#define AMD_PSP_ADDR_MSR            0xC00110A2

//
// TPM2 response header is 10 bytes:
//   tag (UINT16, BE), responseSize (UINT32, BE), responseCode (UINT32, BE).
//
#define TPM2_RESPONSE_HEADER_SIZE   10
#define TPM2_RESPONSE_SIZE_OFFSET   2

//
// Wait timeouts. Command-completion can be slow for key generation, so the
// outer bound is generous. Polling cadence is 100us.
//
#define POLL_INTERVAL_US            100
#define WAIT_PRESENCE_MS            250
#define WAIT_COMMAND_MS             (90 * 1000)

STATIC UINTN    mCrbBase     = 0;
STATIC UINTN    mCmdAddr     = 0;
STATIC UINTN    mRespAddr    = 0;
STATIC UINT32   mCmdSizeMax  = 0;
STATIC UINT32   mRespSizeMax = 0;
STATIC BOOLEAN  mInitialized = FALSE;
STATIC BOOLEAN  mPresent     = FALSE;

/**
  Read a 64-bit pair (low at Reg, high at Reg+4) as two 32-bit MMIO
  reads. The standard CRB spec defines CMD_LADDR/CMD_HADDR and
  RESP_LADDR/RESP_HADDR as separate 32-bit registers, and 0x5C is not
  8-byte aligned, so we cannot use MmioRead64.
**/
STATIC
UINTN
ReadAddrPair (
  IN UINTN  Reg
  )
{
  UINT64  Lo;
  UINT64  Hi;

  Lo = MmioRead32 (Reg);
  Hi = MmioRead32 (Reg + 4);
  return (UINTN)(Lo | (Hi << 32));
}

/**
  Write a 64-bit value as two 32-bit MMIO writes (low first to match how
  coreboot programs them).
**/
STATIC
VOID
WriteAddrPair (
  IN UINTN  Reg,
  IN UINTN  Value
  )
{
  MmioWrite32 (Reg,     (UINT32)Value);
  MmioWrite32 (Reg + 4, (UINT32)(((UINT64)Value) >> 32));
}

/**
  Validate the CPU is AMD and read the PSP MMIO base from PSP_ADDR_MSR.
  Compute the fTPM CRB register-block address, cache the buffer pointers
  and max sizes that coreboot programmed at boot. Cached on first call.

  @retval TRUE   AMD fTPM appears reachable.
  @retval FALSE  Not AMD, MSR not programmed, or buffers not yet set up.
**/
STATIC
BOOLEAN
EnsureCrbBase (
  VOID
  )
{
  UINT32  RegEbx;
  UINT32  RegEcx;
  UINT32  RegEdx;
  UINT64  Msr;
  UINT64  PspBase;

  if (mInitialized) {
    return mPresent;
  }

  mInitialized = TRUE;

  AsmCpuid (0, NULL, &RegEbx, &RegEcx, &RegEdx);
  if ((RegEbx != SIGNATURE_32 ('A', 'u', 't', 'h')) ||
      (RegEdx != SIGNATURE_32 ('e', 'n', 't', 'i')) ||
      (RegEcx != SIGNATURE_32 ('c', 'A', 'M', 'D')))
  {
    DEBUG ((DEBUG_INFO, "AmdFtpm: not an AMD CPU\n"));
    return FALSE;
  }

  Msr     = AsmReadMsr64 (AMD_PSP_ADDR_MSR);
  PspBase = Msr & 0xFFFFFFFFULL;
  if ((PspBase == 0) || (PspBase == 0xFFFFFFFFULL)) {
    DEBUG ((DEBUG_INFO, "AmdFtpm: PSP_ADDR_MSR not programmed (0x%lx)\n", Msr));
    return FALSE;
  }

  mCrbBase = (UINTN)PspBase + FixedPcdGet32 (PcdAmdPspv2MboxCmdOffset) - 0xA0;

  mCmdSizeMax  = MmioRead32 (mCrbBase + AMD_FTPM_CRB_CMD_SIZE);
  mRespSizeMax = MmioRead32 (mCrbBase + AMD_FTPM_CRB_RESP_SIZE);
  mCmdAddr     = ReadAddrPair (mCrbBase + AMD_FTPM_CRB_CMD_ADDR_LO);
  mRespAddr    = ReadAddrPair (mCrbBase + AMD_FTPM_CRB_RESP_ADDR_LO);

  if ((mCmdSizeMax == 0) || (mRespSizeMax == 0) ||
      (mCmdAddr == 0) || (mRespAddr == 0))
  {
    DEBUG ((
      DEBUG_INFO,
      "AmdFtpm: CRB buffers not set up at 0x%lx (cmd=0x%lx/%u resp=0x%lx/%u)\n",
      (UINT64)mCrbBase,
      (UINT64)mCmdAddr,
      mCmdSizeMax,
      (UINT64)mRespAddr,
      mRespSizeMax
      ));
    return FALSE;
  }

  DEBUG ((
    DEBUG_INFO,
    "AmdFtpm: CRB at 0x%lx, cmd=0x%lx/%u, resp=0x%lx/%u\n",
    (UINT64)mCrbBase,
    (UINT64)mCmdAddr,
    mCmdSizeMax,
    (UINT64)mRespAddr,
    mRespSizeMax
    ));

  mPresent = TRUE;
  return TRUE;
}

/**
  Reprogram the four buffer-pointer / size registers from the cached
  values. Defends against Tcg2Dxe (and anything else following the
  TCG-standard CRB layout) writing wrong values into the ControlArea.
**/
STATIC
VOID
RestoreCrbBuffers (
  VOID
  )
{
  WriteAddrPair (mCrbBase + AMD_FTPM_CRB_CMD_ADDR_LO,  mCmdAddr);
  WriteAddrPair (mCrbBase + AMD_FTPM_CRB_RESP_ADDR_LO, mRespAddr);
  MmioWrite32 (mCrbBase + AMD_FTPM_CRB_CMD_SIZE,  mCmdSizeMax);
  MmioWrite32 (mCrbBase + AMD_FTPM_CRB_RESP_SIZE, mRespSizeMax);
}

/**
  Poll a 32-bit register until (Value & Mask) == Expected, or TimeoutMs
  elapses.
**/
STATIC
EFI_STATUS
WaitForBits (
  IN UINTN   Reg,
  IN UINT32  Mask,
  IN UINT32  Expected,
  IN UINTN   TimeoutMs
  )
{
  UINTN  Iter;
  UINTN  Iterations;

  Iterations = (TimeoutMs * 1000) / POLL_INTERVAL_US;
  for (Iter = 0; Iter <= Iterations; Iter++) {
    if ((MmioRead32 (Reg) & Mask) == Expected) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay (POLL_INTERVAL_US);
  }

  return EFI_TIMEOUT;
}

/**
  This service requests use TPM2.

  @retval EFI_SUCCESS      AMD fTPM is reachable and idle.
  @retval EFI_NOT_FOUND    Not on AMD, or fTPM not initialized by coreboot.
  @retval EFI_DEVICE_ERROR fTPM CRB stuck with the START bit set.
**/
EFI_STATUS
EFIAPI
Tpm2RequestUseTpm (
  VOID
  )
{
  EFI_STATUS  Status;

  if (!EnsureCrbBase ()) {
    return EFI_NOT_FOUND;
  }

  Status = WaitForBits (
             mCrbBase + AMD_FTPM_CRB_START,
             AMD_FTPM_CRB_START_GO,
             0,
             WAIT_PRESENCE_MS
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AmdFtpm: START bit stuck on entry\n"));
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

/**
  This service enables the sending of commands to the AMD fTPM.

  Mirrors src/drivers/amd/ftpm/tpm.c::crb_tpm_process_command.
**/
EFI_STATUS
EFIAPI
Tpm2SubmitCommand (
  IN     UINT32  InputParameterBlockSize,
  IN     UINT8   *InputParameterBlock,
  IN OUT UINT32  *OutputParameterBlockSize,
  IN     UINT8   *OutputParameterBlock
  )
{
  EFI_STATUS  Status;
  UINT32      RegStatus;
  UINT32      ReportedRespSize;
  UINT32      TpmRspSize;

  if (!EnsureCrbBase ()) {
    return EFI_NOT_FOUND;
  }

  if (InputParameterBlockSize > mCmdSizeMax) {
    DEBUG ((
      DEBUG_ERROR,
      "AmdFtpm: command size %u exceeds CRB capacity %u\n",
      InputParameterBlockSize,
      mCmdSizeMax
      ));
    return EFI_BUFFER_TOO_SMALL;
  }

  //
  // Reseat the CRB control area in case Tcg2Dxe (or any other code that
  // assumes a TCG-standard CRB layout) clobbered the buffer pointers
  // and/or sizes since we last ran.
  //
  RestoreCrbBuffers ();

  //
  // STEP 1: wait for any prior command to retire.
  //
  Status = WaitForBits (
             mCrbBase + AMD_FTPM_CRB_START,
             AMD_FTPM_CRB_START_GO,
             0,
             WAIT_PRESENCE_MS
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AmdFtpm: START bit didn't clear before command\n"));
    return EFI_DEVICE_ERROR;
  }

  //
  // STEP 2: stage the command bytes in the RAM buffer.
  //
  CopyMem (
    (VOID *)mCmdAddr,
    InputParameterBlock,
    InputParameterBlockSize
    );

  //
  // STEP 3: clear the response buffer.
  //
  ZeroMem ((VOID *)mRespAddr, mRespSizeMax);

  //
  // STEP 4: ring the doorbell.
  //
  MmioWrite8 (mCrbBase + AMD_FTPM_CRB_START, AMD_FTPM_CRB_START_GO);

  //
  // STEP 5: wait for the TPM to clear START.
  //
  Status = WaitForBits (
             mCrbBase + AMD_FTPM_CRB_START,
             AMD_FTPM_CRB_START_GO,
             0,
             WAIT_COMMAND_MS
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AmdFtpm: command timed out\n"));
    return EFI_DEVICE_ERROR;
  }

  //
  // STEP 6: error bit?
  //
  RegStatus = MmioRead32 (mCrbBase + AMD_FTPM_CRB_STATUS);
  if ((RegStatus & AMD_FTPM_CRB_STATUS_ERROR) != 0) {
    DEBUG ((DEBUG_ERROR, "AmdFtpm: STATUS error bit set (0x%x)\n", RegStatus));
    return EFI_DEVICE_ERROR;
  }

  //
  // STEP 7: pull the response. Trust the size encoded in the TPM2 response
  // header (bytes 2-5, big-endian) over the size the hardware reports back
  // through the register, since on some AMD parts the register is static.
  //
  ReportedRespSize = MmioRead32 (mCrbBase + AMD_FTPM_CRB_RESP_SIZE);
  if (ReportedRespSize < TPM2_RESPONSE_HEADER_SIZE) {
    DEBUG ((
      DEBUG_ERROR,
      "AmdFtpm: response too short (reg %u)\n",
      ReportedRespSize
      ));
    return EFI_DEVICE_ERROR;
  }

  CopyMem (
    &TpmRspSize,
    (UINT8 *)mRespAddr + TPM2_RESPONSE_SIZE_OFFSET,
    sizeof (UINT32)
    );
  TpmRspSize = SwapBytes32 (TpmRspSize);

  if ((TpmRspSize < TPM2_RESPONSE_HEADER_SIZE) ||
      (TpmRspSize > mRespSizeMax))
  {
    DEBUG ((
      DEBUG_ERROR,
      "AmdFtpm: bogus response size in header (%u, buf %u)\n",
      TpmRspSize,
      mRespSizeMax
      ));
    return EFI_DEVICE_ERROR;
  }

  if (*OutputParameterBlockSize < TpmRspSize) {
    *OutputParameterBlockSize = TpmRspSize;
    return EFI_BUFFER_TOO_SMALL;
  }

  CopyMem (OutputParameterBlock, (VOID *)mRespAddr, TpmRspSize);
  *OutputParameterBlockSize = TpmRspSize;

  return EFI_SUCCESS;
}

/**
  Required by Tpm2DeviceLib but unused for direct (non-router) instances.
**/
EFI_STATUS
EFIAPI
Tpm2RegisterTpm2DeviceLib (
  IN TPM2_DEVICE_INTERFACE  *Tpm2Device
  )
{
  return EFI_UNSUPPORTED;
}
