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

#endif
