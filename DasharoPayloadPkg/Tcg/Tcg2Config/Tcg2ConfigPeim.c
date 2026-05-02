/** @file
  Set TPM device type

  In SecurityPkg, this module initializes the TPM device type based on a UEFI
  variable and/or hardware detection. In OvmfPkg, the module only performs TPM2
  hardware detection.

  Copyright (c) 2015, Intel Corporation. All rights reserved.<BR>
  Copyright (C) 2018, Red Hat, Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiPei.h>

#include <Guid/TpmInstance.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/PeiServicesLib.h>
#include <Library/Tpm12CommandLib.h>
#include <Library/Tpm12DeviceLib.h>
#include <Library/Tpm2DeviceLib.h>
#include <Ppi/TpmInitialized.h>

//
// AMD PSP MMIO base lives in this MSR; coreboot programs it on every CPU
// via SOC_AMD_COMMON_BLOCK_CPU_SYNC_PSP_ADDR_MSR. The fTPM CRB register
// block sits at psp_base + PSPV2_MBOX_CMD_OFFSET - 0xA0 (see
// src/soc/amd/common/block/psp/ftpm.c). The mailbox-command offset is
// platform-specific (0x10570 on Picasso/Cezanne/Vermeer, 0x10970 on
// Glinda/Turin) and comes in via PcdAmdPspv2MboxCmdOffset.
//
#define AMD_PSP_ADDR_MSR  0xC00110A2

STATIC CONST EFI_PEI_PPI_DESCRIPTOR mTpmSelectedPpi = {
  (EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST),
  &gEfiTpmDeviceSelectedGuid,
  NULL
};

STATIC CONST EFI_PEI_PPI_DESCRIPTOR  mTpmInitializationDonePpiList = {
  EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST,
  &gPeiTpmInitializationDonePpiGuid,
  NULL
};

static
EFI_STATUS
TestTpm12 (
  )
{
  TPM_STCLEAR_FLAGS  VolatileFlags;

  return Tpm12GetCapabilityFlagVolatile (&VolatileFlags);
}

/**
  Return TRUE when running on a Zen-family AMD CPU (vendor "AuthenticAMD").
  PSP_ADDR_MSR is AMD-specific and would #GP on Intel.
**/
STATIC
BOOLEAN
IsAmdCpu (
  VOID
  )
{
  UINT32  RegEbx;
  UINT32  RegEcx;
  UINT32  RegEdx;

  AsmCpuid (0, NULL, &RegEbx, &RegEcx, &RegEdx);
  return (BOOLEAN)(
           (RegEbx == SIGNATURE_32 ('A', 'u', 't', 'h')) &&
           (RegEdx == SIGNATURE_32 ('e', 'n', 't', 'i')) &&
           (RegEcx == SIGNATURE_32 ('c', 'A', 'M', 'D'))
         );
}

/**
  Detect AMD fTPM by reading PSP_ADDR_MSR for the PSP MMIO base, then
  computing the CRB register block address from a fixed offset. When
  successful, override the dTPM library's PCDs so subsequent code targets
  the runtime CRB rather than the build-time default of 0xFED40000.

  Returns TRUE only when the platform is an AMD CPU AND the MSR holds a
  plausible base. The caller treats TRUE as "TPM2 detected" without
  invoking Tpm2RequestUseTpm(): the AMD fTPM exposes a reduced CRB
  register layout that EDK2's stock PtpCrbRequestUseTpm would hang on.
**/
STATIC
BOOLEAN
TryDetectAmdFtpmFromMsr (
  VOID
  )
{
  UINT64      MsrValue;
  UINT64      PspBase;
  UINT64      CrbBase;
  EFI_STATUS  Status;

  if (!IsAmdCpu ()) {
    DEBUG ((DEBUG_INFO, "%a: not an AMD CPU\n", __FUNCTION__));
    return FALSE;
  }

  MsrValue = AsmReadMsr64 (AMD_PSP_ADDR_MSR);
  PspBase  = MsrValue & 0xFFFFFFFFULL;
  if ((PspBase == 0) || (PspBase == 0xFFFFFFFFULL)) {
    DEBUG ((DEBUG_INFO, "%a: PSP_ADDR_MSR not programmed (0x%lx)\n", __FUNCTION__, MsrValue));
    return FALSE;
  }

  CrbBase = PspBase + FixedPcdGet32 (PcdAmdPspv2MboxCmdOffset) - 0xA0;

  Status = PcdSet64S (PcdTpmBaseAddress, CrbBase);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: PcdSet64S(PcdTpmBaseAddress) failed: %r\n", __FUNCTION__, Status));
    return FALSE;
  }

  // The dTPM lib constructor already ran with the stale 0xFED40000 base
  // and cached PcdActiveTpmInterfaceType (likely Tpm2PtpInterfaceMax).
  // GetCachedPtpInterface() re-reads the PCD on each call, so overwriting
  // it here is sufficient.
  PcdSet8S (PcdActiveTpmInterfaceType, Tpm2PtpInterfaceCrb);

  DEBUG ((
    DEBUG_INFO,
    "%a: AMD fTPM CRB at 0x%lx (PSP base 0x%lx)\n",
    __FUNCTION__,
    CrbBase,
    PspBase
    ));
  return TRUE;
}

