/** @file
The Dasharo system features reference implementation

Copyright (c) 2022, 3mdeb Sp. z o.o. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause

**/

#include "DasharoSystemFeatures.h"

#include <Library/DasharoVariablesLib.h>

#define PCH_OC_WDT_CTL				0x54
#define   PCH_OC_WDT_CTL_EN			BIT14
#define   PCH_OC_WDT_CTL_TOV_MASK		0x3FF

#define PRIVATE_DATA(field)   mDasharoSystemFeaturesPrivate.DasharoFeaturesData.field

// Feature state
STATIC CHAR16 mVarStoreName[] = L"FeaturesData";

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

  // Set menu visibility
  PRIVATE_DATA(ShowSecurityMenu) = PcdGetBool (PcdShowSecurityMenu);
  PRIVATE_DATA(ShowIntelMeMenu) = PcdGetBool (PcdShowIntelMeMenu);
  PRIVATE_DATA(ShowUsbMenu) = PcdGetBool (PcdShowUsbMenu);
  PRIVATE_DATA(ShowNetworkMenu) = PcdGetBool (PcdShowNetworkMenu);
  PRIVATE_DATA(ShowChipsetMenu) = PcdGetBool (PcdShowChipsetMenu);
  PRIVATE_DATA(ShowPowerMenu) = PcdGetBool (PcdShowPowerMenu);
  PRIVATE_DATA(ShowPciMenu) = PcdGetBool (PcdShowPciMenu);
  PRIVATE_DATA(ShowMemoryMenu) = PcdGetBool (PcdShowMemoryMenu);
  PRIVATE_DATA(ShowSerialPortMenu) = PcdGetBool (PcdShowSerialPortMenu);
  PRIVATE_DATA(ShowCpuMenu) = PcdGetBool (PcdShowCpuMenu);
  // Set feature visibility
  PRIVATE_DATA(PowerMenuShowFanCurve) = PcdGetBool (PcdPowerMenuShowFanCurve);
  PRIVATE_DATA(PowerMenuShowSleepType) = PcdGetBool (PcdPowerMenuShowSleepType);
  PRIVATE_DATA(PowerMenuShowBatteryThresholds) = PcdGetBool (PcdPowerMenuShowBatteryThresholds);
  PRIVATE_DATA(DasharoEnterprise) = PcdGetBool (PcdDasharoEnterprise);
  PRIVATE_DATA(SecurityMenuShowIommu) = PcdGetBool (PcdShowIommuOptions);
  PRIVATE_DATA(PciMenuShowResizeableBars) = PcdGetBool (PcdPciMenuShowResizeableBars);
  PRIVATE_DATA(ShowSerialPortMenu) = PcdGetBool (PcdShowSerialPortMenu);
  PRIVATE_DATA(SecurityMenuShowWiFiBt) = PcdGetBool (PcdSecurityShowWiFiBtOption);
  PRIVATE_DATA(SecurityMenuShowCamera) = PcdGetBool (PcdSecurityShowCameraOption);
  PRIVATE_DATA(MeHapAvailable) = PcdGetBool (PcdIntelMeHapAvailable);
  PRIVATE_DATA(S3SupportExperimental) = PcdGetBool (PcdS3SupportExperimental);
  PRIVATE_DATA(ShowLockBios) = PcdGetBool (PcdShowLockBios);
  PRIVATE_DATA(ShowSmmBwp) = PcdGetBool (PcdShowSmmBwp);
  PRIVATE_DATA(ShowFum) = PcdGetBool (PcdShowFum);
  PRIVATE_DATA(ShowPs2Option) = PcdGetBool (PcdShowPs2Option);
  PRIVATE_DATA(Have2ndUart) = PcdGetBool (PcdHave2ndUart);
  PRIVATE_DATA(ShowCpuThrottlingThreshold) = PcdGetBool (PcdShowCpuThrottlingThreshold);
  PRIVATE_DATA(CpuMaxTemperature) = FixedPcdGet8 (PcdCpuMaxTemperature);
  PRIVATE_DATA(ShowCpuCoreDisable) = PcdGetBool(PcdShowCpuCoreDisable);
  PRIVATE_DATA(ShowCpuHyperThreading) = PcdGetBool(PcdShowCpuHyperThreading);
  PRIVATE_DATA(ShowCpuHyperThreading) = PcdGetBool(PcdShowCpuHyperThreading);

  // Ensure at least one option is visible in given menu (if enabled), otherwise hide it
  if (PRIVATE_DATA(ShowSecurityMenu))
    PRIVATE_DATA(ShowSecurityMenu) = PcdGetBool (PcdDasharoEnterprise) ||
                                     PcdGetBool (PcdShowIommuOptions) ||
                                     PcdGetBool (PcdSecurityShowWiFiBtOption) ||
                                     PcdGetBool (PcdSecurityShowCameraOption) ||
                                     PcdGetBool (PcdShowLockBios) ||
                                     PcdGetBool (PcdShowSmmBwp) ||
                                     PcdGetBool (PcdShowFum);

  if (PRIVATE_DATA(ShowChipsetMenu))
    PRIVATE_DATA(ShowChipsetMenu) = PcdGetBool (PcdShowOcWdtOptions) ||
                                    PcdGetBool (PcdShowPs2Option);

  if (PRIVATE_DATA(ShowPowerMenu))
    PRIVATE_DATA(ShowPowerMenu) = PcdGetBool (PcdPowerMenuShowFanCurve) ||
                                  PcdGetBool (PcdPowerMenuShowSleepType) ||
                                  PcdGetBool (PcdPowerMenuShowBatteryThresholds) ||
                                  PcdGetBool (PcdShowCpuThrottlingThreshold) ||
                                  (FixedPcdGet8 (PcdDefaultPowerFailureState) != POWER_FAILURE_STATE_HIDDEN);

  if (PRIVATE_DATA(ShowCpuMenu))
    PRIVATE_DATA(ShowCpuMenu) = PcdGetBool(PcdShowCpuCoreDisable) ||
                                PcdGetBool(PcdShowCpuHyperThreading);

  GetCpuInfo(&PRIVATE_DATA(BigCoreMaxCount),
             &PRIVATE_DATA(SmallCoreMaxCount),
             &PRIVATE_DATA(HybridCpuArchitecture),
             &PRIVATE_DATA(HyperThreadingSupported));

  if (!PRIVATE_DATA(HybridCpuArchitecture))
    PRIVATE_DATA(CoreMaxCount) = PRIVATE_DATA(BigCoreMaxCount);

