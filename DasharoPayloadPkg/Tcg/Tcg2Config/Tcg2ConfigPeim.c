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
#include <Guid/SystemTableInfoGuid.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/PcdLib.h>
#include <Library/PeiServicesLib.h>
#include <Library/Tpm2DeviceLib.h>
#include <Library/Tpm12DeviceLib.h>
#include <Library/Tpm12CommandLib.h>
#include <Ppi/TpmInitialized.h>
#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/Tpm2Acpi.h>

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

EFIAPI
EFI_TPM2_ACPI_TABLE *
FindTpm2TableinXsdt (
  IN  EFI_ACPI_DESCRIPTION_HEADER   *Xsdt
  )
{
  EFI_TPM2_ACPI_TABLE                             *Tpm2Table;
  VOID                                            *CurrentTableEntry;
  UINTN                                           CurrentTablePointer;
  EFI_ACPI_DESCRIPTION_HEADER                     *CurrentTable;
  UINTN                                           Index;
  UINTN                                           NumberOfTableEntries;

  NumberOfTableEntries = (Xsdt->Length -
                           sizeof (EFI_ACPI_DESCRIPTION_HEADER)) /
                           sizeof (UINT64);
  for (Index = 0; Index < NumberOfTableEntries; Index++) {
    CurrentTableEntry = (VOID *) ((UINT8 *) Xsdt +
                          sizeof (EFI_ACPI_DESCRIPTION_HEADER) +
                          Index * sizeof (UINT64));
    CurrentTablePointer = (UINTN) *(UINT64 *) CurrentTableEntry;
    CurrentTable = (EFI_ACPI_DESCRIPTION_HEADER *) CurrentTablePointer;

    if (!AsciiStrnCmp ((CHAR8 *) &CurrentTable->Signature, "TPM2", 4)) {
      Tpm2Table = (EFI_TPM2_ACPI_TABLE *)
                    (UINTN) CurrentTablePointer;

      /* Detect TPMs that are not on standard address */
      if ((Tpm2Table->AddressOfControlArea & 0xffff0000) != 0xFED40000)
        return Tpm2Table;
      else
        return NULL;
    }
  }

  return NULL;
}

EFIAPI
EFI_TPM2_ACPI_TABLE *
FindTpm2TableinRsdt (
  IN  EFI_ACPI_DESCRIPTION_HEADER   *Rsdt
  )
{
  EFI_TPM2_ACPI_TABLE                             *Tpm2Table;
  VOID                                            *CurrentTableEntry;
  UINTN                                           CurrentTablePointer;
  EFI_ACPI_DESCRIPTION_HEADER                     *CurrentTable;
  UINTN                                           Index;
  UINTN                                           NumberOfTableEntries;

  NumberOfTableEntries = (Rsdt->Length -
                           sizeof (EFI_ACPI_DESCRIPTION_HEADER)) /
                           sizeof (UINT32);

  for (Index = 0; Index < NumberOfTableEntries; Index++) {
    CurrentTableEntry = (VOID *) ((UINT8 *) Rsdt +
                          sizeof (EFI_ACPI_DESCRIPTION_HEADER) +
                          Index * sizeof (UINT32));
    CurrentTablePointer = (UINTN) *(UINT32 *) CurrentTableEntry;
    CurrentTable = (EFI_ACPI_DESCRIPTION_HEADER *) CurrentTablePointer;

    if (!AsciiStrnCmp ((CHAR8 *) &CurrentTable->Signature, "TPM2", 4)) {
      Tpm2Table = (EFI_TPM2_ACPI_TABLE *)(UINTN) CurrentTablePointer;

      /* Detect TPMs that are not on standard address */
      if ((Tpm2Table->AddressOfControlArea != 0) && 
          ((Tpm2Table->AddressOfControlArea & 0xffff0000) != 0xFED40000)) {
        return Tpm2Table;
      } else {
        return NULL;
      }
    }
  }

  return NULL;
}

BOOLEAN
CheckAmdFTpmPresence (
  VOID
  )
{
  EFI_HOB_GUID_TYPE                               *GuidHob;
  EFI_ACPI_6_3_ROOT_SYSTEM_DESCRIPTION_POINTER    *Rsdp;
  SYSTEM_TABLE_INFO                               *SystemTableInfo;
  EFI_TPM2_ACPI_TABLE                             *Tpm2Table;

  if (GetHobList () == NULL) {
    return FALSE;
  }

  GuidHob = GetFirstGuidHob (&gUefiSystemTableInfoGuid);
  ASSERT (GuidHob != NULL);
  SystemTableInfo = (SYSTEM_TABLE_INFO *)GET_GUID_HOB_DATA (GuidHob);

  if (SystemTableInfo->AcpiTableBase == 0 || SystemTableInfo->AcpiTableSize == 0) {
    return FALSE;
  }

  Tpm2Table = NULL;
  Rsdp = (EFI_ACPI_6_3_ROOT_SYSTEM_DESCRIPTION_POINTER *)(UINTN)SystemTableInfo->AcpiTableBase;
  if (Rsdp->XsdtAddress) {
    Tpm2Table = FindTpm2TableinXsdt((EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Rsdp->XsdtAddress);
  } else if (Rsdp->RsdtAddress) {
    Tpm2Table = FindTpm2TableinRsdt((EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Rsdp->RsdtAddress);
  }

  if (Tpm2Table == NULL) {
    return FALSE;
  }

  if (Tpm2Table->AddressOfControlArea == 0) {
    return FALSE;
  }

  if (Tpm2Table->StartMethod != EFI_TPM2_ACPI_TABLE_START_METHOD_ACPI) {
    return FALSE;
  }

  PcdSet64S(PcdTpmBaseAddress, Tpm2Table->AddressOfControlArea - 0x40);

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

  if (CheckAmdFTpmPresence()) {
    PcdSet8S(PcdActiveTpmInterfaceType, Tpm2PtpInterfaceAmdCrb);
    PcdSet8S(PcdCRBIdleByPass, 1);

    Status = Tpm2RequestUseTpm ();
    if (!EFI_ERROR (Status)) {
      DEBUG ((DEBUG_INFO, "%a: AMD fTPM 2.0 detected\n", __FUNCTION__));
      Size = sizeof (gEfiTpmDeviceInstanceTpm20DtpmGuid);
      Status = PcdSetPtrS (
                  PcdTpmInstanceGuid,
                  &Size,
                  &gEfiTpmDeviceInstanceTpm20DtpmGuid
                  );
      ASSERT_EFI_ERROR (Status);
    } else {
      DEBUG ((DEBUG_INFO, "%a: AMD fTPM 2.0 not detected\n", __FUNCTION__));
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
  } else {
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
  }

  //
  // Selection done
  //
  Status = PeiServicesInstallPpi (&mTpmSelectedPpi);
  ASSERT_EFI_ERROR (Status);

  return Status;
}