/**
  The entry point for Tcg2 configuration driver.

  @param  FileHandle  Handle of the file being invoked.
  @param  PeiServices Describes the list of possible PEI Services.
**/
EFI_STATUS
EFIAPI
Tcg2ConfigPeimEntryPoint (
  IN       EFI_PEI_FILE_HANDLE  FileHandle,
  IN CONST EFI_PEI_SERVICES     **PeiServices
  )
{
  UINTN                           Size;
  EFI_STATUS                      Status;

  if (TryDetectAmdFtpmFromMsr ()) {
    DEBUG ((DEBUG_INFO, "%a: TPM2 detected via AMD PSP MSR\n", __FUNCTION__));
    Size = sizeof (gEfiTpmDeviceInstanceTpm20DtpmGuid);
    Status = PcdSetPtrS (
               PcdTpmInstanceGuid,
               &Size,
               &gEfiTpmDeviceInstanceTpm20DtpmGuid
               );
    ASSERT_EFI_ERROR (Status);
    goto Done;
  }

  Status = Tpm12RequestUseTpm ();
  if (!EFI_ERROR (Status) && !EFI_ERROR (TestTpm12 ())) {
    DEBUG ((DEBUG_INFO, "%a: TPM1.2 detected\n", __FUNCTION__));
    Size = sizeof (gEfiTpmDeviceInstanceTpm12Guid);
    Status = PcdSetPtrS (
               PcdTpmInstanceGuid,
               &Size,
               &gEfiTpmDeviceInstanceTpm12Guid
               );
    ASSERT_EFI_ERROR (Status);
  } else {
    Status = Tpm2RequestUseTpm ();
    if (!EFI_ERROR (Status)) {
      DEBUG ((DEBUG_INFO, "%a: TPM2 detected\n", __FUNCTION__));
      Size = sizeof (gEfiTpmDeviceInstanceTpm20DtpmGuid);
      Status = PcdSetPtrS (
                 PcdTpmInstanceGuid,
                 &Size,
                 &gEfiTpmDeviceInstanceTpm20DtpmGuid
                 );
      ASSERT_EFI_ERROR (Status);
    } else {
      DEBUG ((DEBUG_INFO, "%a: no TPM detected\n", __FUNCTION__));
      //
      // If no TPM2 was detected, we still need to install
      // TpmInitializationDonePpi. Namely, Tcg2Pei will exit early upon seeing
      // the default (all-bits-zero) contents of PcdTpmInstanceGuid, thus we have
      // to install the PPI in its place, in order to unblock any dependent
      // PEIMs.
      //
      Status = PeiServicesInstallPpi (&mTpmInitializationDonePpiList);
      ASSERT_EFI_ERROR (Status);
    }
  }

Done:
  //
  // Selection done
  //
  Status = PeiServicesInstallPpi (&mTpmSelectedPpi);
  ASSERT_EFI_ERROR (Status);

  return Status;
}
