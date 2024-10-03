/** @file
  Library that query laptop EC for AC state and battery capacity.

Copyright (c) 2023, 3mdeb. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>

#ifndef __LAPTOP_BATTERY_LIB__
#define __LAPTOP_BATTERY_LIB__

/**
  This function retrieves the AC adapter connection state from EC.

  @param  AcState                     Pointer to the AC state

  @retval RETURN_SUCCESS              Successfully probed the AC connection state.
  @retval RETURN_UNSUPPORTED          Function is unsupported.
  @retval RETURN_TIMEOUT              EC cpommunication timeout.
  @retval RETURN_INVALID_PARAMETER    NULL pointer passed as parameter

**/
RETURN_STATUS
EFIAPI
LaptopGetAcState (
  BOOLEAN           *AcState
  );

/**
  This function retrieves the battery connection state from EC.

  @param  AcState                     Pointer to the AC state

  @retval RETURN_SUCCESS              Successfully probed the battery connection state.
  @retval RETURN_UNSUPPORTED          Function is unsupported.
  @retval RETURN_TIMEOUT              EC cpommunication timeout.
  @retval RETURN_INVALID_PARAMETER    NULL pointer passed as parameter

**/
RETURN_STATUS
EFIAPI
LaptopGetBatState (
  BOOLEAN           *BatState
  );

/**
  This function retrieves the current battery capacity from EC.

  @param  BatteryCapacity             Pointer to the battery capacity in percent

  @retval RETURN_SUCCESS              Successfully probed the battery capacity.
  @retval RETURN_UNSUPPORTED          Function is unsupported.
  @retval RETURN_TIMEOUT              EC cpommunication timeout.
  @retval RETURN_INVALID_PARAMETER    NULL pointer passed as parameter

**/
RETURN_STATUS
EFIAPI
LaptopGetBatteryCapacity (
  UINT32            *BatteryCapacity
  );

#endif
