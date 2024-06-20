/** @file
  A library for providing services related to Dasharo-specific EFI variables.

  Copyright (c) 2024, 3mdeb Sp. z o.o. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "Library/DasharoVariablesLib.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TpmMeasurementLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include <DasharoOptions.h>

// PCR number for Dasharo variables.
#define DASHARO_VAR_PCR  1

// Event type for Dasharo variables.
#define EV_DASHARO_VAR  0x00DA0000

// Description of a single variable.
typedef struct {
  // Default value.
  DASHARO_VAR_DATA  Data;  // Value for the variable.
  UINTN             Size;  // Number of bytes of Data actually used.

  UINT32  Attributes;  // EFI variable attributes for this variable.
} VAR_INFO;

// List of Dasharo EFI variables in gDasharoSystemFeaturesGuid namespace that
// are created if missing.
STATIC CHAR16 *mAutoCreatedVariables[] = {
  DASHARO_VAR_BATTERY_CONFIG,
  DASHARO_VAR_BOOT_MANAGER_ENABLED,
  DASHARO_VAR_CPU_THROTTLING_OFFSET,
  DASHARO_VAR_ENABLE_CAMERA,
  DASHARO_VAR_ENABLE_WIFI_BT,
  DASHARO_VAR_FAN_CURVE_OPTION,
  DASHARO_VAR_IOMMU_CONFIG,
  DASHARO_VAR_LOCK_BIOS,
  DASHARO_VAR_MEMORY_PROFILE,
  DASHARO_VAR_ME_MODE,
  DASHARO_VAR_NETWORK_BOOT,
  DASHARO_VAR_OPTION_ROM_POLICY,
  DASHARO_VAR_POWER_FAILURE_STATE,
  DASHARO_VAR_PS2_CONTROLLER,
  DASHARO_VAR_RESIZEABLE_BARS_ENABLED,
  DASHARO_VAR_SERIAL_REDIRECTION,
  DASHARO_VAR_SERIAL_REDIRECTION2,
  DASHARO_VAR_SLEEP_TYPE,
  DASHARO_VAR_SMM_BWP,
  DASHARO_VAR_USB_MASS_STORAGE,
  DASHARO_VAR_USB_STACK,
  DASHARO_VAR_WATCHDOG,
  DASHARO_VAR_WATCHDOG_AVAILABLE,
  DASHARO_VAR_SMALL_CORE_ACTIVE_COUNT,
  DASHARO_VAR_CORE_ACTIVE_COUNT,
  DASHARO_VAR_HYPER_THREADING,
};

/**
  Produce a default value for a specified variable.

  @param VarName  Name of the variable.

  @retval Default value and its length which is zero for unknown variable name.
**/
STATIC
VAR_INFO
GetVariableInfo (
  CHAR16  *VarName
  )
{
  VAR_INFO          Value;
  DASHARO_VAR_DATA  Data;
  UINTN             Size;
  UINT32            ExtraAttrs;

  SetMem (&Data, sizeof (Data), 0);
  Size = 0;
  ExtraAttrs = 0;

  if (StrCmp (VarName, DASHARO_VAR_BATTERY_CONFIG) == 0) {
    Data.Battery.StartThreshold = 95;
    Data.Battery.StopThreshold = 98;
    Size = sizeof (Data.Battery);
  } else if (StrCmp (VarName, DASHARO_VAR_BOOT_MANAGER_ENABLED) == 0) {
    Data.Boolean = TRUE;
    Size = sizeof (Data.Boolean);
  } else if (StrCmp (VarName, DASHARO_VAR_CPU_THROTTLING_OFFSET) == 0) {
    Data.Uint8 = FixedPcdGet8 (PcdCpuThrottlingOffsetDefault);
    Size = sizeof (Data.Uint8);
  } else if (StrCmp (VarName, DASHARO_VAR_ENABLE_CAMERA) == 0) {
    Data.Boolean = TRUE;
    Size = sizeof (Data.Boolean);
  } else if (StrCmp (VarName, DASHARO_VAR_ENABLE_WIFI_BT) == 0) {
    Data.Boolean = TRUE;
    Size = sizeof (Data.Boolean);
  } else if (StrCmp (VarName, DASHARO_VAR_FAN_CURVE_OPTION) == 0) {
    Data.Uint8 = DASHARO_FAN_CURVE_OPTION_SILENT;
    Size = sizeof (Data.Uint8);
  } else if (StrCmp (VarName, DASHARO_VAR_IOMMU_CONFIG) == 0) {
    Data.Iommu.IommuEnable = FALSE;
    Data.Iommu.IommuHandoff = FALSE;
    Size = sizeof (Data.Iommu);
  } else if (StrCmp (VarName, DASHARO_VAR_LOCK_BIOS) == 0) {
    Data.Boolean = TRUE;
    Size = sizeof (Data.Boolean);
  } else if (StrCmp (VarName, DASHARO_VAR_MEMORY_PROFILE) == 0) {
    Data.Uint8 = DASHARO_MEMORY_PROFILE_JEDEC;
    Size = sizeof (Data.Uint8);
  } else if (StrCmp (VarName, DASHARO_VAR_ME_MODE) == 0) {
    Data.Uint8 = FixedPcdGet8 (PcdIntelMeDefaultState);
    Size = sizeof (Data.Uint8);
  } else if (StrCmp (VarName, DASHARO_VAR_NETWORK_BOOT) == 0) {
    Data.Boolean = PcdGetBool (PcdDefaultNetworkBootEnable);
    Size = sizeof (Data.Boolean);
  } else if (StrCmp (VarName, DASHARO_VAR_OPTION_ROM_POLICY) == 0) {
    Data.Uint8 = PcdGetBool (PcdLoadOptionRoms)
      ? DASHARO_OPTION_ROM_POLICY_ENABLE_ALL
      : DASHARO_OPTION_ROM_POLICY_DISABLE_ALL;
    Size = sizeof (Data.Uint8);
  } else if (StrCmp (VarName, DASHARO_VAR_POWER_FAILURE_STATE) == 0) {
    Data.Uint8 = FixedPcdGet8 (PcdDefaultPowerFailureState);
    Size = sizeof (Data.Uint8);
  } else if (StrCmp (VarName, DASHARO_VAR_PS2_CONTROLLER) == 0) {
    Data.Boolean = TRUE;
    Size = sizeof (Data.Boolean);
  } else if (StrCmp (VarName, DASHARO_VAR_RESIZEABLE_BARS_ENABLED) == 0) {
    Data.Boolean = FALSE;
    Size = sizeof (Data.Boolean);
  } else if (StrCmp (VarName, DASHARO_VAR_SERIAL_REDIRECTION) == 0) {
    Data.Boolean = PcdGetBool (PcdSerialRedirectionDefaultState);
    Size = sizeof (Data.Boolean);
    ExtraAttrs = EFI_VARIABLE_RUNTIME_ACCESS;
  } else if (StrCmp (VarName, DASHARO_VAR_SERIAL_REDIRECTION2) == 0) {
    Data.Boolean = PcdGetBool (PcdHave2ndUart) ? PcdGetBool (PcdSerialRedirection2DefaultState) : FALSE;
    Size = sizeof (Data.Boolean);
    ExtraAttrs = EFI_VARIABLE_RUNTIME_ACCESS;
  } else if (StrCmp (VarName, DASHARO_VAR_SLEEP_TYPE) == 0) {
    Data.Uint8 = PcdGetBool (PcdSleepTypeDefaultS3) ? DASHARO_SLEEP_TYPE_S3 : DASHARO_SLEEP_TYPE_S0IX;
    Size = sizeof (Data.Uint8);
  } else if (StrCmp (VarName, DASHARO_VAR_SMM_BWP) == 0) {
    Data.Boolean = FALSE;
    Size = sizeof (Data.Boolean);
  } else if (StrCmp (VarName, DASHARO_VAR_USB_MASS_STORAGE) == 0) {
    Data.Boolean = TRUE;
    Size = sizeof (Data.Boolean);
  } else if (StrCmp (VarName, DASHARO_VAR_USB_STACK) == 0) {
    Data.Boolean = TRUE;
    Size = sizeof (Data.Boolean);
  } else if (StrCmp (VarName, DASHARO_VAR_WATCHDOG) == 0) {
    Data.Watchdog.WatchdogEnable = PcdGetBool (PcdOcWdtEnableDefault);
    Data.Watchdog.WatchdogTimeout = FixedPcdGet16 (PcdOcWdtTimeoutDefault);
    Size = sizeof (Data.Watchdog);
  } else if (StrCmp (VarName, DASHARO_VAR_WATCHDOG_AVAILABLE) == 0) {
    Data.Boolean = PcdGetBool (PcdShowOcWdtOptions);
    Size = sizeof (Data.Boolean);
  } else if (StrCmp (VarName, DASHARO_VAR_SMALL_CORE_ACTIVE_COUNT) == 0) {
    Data.Uint8 = DASHARO_CPU_CORES_ENABLE_ALL;
    Size = sizeof (Data.Uint8);
  } else if (StrCmp (VarName, DASHARO_VAR_CORE_ACTIVE_COUNT) == 0) {
    Data.Uint8 = DASHARO_CPU_CORES_ENABLE_ALL;
    Size = sizeof (Data.Uint8);
  } else if (StrCmp (VarName, DASHARO_VAR_HYPER_THREADING) == 0) {
    Data.Boolean = PcdGetBool (PcdCpuHyperThreadingDefault);
    Size = sizeof (Data.Boolean);
  } else {
    DEBUG ((EFI_D_ERROR, "%a(): Unknown variable: %s.\n", __FUNCTION__, VarName));
    ASSERT ((0 && "No default value set for a variable."));
  }

  Value.Data = Data;
  Value.Size = Size;
  Value.Attributes = EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE | ExtraAttrs;

  return Value;
}