#define LOAD_VAR(var, field) do {                                                   \
    BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.field);  \
    Status = gRT->GetVariable (                                                     \
        (var),                                                                      \
        &gDasharoSystemFeaturesGuid,                                                \
        NULL,                                                                       \
        &BufferSize,                                                                \
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.field                    \
        );                                                                          \
    ASSERT_EFI_ERROR (Status);                                                      \
  } while (FALSE)

  LOAD_VAR (DASHARO_VAR_BATTERY_CONFIG, BatteryConfig);
  LOAD_VAR (DASHARO_VAR_BOOT_MANAGER_ENABLED, BootManagerEnabled);
  LOAD_VAR (DASHARO_VAR_CPU_THROTTLING_OFFSET, CpuThrottlingOffset);
  LOAD_VAR (DASHARO_VAR_ENABLE_CAMERA, EnableCamera);
  LOAD_VAR (DASHARO_VAR_ENABLE_WIFI_BT, EnableWifiBt);
  LOAD_VAR (DASHARO_VAR_FAN_CURVE_OPTION, FanCurveOption);
  LOAD_VAR (DASHARO_VAR_IOMMU_CONFIG, IommuConfig);
  LOAD_VAR (DASHARO_VAR_LOCK_BIOS, LockBios);
  LOAD_VAR (DASHARO_VAR_MEMORY_PROFILE, MemoryProfile);
  LOAD_VAR (DASHARO_VAR_ME_MODE, MeMode);
  LOAD_VAR (DASHARO_VAR_NETWORK_BOOT, NetworkBoot);
  LOAD_VAR (DASHARO_VAR_OPTION_ROM_POLICY, OptionRomExecution);
  LOAD_VAR (DASHARO_VAR_POWER_FAILURE_STATE, PowerFailureState);
  LOAD_VAR (DASHARO_VAR_PS2_CONTROLLER, Ps2Controller);
  LOAD_VAR (DASHARO_VAR_RESIZEABLE_BARS_ENABLED, ResizeableBarsEnabled);
  LOAD_VAR (DASHARO_VAR_SERIAL_REDIRECTION, SerialPortRedirection);
  LOAD_VAR (DASHARO_VAR_SERIAL_REDIRECTION2, SerialPort2Redirection);
  LOAD_VAR (DASHARO_VAR_SLEEP_TYPE, SleepType);
  LOAD_VAR (DASHARO_VAR_SMM_BWP, SmmBwp);
  LOAD_VAR (DASHARO_VAR_USB_MASS_STORAGE, UsbMassStorage);
  LOAD_VAR (DASHARO_VAR_USB_STACK, UsbStack);
  LOAD_VAR (DASHARO_VAR_WATCHDOG, WatchdogConfig);
  LOAD_VAR (DASHARO_VAR_WATCHDOG_AVAILABLE, WatchdogAvailable);
  LOAD_VAR (DASHARO_VAR_SMALL_CORE_ACTIVE_COUNT, SmallCoreActiveCount);
  LOAD_VAR (DASHARO_VAR_CORE_ACTIVE_COUNT, BigCoreActiveCount);
  LOAD_VAR (DASHARO_VAR_CORE_ACTIVE_COUNT, CoreActiveCount);
  LOAD_VAR (DASHARO_VAR_HYPER_THREADING, HyperThreading);

