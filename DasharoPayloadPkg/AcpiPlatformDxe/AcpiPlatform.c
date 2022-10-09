/** @file
  Sample ACPI Platform Driver

  Copyright (c) 2008 - 2018, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiDxe.h>

#include <Guid/SystemTableInfoGuid.h>

#include <Protocol/AcpiTable.h>
#include <Protocol/AcpiSystemDescriptionTable.h>
#include <Protocol/PciIo.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiLib.h>

#include <IndustryStandard/Acpi.h>

EFI_ACPI_TABLE_PROTOCOL       *mAcpiProtocol;
EFI_ACPI_SDT_PROTOCOL         *mSdtProtocol;
EFI_EVENT                     mEfiExitBootServicesEvent;

EFI_STATUS
EFIAPI
InstallTablesFromXsdt (
  IN  EFI_ACPI_DESCRIPTION_HEADER   *Xsdt,
  IN  UINTN                         *TableHandle
  )
{
  EFI_STATUS                                      Status;
  EFI_ACPI_6_3_FIXED_ACPI_DESCRIPTION_TABLE       *FadtTable;
  EFI_ACPI_6_3_FIRMWARE_ACPI_CONTROL_STRUCTURE    *FacsTable;
  EFI_ACPI_DESCRIPTION_HEADER                     *DsdtTable;
  VOID                                            *CurrentTableEntry;
  UINTN                                           CurrentTablePointer;
  EFI_ACPI_DESCRIPTION_HEADER                     *CurrentTable;
  UINTN                                           Index;
  UINTN                                           NumberOfTableEntries;

  Status = EFI_SUCCESS;
  //
  // Retrieve the addresses of XSDT and
  // calculate the number of its table entries.
  //
  NumberOfTableEntries = (Xsdt->Length -
                           sizeof (EFI_ACPI_DESCRIPTION_HEADER)) /
                           sizeof (UINT64);
  //
  // Install ACPI tables found in XSDT.
  //
  for (Index = 0; Index < NumberOfTableEntries; Index++) {
    //
    // Get the table entry from XSDT
    //
    CurrentTableEntry = (VOID *) ((UINT8 *) Xsdt +
                          sizeof (EFI_ACPI_DESCRIPTION_HEADER) +
                          Index * sizeof (UINT64));
    CurrentTablePointer = (UINTN) *(UINT64 *) CurrentTableEntry;
    CurrentTable = (EFI_ACPI_DESCRIPTION_HEADER *) CurrentTablePointer;

    //
    // Get the FACS and DSDT table address from the table FADT
    //
    if (!AsciiStrnCmp ((CHAR8 *) &CurrentTable->Signature, "FACP", 4)) {
      FadtTable = (EFI_ACPI_6_3_FIXED_ACPI_DESCRIPTION_TABLE *)
                    (UINTN) CurrentTablePointer;
      DsdtTable  = (EFI_ACPI_DESCRIPTION_HEADER *) (UINTN) FadtTable->XDsdt;
      FacsTable = (EFI_ACPI_6_3_FIRMWARE_ACPI_CONTROL_STRUCTURE *) (UINTN) FadtTable->XFirmwareCtrl;

      if (!AsciiStrnCmp ((CHAR8 *) &DsdtTable->Signature, "DSDT", 4)) {
        //
        // Install DSDT table.
        //
        Status = mAcpiProtocol->InstallAcpiTable (
                     mAcpiProtocol,
                     DsdtTable,
                     DsdtTable->Length,
                     TableHandle
                     );
        ASSERT_EFI_ERROR (Status);
      } else {
        DEBUG((DEBUG_ERROR, "DSDT not found\n"));
        ASSERT_EFI_ERROR (Status);
      }

      if (!AsciiStrnCmp ((CHAR8 *) &FacsTable->Signature, "FACS", 4)) {
        //
        // Install the FACS tables
        //
        Status = mAcpiProtocol->InstallAcpiTable (
                   mAcpiProtocol,
                   FacsTable,
                   FacsTable->Length,
                   TableHandle
                   );
        ASSERT_EFI_ERROR (Status);
      } else {
        DEBUG((DEBUG_ERROR, "FACS not found\n"));
        ASSERT_EFI_ERROR (Status);
      }
    }

    //
    // Install the XSDT tables
    //
    Status = mAcpiProtocol->InstallAcpiTable (
               mAcpiProtocol,
               CurrentTable,
               CurrentTable->Length,
               TableHandle
               );

    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  return Status;
}


EFI_STATUS
EFIAPI
InstallTablesFromRsdt (
  IN  EFI_ACPI_DESCRIPTION_HEADER   *Rsdt,
  IN  UINTN                         *TableHandle
  )
{
  EFI_STATUS                                      Status;
  EFI_ACPI_6_3_FIXED_ACPI_DESCRIPTION_TABLE       *FadtTable;
  EFI_ACPI_6_3_FIRMWARE_ACPI_CONTROL_STRUCTURE    *FacsTable;
  EFI_ACPI_DESCRIPTION_HEADER                     *DsdtTable;
  VOID                                            *CurrentTableEntry;
  UINTN                                           CurrentTablePointer;
  EFI_ACPI_DESCRIPTION_HEADER                     *CurrentTable;
  UINTN                                           Index;
  UINTN                                           NumberOfTableEntries;

  Status = EFI_SUCCESS;
  //
  // Retrieve the addresses of RSDT and
  // calculate the number of its table entries.
  //
  NumberOfTableEntries = (Rsdt->Length -
                           sizeof (EFI_ACPI_DESCRIPTION_HEADER)) /
                           sizeof (UINT32);
  //
  // Install ACPI tables found in RSDT.
  //
  for (Index = 0; Index < NumberOfTableEntries; Index++) {
    //
    // Get the table entry from RSDT
    //
    CurrentTableEntry = (VOID *) ((UINT8 *) Rsdt +
                          sizeof (EFI_ACPI_DESCRIPTION_HEADER) +
                          Index * sizeof (UINT32));
    CurrentTablePointer = (UINTN) *(UINT32 *) CurrentTableEntry;
    CurrentTable = (EFI_ACPI_DESCRIPTION_HEADER *) CurrentTablePointer;

    //
    // Get the FACS and DSDT table address from the table FADT
    //
    if (!AsciiStrnCmp ((CHAR8 *) &CurrentTable->Signature, "FACP", 4)) {
      FadtTable = (EFI_ACPI_6_3_FIXED_ACPI_DESCRIPTION_TABLE *)
                    (UINTN) CurrentTablePointer;
      DsdtTable  = (EFI_ACPI_DESCRIPTION_HEADER *) (UINTN) FadtTable->Dsdt;
      FacsTable = (EFI_ACPI_6_3_FIRMWARE_ACPI_CONTROL_STRUCTURE *) (UINTN) FadtTable->FirmwareCtrl;

      if (!AsciiStrnCmp ((CHAR8 *) &DsdtTable->Signature, "DSDT", 4)) {
        //
        // Install DSDT table.
        //
        Status = mAcpiProtocol->InstallAcpiTable (
                     mAcpiProtocol,
                     DsdtTable,
                     DsdtTable->Length,
                     TableHandle
                     );
        ASSERT_EFI_ERROR (Status);
      } else {
        DEBUG((DEBUG_ERROR, "DSDT not found\n"));
        ASSERT_EFI_ERROR (Status);
      }

      if (!AsciiStrnCmp ((CHAR8 *) &FacsTable->Signature, "FACS", 4)) {
        //
        // Install the FACS tables
        //
        Status = mAcpiProtocol->InstallAcpiTable (
                   mAcpiProtocol,
                   FacsTable,
                   FacsTable->Length,
                   TableHandle
                   );
        ASSERT_EFI_ERROR (Status);
      } else {
        DEBUG((DEBUG_ERROR, "FACS not found\n"));
        ASSERT_EFI_ERROR (Status);
      }
    }
    //
    // Install the RSDT tables
    //
    Status = mAcpiProtocol->InstallAcpiTable (
               mAcpiProtocol,
               CurrentTable,
               CurrentTable->Length,
               TableHandle
               );

    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  return Status;
}

/**
  This function uses the ACPI SDT protocol to locate an ACPI table.
  It is really only useful for finding tables that only have a single instance,
  e.g. FADT, FACS, MADT, etc.  It is not good for locating SSDT, etc.
  Matches are determined by finding the table with ACPI table that has
  a matching signature.

  @param[in] Signature           - Pointer to an ASCII string containing the OEM Table ID from the ACPI table header
  @param[in, out] Table          - Updated with a pointer to the table
  @param[in, out] Handle         - AcpiSupport protocol table handle for the table found
  @param[in, out] Version        - The version of the table desired

  @retval EFI_SUCCESS            - The function completed successfully.
  @retval EFI_NOT_FOUND          - Failed to locate AcpiTable.
  @retval EFI_NOT_READY          - Not ready to locate AcpiTable.
**/
EFI_STATUS
EFIAPI
LocateAcpiTableBySignature (
  IN      UINT32                        Signature,
  IN OUT  EFI_ACPI_DESCRIPTION_HEADER   **Table,
  IN OUT  UINTN                         *Handle
  )
{
  EFI_STATUS                  Status;
  INTN                        Index;
  EFI_ACPI_TABLE_VERSION      Version;
  EFI_ACPI_DESCRIPTION_HEADER *OrgTable;

  ///
  /// Locate table with matching ID
  ///
  Version = 0;
  Index = 0;
  do {
    Status = mSdtProtocol->GetAcpiTable (Index, (EFI_ACPI_SDT_HEADER **)&OrgTable, &Version, Handle);
    if (Status == EFI_NOT_FOUND) {
      break;
    }
    ASSERT_EFI_ERROR (Status);
    Index++;
  } while (OrgTable->Signature != Signature);

  if (Status != EFI_NOT_FOUND) {
    *Table = AllocateCopyPool (OrgTable->Length, OrgTable);
    ASSERT (*Table);
  }

  ///
  /// If we found the table, there will be no error.
  ///
  return Status;
}


