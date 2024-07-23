/** @file
  This library will parse the coreboot table in memory and extract those required
  information.

  Copyright (c) 2014 - 2019, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/
#include <PiPei.h>
#include <Uefi.h>
#include <Protocol/HiiImage.h>
#include <Protocol/HiiDatabase.h>
#include <Guid/GraphicsInfoHob.h>
#include <Guid/MemoryMapInfoGuid.h>
#include <Guid/SerialPortInfoGuid.h>
#include <Guid/SystemTableInfoGuid.h>
#include <Guid/AcpiBoardInfoGuid.h>
#include <Guid/SmmStoreInfoGuid.h>
#include <Guid/TcgPhysicalPresenceGuid.h>
#include <Ppi/SecPerformance.h>

#ifndef __BOOTLOADER_PARSE_LIB__
#define __BOOTLOADER_PARSE_LIB__

#define GET_BOOTLOADER_PARAMETER()      (*(UINT32 *)(UINTN)(PcdGet32(PcdPayloadStackTop) - sizeof(UINT32)))
#define SET_BOOTLOADER_PARAMETER(Value) GET_BOOTLOADER_PARAMETER()=Value

typedef RETURN_STATUS \
        (*BL_MEM_INFO_CALLBACK) (MEMORY_MAP_ENTRY *MemoryMapEntry, VOID *Param);

typedef VOID \
        (*BL_CAPSULE_CALLBACK) (EFI_PHYSICAL_ADDRESS BaseAddress, UINT64 Length);

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
  );

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
  );

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
  );


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
  );


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
  );

/**
  Find the video frame buffer device information

  @param  GfxDeviceInfo      Pointer to the EFI_PEI_GRAPHICS_DEVICE_INFO_HOB structure

  @retval RETURN_SUCCESS     Successfully find the video frame buffer information.
  @retval RETURN_NOT_FOUND   Failed to find the video frame buffer information .

**/
RETURN_STATUS
EFIAPI
ParseGfxDeviceInfo (
  OUT EFI_PEI_GRAPHICS_DEVICE_INFO_HOB       *GfxDeviceInfo
  );

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
  );

/**
  Acquire boot logo from coreboot

  @param  BmpAddress          Pointer to the bitmap file
  @param  BmpSize             Size of the image

  @retval RETURN_SUCCESS      Successfully find the boot logo.
  @retval RETURN_NOT_FOUND    Failed to find the boot logo.

**/
RETURN_STATUS
EFIAPI
ParseBootLogo (
  OUT UINT64 *BmpAddress,
  OUT UINT32 *BmpSize
  );

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
  );

/**
  Acquire Vboot recovery information from coreboot

  @param  RecoveryCode        Recovery reason code, zero if not in recovery mode.
  @param  RecoveryReason      Why are we in recovery boot as a string.

  @retval RETURN_SUCCESS      Successfully found VBoot data.
  @retval RETURN_NOT_FOUND    Failed to find VBoot data.
**/
RETURN_STATUS
EFIAPI
ParseVBootWorkbuf (
  OUT UINT8        *RecoveryCode,
  OUT CONST CHAR8 **RecoveryReason
  );

/**
  Parse the coreboot timestamps

  @retval RETURN_SUCCESS     Successfully find the timestamps information.
  @retval RETURN_NOT_FOUND   Failed to find the tiemstamps information .
**/
RETURN_STATUS
EFIAPI
ParseTimestampTable (
  OUT FIRMWARE_SEC_PERFORMANCE *Performance
  );

/**
  Parse update capsules passed in by coreboot

  @param  CapsuleCallback   The callback routine invoked for each capsule.

  @retval RETURN_SUCCESS    Successfully parsed capsules.
  @retval RETURN_NOT_FOUND  coreboot table is missing.
**/
RETURN_STATUS
EFIAPI
ParseCapsules (
  IN BL_CAPSULE_CALLBACK  CapsuleCallback
  );

#endif
