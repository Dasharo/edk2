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

#pragma pack(1)
typedef struct {
  UINT64 NemEnabled : 1;
  UINT64 TpmType : 2;
  UINT64 TpmSuccess : 1;
  UINT64 FACB : 1;
  UINT64 MeasuredBoot : 1;
  UINT64 VerifiedBoot : 1;
  UINT64 Revoked : 1;
  UINT64 : 24;
  UINT64 BtgCap : 1;
  UINT64 : 1;
  UINT64 TxtServerCap : 1;
  UINT64 NoRstSecretsProt : 1;
  UINT64 : 28;
} SACM_INFO_MSR_BITS;

typedef struct {
  UINT64 : 30;
  UINT64 TxtSuccess : 1;
  UINT64 BtgSuccess : 1;
  UINT64 BlockBootEn : 1;
  UINT64 PfrSuccess : 1;
  UINT64 : 13;
  UINT64 MemPowerDown : 1;
  UINT64 BtgFailed : 1;
  UINT64 : 10;
  UINT64 BiosTrusted : 1;
  UINT64 TxtDisPol : 1;
  UINT64 BtgStartupErr : 1;
  UINT64 CpuErr : 1;
  UINT64 SacmSuccess : 1;
} BOOT_STATUS_BITS;

typedef struct {
  UINT32 AcType : 4;
  UINT32 Class : 6;
  UINT32 Major : 5;
  UINT32 AcmStarted : 1;
  UINT32 Minor : 12;
  UINT32 : 3;
  UINT32 Valid : 1;
} ACM_STATUS_BITS;

typedef struct {
  UINT64 KmId : 4;
  UINT64 BpMeasured : 1;
  UINT64 BpVerified : 1;
  UINT64 BpHap : 1;
  UINT64 BpTxt : 1;
  UINT64 : 1;
  UINT64 BpDcd : 1;
  UINT64 BpDbi : 1;
  UINT64 BpPbe : 1;
  UINT64 : 1;
  UINT64 TpmType : 2;
  UINT64 TpmSuccess : 1;
  UINT64 : 1;
  UINT64 Pfr : 1;
  UINT64 BackupAct : 2;
  UINT64 TxtProfile : 5;
  UINT64 ScrubPolicy : 2;
  UINT64 : 2;
  UINT64 DmaProtection : 1;
  UINT64 : 2;
  UINT64 ScrtmStatus : 3;
  UINT64 CpuCoSigning : 1;
  UINT64 TpmStartupLocality : 1;
  UINT64 : 27;
} ACM_POLICY_STATUS_BITS;
#pragma pack()

typedef union {
  SACM_INFO_MSR_BITS Bits;
  UINT64 Raw;
} SACM_INFO_MSR;

typedef union {
  BOOT_STATUS_BITS Bits;
  UINT64 Raw;
} BOOT_STATUS;

typedef union {
  ACM_STATUS_BITS Bits;
  UINT64 Raw;
} ACM_STATUS;

typedef union {
  ACM_POLICY_STATUS_BITS Bits;
  UINT64 Raw;
} ACM_POLICY_STATUS;

typedef struct {
  SACM_INFO_MSR     SacmInfoMsr;
  BOOT_STATUS       BootStatus;
  ACM_STATUS        AcmStatus;
  ACM_POLICY_STATUS AcmPolicyStatus;
} IBG_STATUS;

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
  BOOLEAN  PowerMenuShowUsbPower;
  BOOLEAN  PowerMenuShowDGPUPower;
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
  BOOLEAN  HideFanCurveOff;
  BOOLEAN  DgpuOnlyAvailable;
  BOOLEAN  IntelMeMenuShowCbntStatus;
  BOOLEAN  ShowMemorySpdProfile;
  BOOLEAN  ShowMemoryIbecc;
  BOOLEAN  HaveDiskCapsules;
  BOOLEAN  SecurityMenuShowStm;
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
  BOOLEAN                  HybridCpuArchitecture;
  BOOLEAN                  HyperThreadingSupported;
  BOOLEAN                  HyperThreading;
  UINT8                    BigCoreActiveCount;
  UINT8                    BigCoreMaxCount;
  UINT8                    SmallCoreActiveCount;
  UINT8                    SmallCoreMaxCount;
  UINT8                    CoreActiveCount;
  UINT8                    CoreMaxCount;
  UINT8                    UsbPortPower;
  UINT8                    DGPUState;
  BOOLEAN                  MemoryIbecc;
  BOOLEAN                  StmEnable;
  // FIXME: Do not put anything after IBG_STATUS. There is something wrong
  // with alignments/accesses to this structure, which causes values
  // overwrites that are after IBG_STATUS in the DASHARO_FEATURES_DATA
  // structure.
  IBG_STATUS               IbgStatus;
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
#define FAN_CURVE_OPTION_OFF           DASHARO_FAN_CURVE_OPTION_OFF

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

#define USB_POWER_ON_WHEN_POWERED      DASHARO_USB_POWER_ON_WHEN_POWERED
#define USB_POWER_ALWAYS_ON            DASHARO_USB_POWER_ALWAYS_ON

#define DGPU_DISABLED                  DASHARO_DGPU_DISABLED
#define DGPU_ENABLED                   DASHARO_DGPU_ENABLED
#define DGPU_ONLY                      DASHARO_DGPU_ONLY

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
#define CPU_THROTTLING_THRESHOLD_QUESTION_ID 0x800E
#define USB_PORTS_POWER_QUESTION_ID          0x800F
#define DGPU_STATE_QUESTION_ID               0x8010
#define IBG_SACM_INFO_MSR_QUESTION_ID        0x8011
#define IBG_BOOT_STATUS_QUESTION_ID          0x8012
#define IBG_ACM_STATUS_QUESTION_ID           0x8013
#define IBG_ACM_POLICY_STATUS_QUESTION_ID    0x8014

#endif
