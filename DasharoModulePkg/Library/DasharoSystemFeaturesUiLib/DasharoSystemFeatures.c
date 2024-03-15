/** @file
The Dasharo system features reference implementation

Copyright (c) 2022, 3mdeb Sp. z o.o. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause

**/

#include "DasharoSystemFeatures.h"

#define PCH_OC_WDT_CTL				0x54
#define   PCH_OC_WDT_CTL_EN			BIT14
#define   PCH_OC_WDT_CTL_TOV_MASK		0x3FF

// Feature state
STATIC CHAR16 mVarStoreName[] = L"FeaturesData";

STATIC CHAR16 mLockBiosEfiVar[] = L"LockBios";
STATIC CHAR16 mSmmBwpEfiVar[] = L"SmmBwp";
STATIC CHAR16 mMeModeEfiVar[] = L"MeMode";
STATIC CHAR16 mNetworkBootEfiVar[] = L"NetworkBoot";
STATIC CHAR16 mUsbStackEfiVar[] = L"UsbDriverStack";
STATIC CHAR16 mUsbMassStorageEfiVar[] = L"UsbMassStorage";
STATIC CHAR16 mBootManagerEnabledEfiVar[] = L"BootManagerEnabled";
STATIC CHAR16 mPs2ControllerEfiVar[] = L"Ps2Controller";
STATIC CHAR16 mWatchdogEfiVar[] = L"WatchdogConfig";
STATIC CHAR16 mWatchdogAvailableEfiVar[] = L"WatchdogAvailable";
STATIC CHAR16 mFanCurveOptionEfiVar[] = L"FanCurveOption";
STATIC CHAR16 mIommuConfigEfiVar[] = L"IommuConfig";
STATIC CHAR16 mSleepTypeEfiVar[] = L"SleepType";
STATIC CHAR16 mFirmwareUpdateModeEfiVar[] = L"FirmwareUpdateMode";
STATIC CHAR16 mPowerFailureStateEfiVar[] = L"PowerFailureState";
STATIC CHAR16 mResizeableBarsEnabledEfiVar[] = L"PCIeResizeableBarsEnabled";
STATIC CHAR16 mOptionRomPolicyEfiVar[] = L"OptionRomPolicy";
STATIC CHAR16 mEnableCameraEfiVar[] = L"EnableCamera";
STATIC CHAR16 mEnableWifiBtEfiVar[] = L"EnableWifiBt";
STATIC CHAR16 mBatteryConfigEfiVar[] = L"BatteryConfig";
STATIC CHAR16 mMemoryProfileEfiVar[] = L"MemoryProfile";
STATIC CHAR16 mSerialRedirectionEfiVar[] = L"SerialRedirection";
STATIC CHAR16 mSerialRedirection2EfiVar[] = L"SerialRedirection2";

STATIC BOOLEAN   mUsbStackDefault = TRUE;
STATIC BOOLEAN   mUsbMassStorageDefault = TRUE;
STATIC BOOLEAN   mLockBiosDefault = TRUE;
STATIC BOOLEAN   mSmmBwpDefault = FALSE;
STATIC BOOLEAN   mPs2ControllerDefault = TRUE;
STATIC UINT8     mFanCurveOptionDefault = FAN_CURVE_OPTION_SILENT;
STATIC UINT8     mIommuEnableDefault = FALSE;
STATIC UINT8     mIommuHandoffDefault = FALSE;
STATIC BOOLEAN   mBootManagerEnabledDefault = TRUE;
STATIC UINT8     mSleepTypeDefault = SLEEP_TYPE_S0IX;
STATIC UINT8     mResizeableBarsEnabledDefault = FALSE;
STATIC BOOLEAN   mEnableCameraDefault = TRUE;
STATIC BOOLEAN   mEnableWifiBtDefault = TRUE;
STATIC UINT8     mBatteryStartThresholdDefault = 95;
STATIC UINT8     mBatteryStopThresholdDefault = 98;
STATIC UINT8     mMemoryProfileDefault = MEMORY_PROFILE_JEDEC;

STATIC DASHARO_SYSTEM_FEATURES_PRIVATE_DATA  mDasharoSystemFeaturesPrivate = {
  DASHARO_SYSTEM_FEATURES_PRIVATE_DATA_SIGNATURE,
  NULL,
  NULL,
  {
    DasharoSystemFeaturesExtractConfig,
    DasharoSystemFeaturesRouteConfig,
    DasharoSystemFeaturesCallback
  }
};

STATIC HII_VENDOR_DEVICE_PATH  mDasharoSystemFeaturesHiiVendorDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8) (sizeof (VENDOR_DEVICE_PATH)),
        (UINT8) ((sizeof (VENDOR_DEVICE_PATH)) >> 8)
      }
    },
    DASHARO_SYSTEM_FEATURES_GUID
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      (UINT8) (END_DEVICE_PATH_LENGTH),
      (UINT8) ((END_DEVICE_PATH_LENGTH) >> 8)
    }
  }
};