/**
  This function calculates RCR based on PCI Device ID and Vendor ID from the devices
  available on the platform.
  It also includes other instances of BIOS change to calculate CRC and provides as
  HWSignature filed in FADT table.
**/
VOID
IsHardwareChange (
  VOID
  )
{
  EFI_STATUS                    Status;
  UINTN                         Index;
  UINTN                         HandleCount;
  EFI_HANDLE                    *HandleBuffer;
  EFI_PCI_IO_PROTOCOL           *PciIo;
  UINT32                        CRC;
  UINT32                        *HWChange;
  UINTN                         HWChangeSize;
  UINT32                        PciId;
  UINTN                         Handle;
  EFI_ACPI_6_3_FIRMWARE_ACPI_CONTROL_STRUCTURE *FacsPtr;
  EFI_ACPI_6_3_FIXED_ACPI_DESCRIPTION_TABLE    *pFADT;

  HandleCount  = 0;
  HandleBuffer = NULL;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPciIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &HandleBuffer
                  );
  if (EFI_ERROR (Status)) {
    return; // PciIO protocol not installed yet!
  }

  //
  // Allocate memory for HWChange and add additional entrie for
  // pFADT->XDsdt
  //
  HWChangeSize = HandleCount + 1;
  HWChange = AllocateZeroPool (sizeof(UINT32) * HWChangeSize);
  ASSERT(HWChange != NULL);

  if (HWChange == NULL) return;

  //
  // add HWChange inputs: PCI devices
  //
  for (Index = 0; HandleCount > 0; HandleCount--) {
    PciId = 0;
    Status = gBS->HandleProtocol (HandleBuffer[Index], &gEfiPciIoProtocolGuid, (VOID **) &PciIo);
    if (!EFI_ERROR (Status)) {
      Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, 0, 1, &PciId);
      if (EFI_ERROR (Status)) {
        continue;
      }
      HWChange[Index++] = PciId;
    }
  }

  //
  // Locate FACP Table
  //
  Handle = 0;
  Status = LocateAcpiTableBySignature (
              EFI_ACPI_6_3_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE,
              (EFI_ACPI_DESCRIPTION_HEADER **) &pFADT,
              &Handle
              );
  if (EFI_ERROR (Status) || (pFADT == NULL)) {
    return;  //Table not found or out of memory resource for pFADT table
  }

  //
  // add HWChange inputs: others
  //
  HWChange[Index++] = (UINT32)pFADT->XDsdt;

  //
  // Calculate CRC value with HWChange data.
  //
  Status = gBS->CalculateCrc32(HWChange, HWChangeSize, &CRC);
  DEBUG ((DEBUG_INFO, "CRC = %x and Status = %r\n", CRC, Status));

  //
  // Set HardwareSignature value based on CRC value.
  //
  FacsPtr = (EFI_ACPI_6_3_FIRMWARE_ACPI_CONTROL_STRUCTURE *)(UINTN)pFADT->FirmwareCtrl;
  FacsPtr->HardwareSignature = CRC;
  FreePool (HWChange);
}

