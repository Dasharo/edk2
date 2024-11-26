/** @file
  Copyright (c) 2014 - 2016, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

  Copyright (c) 2010 The Chromium OS Authors. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-3-Clause
**/

#include <Uefi/UefiBaseType.h>
#include <Library/BaseLib.h>
#include <Library/BlParseLib.h>

/**
  Find the SMM store information

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
  return RETURN_UNSUPPORTED;
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
  return RETURN_UNSUPPORTED;
}

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
  )
{
  return RETURN_UNSUPPORTED;
}

/**
  Parse the coreboot timestamps

  @retval RETURN_SUCCESS     Successfully find the timestamps information.
  @retval RETURN_NOT_FOUND   Failed to find the tiemstamps information .

**/
RETURN_STATUS
EFIAPI
ParseTimestampTable (
  OUT FIRMWARE_SEC_PERFORMANCE *Performance
  )
{
  return RETURN_UNSUPPORTED;
}

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
  )
{
  return RETURN_UNSUPPORTED;
}

/**
  Acquire boot logo from coreboot

  @param  BmpAddress          Pointer to the bitmap file
  @param  BmpSize             Size of the image

  @retval RETURN_SUCCESS            Successfully find the boot logo.
  @retval RETURN_NOT_FOUND          Failed to find the boot logo.
**/
RETURN_STATUS
EFIAPI
ParseBootLogo (
  OUT UINT64 *BmpAddress,
  OUT UINT32 *BmpSize
  )
{
  return RETURN_UNSUPPORTED;
}
