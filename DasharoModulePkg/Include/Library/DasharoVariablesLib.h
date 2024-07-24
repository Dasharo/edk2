/** @file
  A library for providing services related to Dasharo-specific EFI variables.

  Copyright (c) 2024, 3mdeb Sp. z o.o. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef _DASHARO_VARIABLES_LIB_H_
#define _DASHARO_VARIABLES_LIB_H_

#include <Base.h>
#include <DasharoOptions.h>

/**
  Query a default value for a specified variable.

  @param VarName  Name of the variable.

  @retval Default value which is all zeroes for an unknown variable name.
**/
DASHARO_VAR_DATA
EFIAPI
DasharoGetVariableDefault (
  CHAR16  *VarName
  );

/**
  Query attributes of a specified variable.

  @param VarName  Name of the variable.

  @retval EFI variable attributes (the value is sensible for unknown ones).
**/
UINT32
EFIAPI
DasharoGetVariableAttributes (
  CHAR16  *VarName
  );

/**
  Measure EFI variables specific to Dasharo.

  This function should be called before booting into an OS or a UEFI
  application.

  @retval RETURN_SUCCESS  Successfully measured all variables.
**/
EFI_STATUS
EFIAPI
DasharoMeasureVariables (
  VOID
  );

/**
  Enable firmware update mode (FUM) for the duration of the next boot.

  @retval RETURN_SUCCESS  FUM was successfully enabled.
**/
EFI_STATUS
EFIAPI
DasharoEnableFUM (
  VOID
  );

/**
  Check whether capsule updates which survive a warm system reset are permitted
  by current configuration.

  @retval TRUE   Persistent capsules can be accepted by UpdateCapsule().
  @retval FALSE  UpdateCapsule() must fail with an error for such a capsule.
**/
BOOLEAN
EFIAPI
DasharoCapsulesCanPersistAcrossReset (
  VOID
  );

#endif
