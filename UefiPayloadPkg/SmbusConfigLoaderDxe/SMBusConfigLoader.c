/** @file
  Implementation for a generic i801 SMBus driver.

Copyright (c) 2016, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent


**/

#include "SMBusConfigLoader.h"
#include <Library/SmbusLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>

/**
  The Entry Point for SMBUS driver.

  It installs DriverBinding.

  @retval EFI_SUCCESS       The entry point is executed successfully.
  @retval other             Some error occurs when executing this entry point.

**/
EFI_STATUS
EFIAPI
InstallSMBusConfigLoader (
  IN EFI_HANDLE                        ImageHandle,
  IN EFI_SYSTEM_TABLE                  *SystemTable
  )
{
  EFI_STATUS                Status;
  UINT8                     Data;
  UINT8                     Index;
  UINT16                    Value;
  UINT8                     j;

  for (j = 0; j < 4; j++) {
    DEBUG ((EFI_D_ERROR, "0x%x: ", j));

    for (Index = 0; Index < 0x80; Index ++) {
      Data = SmBusReadDataByte(SMBUS_LIB_ADDRESS(0x50 + j, Index, 0, 0), &Status);

      if (EFI_ERROR (Status)) {
       // DEBUG ((EFI_D_ERROR, "Failed to read SMBUS byte at offset 0x%x\n", Index));
        //return Status;
        continue;
      }
      DEBUG ((EFI_D_ERROR, "Read SMBUS byte at offset 0x%x: 0x%02x\n", Index, Data));
    }
  }

  UINT8 Array[0x80];

  for (Index = 0; Index < 0x80; Index += 2) {
    Value = SmBusProcessCall(SMBUS_LIB_ADDRESS(0x57, 0, 0, 0), Index, &Status);
    if (EFI_ERROR (Status)) {
        DEBUG ((EFI_D_ERROR, "Failed to read SMBUS byte at offset 0x%x\n", Index));
        continue;
    }
    CopyMem(&Array[Index], &Value, sizeof(Value));
  }
  DEBUG ((EFI_D_ERROR, "EEPROM:\n", Array[Index]));

  for (Index = 0; Index < 0x80; Index ++) {
    DEBUG ((EFI_D_ERROR, "%02x", Array[Index]));
    if (Index > 0 && (Index % 16) == 0) {
      DEBUG ((EFI_D_ERROR, "\n"));

    }
  }


  return EFI_SUCCESS;
}