#undef LOAD_VAR

  if (PRIVATE_DATA(HybridCpuArchitecture) &&
      PRIVATE_DATA(SmallCoreActiveCount) == 0 &&
      PRIVATE_DATA(BigCoreActiveCount) == 0) {
    /*
     * Invalid setting, which causes a brick, enable all cores. coreboot will
     * not allow to disable all cores and revert to default: enabling all
     * cores. Match the behavior here, so the variables are not stuck in this
     * state and showing variable state not matching the reality.
     */
    PRIVATE_DATA(SmallCoreActiveCount) = DASHARO_CPU_CORES_ENABLE_ALL;
    PRIVATE_DATA(BigCoreActiveCount) = DASHARO_CPU_CORES_ENABLE_ALL;
    gRT->SetVariable (
          DASHARO_VAR_SMALL_CORE_ACTIVE_COUNT,
          &gDasharoSystemFeaturesGuid,
          DasharoGetVariableAttributes (DASHARO_VAR_SMALL_CORE_ACTIVE_COUNT),
          sizeof (PRIVATE_DATA(SmallCoreActiveCount)),
          &PRIVATE_DATA(SmallCoreActiveCount)
          );
    gRT->SetVariable (
          DASHARO_VAR_CORE_ACTIVE_COUNT,
          &gDasharoSystemFeaturesGuid,
          DasharoGetVariableAttributes (DASHARO_VAR_CORE_ACTIVE_COUNT),
          sizeof (PRIVATE_DATA(BigCoreActiveCount)),
          &PRIVATE_DATA(BigCoreActiveCount)
          );
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
  DASHARO_FEATURES_DATA                 *PrivateData;

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
  PrivateData = &Private->DasharoFeaturesData;

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

  if (PrivateData->HybridCpuArchitecture) {
    if (DasharoFeaturesData.SmallCoreActiveCount == 0 && PrivateData->BigCoreMaxCount == 0)
      return EFI_INVALID_PARAMETER;

    if (DasharoFeaturesData.BigCoreActiveCount == 0 && PrivateData->SmallCoreMaxCount == 0)
      return EFI_INVALID_PARAMETER;
  }

  // Can use CompareMem() on structures instead of a per-field comparison as
  // long as they are packed.
#define STORE_VAR(var, field) do {                               \
    if (CompareMem (&Private->DasharoFeaturesData.field,         \
                    &DasharoFeaturesData.field,                  \
                    sizeof (DasharoFeaturesData.field)) != 0) {  \
      Status = gRT->SetVariable (                                \
          (var),                                                 \
          &gDasharoSystemFeaturesGuid,                           \
          DasharoGetVariableAttributes (var),                    \
          sizeof (DasharoFeaturesData.field),                    \
          &DasharoFeaturesData.field                             \
          );                                                     \
      if (EFI_ERROR (Status)) {                                  \
        return Status;                                           \
      }                                                          \
    }                                                            \
  } while (FALSE)

  STORE_VAR (DASHARO_VAR_BATTERY_CONFIG, BatteryConfig);
  STORE_VAR (DASHARO_VAR_BOOT_MANAGER_ENABLED, BootManagerEnabled);
  STORE_VAR (DASHARO_VAR_CPU_THROTTLING_OFFSET, CpuThrottlingOffset);
  STORE_VAR (DASHARO_VAR_ENABLE_CAMERA, EnableCamera);
  STORE_VAR (DASHARO_VAR_ENABLE_WIFI_BT, EnableWifiBt);
  STORE_VAR (DASHARO_VAR_FAN_CURVE_OPTION, FanCurveOption);
  STORE_VAR (DASHARO_VAR_IOMMU_CONFIG, IommuConfig);
  STORE_VAR (DASHARO_VAR_LOCK_BIOS, LockBios);
  STORE_VAR (DASHARO_VAR_MEMORY_PROFILE, MemoryProfile);
  STORE_VAR (DASHARO_VAR_ME_MODE, MeMode);
  STORE_VAR (DASHARO_VAR_NETWORK_BOOT, NetworkBoot);
  STORE_VAR (DASHARO_VAR_OPTION_ROM_POLICY, OptionRomExecution);
  STORE_VAR (DASHARO_VAR_POWER_FAILURE_STATE, PowerFailureState);
  STORE_VAR (DASHARO_VAR_PS2_CONTROLLER, Ps2Controller);
  STORE_VAR (DASHARO_VAR_RESIZEABLE_BARS_ENABLED, ResizeableBarsEnabled);
  STORE_VAR (DASHARO_VAR_SERIAL_REDIRECTION, SerialPortRedirection);
  STORE_VAR (DASHARO_VAR_SERIAL_REDIRECTION2, SerialPort2Redirection);
  STORE_VAR (DASHARO_VAR_SLEEP_TYPE, SleepType);
  STORE_VAR (DASHARO_VAR_SMM_BWP, SmmBwp);
  STORE_VAR (DASHARO_VAR_USB_MASS_STORAGE, UsbMassStorage);
  STORE_VAR (DASHARO_VAR_USB_STACK, UsbStack);
  STORE_VAR (DASHARO_VAR_WATCHDOG, WatchdogConfig);
  STORE_VAR (DASHARO_VAR_WATCHDOG_AVAILABLE, WatchdogAvailable);
  STORE_VAR (DASHARO_VAR_HYPER_THREADING, HyperThreading);

  if (PrivateData->HybridCpuArchitecture) {
    STORE_VAR (DASHARO_VAR_SMALL_CORE_ACTIVE_COUNT, SmallCoreActiveCount);
    STORE_VAR (DASHARO_VAR_CORE_ACTIVE_COUNT, BigCoreActiveCount);
  } else {
    // CoreActiveCount used for P-cores and non-hybrid CPU architectures to match FSP
    STORE_VAR (DASHARO_VAR_CORE_ACTIVE_COUNT, CoreActiveCount);
  }