/**
  This function uses the ACPI SDT protocol to locate an ACPI table.
  It is really only useful for finding tables that only have a single instance,
  e.g. FADT, FACS, MADT, etc.  It is not good for locating SSDT, etc.
  Matches are determined by finding the table with ACPI table that has
  a matching signature.

  @param[in] Signature           - Pointer to an ASCII string containing the OEM Table ID from the ACPI table header
  @param[in, out] Table          - Updated with a pointer to the table
  @param[in, out] Handle         - AcpiSupport protocol table handle for the table found
  @param[in, out] Version        - The version of the table desired

  @retval EFI_SUCCESS            - The function completed successfully.
  @retval EFI_NOT_FOUND          - Failed to locate AcpiTable.
  @retval EFI_NOT_READY          - Not ready to locate AcpiTable.
**/
EFI_STATUS
EFIAPI
LocateAcpiTableBySignature (
  IN      UINT32                        Signature,
  IN OUT  EFI_ACPI_DESCRIPTION_HEADER   **Table
  )
{
  EFI_STATUS                  Status;
  INTN                        Index;
  EFI_ACPI_TABLE_VERSION      Version;
  EFI_ACPI_DESCRIPTION_HEADER *OrgTable;
  EFI_ACPI_SDT_PROTOCOL       *SdtProtocol;
  UINTN                       Handle;

  Status = gBS->LocateProtocol (&gEfiAcpiSdtProtocolGuid, NULL, (VOID **)&SdtProtocol);
  if (EFI_ERROR (Status))
    return Status;

  ///
  /// Locate table with matching ID
  ///
  Version = 0;
  Index = 0;
  Handle = 0;
  do {
    Status = SdtProtocol->GetAcpiTable (
                            Index,
                            (EFI_ACPI_SDT_HEADER **)&OrgTable,
                            &Version,
                            &Handle
                            );
    if (Status == EFI_NOT_FOUND) {
      break;
    }
    ASSERT_EFI_ERROR (Status);
    Index++;
  } while (OrgTable->Signature != Signature);

  if (Status != EFI_NOT_FOUND) {
    *Table = AllocateCopyPool (OrgTable->Length, OrgTable);
    if (*Table == NULL)
      return EFI_OUT_OF_RESOURCES;
  }

  ///
  /// If we found the table, there will be no error.
  ///
  return Status;
}

/**
  This function will be called only if the Watchdog variable is not present.
  It will populate the initial state based on what coreboot has programmed.
  If watchdog was not enabled on first boot, it means it was not enabled,
  and watchdog options should be hidden (WatchdogAvailable == FALSE);
**/
VOID
EFIAPI
GetDefaultWatchdogConfig (
  IN OUT  DASHARO_FEATURES_DATA       *FeaturesData
  )
{
    FeaturesData->WatchdogAvailable = PcdGetBool (PcdShowOcWdtOptions);
    FeaturesData->WatchdogConfig.WatchdogEnable = PcdGetBool (PcdOcWdtEnableDefault);
    FeaturesData->WatchdogConfig.WatchdogTimeout = FixedPcdGet16 (PcdOcWdtTimeoutDefault);
}

