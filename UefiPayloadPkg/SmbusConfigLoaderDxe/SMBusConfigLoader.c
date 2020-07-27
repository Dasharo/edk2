/** @file
  Implementation for a generic i801 SMBus driver.

Copyright (c) 2016, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent


**/

#include "SMBusConfigLoader.h"
#include <Library/SmbusLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/PciExpressLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Guid/AuthenticatedVariableFormat.h>


/**
  GetPciConfigSpaceAddress

  Return the PCI Config Space Address if found, zero otherwise.

  @retval 0                 Can not find any SMBusController
  @retval UINT32            PCI Config Space Address
**/
STATIC UINT32
EFIAPI
GetPciConfigSpaceAddress(
  )
{
  UINT8                     Device;
  UINT8                     Function;

  //
  // Search for SMBus Controller within PCI Devices
  //
  for (Device = 0; Device < 32; Device++) {
    for (Function = 0; Function < 8; Function++) {
      if (PciExpressRead16(PCI_EXPRESS_LIB_ADDRESS(0, Device, Function, 0x00)) != 0x8086)
        continue;

      UINT8 BaseClass = PciExpressRead8(PCI_EXPRESS_LIB_ADDRESS(0, Device, Function, 0x0B));

      if (BaseClass == 0x0C) {
        UINT8 SubClass = PciExpressRead8(PCI_EXPRESS_LIB_ADDRESS(0, Device, Function, 0xA));

        if (SubClass == 0x05) {
          return PCI_EXPRESS_LIB_ADDRESS(0, Device, Function, 0x00);
        }
      }
    }
  }

  return 0;
}

/**
  ReadBoardOptionFromEEPROM

  Reads the Board options like Primary Video from the EEPROM

  @param Buffer         Pointer to the Buffer Array

**/
VOID
EFIAPI
ReadBoardOptionFromEEPROM (
  IN OUT UINT8          *Buffer
  )
{
  EFI_STATUS                Status;
  UINT16                    Index;
  UINT16                    Value;

  for (Index = BOARD_SETTINGS_OFFSET; Index < BOARD_SETTINGS_OFFSET + BOARD_SETTINGS_SIZE; Index += 2) { // 
    Value = SmBusProcessCall(SMBUS_LIB_ADDRESS(0x57, 0, 0, 0), SwapBytes16(Index), &Status);
    if (EFI_ERROR (Status)) {
        DEBUG ((EFI_D_ERROR, "Failed to read SMBUS byte at offset 0x%x\n", Index));
        continue;
    }
    CopyMem(&Buffer[Index-BOARD_SETTINGS_OFFSET], &Value, sizeof(Value));
  }
}

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
  UINT16                    Index;
  BOARD_SETTINGS            BoardSettings;
  UINT32                    BaseAddress;
  UINT32                    CRC32Array;

  DEBUG ((EFI_D_ERROR, "SMBusConfigLoader: InstallSMBusConfigLoader\n"));

  BaseAddress = GetPciConfigSpaceAddress();
  if (BaseAddress == 0)
    return EFI_NOT_FOUND;

  ZeroMem(&BoardSettings, sizeof(BOARD_SETTINGS));

  // Set I2C_EN Bit
  UINT32 Val = PciExpressRead32(BaseAddress + HOSTC);
  PciExpressWrite32(BaseAddress + HOSTC, Val | I2C_EN_HOSTC);

  UINT8 Array[BOARD_SETTINGS_SIZE];
  ReadBoardOptionFromEEPROM(Array);
  CopyMem(&BoardSettings, Array, sizeof(BOARD_SETTINGS));
  BoardSettings.Signature = SwapBytes32(BoardSettings.Signature); // we need to do this

  DEBUG (( EFI_D_INFO, "SMBusConfigLoader: Board Settings:\n"));
  DEBUG (( EFI_D_INFO, "SMBusConfigLoader: CRC: %08x - SecureBoot: %02x - PrimaryVideo: %02x\n", BoardSettings.Signature, BoardSettings.SecureBoot, BoardSettings.PrimaryVideo));

  CRC32Array = CalculateCrc32(&Array[4], BOARD_SETTINGS_SIZE - 4);
  if (CRC32Array != BoardSettings.Signature) {
    DEBUG ((EFI_D_ERROR, "SMBusConfigLoader: Checksum invalid. Should be %04X - is: %04x.\nReseting to defaults.\n", CRC32Array, BoardSettings.Signature));
    BoardSettings.PrimaryVideo = 0;
    BoardSettings.SecureBoot = 1;
  }

  // Set SecureBoot
  Status = gRT->SetVariable (EFI_SECURE_BOOT_ENABLE_NAME, 
           &gEfiSecureBootEnableDisableGuid,
           EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
           sizeof BoardSettings.SecureBoot, &BoardSettings.SecureBoot);

  if (EFI_ERROR(Status)) {
    DEBUG ((EFI_D_ERROR, "SMBusConfigLoader: Failed to set SecureBoot: %x\n", Status));
  }
  
  // Restore I2C_EN Bit
  PciExpressWrite32(BaseAddress + HOSTC, Val);
  DEBUG ((EFI_D_VERBOSE, "EEPROM:\n"));

  for (Index = 0; Index < BOARD_SETTINGS_SIZE; Index ++) {
    DEBUG ((EFI_D_VERBOSE, "%02x", Array[Index]));
    if (Index > 0 && (Index % 16) == 0) {
      DEBUG ((EFI_D_VERBOSE, "\n"));
    }
  }
  DEBUG ((EFI_D_VERBOSE, "\n"));

  // Save data into UEFI Variable for later use
  Status = gRT->SetVariable(
                  BOARD_SETTINGS_NAME,               // Variable Name
                  &gEfiBoardSettingsVariableGuid, // Variable Guid
                  (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS),// Variable Attributes
                  sizeof(BOARD_SETTINGS), 
                  &BoardSettings
                  );
  if (EFI_ERROR(Status)) {
    DEBUG ((EFI_D_ERROR, "SMBusConfigLoader: Failed to save BoardSettings %x\n", Status));
    return Status;
  }

  return EFI_SUCCESS;
}