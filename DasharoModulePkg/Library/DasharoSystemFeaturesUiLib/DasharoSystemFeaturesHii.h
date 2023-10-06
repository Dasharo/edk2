/** @file
The Dasharo system features implementation

Copyright (c) 2022, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause

**/

#ifndef _DASHARO_SYSTEM_FEATURES_HII_H_
#define _DASHARO_SYSTEM_FEATURES_HII_H_

#include <DasharoOptions.h>

#define DASHARO_SYSTEM_FEATURES_GUID  \
  { 0xd15b327e, 0xff2d, 0x4fc1, {0xab, 0xf6, 0xc1, 0x2b, 0xd0, 0x8c, 0x13, 0x59} }

#define DASHARO_SYSTEM_FEATURES_FORM_ID             0x1000
#define DASHARO_SECURITY_OPTIONS_FORM_ID            0x1001
#define DASHARO_NETWORK_OPTIONS_FORM_ID             0x1002
#define DASHARO_USB_CONFIGURATION_FORM_ID           0x1003
#define DASHARO_INTEL_ME_OPTIONS_FORM_ID            0x1004
#define DASHARO_CHIPSET_CONFIGURATION_FORM_ID       0x1005
#define DASHARO_POWER_CONFIGURATION_FORM_ID         0x1006
#define DASHARO_PCI_CONFIGURATION_FORM_ID           0x1007
#define DASHARO_MEMORY_CONFIGURATION_FORM_ID        0x1008
#define DASHARO_SERIAL_PORT_CONFIGURATION_FORM_ID   0x1009

#define DASHARO_FEATURES_DATA_VARSTORE_ID      0x0001

#pragma pack(push,1)
typedef struct {
  BOOLEAN WatchdogEnable;
  UINT16  WatchdogTimeout;
} WATCHDOG_CONFIG;

typedef struct {
  BOOLEAN IommuEnable;
  BOOLEAN IommuHandoff;
} IOMMU_CONFIG;

typedef struct {
  UINT8  StartThreshold;
  UINT8  StopThreshold;
} BATTERY_CONFIG;
#pragma pack(pop)

#define FAN_CURVE_OPTION_SILENT 0
#define FAN_CURVE_OPTION_PERFORMANCE 1

typedef struct {
  // Feature visibility
  BOOLEAN            ShowSecurityMenu;
  BOOLEAN            ShowIntelMeMenu;
  BOOLEAN            ShowUsbMenu;
  BOOLEAN            ShowNetworkMenu;
  BOOLEAN            ShowChipsetMenu;
  BOOLEAN            ShowPowerMenu;
  BOOLEAN            ShowPciMenu;
  BOOLEAN            ShowMemoryMenu;
  BOOLEAN            ShowSerialPortMenu;
  BOOLEAN            PowerMenuShowFanCurve;
  BOOLEAN            PowerMenuShowSleepType;
  BOOLEAN            PowerMenuShowBatteryThresholds;
  BOOLEAN            DasharoEnterprise;
  BOOLEAN            SecurityMenuShowIommu;
  BOOLEAN            PciMenuShowResizeableBars;
  BOOLEAN            SecurityMenuShowWiFiBt;
  BOOLEAN            SecurityMenuShowCamera;
  // Feature data
  BOOLEAN            LockBios;
  BOOLEAN            SmmBwp;
  BOOLEAN            NetworkBoot;
  BOOLEAN            UsbStack;
  BOOLEAN            UsbMassStorage;
  UINT8              MeMode;
  BOOLEAN            Ps2Controller;
  WATCHDOG_CONFIG    WatchdogConfig;
  BOOLEAN            WatchdogState; // holds the state of watchdog before VAR population
  UINT8              FanCurveOption;
  IOMMU_CONFIG       IommuConfig;
  BOOLEAN            BootManagerEnabled;
  UINT8              SleepType;
  UINT8              PowerFailureState;
  BOOLEAN            ResizeableBarsEnabled;
  UINT8              OptionRomExecution;
  BOOLEAN            EnableCamera;
  BOOLEAN            EnableWifiBt;
  BATTERY_CONFIG     BatteryConfig;
  UINT8              MemoryProfile;
  BOOLEAN            SerialPortRedirection;
} DASHARO_FEATURES_DATA;

#define ME_MODE_ENABLE        0
#define ME_MODE_DISABLE_HECI  1
#define ME_MODE_DISABLE_HAP   2

#define OPTION_ROM_POLICY_DISABLE_ALL  DASHARO_OPTION_ROM_POLICY_DISABLE_ALL
#define OPTION_ROM_POLICY_ENABLE_ALL   DASHARO_OPTION_ROM_POLICY_ENABLE_ALL
#define OPTION_ROM_POLICY_VGA_ONLY     DASHARO_OPTION_ROM_POLICY_VGA_ONLY

#define SLEEP_TYPE_S0IX  0
#define SLEEP_TYPE_S3    1

#define POWER_FAILURE_STATE_OFF     0
#define POWER_FAILURE_STATE_ON      1
#define POWER_FAILURE_STATE_KEEP    2
#define POWER_FAILURE_STATE_HIDDEN  0xff

// Values aren't random, they match FSP_M_CONFIG::SpdProfileSelected
#define MEMORY_PROFILE_JEDEC  0
#define MEMORY_PROFILE_XMP1   2
#define MEMORY_PROFILE_XMP2   3
#define MEMORY_PROFILE_XMP3   4

#define NETWORK_BOOT_QUESTION_ID             0x8000
#define WATCHDOG_OPTIONS_QUESTION_ID         0x8001
#define WATCHDOG_TIMEOUT_QUESTION_ID         0x8002
#define FIRMWARE_UPDATE_MODE_QUESTION_ID     0x8003
#define POWER_FAILURE_STATE_QUESTION_ID      0x8004
#define OPTION_ROM_STATE_QUESTION_ID         0x8005
#define SERIAL_PORT_REDIR_QUESTION_ID        0x8006
#define BATTERY_START_THRESHOLD_QUESTION_ID  0x8007
#define BATTERY_STOP_THRESHOLD_QUESTION_ID   0x8008

extern EFI_GUID gDasharoSystemFeaturesGuid;

#endif