/**
  Install Dasharo System Features Menu driver.

  @param ImageHandle          The image handle.
  @param SystemTable          The system table.

  @retval  EFI_SUCEESS        Installed Dasharo System Features.
  @retval  EFI_NOT_SUPPORTED  Dasharo System Features not supported.
  @retval  Other              Error.

**/
EFI_STATUS
EFIAPI
DasharoSystemFeaturesUiLibConstructor (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
)
{
  EFI_STATUS  Status;
  UINTN       BufferSize;

  if (!PcdGetBool (PcdShowMenu))
    return EFI_SUCCESS;

  mDasharoSystemFeaturesPrivate.DriverHandle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
      &mDasharoSystemFeaturesPrivate.DriverHandle,
      &gEfiDevicePathProtocolGuid,
      &mDasharoSystemFeaturesHiiVendorDevicePath,
      &gEfiHiiConfigAccessProtocolGuid,
      &mDasharoSystemFeaturesPrivate.ConfigAccess,
      NULL
      );
  ASSERT_EFI_ERROR (Status);

  // Publish our HII data.
  mDasharoSystemFeaturesPrivate.HiiHandle = HiiAddPackages (
      &gDasharoSystemFeaturesGuid,
      mDasharoSystemFeaturesPrivate.DriverHandle,
      DasharoSystemFeaturesVfrBin,
      DasharoSystemFeaturesUiLibStrings,
      NULL
      );
  ASSERT (mDasharoSystemFeaturesPrivate.HiiHandle != NULL);

  // Set feature visibility
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ShowSecurityMenu = PcdGetBool (PcdShowSecurityMenu);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ShowIntelMeMenu = PcdGetBool (PcdShowIntelMeMenu);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ShowUsbMenu = PcdGetBool (PcdShowUsbMenu);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ShowNetworkMenu = PcdGetBool (PcdShowNetworkMenu);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ShowChipsetMenu = PcdGetBool (PcdShowChipsetMenu);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ShowPowerMenu = PcdGetBool (PcdShowPowerMenu);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ShowPciMenu = PcdGetBool (PcdShowPciMenu);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ShowMemoryMenu = PcdGetBool (PcdShowMemoryMenu);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.PowerMenuShowFanCurve = PcdGetBool (PcdPowerMenuShowFanCurve);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.PowerMenuShowSleepType = PcdGetBool (PcdPowerMenuShowSleepType);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.PowerMenuShowBatteryThresholds = PcdGetBool (PcdPowerMenuShowBatteryThresholds);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.DasharoEnterprise = PcdGetBool (PcdDasharoEnterprise);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SecurityMenuShowIommu = PcdGetBool (PcdShowIommuOptions);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.PciMenuShowResizeableBars = PcdGetBool (PcdPciMenuShowResizeableBars);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ShowSerialPortMenu = PcdGetBool (PcdShowSerialPortMenu);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SecurityMenuShowWiFiBt = PcdGetBool (PcdSecurityShowWiFiBtOption);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SecurityMenuShowCamera = PcdGetBool (PcdSecurityShowCameraOption);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.MeHapAvailable = PcdGetBool (PcdIntelMeHapAvailable);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.S3SupportExperimental = PcdGetBool (PcdS3SupportExperimental);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ShowLockBios = PcdGetBool (PcdShowLockBios);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ShowSmmBwp = PcdGetBool (PcdShowSmmBwp);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ShowFum = PcdGetBool (PcdShowFum);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ShowPs2Option = PcdGetBool (PcdShowPs2Option);
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.Have2ndUart = PcdGetBool (PcdHave2ndUart);

  // Ensure at least one option is visible in given menu (if enabled), otherwise hide it
  if (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ShowSecurityMenu)
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ShowSecurityMenu = PcdGetBool (PcdDasharoEnterprise) ||
                                                                         PcdGetBool (PcdShowIommuOptions) ||
                                                                         PcdGetBool (PcdSecurityShowWiFiBtOption) ||
                                                                         PcdGetBool (PcdSecurityShowCameraOption) ||
                                                                         PcdGetBool (PcdShowLockBios) ||
                                                                         PcdGetBool (PcdShowSmmBwp) ||
                                                                         PcdGetBool (PcdShowFum);

  if (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ShowChipsetMenu)
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ShowChipsetMenu = PcdGetBool (PcdShowOcWdtOptions) ||
                                                                        PcdGetBool (PcdShowPs2Option);
  if (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ShowPowerMenu)
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ShowPowerMenu = PcdGetBool (PcdPowerMenuShowFanCurve) ||
                                                                      PcdGetBool (PcdPowerMenuShowSleepType) ||
                                                                      PcdGetBool (PcdPowerMenuShowBatteryThresholds) ||
                                                                      (FixedPcdGet8 (PcdDefaultPowerFailureState) != POWER_FAILURE_STATE_HIDDEN);

  // Setup feature state
  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.LockBios);
  Status = gRT->GetVariable (
      mLockBiosEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.LockBios
      );

  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.LockBios = mLockBiosDefault;
    Status = gRT->SetVariable (
        mLockBiosEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.LockBios),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.LockBios
        );
    ASSERT_EFI_ERROR (Status);
  }

  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.NetworkBoot);
  Status = gRT->GetVariable (
      mNetworkBootEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.NetworkBoot
      );

  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.NetworkBoot = PcdGetBool (PcdDefaultNetworkBootEnable);
    Status = gRT->SetVariable (
        mNetworkBootEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.NetworkBoot),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.NetworkBoot
        );
    ASSERT_EFI_ERROR (Status);
  }

  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.UsbStack);
  Status = gRT->GetVariable (
      mUsbStackEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.UsbStack
      );

  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.UsbStack = mUsbStackDefault;
    Status = gRT->SetVariable (
        mUsbStackEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.UsbStack),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.UsbStack
        );
    ASSERT_EFI_ERROR (Status);
  }

  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.UsbMassStorage);
  Status = gRT->GetVariable (
      mUsbMassStorageEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.UsbMassStorage
      );

  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.UsbMassStorage = mUsbMassStorageDefault;
    Status = gRT->SetVariable (
        mUsbMassStorageEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.UsbMassStorage),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.UsbMassStorage
        );
    ASSERT_EFI_ERROR (Status);
  }

  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SmmBwp);
  Status = gRT->GetVariable (
      mSmmBwpEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SmmBwp
      );

  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SmmBwp = mSmmBwpDefault;
    Status = gRT->SetVariable (
        mSmmBwpEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SmmBwp),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SmmBwp
        );
    ASSERT_EFI_ERROR (Status);
  }

  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.MeMode);
  Status = gRT->GetVariable (
      mMeModeEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.MeMode
      );

  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.MeMode = FixedPcdGet8(PcdIntelMeDefaultState);
    Status = gRT->SetVariable (
        mMeModeEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.MeMode),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.MeMode
        );
    ASSERT_EFI_ERROR (Status);
  }

  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.OptionRomExecution);
  Status = gRT->GetVariable (
      mOptionRomPolicyEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.OptionRomExecution
      );

  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.OptionRomExecution = PcdGetBool (PcdLoadOptionRoms)
        ? OPTION_ROM_POLICY_ENABLE_ALL
        : OPTION_ROM_POLICY_DISABLE_ALL;
    Status = gRT->SetVariable (
        mOptionRomPolicyEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.OptionRomExecution),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.OptionRomExecution
        );
    ASSERT_EFI_ERROR (Status);
  }

  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.Ps2Controller);
  Status = gRT->GetVariable (
      mPs2ControllerEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.Ps2Controller
      );

  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.Ps2Controller = mPs2ControllerDefault;
    Status = gRT->SetVariable (
        mPs2ControllerEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.Ps2Controller),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.Ps2Controller
        );
    ASSERT_EFI_ERROR (Status);
  }

  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.WatchdogAvailable);
  Status = gRT->GetVariable (
      mWatchdogAvailableEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.WatchdogAvailable
      );

  if (Status == EFI_NOT_FOUND) {
    GetDefaultWatchdogConfig(&mDasharoSystemFeaturesPrivate.DasharoFeaturesData);

    Status = gRT->SetVariable (
        mWatchdogEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.WatchdogConfig),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.WatchdogConfig
        );
    ASSERT_EFI_ERROR (Status);

    Status = gRT->SetVariable (
        mWatchdogAvailableEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.WatchdogAvailable),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.WatchdogAvailable
        );
    ASSERT_EFI_ERROR (Status);
  } else {
    BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.WatchdogConfig);
    Status = gRT->GetVariable (
        mWatchdogEfiVar,
        &gDasharoSystemFeaturesGuid,
        NULL,
        &BufferSize,
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.WatchdogConfig
        );

    if (Status == EFI_NOT_FOUND) {
      Status = gRT->SetVariable (
          mWatchdogEfiVar,
          &gDasharoSystemFeaturesGuid,
          EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
          sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.WatchdogConfig),
          &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.WatchdogConfig
          );
      ASSERT_EFI_ERROR (Status);
    }
  }

  BufferSize = sizeof(mDasharoSystemFeaturesPrivate.DasharoFeaturesData.BootManagerEnabled);
  Status = gRT->GetVariable(
      mBootManagerEnabledEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
	    &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.BootManagerEnabled
      );
  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.BootManagerEnabled = mBootManagerEnabledDefault;
    Status = gRT->SetVariable(
	    mBootManagerEnabledEfiVar,
      &gDasharoSystemFeaturesGuid,
	    EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
	    sizeof(mDasharoSystemFeaturesPrivate.DasharoFeaturesData.BootManagerEnabled),
	    &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.BootManagerEnabled
      );

    ASSERT_EFI_ERROR(Status);
  }

  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.FanCurveOption);
  Status = gRT->GetVariable (
      mFanCurveOptionEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.FanCurveOption
      );

  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.FanCurveOption = mFanCurveOptionDefault;
    Status = gRT->SetVariable (
        mFanCurveOptionEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.FanCurveOption),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.FanCurveOption
        );
    ASSERT_EFI_ERROR (Status);
  }

  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.IommuConfig);
  Status = gRT->GetVariable (
      mIommuConfigEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.IommuConfig
      );

  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.IommuConfig.IommuEnable = mIommuEnableDefault;
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.IommuConfig.IommuHandoff = mIommuHandoffDefault;
    Status = gRT->SetVariable (
        mIommuConfigEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.IommuConfig),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.IommuConfig
        );
    ASSERT_EFI_ERROR (Status);
  }

  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SleepType);
  Status = gRT->GetVariable (
      mSleepTypeEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SleepType
      );

  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SleepType = mSleepTypeDefault;
    Status = gRT->SetVariable (
        mSleepTypeEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SleepType),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SleepType
        );
    ASSERT_EFI_ERROR (Status);
  }

  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.PowerFailureState);
  Status = gRT->GetVariable (
      mPowerFailureStateEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.PowerFailureState
      );

  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.PowerFailureState =
        FixedPcdGet8 (PcdDefaultPowerFailureState);

    Status = gRT->SetVariable (
        mPowerFailureStateEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.PowerFailureState),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.PowerFailureState
        );
    ASSERT_EFI_ERROR (Status);
  }

  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ResizeableBarsEnabled);
  Status = gRT->GetVariable (
      mResizeableBarsEnabledEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ResizeableBarsEnabled
      );

  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ResizeableBarsEnabled = mResizeableBarsEnabledDefault;
    Status = gRT->SetVariable (
        mResizeableBarsEnabledEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ResizeableBarsEnabled),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.ResizeableBarsEnabled
        );
    ASSERT_EFI_ERROR (Status);
  }

  BufferSize = sizeof(mDasharoSystemFeaturesPrivate.DasharoFeaturesData.EnableCamera);
  Status = gRT->GetVariable (
    mEnableCameraEfiVar,
    &gDasharoSystemFeaturesGuid,
    NULL,
    &BufferSize,
    &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.EnableCamera
  );

  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.EnableCamera = mEnableCameraDefault;
    Status = gRT->SetVariable (
        mEnableCameraEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.EnableCamera),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.EnableCamera
        );
    ASSERT_EFI_ERROR (Status);
  }

  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.EnableWifiBt);
  Status = gRT->GetVariable (
      mEnableWifiBtEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.EnableWifiBt
      );

  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.EnableWifiBt = mEnableWifiBtDefault;
    Status = gRT->SetVariable (
        mEnableWifiBtEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.EnableWifiBt),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.EnableWifiBt
        );
    ASSERT_EFI_ERROR (Status);
  }

  BufferSize = sizeof(mDasharoSystemFeaturesPrivate.DasharoFeaturesData.BatteryConfig);
  Status = gRT->GetVariable (
    mBatteryConfigEfiVar,
    &gDasharoSystemFeaturesGuid,
    NULL,
    &BufferSize,
    &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.BatteryConfig
  );

  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.BatteryConfig.StartThreshold = mBatteryStartThresholdDefault;
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.BatteryConfig.StopThreshold = mBatteryStopThresholdDefault;
    Status = gRT->SetVariable (
        mBatteryConfigEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.BatteryConfig),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.BatteryConfig
        );
    ASSERT_EFI_ERROR (Status);
  }

  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.MemoryProfile);
  Status = gRT->GetVariable (
      mMemoryProfileEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.MemoryProfile
      );

  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.MemoryProfile = mMemoryProfileDefault;
    Status = gRT->SetVariable (
        mMemoryProfileEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.MemoryProfile),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.MemoryProfile
        );
    ASSERT_EFI_ERROR (Status);
  }

  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SerialPortRedirection);
  Status = gRT->GetVariable (
      mSerialRedirectionEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SerialPortRedirection
      );

  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SerialPortRedirection = PcdGetBool (PcdSerialRedirectionDefaultState);
    Status = gRT->SetVariable (
        mSerialRedirectionEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SerialPortRedirection),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SerialPortRedirection
        );
    ASSERT_EFI_ERROR (Status);
  }


  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SerialPort2Redirection);
  Status = gRT->GetVariable (
      mSerialRedirection2EfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SerialPort2Redirection
      );

  if (Status == EFI_NOT_FOUND) {
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SerialPort2Redirection = PcdGetBool(PcdHave2ndUart) ?
                                                                                PcdGetBool (PcdSerialRedirection2DefaultState) :
                                                                                FALSE;
    Status = gRT->SetVariable (
        mSerialRedirection2EfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SerialPort2Redirection),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.SerialPort2Redirection
        );
    ASSERT_EFI_ERROR (Status);
  }


  return EFI_SUCCESS;
}

