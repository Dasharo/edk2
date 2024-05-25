/** @file
Declarations for options of Dasharo system features

Copyright (c) 2023, 3mdeb Sp. z o.o. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef _DASHARO_OPTIONS_H_
#define _DASHARO_OPTIONS_H_

//
// Names of Dasharo-specific EFI variables in DasharoSystemFeaturesGuid
// namespace.
//

// Settings
#define DASHARO_VAR_BATTERY_CONFIG                L"BatteryConfig"
#define DASHARO_VAR_BOOT_MANAGER_ENABLED          L"BootManagerEnabled"
#define DASHARO_VAR_CPU_MAX_TEMPERATURE           L"CpuMaxTemperature"
#define DASHARO_VAR_CPU_MIN_THROTTLING_THRESHOLD  L"CpuMinThrottlingThreshold"
#define DASHARO_VAR_CPU_THROTTLING_THRESHOLD      L"CpuThrottlingThreshold"
#define DASHARO_VAR_ENABLE_CAMERA                 L"EnableCamera"
#define DASHARO_VAR_ENABLE_WIFI_BT                L"EnableWifiBt"
#define DASHARO_VAR_FAN_CURVE_OPTION              L"FanCurveOption"
#define DASHARO_VAR_FIRMWARE_UPDATE_MODE          L"FirmwareUpdateMode"
#define DASHARO_VAR_IOMMU_CONFIG                  L"IommuConfig"
#define DASHARO_VAR_LOCK_BIOS                     L"LockBios"
#define DASHARO_VAR_MEMORY_PROFILE                L"MemoryProfile"
#define DASHARO_VAR_ME_MODE                       L"MeMode"
#define DASHARO_VAR_NETWORK_BOOT                  L"NetworkBoot"
#define DASHARO_VAR_OPTION_ROM_POLICY             L"OptionRomPolicy"
#define DASHARO_VAR_POWER_FAILURE_STATE           L"PowerFailureState"
#define DASHARO_VAR_PS2_CONTROLLER                L"Ps2Controller"
#define DASHARO_VAR_RESIZEABLE_BARS_ENABLED       L"PCIeResizeableBarsEnabled"
#define DASHARO_VAR_SERIAL_REDIRECTION            L"SerialRedirection"
#define DASHARO_VAR_SERIAL_REDIRECTION2           L"SerialRedirection2"
#define DASHARO_VAR_SLEEP_TYPE                    L"SleepType"
#define DASHARO_VAR_SMM_BWP                       L"SmmBwp"
#define DASHARO_VAR_USB_MASS_STORAGE              L"UsbMassStorage"
#define DASHARO_VAR_USB_STACK                     L"UsbDriverStack"
#define DASHARO_VAR_WATCHDOG                      L"WatchdogConfig"
#define DASHARO_VAR_WATCHDOG_AVAILABLE            L"WatchdogAvailable"

// Other
#define DASHARO_VAR_SMBIOS_UUID  L"Type1UUID"
#define DASHARO_VAR_SMBIOS_SN    L"Type2SN"

//
// Constants for some of the above EFI variables which typically have a value of
// UINT8 type.
//

#define DASHARO_OPTION_ROM_POLICY_DISABLE_ALL  0
#define DASHARO_OPTION_ROM_POLICY_ENABLE_ALL   1
#define DASHARO_OPTION_ROM_POLICY_VGA_ONLY     2

#endif
