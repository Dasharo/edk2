/** @file
  This library will parse the Slim Bootloader to get required information.

  Copyright (c) 2014 - 2019, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiDxe.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/IoLib.h>
#include <Library/HobLib.h>
#include <Library/BlParseLib.h>
#include <IndustryStandard/Acpi.h>


/**
  This function retrieves the parameter base address from boot loader.

  This function will get bootloader specific parameter address for UEFI payload.
  e.g. HobList pointer for Slim Bootloader, and coreboot table header for Coreboot.

  @retval NULL            Failed to find the GUID HOB.
  @retval others          GUIDed HOB data pointer.

**/
VOID *
EFIAPI
GetParameterBase (
  VOID
  )
{
  EFI_HOB_HANDOFF_INFO_TABLE          *HandoffTable;

  HandoffTable = (EFI_HOB_HANDOFF_INFO_TABLE *)(UINTN) GET_BOOTLOADER_PARAMETER ();
  if ((HandoffTable->Header.HobType == EFI_HOB_TYPE_HANDOFF) &&
    (HandoffTable->Header.HobLength == sizeof (EFI_HOB_HANDOFF_INFO_TABLE)) &&
    (HandoffTable->Header.Reserved == 0)) {
    return (VOID *)HandoffTable;
  }

  return NULL;
}


/**
  This function retrieves a GUIDed HOB data from Slim Bootloader.

  This function will search SBL HOB list to find the first GUIDed HOB that
  its GUID matches Guid.

  @param[in]  Guid        A pointer to HOB GUID to search.

  @retval NULL            Failed to find the GUID HOB.
  @retval others          GUIDed HOB data pointer.

**/
VOID *
GetGuidHobDataFromSbl (
  IN       EFI_GUID      *Guid
  )
{
  UINT8                  *GuidHob;
  CONST VOID             *HobList;

  HobList = GetParameterBase ();
  ASSERT (HobList != NULL);
  GuidHob = GetNextGuidHob (Guid, HobList);
  if (GuidHob != NULL) {
    return GET_GUID_HOB_DATA (GuidHob);
  }

  return NULL;
}

/**
  Acquire the memory map information.

  @param  MemInfoCallback     The callback routine
  @param  Params              Pointer to the callback routine parameter

  @retval RETURN_SUCCESS     Successfully find out the memory information.
  @retval RETURN_NOT_FOUND   Failed to find the memory information.

**/
RETURN_STATUS
EFIAPI
ParseMemoryInfo (
  IN  BL_MEM_INFO_CALLBACK       MemInfoCallback,
  IN  VOID                       *Params
  )
{
  MEMROY_MAP_INFO               *MemoryMapInfo;
  MEMROY_MAP_ENTRY               MemoryMap;
  UINTN                          Idx;

  MemoryMapInfo = (MEMROY_MAP_INFO *) GetGuidHobDataFromSbl (&gLoaderMemoryMapInfoGuid);
  if (MemoryMapInfo == NULL) {
    ASSERT (FALSE);
    return RETURN_NOT_FOUND;
  }

  for (Idx = 0; Idx < MemoryMapInfo->Count; Idx++) {
    MemoryMap.Base = MemoryMapInfo->Entry[Idx].Base;
    MemoryMap.Size = MemoryMapInfo->Entry[Idx].Size;

    switch (MemoryMapInfo->Entry[Idx].Type) {
      case 1:
        MemoryMap.Type = EFI_RESOURCE_SYSTEM_MEMORY;
        MemoryMap.Flag = EFI_RESOURCE_ATTRIBUTE_PRESENT;
        break;
      case 2:
        MemoryMap.Type = EFI_RESOURCE_MEMORY_RESERVED;
        MemoryMap.Flag = EFI_RESOURCE_ATTRIBUTE_PRESENT;
        break;
      default:
        continue;
    }

    MemInfoCallback (&MemoryMap, Params);
  }

  return RETURN_SUCCESS;
}

/**
  Acquire acpi table and smbios table from slim bootloader

  @param  SystemTableInfo           Pointer to the system table info

  @retval RETURN_SUCCESS            Successfully find out the tables.
  @retval RETURN_NOT_FOUND          Failed to find the tables.

**/
RETURN_STATUS
EFIAPI
ParseSystemTable (
  OUT SYSTEM_TABLE_INFO     *SystemTableInfo
  )
{
  SYSTEM_TABLE_INFO         *TableInfo;

  TableInfo = (SYSTEM_TABLE_INFO *)GetGuidHobDataFromSbl (&gUefiSystemTableInfoGuid);
  if (TableInfo == NULL) {
    ASSERT (FALSE);
    return RETURN_NOT_FOUND;
  }

  CopyMem (SystemTableInfo, TableInfo, sizeof (SYSTEM_TABLE_INFO));

  return RETURN_SUCCESS;
}


