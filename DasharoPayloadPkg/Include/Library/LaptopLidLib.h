/** @file
  Library that query laptop EC for lid state.

Copyright (c) 2023, 3mdeb. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>

#ifndef __LAPTOP_LID_LIB__
#define __LAPTOP_LID_LIB__

#pragma pack(1)

typedef enum {
  LidClosed,
  LidOpen,
  LidStatusMax
} LID_STATUS;

#pragma pack()

/**
  This function retrieves the lid state from EC.

  @param  LidState                    Pointer to the lid state

  @retval RETURN_SUCCESS              Successfully probed the lid state.
  @retval RETURN_UNSUPPORTED          Function is unsupported.
  @retval RETURN_TIMEOUT              EC communication timeout.
  @retval RETURN_INVALID_PARAMETER    NULL pointer passed as parameter

**/
EFI_STATUS
EFIAPI
LaptopGetLidState (
  LID_STATUS           *LidState
  );

#endif