#undef STORE_VAR

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
      if (Value == NULL)
        return EFI_INVALID_PARAMETER;

      switch (QuestionId) {
      case NETWORK_BOOT_QUESTION_ID:
        Value->b = DasharoGetVariableDefault (DASHARO_VAR_NETWORK_BOOT).Boolean;
        break;
      case WATCHDOG_ENABLE_QUESTION_ID:
        Value->b = DasharoGetVariableDefault (DASHARO_VAR_WATCHDOG).Watchdog.WatchdogEnable;
        break;
      case WATCHDOG_TIMEOUT_QUESTION_ID:
        Value->u16 = DasharoGetVariableDefault (DASHARO_VAR_WATCHDOG).Watchdog.WatchdogTimeout;
        break;
      case POWER_FAILURE_STATE_QUESTION_ID:
        Value->u8 = DasharoGetVariableDefault (DASHARO_VAR_POWER_FAILURE_STATE).Boolean;
        break;
      case OPTION_ROM_STATE_QUESTION_ID:
        Value->u8 = DasharoGetVariableDefault (DASHARO_VAR_OPTION_ROM_POLICY).Uint8;
        break;
      case SERIAL_PORT_REDIR_QUESTION_ID:
        Value->u8 = DasharoGetVariableDefault (DASHARO_VAR_SERIAL_REDIRECTION).Boolean;
        break;
      case SERIAL_PORT2_REDIR_QUESTION_ID:
        Value->b = DasharoGetVariableDefault (DASHARO_VAR_SERIAL_REDIRECTION2).Boolean;
        break;
      case BATTERY_START_THRESHOLD_QUESTION_ID:
        Value->u8 = DasharoGetVariableDefault (DASHARO_VAR_BATTERY_CONFIG).Battery.StartThreshold;
        break;
      case BATTERY_STOP_THRESHOLD_QUESTION_ID:
        Value->u8 = DasharoGetVariableDefault (DASHARO_VAR_BATTERY_CONFIG).Battery.StopThreshold;
        break;
      case INTEL_ME_MODE_QUESTION_ID:
        Value->u8 = DasharoGetVariableDefault (DASHARO_VAR_ME_MODE).Uint8;
        break;
      case SLEEP_TYPE_QUESTION_ID:
        Value->u8 = DasharoGetVariableDefault (DASHARO_VAR_SLEEP_TYPE).Uint8;
        break;
      case HYPER_THREADING_QUESTION_ID:
        Value->b = DasharoGetVariableDefault (DASHARO_VAR_HYPER_THREADING).Boolean;
        break;
      case CPU_THROTTLING_OFFSET_QUESTION_ID:
        Value->u8 = DasharoGetVariableDefault (DASHARO_VAR_CPU_THROTTLING_OFFSET).Uint8;
        break;
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
              DASHARO_VAR_FIRMWARE_UPDATE_MODE,
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
