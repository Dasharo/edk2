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
STATIC CHAR16 mPs2ControllerEfiVar[] = L"Ps2Controller";
STATIC CHAR16 mWatchdogEfiVar[] = L"WatchdogConfig";
STATIC CHAR16 mWatchdogStateEfiVar[] = L"WatchdogAvailable";
STATIC CHAR16 mFanCurveOptionEfiVar[] = L"FanCurveOption";
STATIC CHAR16 mDmaProtectionEfiVar[] = L"DmaProtection";
STATIC CHAR16 mIommuHandoffEfiVar[] = L"IommuHandoff";

STATIC BOOLEAN   mUsbStackDefault = TRUE;
STATIC BOOLEAN   mUsbMassStorageDefault = TRUE;
STATIC BOOLEAN   mLockBiosDefault = TRUE;
STATIC BOOLEAN   mSmmBwpDefault = FALSE;
STATIC UINT8     mMeModeDefault   = ME_MODE_ENABLE;
STATIC BOOLEAN   mPs2ControllerDefault = TRUE;
STATIC UINT8     mFanCurveOptionDefault = FAN_CURVE_OPTION_SILENT;
STATIC BOOLEAN   mDmaProtectionDefault = TRUE;
STATIC BOOLEAN   mIommuHandoffDefault = TRUE;
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
  and watchdog options should be hidden (WatchdogState == FALSE);
**/
VOID
EFIAPI
GetDefaultWatchdogConfig (
  IN OUT  DASHARO_FEATURES_DATA       *FeaturesData
  )
{
  EFI_STATUS                                      Status;
  EFI_ACPI_6_3_FIXED_ACPI_DESCRIPTION_TABLE       *FadtTable;
  UINTN                                           AcpiBase;
  UINT32                                          WatchdogCtl;

  Status = LocateAcpiTableBySignature (
              EFI_ACPI_6_3_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE,
              (EFI_ACPI_DESCRIPTION_HEADER **) &FadtTable
              );
  if (EFI_ERROR (Status) || (FadtTable == NULL)) {
    FeaturesData->WatchdogState = FALSE;
    FeaturesData->WatchdogConfig.WatchdogEnable = FALSE;
    return;
  }

  /* On Intel platforms PM1A Event Block is the ACPI Base */
  AcpiBase = FadtTable->Pm1aEvtBlk;

  /* ACPI size is 0x100 bytes, check for invalid base */
  if (AcpiBase > 0xFF00) {
    FeaturesData->WatchdogState = FALSE;
    FeaturesData->WatchdogConfig.WatchdogEnable = FALSE;
    return;
  }

  WatchdogCtl = IoRead32(AcpiBase + 0x54);

  if (WatchdogCtl & PCH_OC_WDT_CTL_EN) {
    FeaturesData->WatchdogState = TRUE;
    FeaturesData->WatchdogConfig.WatchdogEnable = TRUE;
    /* OC WDT timeout is 0 based (0 means 1 second) so increment to match the VFR */
    FeaturesData->WatchdogConfig.WatchdogTimeout = (WatchdogCtl & PCH_OC_WDT_CTL_TOV_MASK) + 1;
  } else {
    FeaturesData->WatchdogState = FALSE;
    FeaturesData->WatchdogConfig.WatchdogEnable = FALSE;
  }
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
  mDasharoSystemFeaturesPrivate.DasharoFeaturesData.PowerMenuShowFanCurve = PcdGetBool (PcdPowerMenuShowFanCurve);

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
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.MeMode = mMeModeDefault;
    Status = gRT->SetVariable (
        mMeModeEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.MeMode),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.MeMode
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

  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.WatchdogState);
  Status = gRT->GetVariable (
      mWatchdogStateEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.WatchdogState
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
        mWatchdogStateEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.WatchdogState),
        &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.WatchdogState
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

  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.DmaProtection);
  Status = gRT->GetVariable (
      mDmaProtectionEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.DmaProtection
      );

  if (Status == EFI_NOT_FOUND) {
    Status = gRT->SetVariable (
        mDmaProtectionEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mDmaProtectionDefault),
        &mDmaProtectionDefault
        );
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.DmaProtection = mDmaProtectionDefault;
    ASSERT_EFI_ERROR (Status);
  }

  BufferSize = sizeof (mDasharoSystemFeaturesPrivate.DasharoFeaturesData.IommuHandoff);
  Status = gRT->GetVariable (
      mIommuHandoffEfiVar,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &BufferSize,
      &mDasharoSystemFeaturesPrivate.DasharoFeaturesData.IommuHandoff
      );

  if (Status == EFI_NOT_FOUND) {
    Status = gRT->SetVariable (
        mIommuHandoffEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (mIommuHandoffDefault),
        &mIommuHandoffDefault
        );
    mDasharoSystemFeaturesPrivate.DasharoFeaturesData.IommuHandoff = mIommuHandoffDefault;
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

  if (Private->DasharoFeaturesData.DmaProtection != DasharoFeaturesData.DmaProtection) {
    Status = gRT->SetVariable (
        mDmaProtectionEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.DmaProtection),
        &DasharoFeaturesData.DmaProtection
        );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Private->DasharoFeaturesData.IommuHandoff != DasharoFeaturesData.IommuHandoff) {
    Status = gRT->SetVariable (
        mIommuHandoffEfiVar,
        &gDasharoSystemFeaturesGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
        sizeof (DasharoFeaturesData.IommuHandoff),
        &DasharoFeaturesData.IommuHandoff
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
  return EFI_UNSUPPORTED;
}
