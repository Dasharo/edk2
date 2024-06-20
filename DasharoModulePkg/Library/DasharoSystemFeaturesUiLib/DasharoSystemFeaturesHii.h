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
#define DASHARO_CPU_CONFIGURATION_FORM_ID           0x100a

#define DASHARO_FEATURES_DATA_VARSTORE_ID      0x0001

typedef struct {
  // Feature visibility
  BOOLEAN  ShowSecurityMenu;
  BOOLEAN  ShowIntelMeMenu;
  BOOLEAN  ShowUsbMenu;
  BOOLEAN  ShowNetworkMenu;
  BOOLEAN  ShowChipsetMenu;
  BOOLEAN  ShowPowerMenu;
  BOOLEAN  ShowPciMenu;
  BOOLEAN  ShowMemoryMenu;
  BOOLEAN  ShowSerialPortMenu;
  BOOLEAN  ShowCpuMenu;
  BOOLEAN  ShowLockBios;
  BOOLEAN  ShowSmmBwp;
  BOOLEAN  ShowFum;
  BOOLEAN  ShowPs2Option;
  BOOLEAN  PowerMenuShowFanCurve;
  BOOLEAN  PowerMenuShowSleepType;
  BOOLEAN  PowerMenuShowBatteryThresholds;
  BOOLEAN  DasharoEnterprise;
  BOOLEAN  SecurityMenuShowIommu;
  BOOLEAN  PciMenuShowResizeableBars;
  BOOLEAN  SecurityMenuShowWiFiBt;
  BOOLEAN  SecurityMenuShowCamera;
  BOOLEAN  MeHapAvailable;
  BOOLEAN  S3SupportExperimental;
  BOOLEAN  Have2ndUart;
  BOOLEAN  ShowCpuThrottlingThreshold;
  BOOLEAN  ShowCpuCoreDisable;
  BOOLEAN  ShowCpuHyperThreading;
  BOOLEAN  ShowPowerFailureState;
  // Feature data
  BOOLEAN                  LockBios;
  BOOLEAN                  SmmBwp;
  BOOLEAN                  NetworkBoot;
  BOOLEAN                  UsbStack;
  BOOLEAN                  UsbMassStorage;
  UINT8                    MeMode;
  BOOLEAN                  Ps2Controller;
  DASHARO_WATCHDOG_CONFIG  WatchdogConfig;
  BOOLEAN                  WatchdogAvailable;
  UINT8                    FanCurveOption;
  DASHARO_IOMMU_CONFIG     IommuConfig;
  BOOLEAN                  BootManagerEnabled;
  UINT8                    SleepType;
  UINT8                    PowerFailureState;
  BOOLEAN                  ResizeableBarsEnabled;
  UINT8                    OptionRomExecution;
  BOOLEAN                  EnableCamera;
  BOOLEAN                  EnableWifiBt;
  DASHARO_BATTERY_CONFIG   BatteryConfig;
  UINT8                    MemoryProfile;
  BOOLEAN                  SerialPortRedirection;
  BOOLEAN                  SerialPort2Redirection;
  UINT8                    CpuThrottlingThreshold;
  UINT8                    CpuThrottlingOffset;
  UINT8                    CpuMaxTemperature;
  BOOLEAN                  HybridCpuArchitecture;
  BOOLEAN                  HyperThreadingSupported;
  BOOLEAN                  HyperThreading;
  UINT8                    BigCoreActiveCount;
  UINT8                    BigCoreMaxCount;
  UINT8                    SmallCoreActiveCount;
  UINT8                    SmallCoreMaxCount;
  UINT8                    CoreActiveCount;
  UINT8                    CoreMaxCount;
} DASHARO_FEATURES_DATA;

//
// DasharoOptions.h can be included by files unrelated to Dasharo in which case
// it's useful to indicate where they came from.
//
// HII code, however, is already specific to Dasharo and there is no need to
// have extra 8 characters here.
//

#define FAN_CURVE_OPTION_SILENT        DASHARO_FAN_CURVE_OPTION_SILENT
#define FAN_CURVE_OPTION_PERFORMANCE   DASHARO_FAN_CURVE_OPTION_PERFORMANCE

#define ME_MODE_ENABLE                 DASHARO_ME_MODE_ENABLE
#define ME_MODE_DISABLE_HECI           DASHARO_ME_MODE_DISABLE_HECI
#define ME_MODE_DISABLE_HAP            DASHARO_ME_MODE_DISABLE_HAP

#define OPTION_ROM_POLICY_DISABLE_ALL  DASHARO_OPTION_ROM_POLICY_DISABLE_ALL
#define OPTION_ROM_POLICY_ENABLE_ALL   DASHARO_OPTION_ROM_POLICY_ENABLE_ALL
#define OPTION_ROM_POLICY_VGA_ONLY     DASHARO_OPTION_ROM_POLICY_VGA_ONLY

#define SLEEP_TYPE_S0IX                DASHARO_SLEEP_TYPE_S0IX
#define SLEEP_TYPE_S3                  DASHARO_SLEEP_TYPE_S3

#define POWER_FAILURE_STATE_OFF        DASHARO_POWER_FAILURE_STATE_OFF
#define POWER_FAILURE_STATE_ON         DASHARO_POWER_FAILURE_STATE_ON
#define POWER_FAILURE_STATE_KEEP       DASHARO_POWER_FAILURE_STATE_KEEP
#define POWER_FAILURE_STATE_HIDDEN     DASHARO_POWER_FAILURE_STATE_HIDDEN

#define MEMORY_PROFILE_JEDEC           DASHARO_MEMORY_PROFILE_JEDEC
#define MEMORY_PROFILE_XMP1            DASHARO_MEMORY_PROFILE_XMP1
#define MEMORY_PROFILE_XMP2            DASHARO_MEMORY_PROFILE_XMP2
#define MEMORY_PROFILE_XMP3            DASHARO_MEMORY_PROFILE_XMP3

#define CPU_CORES_ENABLE_ALL           DASHARO_CPU_CORES_ENABLE_ALL

//
// Question IDs are used in VFR file to let the code in
// DasharoSystemFeaturesCallback() know what form element caused
// invocation of the callback.
//

#define NETWORK_BOOT_QUESTION_ID             0x8000
#define WATCHDOG_ENABLE_QUESTION_ID          0x8001
#define WATCHDOG_TIMEOUT_QUESTION_ID         0x8002
#define FIRMWARE_UPDATE_MODE_QUESTION_ID     0x8003
#define POWER_FAILURE_STATE_QUESTION_ID      0x8004
#define OPTION_ROM_STATE_QUESTION_ID         0x8005
#define SERIAL_PORT_REDIR_QUESTION_ID        0x8006
#define BATTERY_START_THRESHOLD_QUESTION_ID  0x8007
#define BATTERY_STOP_THRESHOLD_QUESTION_ID   0x8008
#define INTEL_ME_MODE_QUESTION_ID            0x8009
#define SLEEP_TYPE_QUESTION_ID               0x800A
#define SERIAL_PORT2_REDIR_QUESTION_ID       0x800B
#define HYPER_THREADING_QUESTION_ID          0x800C
#define CPU_THROTTLING_OFFSET_QUESTION_ID    0x800D

#endif