DASHARO_VAR_DATA
EFIAPI
DasharoGetVariableDefault (
  CHAR16  *VarName
  )
{
  VAR_INFO  VarInfo;

  VarInfo = GetVariableInfo (VarName);
  if (VarInfo.Size == 0) {
    DEBUG ((EFI_D_VERBOSE, "%a(): Failed to look up default for %s.\n", __FUNCTION__, VarName));
  }

  return VarInfo.Data;
}

UINT32
EFIAPI
DasharoGetVariableAttributes (
  CHAR16  *VarName
  )
{
  VAR_INFO  VarInfo;

  VarInfo = GetVariableInfo (VarName);
  if (VarInfo.Size == 0) {
    DEBUG ((EFI_D_VERBOSE, "%a(): Failed to look up attributes of %s.\n", __FUNCTION__, VarName));
  }

  return VarInfo.Attributes;
}

/**
  Reset a single Dasharo EFI variable to its default value.

  @param VarName  Name of the variable to reset.

  @retval RETURN_SUCCESS  Successfully measured all variables.
**/
STATIC
EFI_STATUS
ResetVariable (
  CHAR16 *VarName
  )
{
  EFI_STATUS  Status;
  VAR_INFO    VarInfo;

  VarInfo = GetVariableInfo (VarName);
  if (VarInfo.Size == 0)
    return EFI_NOT_FOUND;

  Status = gRT->SetVariable (
      VarName,
      &gDasharoSystemFeaturesGuid,
      VarInfo.Attributes,
      VarInfo.Size,
      &VarInfo.Data
      );

  return Status;
}