/**
  Unloads the application and its installed protocol.

  @param  ImageHandle     Handle that identifies the image to be unloaded.
  @param  SystemTable     The system table.

  @retval EFI_SUCCESS     The image has been unloaded.
**/
EFI_STATUS
EFIAPI
DasharoSystemFeaturesUiLibDestructor (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
)
{
  EFI_STATUS                  Status;

  Status = gBS->UninstallMultipleProtocolInterfaces (
      mDasharoSystemFeaturesPrivate.DriverHandle,
      &gEfiDevicePathProtocolGuid,
      &mDasharoSystemFeaturesHiiVendorDevicePath,
      &gEfiHiiConfigAccessProtocolGuid,
      &mDasharoSystemFeaturesPrivate.ConfigAccess,
      NULL
      );
  ASSERT_EFI_ERROR (Status);

  HiiRemovePackages (mDasharoSystemFeaturesPrivate.HiiHandle);

  return EFI_SUCCESS;
}

/**
  This function allows a caller to extract the current configuration for one
  or more named elements from the target driver.


  @param This            Points to the EFI_HII_CONFIG_ACCESS_PROTOCOL.
  @param Request         A null-terminated Unicode string in <ConfigRequest> format.
  @param Progress        On return, points to a character in the Request string.
                         Points to the string's null terminator if request was successful.
                         Points to the most recent '&' before the first failing name/value
                         pair (or the beginning of the string if the failure is in the
                         first name/value pair) if the request was not successful.
  @param Results         A null-terminated Unicode string in <ConfigAltResp> format which
                         has all values filled in for the names in the Request string.
                         String to be allocated by the called function.

  @retval  EFI_SUCCESS            The Results is filled with the requested values.
  @retval  EFI_OUT_OF_RESOURCES   Not enough memory to store the results.
  @retval  EFI_INVALID_PARAMETER  Request is illegal syntax, or unknown name.
  @retval  EFI_NOT_FOUND          Routing data doesn't match any storage in this driver.

**/
EFI_STATUS
EFIAPI
DasharoSystemFeaturesExtractConfig (
  IN  CONST EFI_HII_CONFIG_ACCESS_PROTOCOL   *This,
  IN  CONST EFI_STRING                       Request,
  OUT EFI_STRING                             *Progress,
  OUT EFI_STRING                             *Results
  )
{
  EFI_STATUS                            Status;
  DASHARO_SYSTEM_FEATURES_PRIVATE_DATA  *Private;
  UINTN                                 BufferSize;
  EFI_STRING                            ConfigRequestHdr;
  EFI_STRING                            ConfigRequest;
  UINTN                                 Size;

  if (Progress == NULL || Results == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *Progress = Request;
  if (Request != NULL &&
      !HiiIsConfigHdrMatch (Request, &gDasharoSystemFeaturesGuid, mVarStoreName)) {
    return EFI_NOT_FOUND;
  }

  Private = DASHARO_SYSTEM_FEATURES_PRIVATE_DATA_FROM_THIS (This);

  BufferSize = sizeof (DASHARO_FEATURES_DATA);
  ConfigRequest = Request;
  if (Request == NULL || (StrStr (Request, L"OFFSET") == NULL)) {
    // Request has no request element, construct full request string.
    // Allocate and fill a buffer large enough to hold the <ConfigHdr> template
    // followed by "&OFFSET=0&WIDTH=WWWWWWWWWWWWWWWW" followed by a Null-terminator.
    ConfigRequestHdr = HiiConstructConfigHdr (
        &gDasharoSystemFeaturesGuid,
        mVarStoreName,
        Private->DriverHandle
        );
    Size = (StrLen (ConfigRequestHdr) + 32 + 1) * sizeof (CHAR16);
    ConfigRequest = AllocateZeroPool (Size);
    ASSERT (ConfigRequest != NULL);
    UnicodeSPrint (
        ConfigRequest,
        Size,
        L"%s&OFFSET=0&WIDTH=%016LX",
        ConfigRequestHdr,
        (UINT64) BufferSize
        );
    FreePool (ConfigRequestHdr);
  }

  // Convert fields of binary structure to string representation.
  Status = gHiiConfigRouting->BlockToConfig (
      gHiiConfigRouting,
      ConfigRequest,
      (CONST UINT8 *) &Private->DasharoFeaturesData,
      BufferSize,
      Results,
      Progress
      );
  ASSERT_EFI_ERROR (Status);

  // Free config request string if it was allocated.
  if (ConfigRequest != Request) {
    FreePool (ConfigRequest);
  }

  if (Request != NULL && StrStr (Request, L"OFFSET") == NULL) {
    *Progress = Request + StrLen (Request);
  }

  return Status;
}

/**
  This function processes the results of changes in configuration.

  @param This            Points to the EFI_HII_CONFIG_ACCESS_PROTOCOL.
  @param Configuration   A null-terminated Unicode string in <ConfigResp> format.
  @param Progress        A pointer to a string filled in with the offset of the most
                         recent '&' before the first failing name/value pair (or the
                         beginning of the string if the failure is in the first
                         name/value pair) or the terminating NULL if all was successful.

  @retval  EFI_SUCCESS            The Results is processed successfully.
  @retval  EFI_INVALID_PARAMETER  Configuration is NULL.
  @retval  EFI_NOT_FOUND          Routing data doesn't match any storage in this driver.

**/
EFI_STATUS
EFIAPI
DasharoSystemFeaturesRouteConfig (
  IN  CONST EFI_HII_CONFIG_ACCESS_PROTOCOL   *This,
  IN  CONST EFI_STRING                       Configuration,
  OUT EFI_STRING                             *Progress
  )
{
  EFI_STATUS                            Status;
  UINTN                                 BufferSize;
  DASHARO_SYSTEM_FEATURES_PRIVATE_DATA  *Private;
  DASHARO_FEATURES_DATA                 DasharoFeaturesData;

  if (Progress == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *Progress = Configuration;
  if (Configuration == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (!HiiIsConfigHdrMatch (Configuration, &gDasharoSystemFeaturesGuid, mVarStoreName)) {
    return EFI_NOT_FOUND;
  }

  Private = DASHARO_SYSTEM_FEATURES_PRIVATE_DATA_FROM_THIS (This);

  // Construct data structure from configuration string.
  BufferSize = sizeof (DasharoFeaturesData);
  Status = gHiiConfigRouting->ConfigToBlock (
      gHiiConfigRouting,
      Configuration,
      (UINT8 *) &DasharoFeaturesData,
      &BufferSize,
      Progress
      );
  ASSERT_EFI_ERROR (Status);

  if (Private->DasharoFeaturesData.LockBios != DasharoFeaturesData.LockBios) {
    Status = gRT->SetVariable (
        mLockBiosEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.LockBios),
        &DasharoFeaturesData.LockBios
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Private->DasharoFeaturesData.SmmBwp != DasharoFeaturesData.SmmBwp) {
    Status = gRT->SetVariable (
        mSmmBwpEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.SmmBwp),
        &DasharoFeaturesData.SmmBwp
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Private->DasharoFeaturesData.NetworkBoot != DasharoFeaturesData.NetworkBoot) {
    Status = gRT->SetVariable (
        mNetworkBootEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.NetworkBoot),
        &DasharoFeaturesData.NetworkBoot
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Private->DasharoFeaturesData.UsbStack != DasharoFeaturesData.UsbStack) {
    Status = gRT->SetVariable (
        mUsbStackEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.UsbStack),
        &DasharoFeaturesData.UsbStack
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Private->DasharoFeaturesData.UsbMassStorage != DasharoFeaturesData.UsbMassStorage) {
    Status = gRT->SetVariable (
        mUsbMassStorageEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.UsbMassStorage),
        &DasharoFeaturesData.UsbMassStorage
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Private->DasharoFeaturesData.MeMode != DasharoFeaturesData.MeMode) {
    Status = gRT->SetVariable (
        mMeModeEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.MeMode),
        &DasharoFeaturesData.MeMode
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Private->DasharoFeaturesData.Ps2Controller != DasharoFeaturesData.Ps2Controller) {
    Status = gRT->SetVariable (
        mPs2ControllerEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.Ps2Controller),
        &DasharoFeaturesData.Ps2Controller
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Private->DasharoFeaturesData.FanCurveOption != DasharoFeaturesData.FanCurveOption) {
    Status = gRT->SetVariable (
        mFanCurveOptionEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.FanCurveOption),
        &DasharoFeaturesData.FanCurveOption
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Private->DasharoFeaturesData.BootManagerEnabled != DasharoFeaturesData.BootManagerEnabled) {
    Status = gRT->SetVariable (
        mBootManagerEnabledEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.BootManagerEnabled),
        &DasharoFeaturesData.BootManagerEnabled
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Private->DasharoFeaturesData.WatchdogConfig.WatchdogEnable !=
        DasharoFeaturesData.WatchdogConfig.WatchdogEnable ||
      Private->DasharoFeaturesData.WatchdogConfig.WatchdogTimeout !=
        DasharoFeaturesData.WatchdogConfig.WatchdogTimeout) {
    Status = gRT->SetVariable (
        mWatchdogEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.WatchdogConfig),
        &DasharoFeaturesData.WatchdogConfig
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Private->DasharoFeaturesData.IommuConfig.IommuEnable != DasharoFeaturesData.IommuConfig.IommuEnable ||
      Private->DasharoFeaturesData.IommuConfig.IommuHandoff != DasharoFeaturesData.IommuConfig.IommuHandoff) {
    Status = gRT->SetVariable (
        mIommuConfigEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.IommuConfig),
        &DasharoFeaturesData.IommuConfig
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Private->DasharoFeaturesData.SleepType != DasharoFeaturesData.SleepType) {
    Status = gRT->SetVariable (
        mSleepTypeEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.SleepType),
        &DasharoFeaturesData.SleepType
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Private->DasharoFeaturesData.PowerFailureState != DasharoFeaturesData.PowerFailureState) {
    Status = gRT->SetVariable (
        mPowerFailureStateEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.PowerFailureState),
        &DasharoFeaturesData.PowerFailureState
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Private->DasharoFeaturesData.EnableWifiBt != DasharoFeaturesData.EnableWifiBt) {
    Status = gRT->SetVariable (
        mEnableWifiBtEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.EnableWifiBt),
        &DasharoFeaturesData.EnableWifiBt
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Private->DasharoFeaturesData.ResizeableBarsEnabled != DasharoFeaturesData.ResizeableBarsEnabled) {
    Status = gRT->SetVariable (
        mResizeableBarsEnabledEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.ResizeableBarsEnabled),
        &DasharoFeaturesData.ResizeableBarsEnabled
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Private->DasharoFeaturesData.OptionRomExecution != DasharoFeaturesData.OptionRomExecution) {
    Status = gRT->SetVariable (
        mOptionRomPolicyEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.OptionRomExecution),
        &DasharoFeaturesData.OptionRomExecution
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if(Private->DasharoFeaturesData.EnableCamera != DasharoFeaturesData.EnableCamera) {
    Status = gRT->SetVariable (
        mEnableCameraEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.EnableCamera),
        &DasharoFeaturesData.EnableCamera
        );
    if (EFI_ERROR (Status)) {
        return Status;
    }
  }

  if (Private->DasharoFeaturesData.BatteryConfig.StartThreshold !=
        DasharoFeaturesData.BatteryConfig.StartThreshold ||
      Private->DasharoFeaturesData.BatteryConfig.StopThreshold !=
        DasharoFeaturesData.BatteryConfig.StopThreshold) {
    Status = gRT->SetVariable (
        mBatteryConfigEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.BatteryConfig),
        &DasharoFeaturesData.BatteryConfig
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Private->DasharoFeaturesData.MemoryProfile != DasharoFeaturesData.MemoryProfile) {
    Status = gRT->SetVariable (
        mMemoryProfileEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.MemoryProfile),
        &DasharoFeaturesData.MemoryProfile
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Private->DasharoFeaturesData.SerialPortRedirection != DasharoFeaturesData.SerialPortRedirection) {
    Status = gRT->SetVariable (
        mSerialRedirectionEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.SerialPortRedirection),
        &DasharoFeaturesData.SerialPortRedirection
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Private->DasharoFeaturesData.SerialPort2Redirection != DasharoFeaturesData.SerialPort2Redirection) {
    Status = gRT->SetVariable (
        mSerialRedirection2EfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.SerialPort2Redirection),
        &DasharoFeaturesData.SerialPort2Redirection
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Private->DasharoFeaturesData = DasharoFeaturesData;
  return EFI_SUCCESS;
}

/**
  This function is invoked if user selected a interactive opcode from Device Manager's
  Formset. If user toggles bios lock, the new value is saved to EFI variable.

  @param This            Points to the EFI_HII_CONFIG_ACCESS_PROTOCOL.
  @param Action          Specifies the type of action taken by the browser.
  @param QuestionId      A unique value which is sent to the original exporting driver
                         so that it can identify the type of data to expect.
  @param Type            The type of value for the question.
  @param Value           A pointer to the data being sent to the original exporting driver.
  @param ActionRequest   On return, points to the action requested by the callback function.

  @retval  EFI_SUCCESS           The callback successfully handled the action.
  @retval  EFI_INVALID_PARAMETER The setup browser call this function with invalid parameters.

**/
EFI_STATUS
EFIAPI
DasharoSystemFeaturesCallback (
  IN  CONST EFI_HII_CONFIG_ACCESS_PROTOCOL   *This,
  IN  EFI_BROWSER_ACTION                     Action,
  IN  EFI_QUESTION_ID                        QuestionId,
  IN  UINT8                                  Type,
  IN  EFI_IFR_TYPE_VALUE                     *Value,
  OUT EFI_BROWSER_ACTION_REQUEST             *ActionRequest
  )
{
  EFI_STATUS                                 Status;
  EFI_INPUT_KEY                              Key;
  BOOLEAN                                    Enable;

  Enable = TRUE;
  Status = EFI_SUCCESS;

  switch (Action) {
  case EFI_BROWSER_ACTION_DEFAULT_STANDARD:
  case EFI_BROWSER_ACTION_DEFAULT_MANUFACTURING:
    {
      switch (QuestionId) {
      case NETWORK_BOOT_QUESTION_ID:
        {
          if (Value == NULL)
            return EFI_INVALID_PARAMETER;

          Value->b = PcdGetBool (PcdDefaultNetworkBootEnable);
          break;
        }
      case WATCHDOG_ENABLE_QUESTION_ID:
        {
          if (Value == NULL)
            return EFI_INVALID_PARAMETER;

          Value->b = PcdGetBool (PcdOcWdtEnableDefault);
          break;
        }
      case WATCHDOG_TIMEOUT_QUESTION_ID:
        {
          if (Value == NULL)
            return EFI_INVALID_PARAMETER;

          Value->u16 = FixedPcdGet16 (PcdOcWdtTimeoutDefault);
          break;
        }
      case POWER_FAILURE_STATE_QUESTION_ID:
        {
          if (Value == NULL)
            return EFI_INVALID_PARAMETER;

          Value->u8 = FixedPcdGet8 (PcdDefaultPowerFailureState);
          break;
        }
      case OPTION_ROM_STATE_QUESTION_ID:
        {
          if (Value == NULL)
            return EFI_INVALID_PARAMETER;

          Value->u8 = PcdGetBool (PcdLoadOptionRoms) ? OPTION_ROM_POLICY_ENABLE_ALL
                                                          : OPTION_ROM_POLICY_DISABLE_ALL;
          break;
        }
      case SERIAL_PORT_REDIR_QUESTION_ID:
        {
          if (Value == NULL)
            return EFI_INVALID_PARAMETER;

          Value->b = PcdGetBool (PcdSerialRedirectionDefaultState);
          break;
        }
        case SERIAL_PORT2_REDIR_QUESTION_ID:
        {
          if (Value == NULL)
            return EFI_INVALID_PARAMETER;

          if (PcdGetBool (PcdHave2ndUart))
            Value->b = PcdGetBool (PcdSerialRedirection2DefaultState);
          else
            Value->b = FALSE;
          break;
        }
      case BATTERY_START_THRESHOLD_QUESTION_ID:
        {
          if (Value == NULL)
            return EFI_INVALID_PARAMETER;

          Value->u8 = 95;
          break;
        }
      case BATTERY_STOP_THRESHOLD_QUESTION_ID:
        {
          if (Value == NULL)
            return EFI_INVALID_PARAMETER;

          Value->u8 = 98;
          break;
        }
      case INTEL_ME_MODE_QUESTION_ID:
        {
          if (Value == NULL)
            return EFI_INVALID_PARAMETER;

          Value->u8 = FixedPcdGet8(PcdIntelMeDefaultState);
          break;
        }
      case SLEEP_TYPE_QUESTION_ID:
        {
          if (Value == NULL)
            return EFI_INVALID_PARAMETER;

          if (PcdGetBool (PcdSleepTypeDefaultS3))
            Value->u8 = SLEEP_TYPE_S3;
          else
            Value->u8 = SLEEP_TYPE_S0IX;

          break;
        }
      default:
        Status = EFI_UNSUPPORTED;
        break;
      }
      break;
    }
  case EFI_BROWSER_ACTION_CHANGED:
    {
      if (QuestionId == FIRMWARE_UPDATE_MODE_QUESTION_ID) {
        if (!PcdGetBool(PcdShowFum))
          return EFI_UNSUPPORTED;

        do {
          CreatePopUp (
            EFI_BLACK | EFI_BACKGROUND_RED,
            &Key,
            L"",
            L"You are about to enable Firmware Update Mode.",
            L"This will turn off all flash protection mechanisms",
            L"for the duration of the next boot.",
            L"",
            L"DTS will be started automatically through iPXE, please",
            L"make sure an Ethernet cable is connected before continuing.",
            L"",
            L"Press ENTER to continue and reboot or ESC to cancel...",
            L"",
            NULL
            );
        } while ((Key.ScanCode != SCAN_ESC) && (Key.UnicodeChar != CHAR_CARRIAGE_RETURN));

        if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
          Status = gRT->SetVariable (
              mFirmwareUpdateModeEfiVar,
              &gDasharoSystemFeaturesGuid,
              EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
              sizeof (Enable),
              &Enable
              );
          if (EFI_ERROR (Status)) {
            return Status;
          }
          gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
        }
      } else {
        Status = EFI_UNSUPPORTED;
      }
    }
    break;
  default:
    Status = EFI_UNSUPPORTED;
    break;
  }

  return Status;
}
