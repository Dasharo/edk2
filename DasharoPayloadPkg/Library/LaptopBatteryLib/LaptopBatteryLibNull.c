/** @file
  Library that query laptop EC for AC state and battery capacity.

Copyright (c) 2023, 3mdeb. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/LaptopBatteryLib.h>

/**
  This function retrieves the AC adapter connection state from EC.

  @param  AcState                     Pointer to the AC state

  @retval RETURN_SUCCESS              Successfully probed the battery capacity.
  @retval RETURN_UNSUPPORTED          Function is unsupported.
  @retval RETURN_TIMEOUT              EC cpommunication timeout.
  @retval RETURN_INVALID_PARAMETER    NULL pointer passed as parameter

**/
RETURN_STATUS
EFIAPI
LaptopGetAcState (
  BOOLEAN           *AcState
  )
{
  return RETURN_UNSUPPORTED;
}

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
  )
{
  return RETURN_UNSUPPORTED;
}

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
  )
{
  return RETURN_UNSUPPORTED;
}