/**
  Check whether a specified variable exists and create it if it doesn't.

  The variable is created with a default value.

  @param VarName  Name of the variable to initialize.
**/
STATIC
VOID
InitVariable (
  CHAR16  *VarName
  )
{
  EFI_STATUS  Status;
  UINTN       BufferSize;

  BufferSize = 0;
  Status = gRT->GetVariable (
      VarName,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      NULL
      );

  if (Status == EFI_NOT_FOUND) {
    Status = ResetVariable (VarName);
    ASSERT_EFI_ERROR (Status);
  }
}

/**
  Measure a single variable into DASHARO_VAR_PCR with EV_DASHARO_VAR event type.

  @param VarName  Name of the variable.
  @param Vendor   Namespace of the variable.

  @retval EFI_SUCCESS  If the variable was read and measured without errors.
  @retval EFI_OUT_OF_RESOURCES  On memory allocation failure.
**/
STATIC
EFI_STATUS
MeasureVariable (
  CHAR16    *VarName,
  EFI_GUID  *Vendor
  )
{
  EFI_STATUS  Status;
  UINTN       PrefixSize;
  VOID        *VarData;
  UINTN       VarSize;
  CHAR8       *EventData;

  DEBUG ((EFI_D_VERBOSE, "%a(): %g:%s.\r\n", __FUNCTION__, Vendor, VarName));

  PrefixSize = StrLen (VarName) + 1;

  Status = GetVariable2 (VarName, Vendor, &VarData, &VarSize);
  ASSERT_EFI_ERROR (Status);

  EventData = AllocatePool (PrefixSize + VarSize);
  if (EventData == NULL) {
    FreePool (VarData);
    return EFI_OUT_OF_RESOURCES;
  }

  UnicodeStrToAsciiStrS (VarName, EventData, PrefixSize);
  CopyMem (EventData + PrefixSize, VarData, VarSize);

  Status = TpmMeasureAndLogData (
      DASHARO_VAR_PCR,
      EV_DASHARO_VAR,
      EventData,
      PrefixSize + VarSize,
      VarData,
      VarSize
      );

  FreePool (EventData);
  FreePool (VarData);

  return Status;
}