/**
  Find the serial port information

  @param  SERIAL_PORT_INFO   Pointer to serial port info structure

  @retval RETURN_SUCCESS     Successfully find the serial port information.
  @retval RETURN_NOT_FOUND   Failed to find the serial port information .

**/
RETURN_STATUS
EFIAPI
ParseSerialInfo (
  OUT SERIAL_PORT_INFO     *SerialPortInfo
  )
{
  SERIAL_PORT_INFO              *BlSerialInfo;

  BlSerialInfo = (SERIAL_PORT_INFO *) GetGuidHobDataFromSbl (&gUefiSerialPortInfoGuid);
  if (BlSerialInfo == NULL) {
    ASSERT (FALSE);
    return RETURN_NOT_FOUND;
  }

  CopyMem (SerialPortInfo, BlSerialInfo, sizeof (SERIAL_PORT_INFO));

  return RETURN_SUCCESS;
}


/**
  Find the video frame buffer information

  @param  GfxInfo             Pointer to the EFI_PEI_GRAPHICS_INFO_HOB structure

  @retval RETURN_SUCCESS     Successfully find the video frame buffer information.
  @retval RETURN_NOT_FOUND   Failed to find the video frame buffer information .

**/
RETURN_STATUS
EFIAPI
ParseGfxInfo (
  OUT EFI_PEI_GRAPHICS_INFO_HOB       *GfxInfo
  )
{
  EFI_PEI_GRAPHICS_INFO_HOB           *BlGfxInfo;

  BlGfxInfo = (EFI_PEI_GRAPHICS_INFO_HOB *) GetGuidHobDataFromSbl (&gEfiGraphicsInfoHobGuid);
  if (BlGfxInfo == NULL) {
    return RETURN_NOT_FOUND;
  }

  CopyMem (GfxInfo, BlGfxInfo, sizeof (EFI_PEI_GRAPHICS_INFO_HOB));

  return RETURN_SUCCESS;
}

/**
  Find the video frame buffer device information

  @param  GfxDeviceInfo      Pointer to the EFI_PEI_GRAPHICS_DEVICE_INFO_HOB structure

  @retval RETURN_SUCCESS     Successfully find the video frame buffer information.
  @retval RETURN_NOT_FOUND   Failed to find the video frame buffer information.

**/
RETURN_STATUS
EFIAPI
ParseGfxDeviceInfo (
  OUT EFI_PEI_GRAPHICS_DEVICE_INFO_HOB       *GfxDeviceInfo
  )
{
  EFI_PEI_GRAPHICS_DEVICE_INFO_HOB           *BlGfxDeviceInfo;

  BlGfxDeviceInfo = (EFI_PEI_GRAPHICS_DEVICE_INFO_HOB *) GetGuidHobDataFromSbl (&gEfiGraphicsDeviceInfoHobGuid);
  if (BlGfxDeviceInfo == NULL) {
    return RETURN_NOT_FOUND;
  }

  CopyMem (GfxDeviceInfo, BlGfxDeviceInfo, sizeof (EFI_PEI_GRAPHICS_DEVICE_INFO_HOB));

  return RETURN_SUCCESS;
}

/**
  Find the video frame buffer device information

  @param  SMMSTOREInfo       Pointer to the SMMSTORE_INFO structure

  @retval RETURN_SUCCESS     Successfully find the SMM store buffer information.
  @retval RETURN_NOT_FOUND   Failed to find the SMM store buffer information .

**/
RETURN_STATUS
EFIAPI
ParseSMMSTOREInfo (
  OUT SMMSTORE_INFO       *SMMSTOREInfo
  )
{
  return RETURN_NOT_FOUND;
}

/**
  Find the Tcg Physical Presence store information

  @param  PPIInfo       Pointer to the TCG_PHYSICAL_PRESENCE_INFO structure

  @retval RETURN_SUCCESS     Successfully find the SMM store buffer information.
  @retval RETURN_NOT_FOUND   Failed to find the SMM store buffer information .

**/
RETURN_STATUS
EFIAPI
ParseTPMPPIInfo (
  OUT TCG_PHYSICAL_PRESENCE_INFO       *PPIInfo
  )
{
  return RETURN_NOT_FOUND;
}