VOID
EFIAPI
AcpiEndOfDxeEvent (
  EFI_EVENT           Event,
  VOID                *ParentImageHandle
  )
{
  if (Event != NULL) {
    gBS->CloseEvent (Event);
  }

  //
  // Calculate Hardware Signature value based on current platform configurations
  //
  IsHardwareChange ();
}

/** On exiting boot services we must make sure the new RSDP is in the legacy
    segment where coreboot expects it.
**/
STATIC
VOID
EFIAPI
AcpiExitBootServicesEventNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINTN                                           Ptr;
  EFI_STATUS                                      Status;
  EFI_ACPI_6_3_ROOT_SYSTEM_DESCRIPTION_POINTER    *cbRsdp;
  EFI_ACPI_6_3_ROOT_SYSTEM_DESCRIPTION_POINTER    *Rsdp; 

  cbRsdp = NULL;
  Rsdp = NULL;

  /* Find coreboot RSDP. */
  for (Ptr = 0xe0000; Ptr < 0xfffff; Ptr += 16) {
    if (!AsciiStrnCmp ((CHAR8 *)Ptr, "RSD PTR ", 8)) {
      cbRsdp = (EFI_ACPI_6_3_ROOT_SYSTEM_DESCRIPTION_POINTER *)Ptr;
      break;
    }
  }

  if (cbRsdp == NULL) {
    DEBUG ((EFI_D_ERROR, "No coreboot RSDP found, wake up from S3 not possible.\n"));
    return;
  }

  Status = EfiGetSystemConfigurationTable (&gEfiAcpiTableGuid, (VOID **) &Rsdp);
  if (EFI_ERROR (Status) || (Rsdp == NULL)) {
    DEBUG ((EFI_D_ERROR, "No RSDP found, wake up from S3 not possible.\n"));
    return;
  }

  CopyMem((VOID *)cbRsdp, (CONST VOID *)Rsdp, sizeof(*Rsdp));
  DEBUG ((EFI_D_INFO, "coreboot RSDP updated\n"));
}