/**
  Measures single all existing variables with the specified GUID.

  @param Vendor   Namespace of the variable.

  @retval EFI_SUCCESS  If the variable was read and measured without errors.
**/
STATIC
EFI_STATUS
MeasureVariables (
  EFI_GUID  *Vendor
  )
{
  EFI_STATUS  Status;
  CHAR16      *Name;
  CHAR16      *NewBuf;
  UINTN       MaxNameSize;
  UINTN       NameSize;
  EFI_GUID    Guid;

  MaxNameSize = 32*sizeof (CHAR16);
  Name = AllocateZeroPool (MaxNameSize);
  if (Name == NULL)
    return EFI_OUT_OF_RESOURCES;

  while (TRUE) {
    NameSize = MaxNameSize;
    Status = gRT->GetNextVariableName (&NameSize, Name, &Guid);
    if (Status == EFI_BUFFER_TOO_SMALL) {
      NewBuf = AllocatePool (NameSize);
      if (NewBuf == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        break;
      }

      StrnCpyS (NewBuf, NameSize/sizeof (CHAR16), Name, MaxNameSize/sizeof (CHAR16));
      FreePool (Name);

      Name = NewBuf;
      MaxNameSize = NameSize;

      Status = gRT->GetNextVariableName (&NameSize, Name, &Guid);
    }

    if (Status == EFI_NOT_FOUND) {
      Status = EFI_SUCCESS;
      break;
    }

    if (EFI_ERROR (Status))
      break;

    if (CompareGuid (&Guid, Vendor))
      MeasureVariable (Name, Vendor);
  }

  FreePool (Name);
  return Status;
}

EFI_STATUS
EFIAPI
DasharoMeasureVariables (
  VOID
  )
{
  EFI_STATUS  Status;

  Status = MeasureVariables (&gDasharoSystemFeaturesGuid);
  if (Status == EFI_SUCCESS)
    Status = MeasureVariables (&gApuConfigurationFormsetGuid);

  return Status;
}

EFI_STATUS
EFIAPI
DasharoVariablesLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINTN  Idx;

  // Create Dasharo-specific variables that are missing by initializing
  // them with default values.
  for (Idx = 0; Idx < ARRAY_SIZE (mAutoCreatedVariables); Idx++)
    InitVariable (mAutoCreatedVariables[Idx]);

  return EFI_SUCCESS;
}