/**
  Entrypoint of Acpi Platform driver.

  @param  ImageHandle
  @param  SystemTable

  @return EFI_SUCCESS
  @return EFI_LOAD_ERROR
  @return EFI_OUT_OF_RESOURCES

**/
EFI_STATUS
EFIAPI
AcpiPlatformEntryPoint (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
  )
{
  EFI_STATUS                                      Status;
  EFI_HOB_GUID_TYPE                               *GuidHob;
  EFI_ACPI_6_3_ROOT_SYSTEM_DESCRIPTION_POINTER    *Rsdp;
  SYSTEM_TABLE_INFO                               *SystemTableInfo;
  UINTN                                           TableHandle;
  EFI_EVENT                                       EndOfDxeEvent;

  TableHandle  = 0;

  //
  // Find the AcpiTable protocol
  //
  Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL, (VOID**)&mAcpiProtocol);
  ASSERT_EFI_ERROR (Status);

  Status = gBS->LocateProtocol (&gEfiAcpiSdtProtocolGuid, NULL, (VOID **)&mSdtProtocol);
  ASSERT_EFI_ERROR (Status);

  //
  // Find the system table information guid hob
  //
  GuidHob = GetFirstGuidHob (&gUefiSystemTableInfoGuid);
  ASSERT (GuidHob != NULL);
  SystemTableInfo = (SYSTEM_TABLE_INFO *)GET_GUID_HOB_DATA (GuidHob);

  //
  // Set pointers to ACPI tables
  //
  if (SystemTableInfo->AcpiTableBase != 0 && SystemTableInfo->AcpiTableSize != 0) {
    ASSERT_EFI_ERROR (Status);
  }

  Rsdp = (EFI_ACPI_6_3_ROOT_SYSTEM_DESCRIPTION_POINTER *) SystemTableInfo->AcpiTableBase;
  //
  // If XSDT table is found, just install its tables.
  //
  if (Rsdp->XsdtAddress) {
    Status = InstallTablesFromXsdt ((EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Rsdp->XsdtAddress,
                                    &TableHandle);
    if (EFI_ERROR (Status)) {
      DEBUG((DEBUG_ERROR, "Failed to install ACPI tables from XSDT\n"));
      return Status;
    }
  } else {
    DEBUG((DEBUG_ERROR, "XSDT not found, trying RSDT\n"));
    if (Rsdp->RsdtAddress) {
      Status = InstallTablesFromRsdt ((EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Rsdp->RsdtAddress,
                                      &TableHandle);
      if (EFI_ERROR (Status)) {
        DEBUG((DEBUG_ERROR, "Failed to install ACPI tables from RSDT\n"));
        return Status;
      }
    } else {
      DEBUG((DEBUG_ERROR, "RSDT not found. Failed to install ACPI tables\n"));
      ASSERT_EFI_ERROR (Status);
    }
  }

  //
  // Create an End of DXE event.
  //
  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  AcpiEndOfDxeEvent,
                  NULL,
                  &gEfiEndOfDxeEventGroupGuid,
                  &EndOfDxeEvent
                  );
  ASSERT_EFI_ERROR (Status);

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  AcpiExitBootServicesEventNotify,
                  NULL,
                  &gEfiEventExitBootServicesGuid,
                  &mEfiExitBootServicesEvent
                  );
  ASSERT_EFI_ERROR (Status);

  return Status;
}

