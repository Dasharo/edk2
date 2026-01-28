/** @file
  Provides firmware device specific services to support updates of a firmware
  image stored in a firmware device.

  Copyright (c) Microsoft Corporation.<BR>
  Copyright (c) 2018 - 2019, Intel Corporation. All rights reserved.<BR>
  Copyright (c) 2024, 3mdeb Sp. z o.o. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Coreboot.h>
#include <Guid/SystemResourceTable.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BlParseLib.h>
#include <Library/CmosOptionsLib.h>
#include <Library/DebugLib.h>
#include <Library/FmpDeviceLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PopUpLib.h>
#include <Library/PrintLib.h>
#include <Library/SmmStoreLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <PiDxe.h>

#include "Flashing.h"

// Number of times recovery attempts to erase and write a block before moving to
// the next one.
#define BLOCK_RECOVERY_ATTEMPTS  3

typedef struct {
  EFI_GUID  FwGuid;
  UINT32    FwVersion;
  UINT32    FwLsv;
  UINT32    FwSize;
} FwInfo;

/**
  This function requests firmware information on the first call, caches it and
  returns on all calls afterwards.

  @param[out] Info  Place to store a pointer to firmware information.

  @retval EFI_SUCCESS            Info points to firmware information.
  @retval EFI_INVALID_PARAMETER  Info is NULL.
**/
STATIC
EFI_STATUS
GetFwInfo (
  OUT FwInfo  **Info
  )
{
  STATIC FwInfo   Storage;
  STATIC BOOLEAN  Initialized;

  EFI_STATUS  Status;

  if (Info == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (!Initialized) {
    Status = ParseFwInfo (&Storage.FwGuid, &Storage.FwVersion, &Storage.FwLsv, &Storage.FwSize);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a(): ParseFwInfo() failed with: %r\n", __FUNCTION__, Status));
      return Status;
    }

    Initialized = TRUE;
  }

  *Info = &Storage;
  return EFI_SUCCESS;
}

/**
  Provide a function to install the Firmware Management Protocol instance onto a
  device handle when the device is managed by a driver that follows the UEFI
  Driver Model.  If the device is not managed by a driver that follows the UEFI
  Driver Model, then EFI_UNSUPPORTED is returned.

  @param[in] FmpInstaller  Function that installs the Firmware Management
                           Protocol.

  @retval EFI_SUCCESS      The device is managed by a driver that follows the
                           UEFI Driver Model.  FmpInstaller must be called on
                           each Driver Binding Start().
  @retval EFI_UNSUPPORTED  The device is not managed by a driver that follows
                           the UEFI Driver Model.
  @retval other            The Firmware Management Protocol for this firmware
                           device is not installed.  The firmware device is
                           still locked using FmpDeviceLock().
**/
EFI_STATUS
EFIAPI
RegisterFmpInstaller (
  IN FMP_DEVICE_LIB_REGISTER_FMP_INSTALLER  Function
  )
{
  return EFI_UNSUPPORTED;
}

/**
  Provide a function to uninstall the Firmware Management Protocol instance from a
  device handle when the device is managed by a driver that follows the UEFI
  Driver Model.  If the device is not managed by a driver that follows the UEFI
  Driver Model, then EFI_UNSUPPORTED is returned.

  @param[in] FmpUninstaller  Function that installs the Firmware Management
                             Protocol.

  @retval EFI_SUCCESS      The device is managed by a driver that follows the
                           UEFI Driver Model.  FmpUninstaller must be called on
                           each Driver Binding Stop().
  @retval EFI_UNSUPPORTED  The device is not managed by a driver that follows
                           the UEFI Driver Model.
  @retval other            The Firmware Management Protocol for this firmware
                           device is not installed.  The firmware device is
                           still locked using FmpDeviceLock().
**/
EFI_STATUS
EFIAPI
RegisterFmpUninstaller (
  IN FMP_DEVICE_LIB_REGISTER_FMP_UNINSTALLER  Function
  )
{
  return EFI_UNSUPPORTED;
}

/**
  Set the device context for the FmpDeviceLib services when the device is
  managed by a driver that follows the UEFI Driver Model.  If the device is not
  managed by a driver that follows the UEFI Driver Model, then EFI_UNSUPPORTED
  is returned.  Once a device context is set, the FmpDeviceLib services
  operate on the currently set device context.

  @param[in]      Handle   Device handle for the FmpDeviceLib services.
                           If Handle is NULL, then Context is freed.
  @param[in, out] Context  Device context for the FmpDeviceLib services.
                           If Context is NULL, then a new context is allocated
                           for Handle and the current device context is set and
                           returned in Context.  If Context is not NULL, then
                           the current device context is set.

  @retval EFI_SUCCESS      The device is managed by a driver that follows the
                           UEFI Driver Model.
  @retval EFI_UNSUPPORTED  The device is not managed by a driver that follows
                           the UEFI Driver Model.
  @retval other            The Firmware Management Protocol for this firmware
                           device is not installed.  The firmware device is
                           still locked using FmpDeviceLock().
**/
EFI_STATUS
EFIAPI
FmpDeviceSetContext (
  IN EFI_HANDLE  Handle,
  IN OUT VOID    **Context
  )
{
  return EFI_UNSUPPORTED;
}

/**
  Returns the size, in bytes, of the firmware image currently stored in the
  firmware device.  This function is used to by the GetImage() and
  GetImageInfo() services of the Firmware Management Protocol.  If the image
  size can not be determined from the firmware device, then 0 must be returned.

  @param[out] Size  Pointer to the size, in bytes, of the firmware image
                    currently stored in the firmware device.

  @retval EFI_SUCCESS            The size of the firmware image currently
                                 stored in the firmware device was returned.
  @retval EFI_INVALID_PARAMETER  Size is NULL.
  @retval EFI_UNSUPPORTED        The firmware device does not support reporting
                                 the size of the currently stored firmware image.
  @retval EFI_DEVICE_ERROR       An error occurred attempting to determine the
                                 size of the firmware image currently stored in
                                 in the firmware device.
**/
EFI_STATUS
EFIAPI
FmpDeviceGetSize (
  OUT UINTN  *Size
  )
{
  EFI_STATUS  Status;
  FwInfo      *FwInfo;

  if (Size == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = GetFwInfo (&FwInfo);
  if (!EFI_ERROR (Status)) {
    *Size = FwInfo->FwSize;
  }
  return Status;
}

/**
  Returns the GUID value used to fill in the ImageTypeId field of the
  EFI_FIRMWARE_IMAGE_DESCRIPTOR structure that is returned by the GetImageInfo()
  service of the Firmware Management Protocol.  If EFI_UNSUPPORTED is returned,
  then the ImageTypeId field is set to gEfiCallerIdGuid.  If EFI_SUCCESS is
  returned, then ImageTypeId is set to the Guid returned from this function.

  @param[out] Guid  Double pointer to a GUID value that is updated to point to
                    to a GUID value.  The GUID value is not allocated and must
                    not be modified or freed by the caller.

  @retval EFI_SUCCESS      EFI_FIRMWARE_IMAGE_DESCRIPTOR ImageTypeId GUID is set
                           to the returned Guid value.
  @retval EFI_UNSUPPORTED  EFI_FIRMWARE_IMAGE_DESCRIPTOR ImageTypeId GUID is set
                           to gEfiCallerIdGuid.
**/
EFI_STATUS
EFIAPI
FmpDeviceGetImageTypeIdGuidPtr (
  OUT EFI_GUID  **Guid
  )
{
  EFI_STATUS  Status;
  FwInfo      *FwInfo;

  if (Guid == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = GetFwInfo (&FwInfo);
  if (!EFI_ERROR (Status)) {
    *Guid = &FwInfo->FwGuid;
  }
  return Status;
}

/**
  Returns values used to fill in the AttributesSupported and AttributesSettings
  fields of the EFI_FIRMWARE_IMAGE_DESCRIPTOR structure that is returned by the
  GetImageInfo() service of the Firmware Management Protocol.  The following
  bit values from the Firmware Management Protocol may be combined:
    IMAGE_ATTRIBUTE_IMAGE_UPDATABLE
    IMAGE_ATTRIBUTE_RESET_REQUIRED
    IMAGE_ATTRIBUTE_AUTHENTICATION_REQUIRED
    IMAGE_ATTRIBUTE_IN_USE
    IMAGE_ATTRIBUTE_UEFI_IMAGE

  @param[out] Supported  Attributes supported by this firmware device.
  @param[out] Setting    Attributes settings for this firmware device.

  @retval EFI_SUCCESS            The attributes supported by the firmware
                                 device were returned.
  @retval EFI_INVALID_PARAMETER  Supported is NULL.
  @retval EFI_INVALID_PARAMETER  Setting is NULL.
**/
EFI_STATUS
EFIAPI
FmpDeviceGetAttributes (
  OUT UINT64  *Supported,
  OUT UINT64  *Setting
  )
{
  if (Supported == NULL || Setting == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *Supported = IMAGE_ATTRIBUTE_IMAGE_UPDATABLE
             | IMAGE_ATTRIBUTE_RESET_REQUIRED
             | IMAGE_ATTRIBUTE_AUTHENTICATION_REQUIRED
             | IMAGE_ATTRIBUTE_IN_USE;
  *Setting   = *Supported;
  return EFI_SUCCESS;
}

/**
  Returns the value used to fill in the LowestSupportedVersion field of the
  EFI_FIRMWARE_IMAGE_DESCRIPTOR structure that is returned by the GetImageInfo()
  service of the Firmware Management Protocol.  If EFI_SUCCESS is returned, then
  the firmware device supports a method to report the LowestSupportedVersion
  value from the currently stored firmware image.  If the value can not be
  reported for the firmware image currently stored in the firmware device, then
  EFI_UNSUPPORTED must be returned.  EFI_DEVICE_ERROR is returned if an error
  occurs attempting to retrieve the LowestSupportedVersion value for the
  currently stored firmware image.

  @note It is recommended that all firmware devices support a method to report
        the LowestSupportedVersion value from the currently stored firmware
        image.

  @param[out] LowestSupportedVersion  LowestSupportedVersion value retrieved
                                      from the currently stored firmware image.

  @retval EFI_SUCCESS       The lowest supported version of currently stored
                            firmware image was returned in LowestSupportedVersion.
  @retval EFI_UNSUPPORTED   The firmware device does not support a method to
                            report the lowest supported version of the currently
                            stored firmware image.
  @retval EFI_DEVICE_ERROR  An error occurred attempting to retrieve the lowest
                            supported version of the currently stored firmware
                            image.
**/
EFI_STATUS
EFIAPI
FmpDeviceGetLowestSupportedVersion (
  OUT UINT32  *LowestSupportedVersion
  )
{
  EFI_STATUS  Status;
  FwInfo      *FwInfo;

  if (LowestSupportedVersion == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = GetFwInfo (&FwInfo);
  if (!EFI_ERROR (Status)) {
    *LowestSupportedVersion = FwInfo->FwLsv;
  }
  return Status;
}

/**
  Returns the Null-terminated Unicode string that is used to fill in the
  VersionName field of the EFI_FIRMWARE_IMAGE_DESCRIPTOR structure that is
  returned by the GetImageInfo() service of the Firmware Management Protocol.
  The returned string must be allocated using EFI_BOOT_SERVICES.AllocatePool().

  @note It is recommended that all firmware devices support a method to report
        the VersionName string from the currently stored firmware image.

  @param[out] VersionString  The version string retrieved from the currently
                             stored firmware image.

  @retval EFI_SUCCESS            The version string of currently stored
                                 firmware image was returned in Version.
  @retval EFI_INVALID_PARAMETER  VersionString is NULL.
  @retval EFI_UNSUPPORTED        The firmware device does not support a method
                                 to report the version string of the currently
                                 stored firmware image.
  @retval EFI_DEVICE_ERROR       An error occurred attempting to retrieve the
                                 version string of the currently stored
                                 firmware image.
  @retval EFI_OUT_OF_RESOURCES   There are not enough resources to allocate the
                                 buffer for the version string of the currently
                                 stored firmware image.
**/
EFI_STATUS
EFIAPI
FmpDeviceGetVersionString (
  OUT CHAR16  **VersionString
  )
{
  CONST CHAR8  *CbVersion;
  CONST CHAR8  *CbExtraVersion;
  UINTN        Size;

  if (VersionString == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  CbVersion = ParseInfoString (CB_TAG_VERSION);
  CbExtraVersion = ParseInfoString (CB_TAG_EXTRA_VERSION);

  if (CbVersion == NULL) {
    CbVersion = "";
  }

  if (CbExtraVersion == NULL) {
    CbExtraVersion = "";
  }

  Size = (AsciiStrLen (CbVersion) + AsciiStrLen (CbExtraVersion) + 1) * sizeof (CHAR16);
  *VersionString = AllocatePool (Size);
  if (*VersionString == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  UnicodeSPrint (*VersionString, Size, L"%a%a", CbVersion, CbExtraVersion);

  return EFI_SUCCESS;
}

/**
  Returns the value used to fill in the Version field of the
  EFI_FIRMWARE_IMAGE_DESCRIPTOR structure that is returned by the GetImageInfo()
  service of the Firmware Management Protocol.  If EFI_SUCCESS is returned, then
  the firmware device supports a method to report the Version value from the
  currently stored firmware image.  If the value can not be reported for the
  firmware image currently stored in the firmware device, then EFI_UNSUPPORTED
  must be returned.  EFI_DEVICE_ERROR is returned if an error occurs attempting
  to retrieve the LowestSupportedVersion value for the currently stored firmware
  image.

  @note It is recommended that all firmware devices support a method to report
        the Version value from the currently stored firmware image.

  @param[out] Version  The version value retrieved from the currently stored
                       firmware image.

  @retval EFI_SUCCESS       The version of currently stored firmware image was
                            returned in Version.
  @retval EFI_UNSUPPORTED   The firmware device does not support a method to
                            report the version of the currently stored firmware
                            image.
  @retval EFI_DEVICE_ERROR  An error occurred attempting to retrieve the version
                            of the currently stored firmware image.
**/
EFI_STATUS
EFIAPI
FmpDeviceGetVersion (
  OUT UINT32  *Version
  )
{
  EFI_STATUS  Status;
  FwInfo      *FwInfo;

  if (Version == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = GetFwInfo (&FwInfo);
  if (!EFI_ERROR (Status)) {
    *Version = FwInfo->FwVersion;
  }
  return Status;
}

/**
  Returns the value used to fill in the HardwareInstance field of the
  EFI_FIRMWARE_IMAGE_DESCRIPTOR structure that is returned by the GetImageInfo()
  service of the Firmware Management Protocol.  If EFI_SUCCESS is returned, then
  the firmware device supports a method to report the HardwareInstance value.
  If the value can not be reported for the firmware device, then EFI_UNSUPPORTED
  must be returned.  EFI_DEVICE_ERROR is returned if an error occurs attempting
  to retrieve the HardwareInstance value for the firmware device.

  @param[out] HardwareInstance  The hardware instance value for the firmware
                                device.

  @retval EFI_SUCCESS       The hardware instance for the current firmware
                            device is returned in HardwareInstance.
  @retval EFI_UNSUPPORTED   The firmware device does not support a method to
                            report the hardware instance value.
  @retval EFI_DEVICE_ERROR  An error occurred attempting to retrieve the hardware
                            instance value.
**/
EFI_STATUS
EFIAPI
FmpDeviceGetHardwareInstance (
  OUT UINT64  *HardwareInstance
  )
{
  if (HardwareInstance == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *HardwareInstance = 0;
  return EFI_SUCCESS;
}

/**
  Returns a copy of the firmware image currently stored in the firmware device.

  @note It is recommended that all firmware devices support a method to retrieve
        a copy currently stored firmware image.  This can be used to support
        features such as recovery and rollback.

  @param[out]     Image     Pointer to a caller allocated buffer where the
                            currently stored firmware image is copied to.
  @param[in, out] ImageSize Pointer the size, in bytes, of the Image buffer.
                            On return, points to the size, in bytes, of firmware
                            image currently stored in the firmware device.

  @retval EFI_SUCCESS            Image contains a copy of the firmware image
                                 currently stored in the firmware device, and
                                 ImageSize contains the size, in bytes, of the
                                 firmware image currently stored in the
                                 firmware device.
  @retval EFI_BUFFER_TOO_SMALL   The buffer specified by ImageSize is too small
                                 to hold the firmware image currently stored in
                                 the firmware device. The buffer size required
                                 is returned in ImageSize.
  @retval EFI_INVALID_PARAMETER  The Image is NULL.
  @retval EFI_INVALID_PARAMETER  The ImageSize is NULL.
  @retval EFI_UNSUPPORTED        The operation is not supported.
  @retval EFI_DEVICE_ERROR       An error occurred attempting to retrieve the
                                 firmware image currently stored in the firmware
                                 device.
**/
EFI_STATUS
EFIAPI
FmpDeviceGetImage (
  OUT    VOID   *Image,
  IN OUT UINTN  *ImageSize
  )
{
  //
  // This seems useful only if FMP device is part of the running firmware.
  //
  return EFI_UNSUPPORTED;
}

/**
  Checks if a new firmware image is valid for the firmware device.  This
  function allows firmware update operation to validate the firmware image
  before FmpDeviceSetImage() is called.

  @param[in]  Image           Points to a new firmware image.
  @param[in]  ImageSize       Size, in bytes, of a new firmware image.
  @param[out] ImageUpdatable  Indicates if a new firmware image is valid for
                              a firmware update to the firmware device.  The
                              following values from the Firmware Management
                              Protocol are supported:
                                IMAGE_UPDATABLE_VALID
                                IMAGE_UPDATABLE_INVALID
                                IMAGE_UPDATABLE_INVALID_TYPE
                                IMAGE_UPDATABLE_INVALID_OLD
                                IMAGE_UPDATABLE_VALID_WITH_VENDOR_CODE

  @retval EFI_SUCCESS            The image was successfully checked.  Additional
                                 status information is returned in
                                 ImageUpdatable.
  @retval EFI_INVALID_PARAMETER  Image is NULL.
  @retval EFI_INVALID_PARAMETER  ImageUpdatable is NULL.
**/
EFI_STATUS
EFIAPI
FmpDeviceCheckImage (
  IN  CONST VOID  *Image,
  IN  UINTN       ImageSize,
  OUT UINT32      *ImageUpdatable
  )
{
  UINT32  LastAttemptStatus;

  return FmpDeviceCheckImageWithStatus (Image, ImageSize, ImageUpdatable, &LastAttemptStatus);
}

/**
  Checks if a new firmware image is valid for the firmware device.  This
  function allows firmware update operation to validate the firmware image
  before FmpDeviceSetImage() is called.

  @param[in]  Image               Points to a new firmware image.
  @param[in]  ImageSize           Size, in bytes, of a new firmware image.
  @param[out] ImageUpdatable      Indicates if a new firmware image is valid for
                                  a firmware update to the firmware device.  The
                                  following values from the Firmware Management
                                  Protocol are supported:
                                    IMAGE_UPDATABLE_VALID
                                    IMAGE_UPDATABLE_INVALID
                                    IMAGE_UPDATABLE_INVALID_TYPE
                                    IMAGE_UPDATABLE_INVALID_OLD
                                    IMAGE_UPDATABLE_VALID_WITH_VENDOR_CODE
  @param[out] LastAttemptStatus   A pointer to a UINT32 that holds the last attempt
                                  status to report back to the ESRT table in case
                                  of error.

                                  The return status code must fall in the range of
                                  LAST_ATTEMPT_STATUS_DEVICE_LIBRARY_MIN_ERROR_CODE_VALUE to
                                  LAST_ATTEMPT_STATUS_DEVICE_LIBRARY_MAX_ERROR_CODE_VALUE.

                                  If the value falls outside this range, it will be converted
                                  to LAST_ATTEMPT_STATUS_ERROR_UNSUCCESSFUL.

  @retval EFI_SUCCESS            The image was successfully checked.  Additional
                                 status information is returned in
                                 ImageUpdatable.
  @retval EFI_INVALID_PARAMETER  Image is NULL.
  @retval EFI_INVALID_PARAMETER  ImageUpdatable is NULL.
**/
EFI_STATUS
EFIAPI
FmpDeviceCheckImageWithStatus (
  IN  CONST VOID  *Image,
  IN  UINTN       ImageSize,
  OUT UINT32      *ImageUpdatable,
  OUT UINT32      *LastAttemptStatus
  )
{
  EFI_STATUS  Status;
  UINTN       FwSize;
  UINTN       BlockSize;

  if (Image == NULL || ImageUpdatable == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *LastAttemptStatus = LAST_ATTEMPT_STATUS_ERROR_UNSUCCESSFUL;
  *ImageUpdatable    = IMAGE_UPDATABLE_INVALID;

  Status = FmpDeviceGetSize (&FwSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a(): FmpDeviceGetSize() failed with: %r\n",
            __FUNCTION__, Status));
    return Status;
  }

  if (ImageSize != FwSize) {
    DEBUG ((DEBUG_ERROR, "%a(): image size (0x%x) doesn't match firmware size (0x%x)\n",
           __FUNCTION__, ImageSize, FwSize));
    return EFI_ABORTED;
  }

  Status = SmmStoreLibGetBlockSize (&BlockSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a(): SmmStoreLibGetBlockSize() failed with: %r\n",
            __FUNCTION__, Status));
    return Status;
  }

  if (FwSize % BlockSize != 0) {
    DEBUG ((DEBUG_ERROR, "%a(): firmware size (0x%x) is not a multiple of block size (0x%x)\n",
           __FUNCTION__, FwSize, BlockSize));
    return EFI_ABORTED;
  }

  *LastAttemptStatus = LAST_ATTEMPT_STATUS_SUCCESS;
  *ImageUpdatable    = IMAGE_UPDATABLE_VALID;
  return EFI_SUCCESS;
}

/**
  Updates a firmware device with a new firmware image.  This function returns
  EFI_UNSUPPORTED if the firmware image is not updatable.  If the firmware image
  is updatable, the function should perform the following minimal validations
  before proceeding to do the firmware image update.
    - Validate that the image is a supported image for this firmware device.
      Return EFI_ABORTED if the image is not supported.  Additional details
      on why the image is not a supported image may be returned in AbortReason.
    - Validate the data from VendorCode if is not NULL.  Firmware image
      validation must be performed before VendorCode data validation.
      VendorCode data is ignored or considered invalid if image validation
      fails.  Return EFI_ABORTED if the VendorCode data is invalid.

  VendorCode enables vendor to implement vendor-specific firmware image update
  policy.  Null if the caller did not specify the policy or use the default
  policy.  As an example, vendor can implement a policy to allow an option to
  force a firmware image update when the abort reason is due to the new firmware
  image version is older than the current firmware image version or bad image
  checksum.  Sensitive operations such as those wiping the entire firmware image
  and render the device to be non-functional should be encoded in the image
  itself rather than passed with the VendorCode.  AbortReason enables vendor to
  have the option to provide a more detailed description of the abort reason to
  the caller.

  @param[in]  Image             Points to the new firmware image.
  @param[in]  ImageSize         Size, in bytes, of the new firmware image.
  @param[in]  VendorCode        This enables vendor to implement vendor-specific
                                firmware image update policy.  NULL indicates
                                the caller did not specify the policy or use the
                                default policy.
  @param[in]  Progress          A function used to report the progress of
                                updating the firmware device with the new
                                firmware image.
  @param[in]  CapsuleFwVersion  The version of the new firmware image from the
                                update capsule that provided the new firmware
                                image.
  @param[out] AbortReason       A pointer to a pointer to a Null-terminated
                                Unicode string providing more details on an
                                aborted operation. The buffer is allocated by
                                this function with
                                EFI_BOOT_SERVICES.AllocatePool().  It is the
                                caller's responsibility to free this buffer with
                                EFI_BOOT_SERVICES.FreePool().

  @retval EFI_SUCCESS            The firmware device was successfully updated
                                 with the new firmware image.
  @retval EFI_ABORTED            The operation is aborted.  Additional details
                                 are provided in AbortReason.
  @retval EFI_INVALID_PARAMETER  The Image was NULL.
  @retval EFI_UNSUPPORTED        The operation is not supported.
**/
EFI_STATUS
EFIAPI
FmpDeviceSetImage (
  IN  CONST VOID                                     *Image,
  IN  UINTN                                          ImageSize,
  IN  CONST VOID                                     *VendorCode        OPTIONAL,
  IN  EFI_FIRMWARE_MANAGEMENT_UPDATE_IMAGE_PROGRESS  Progress           OPTIONAL,
  IN  UINT32                                         CapsuleFwVersion,
  OUT CHAR16                                         **AbortReason
  )
{
  UINT32  LastAttemptStatus;

  return FmpDeviceSetImageWithStatus (
           Image,
           ImageSize,
           VendorCode,
           Progress,
           CapsuleFwVersion,
           AbortReason,
           &LastAttemptStatus
           );
}

//
// Weight constants for progress calculation.
// Reading 8 blocks takes approximately the same time as 1 erase+write operation.
//
#define READ_BLOCK_WEIGHT   1
#define UPDATE_BLOCK_WEIGHT  8

/**
  Context structure for tracking progress during firmware update.
**/
typedef struct {
  EFI_FIRMWARE_MANAGEMENT_UPDATE_IMAGE_PROGRESS  Callback;
  UINTN   TotalUnits;
  UINTN   CurrentUnits;
  BOOLEAN ShouldReport;
} ProgressContext;

/**
  Reports current progress as a percentage.

  @param[in, out] Ctx  Progress context to update and report from.
**/
STATIC
VOID
ReportProgress (
  IN OUT ProgressContext  *Ctx
  )
{
  EFI_STATUS  Status;
  UINTN       Progress;

  if (!Ctx->ShouldReport || Ctx->Callback == NULL) {
    Ctx->ShouldReport = FALSE;
    return;
  }

  if (Ctx->TotalUnits == 0) {
    return;
  }

  Progress = (Ctx->CurrentUnits * 100) / Ctx->TotalUnits;

  //
  // Value of 0 means "progress reporting is not supported", so avoid using it.
  //
  if (Progress == 0) {
    Progress = 1;
  }

  //
  // Cap at 100%
  //
  if (Progress > 100) {
    Progress = 100;
  }

  Status = Ctx->Callback (Progress);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "%a(): progress callback failed with: %r\n",
            __FUNCTION__, Status));
    Ctx->ShouldReport = FALSE;
  }
}

/**
  Callback invoked after each block is read during firmware reading.

  @param[in] CurrentBlock   The block that was just read (0-indexed).
  @param[in] TotalBlocks    Total number of blocks to read.
  @param[in] Context        Pointer to ProgressContext.
**/
STATIC
VOID
ReadProgressCallback (
  UINTN CurrentBlock,
  UINTN TotalBlocks,
  VOID  *Context
  )
{
  ProgressContext  *Ctx;

  if (Context == NULL) {
    return;
  }

  Ctx = (ProgressContext *)Context;
  Ctx->CurrentUnits += READ_BLOCK_WEIGHT;
  ReportProgress (Ctx);
}

/**
  Counts the number of blocks that differ between two firmware images.

  @param[in] Image1     First image to compare.
  @param[in] Image2     Second image to compare.
  @param[in] Size       Total size of images in bytes.
  @param[in] BlockSize  Size of each block in bytes.

  @return Number of blocks that differ between the two images.
**/
STATIC
UINTN
CountDifferingBlocks (
  IN CONST UINT8  *Image1,
  IN CONST UINT8  *Image2,
  IN UINTN        Size,
  IN UINTN        BlockSize,
  IN BOOLEAN      DescriptorLocked
  )
{
  UINTN  BlockCount;
  UINTN  Block;
  UINTN  DifferingCount;
  UINTN  Offset;

  if (BlockSize == 0) {
    return 0;
  }

  BlockCount = Size / BlockSize;
  DifferingCount = 0;
  Offset = 0;

  for (Block = 0; Block < BlockCount; Block++, Offset += BlockSize) {
    if (CompareMem (Image1 + Offset, Image2 + Offset, BlockSize) == 0) {
      continue;
    }

    if (!IsRangeWriteable (Image1, Size, Offset, BlockSize)) {
      continue;
    }

    DifferingCount++;
  }  

  return DifferingCount;
}

/**
  This code finds variable in storage blocks (Volatile or Non-Volatile).

  @param[in]      VariableName               Name of Variable to be found.
  @param[in]      VendorGuid                 Variable vendor GUID.
  @param[out]     Attributes                 Attribute value of the variable found.
  @param[in, out] DataSize                   Size of Data found. If size is less than the
                                             data, this value contains the required size.
  @param[out]     Data                       Data pointer.

  @return EFI_INVALID_PARAMETER     Invalid parameter.
  @return EFI_SUCCESS               Find the specified variable.
  @return EFI_NOT_FOUND             Not found.
  @return EFI_BUFFER_TO_SMALL       DataSize is too small for the result.
**/
STATIC
EFI_STATUS
EFIAPI
GetVariableHook (
  IN      CHAR16    *VariableName,
  IN      EFI_GUID  *VendorGuid,
  OUT     UINT32    *Attributes OPTIONAL,
  IN OUT  UINTN     *DataSize,
  OUT     VOID      *Data
  )
{
  DEBUG ((DEBUG_INFO, "%a(): %g:%S\n",
          __FUNCTION__, VendorGuid, VariableName));
  return EFI_NOT_AVAILABLE_YET;
}

/**
  This code Finds the Next available variable.

  @param[in, out] VariableNameSize           Size of the variable name.
  @param[in, out] VariableName               Pointer to variable name.
  @param[in, out] VendorGuid                 Variable Vendor Guid.

  @return EFI_INVALID_PARAMETER     Invalid parameter.
  @return EFI_SUCCESS               Find the specified variable.
  @return EFI_NOT_FOUND             Not found.
  @return EFI_BUFFER_TO_SMALL       DataSize is too small for the result.
**/
STATIC
EFI_STATUS
EFIAPI
GetNextVariableNameHook (
  IN OUT  UINTN     *VariableNameSize,
  IN OUT  CHAR16    *VariableName,
  IN OUT  EFI_GUID  *VendorGuid
  )
{
  DEBUG ((DEBUG_INFO, "%a(): %g:%S\n",
          __FUNCTION__, VendorGuid, VariableName));
  return EFI_NOT_AVAILABLE_YET;
}

/**
  This code sets variable in storage blocks (Volatile or Non-Volatile).

  @param[in] VariableName                     Name of Variable to be found.
  @param[in] VendorGuid                       Variable vendor GUID.
  @param[in] Attributes                       Attribute value of the variable found
  @param[in] DataSize                         Size of Data found. If size is less than the
                                              data, this value contains the required size.
  @param[in] Data                             Data pointer.

  @return EFI_INVALID_PARAMETER           Invalid parameter.
  @return EFI_SUCCESS                     Set successfully.
  @return EFI_OUT_OF_RESOURCES            Resource not enough to set variable.
  @return EFI_NOT_FOUND                   Not found.
  @return EFI_WRITE_PROTECTED             Variable is read-only.
**/
STATIC
EFI_STATUS
EFIAPI
SetVariableHook (
  IN CHAR16    *VariableName,
  IN EFI_GUID  *VendorGuid,
  IN UINT32    Attributes,
  IN UINTN     DataSize,
  IN VOID      *Data
  )
{
  DEBUG ((DEBUG_INFO, "%a(): %g:%S, 0x%x bytes, 0x%x\n",
          __FUNCTION__, VendorGuid, VariableName, DataSize, Attributes));
  return EFI_NOT_AVAILABLE_YET;
}

/**
  This code returns information about the EFI variables.

  @param[in]  Attributes                     Attributes bitmask to specify the type of variables
                                             on which to return information.
  @param[out] MaximumVariableStorageSize     Pointer to the maximum size of the storage space available
                                             for the EFI variables associated with the attributes specified.
  @param[out] RemainingVariableStorageSize   Pointer to the remaining size of the storage space available
                                             for EFI variables associated with the attributes specified.
  @param[out] MaximumVariableSize            Pointer to the maximum size of an individual EFI variables
                                             associated with the attributes specified.

  @return EFI_SUCCESS                   Query successfully.
**/
STATIC
EFI_STATUS
EFIAPI
QueryVariableInfoHook (
  IN  UINT32  Attributes,
  OUT UINT64  *MaximumVariableStorageSize,
  OUT UINT64  *RemainingVariableStorageSize,
  OUT UINT64  *MaximumVariableSize
  )
{
  DEBUG ((DEBUG_INFO, "%a(): 0x%x\n", __FUNCTION__, Attributes));
  return EFI_NOT_AVAILABLE_YET;
}

STATIC
VOID
HexDump (
  IN UINT8 *InputData,
  IN UINTN InputLen,
  OUT CHAR16 *OutputStr
  )
{
  STATIC CONST CHAR8 HexMap[] = "0123456789ABCDEF";
  for (UINTN i = 0; i < InputLen; i++) {
    OutputStr[i * 2]     = HexMap[InputData[i] >> 4];
    OutputStr[i * 2 + 1] = HexMap[InputData[i] & 0x0F];
  }
  OutputStr[InputLen * 2] = '\0';
}

STATIC
VOID
ShowBtgErrorPopup (
  IN VOID   *CurrentImage,
  IN UINTN  ImageSize,
  IN CHAR16 *ErrorMessage
  )
{
  EFI_STATUS     Status;
  UINT8          *CurrentOemRk;
  UINT16         CurrentOemRkString[48 * 2 + 1];
  UINTN          CurrentAttribute;
  BOOLEAN        CursorVisible;
  EFI_EVENT      Events[1];
  UINTN          Index;
  EFI_INPUT_KEY  Key;

  Status = GetOemRootKeyHash(CurrentImage, ImageSize, &CurrentOemRk);
  ASSERT_EFI_ERROR (Status);

  HexDump (CurrentOemRk, 48, CurrentOemRkString);

  CurrentAttribute = gST->ConOut->Mode->Attribute;
  CursorVisible    = gST->ConOut->Mode->CursorVisible;

  gST->ConOut->EnableCursor (gST->ConOut, FALSE);

  DrainInput ();

  Events[0] = gST->ConIn->WaitForKey;

  do {
    DrawGraphicPopUp (
        ErrorPopUp,
        96,
        13,
        L"Firmware Update Skipped",
        L"",
        L"The firmware update is not compatible with this device's security settings.",
        L"The update has been skipped to prevent system instability. No changes were made.",
        L"Please contact us at support@dasharo.com with a photo of the screen.",
        L"",
        L"Abort reason:",
        ErrorMessage,
        L"",
        L"Fused OEM RK Hash:",
        CurrentOemRkString,
        L"",
        L"Press ENTER to boot normally."
        );

    Status = gBS->WaitForEvent (1, Events, &Index);
    ASSERT_EFI_ERROR (Status);

    if (Index == 0) {
      Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      ASSERT_EFI_ERROR (Status);
    }
  } while (Key.UnicodeChar != CHAR_CARRIAGE_RETURN);

  gST->ConOut->EnableCursor (gST->ConOut, CursorVisible);
  gST->ConOut->SetAttribute (gST->ConOut, CurrentAttribute);

  gST->ConOut->ClearScreen (gST->ConOut);
  DrainInput ();
}

/**
  Attempts to restore firmare to its previous state.

  @param[in] OriginalImage     Firmware image to revert to.
  @param[in] ImageSize         Size of the image in bytes.
  @param[in] BlockSize         Size of a single block.
  @param[in] BlockCount        Number of leading blocks to recover.
  @param[in] DescriptorLocked  Whether IFD is locked (when locked, some ranges
                               become read-only).

  @retval TRUE   All requested blocks were reverted to their original state.
  @retval FALSE  Recovery aborted or at least one block wasn't recovered.
**/
STATIC
BOOLEAN
AttemptRecovery (
  IN UINT8    *OriginalImage,
  IN UINTN    ImageSize,
  IN UINTN    BlockSize,
  IN UINTN    BlockCount,
  IN BOOLEAN  DescriptorLocked
  )
{
  EFI_STATUS  Status;
  UINT8       *CurrentImage;
  UINTN       Offset;
  UINTN       Block;
  UINTN       NumBytes;
  UINTN       Try;

  if (!FixedPcdGetBool (PcdCapsuleRecovery)) {
    DEBUG ((DEBUG_INFO, "%a(): recovery is not enabled\n", __FUNCTION__));
    return FALSE;
  }

  CurrentImage = ReadCurrentFirmware (DescriptorLocked, NULL, NULL);
  if (CurrentImage == NULL) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to read current firmware the first time\n",
      __FUNCTION__
      ));
    return FALSE;
  }

  Offset = 0;
  for (Block = 0; Block < BlockCount; Block++, Offset += BlockSize) {
    //
    // Skip blocks that didn't change.
    //
    if (CompareMem (CurrentImage + Offset, OriginalImage + Offset, BlockSize) == 0) {
      continue;
    }

    //
    // We weren't touching blocks unavailable for writing, so they should not
    // need a recovery.  Note the use of the original image as the source of
    // information about ranges.
    //
    if (DescriptorLocked && !IsRangeWriteable (OriginalImage, ImageSize, Offset, BlockSize)) {
      continue;
    }

    //
    // Try recovering each block more than once in case that helps.  Should do
    // no harm other than taking more time, but it's worth a chance of avoiding
    // bricking the system.
    //
    for (Try = 0; Try < BLOCK_RECOVERY_ATTEMPTS; Try++) {
      //
      // Unlike normal writing in FmpDeviceSetImageWithStatus(), do not stop at
      // errors in a hope that whatever gets written is enough for having a
      // bootable firmware.  Still, don't write blocks that weren't erased.
      //
      Status = SmmStoreLibEraseAnyBlock (Block);
      if (!EFI_ERROR (Status)) {
        NumBytes = BlockSize;
        (VOID)SmmStoreLibWriteAnyBlock (Block, 0, &NumBytes, OriginalImage + Offset);
        break;
      }
    }
  }

  FreePool (CurrentImage);

  //
  // Judge outcome of the recovery by re-reading the firmware and comparing it
  // against the original.
  //
  CurrentImage = ReadCurrentFirmware (DescriptorLocked, NULL, NULL);
  if (CurrentImage == NULL) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to read current firmware the second time\n",
      __FUNCTION__
      ));
    return FALSE;
  }

  Offset = 0;
  for (Block = 0; Block < BlockCount; Block++, Offset += BlockSize) {
    //
    // Blocks unavailable for writing may change contents on their own, hence
    // they must be excluded from the comparison.
    //
    if (DescriptorLocked && !IsRangeWriteable (OriginalImage, ImageSize, Offset, BlockSize)) {
      continue;
    }

    //
    // Consider any mismatch an indication of a failed recovery.  Examining any
    // more blocks won't change the result.
    //
    if (CompareMem (CurrentImage + Offset, OriginalImage + Offset, BlockSize) != 0) {
      break;
    }
  }

  FreePool (CurrentImage);

  // Success is defined as all writable blocks within the range being equal to
  // their initial state.
  return Block == BlockCount;
}

/**
  Updates a firmware device with a new firmware image.  This function returns
  EFI_UNSUPPORTED if the firmware image is not updatable.  If the firmware image
  is updatable, the function should perform the following minimal validations
  before proceeding to do the firmware image update.
    - Validate that the image is a supported image for this firmware device.
      Return EFI_ABORTED if the image is not supported.  Additional details
      on why the image is not a supported image may be returned in AbortReason.
    - Validate the data from VendorCode if is not NULL.  Firmware image
      validation must be performed before VendorCode data validation.
      VendorCode data is ignored or considered invalid if image validation
      fails.  Return EFI_ABORTED if the VendorCode data is invalid.

  VendorCode enables vendor to implement vendor-specific firmware image update
  policy.  Null if the caller did not specify the policy or use the default
  policy.  As an example, vendor can implement a policy to allow an option to
  force a firmware image update when the abort reason is due to the new firmware
  image version is older than the current firmware image version or bad image
  checksum.  Sensitive operations such as those wiping the entire firmware image
  and render the device to be non-functional should be encoded in the image
  itself rather than passed with the VendorCode.  AbortReason enables vendor to
  have the option to provide a more detailed description of the abort reason to
  the caller.

  @param[in]  Image             Points to the new firmware image.
  @param[in]  ImageSize         Size, in bytes, of the new firmware image.
  @param[in]  VendorCode        This enables vendor to implement vendor-specific
                                firmware image update policy.  NULL indicates
                                the caller did not specify the policy or use the
                                default policy.
  @param[in]  Progress          A function used to report the progress of
                                updating the firmware device with the new
                                firmware image.
  @param[in]  CapsuleFwVersion  The version of the new firmware image from the
                                update capsule that provided the new firmware
                                image.
  @param[out] AbortReason       A pointer to a pointer to a Null-terminated
                                Unicode string providing more details on an
                                aborted operation. The buffer is allocated by
                                this function with
                                EFI_BOOT_SERVICES.AllocatePool().  It is the
                                caller's responsibility to free this buffer with
                                EFI_BOOT_SERVICES.FreePool().
  @param[out] LastAttemptStatus A pointer to a UINT32 that holds the last attempt
                                status to report back to the ESRT table in case
                                of error. This value will only be checked when this
                                function returns an error.

                                The return status code must fall in the range of
                                LAST_ATTEMPT_STATUS_DEVICE_LIBRARY_MIN_ERROR_CODE_VALUE to
                                LAST_ATTEMPT_STATUS_DEVICE_LIBRARY_MAX_ERROR_CODE_VALUE.

                                If the value falls outside this range, it will be converted
                                to LAST_ATTEMPT_STATUS_ERROR_UNSUCCESSFUL.

  @retval EFI_SUCCESS            The firmware device was successfully updated
                                 with the new firmware image.
  @retval EFI_ABORTED            The operation is aborted.  Additional details
                                 are provided in AbortReason.
  @retval EFI_INVALID_PARAMETER  The Image was NULL.
  @retval EFI_UNSUPPORTED        The operation is not supported.
**/
EFI_STATUS
EFIAPI
FmpDeviceSetImageWithStatus (
  IN  CONST VOID                                     *Image,
  IN  UINTN                                          ImageSize,
  IN  CONST VOID                                     *VendorCode        OPTIONAL,
  IN  EFI_FIRMWARE_MANAGEMENT_UPDATE_IMAGE_PROGRESS  Progress           OPTIONAL,
  IN  UINT32                                         CapsuleFwVersion,
  OUT CHAR16                                         **AbortReason,
  OUT UINT32                                         *LastAttemptStatus
  )
{
  EFI_STATUS       Status;
  UINTN            BlockSize;
  UINTN            BlockCount;
  UINTN            Block;
  UINTN            NumBytes;
  UINT8            *CurrentImage;
  UINT8            *UpdatedImage;
  UINTN            Offset;
  BOOLEAN          DescriptorLocked;
  CHAR16           *ErrorString = NULL;
  UINT32           AttemptSlotB;
  ProgressContext  ProgressCtx;
  UINTN            DifferingBlocks;
  UINTN            WrittenBlocks;
  UINTN            ScaledWriteWeight;
  BOOLEAN          SmoothProgress;

  *LastAttemptStatus = LAST_ATTEMPT_STATUS_ERROR_UNSUCCESSFUL;

  //
  // FmpDeviceCheckImageWithStatus() has already validated the image, so not
  // repeating the checks.  However, could move the checks here to be able to
  // report abort reason which can't be done in FmpDeviceCheckImageWithStatus().
  //

  Status = SmmStoreLibGetBlockSize (&BlockSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a(): SmmStoreLibGetBlockSize() failed with: %r\n",
            __FUNCTION__, Status));
    return Status;
  }

  BlockCount = ImageSize / BlockSize;
  DEBUG ((DEBUG_INFO, "%a(): 0x%x blocks of 0x%x bytes\n",
          __FUNCTION__, BlockCount, BlockSize));

  SmoothProgress = FixedPcdGetBool (PcdSmoothCapsuleProgress);

  //
  // Initialize progress context.
  // Phase 1 (reading): BlockCount * READ_BLOCK_WEIGHT units
  // Phase 2 (writing): BlockCount * UPDATE_BLOCK_WEIGHT units (worst case)
  //
  // When SmoothProgress is enabled, TotalUnits is recalculated after merge
  // based on actual differing blocks for accurate progress reporting.
  //
  ProgressCtx.Callback = Progress;
  ProgressCtx.CurrentUnits = 0;
  ProgressCtx.TotalUnits = BlockCount * READ_BLOCK_WEIGHT + BlockCount * UPDATE_BLOCK_WEIGHT;
  ProgressCtx.ShouldReport = TRUE;

  DescriptorLocked = IsDescriptorLocked();

  //
  // Phase 1: Read current firmware.
  // When SmoothProgress is enabled, report progress after each block read.
  //
  CurrentImage = ReadCurrentFirmware (
                   DescriptorLocked,
                   SmoothProgress ? ReadProgressCallback : NULL,
                   SmoothProgress ? &ProgressCtx : NULL
                   );
  if (CurrentImage == NULL) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to read current firmware\n",
      __FUNCTION__
      ));
    return EFI_END_OF_MEDIA;
  }

  if (!SmoothProgress) {
    ProgressCtx.CurrentUnits = BlockCount * READ_BLOCK_WEIGHT;
    ReportProgress (&ProgressCtx);
  }

  if (!AreImageBtgKeysCompatible(CurrentImage, Image, ImageSize)) {
    FreePool (CurrentImage);
    DEBUG ((
      DEBUG_ERROR,
      "%a(): New image is not signed with a compatible OEM Root Key, aborting update\n",
      __FUNCTION__
      ));
    ErrorString = L"Intel Boot Guard OEM Root Key mismatch";
    if (AbortReason != NULL) {
        *AbortReason = AllocateCopyPool(StrSize(ErrorString), ErrorString);
    }

    ShowBtgErrorPopup(CurrentImage, ImageSize, ErrorString);

    return EFI_ABORTED;
  }

  UpdatedImage = MergeFirmwareImages (CurrentImage, Image);
  if (UpdatedImage == NULL) {
    FreePool (CurrentImage);
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to migrate data into new firmware image\n",
      __FUNCTION__
      ));
    return EFI_ABORTED;
  }

  //
  // Phase 2: Write differing blocks.
  //
  ScaledWriteWeight = UPDATE_BLOCK_WEIGHT;
  if (SmoothProgress) {
    //
    // Count how many blocks actually differ to get accurate progress.
    //
    DifferingBlocks = CountDifferingBlocks (
                        CurrentImage,
                        UpdatedImage,
                        ImageSize,
                        BlockSize,
                        DescriptorLocked
                      );
    DEBUG ((DEBUG_INFO, "%a(): %d blocks differ out of %d total\n",
            __FUNCTION__, DifferingBlocks, BlockCount));

    //
    // Scale the per-block write weight so that writing all differing blocks
    // still consumes exactly BlockCount * UPDATE_BLOCK_WEIGHT units, keeping
    // TotalUnits unchanged and avoiding a sudden progress jump.
    //
    if (DifferingBlocks > 0) {
      ScaledWriteWeight = UPDATE_BLOCK_WEIGHT * BlockCount / DifferingBlocks;
    }
  }

  Offset = 0;
  WrittenBlocks = 0;
  for (Block = 0; Block < BlockCount; Block++, Offset += BlockSize) {
    //
    // Save the flash and time by only writing a block if new contents differs
    // from the old one.
    //
    if (CompareMem (CurrentImage + Offset, UpdatedImage + Offset, BlockSize) == 0) {
      if (!SmoothProgress) {
        //
        // All blocks counted in TotalUnits, so advance progress for identical ones.
        //
        ProgressCtx.CurrentUnits += UPDATE_BLOCK_WEIGHT;
        ReportProgress (&ProgressCtx);
      }
      continue;
    }

    // Only touch blocks that are fully unlocked for writing.
    // This assumes that the protected ranges are aligned to SMMSTORE's block
    // size, otherwise the start/end of unprotected ranges might not get
    // updated.
    if (DescriptorLocked && !IsRangeWriteable(CurrentImage, ImageSize, Offset, BlockSize)) {
      DEBUG ((
        DEBUG_INFO,
        "%a(): range %d - %d is not writeable\n",
        __FUNCTION__, Offset, BlockSize + Offset
        ));
      if (!SmoothProgress) {
        //
        // In non-smooth mode all blocks are counted in TotalUnits, so advance
        // progress for non-writeable blocks too.
        //
        ProgressCtx.CurrentUnits += UPDATE_BLOCK_WEIGHT;
        ReportProgress (&ProgressCtx);
      }
      continue;
    }

    Status = SmmStoreLibEraseAnyBlock (Block);
    if (EFI_ERROR (Status))
      goto IoError;

    //
    // Report progress after erase (half of write weight).
    //
    ProgressCtx.CurrentUnits += ScaledWriteWeight / 2;
    ReportProgress (&ProgressCtx);

    NumBytes = BlockSize;
    Status = SmmStoreLibWriteAnyBlock (Block, 0, &NumBytes, UpdatedImage + Offset);
    if (EFI_ERROR (Status) || NumBytes != BlockSize)
      goto IoError;

    //
    // Report progress after write (remaining half of write weight).
    //
    ProgressCtx.CurrentUnits += ScaledWriteWeight - (ScaledWriteWeight / 2);
    ReportProgress (&ProgressCtx);

    WrittenBlocks++;
  }

  DEBUG ((DEBUG_INFO, "%a(): wrote %d blocks\n", __FUNCTION__, WrittenBlocks));

  FreePool (CurrentImage);
  FreePool (UpdatedImage);

  *LastAttemptStatus = LAST_ATTEMPT_STATUS_SUCCESS;

  /* Check if option for slot B exists, if so, set it */
  Status = CmosReadOptionByName("attempt_slot_b", &AttemptSlotB);
  if (!EFI_ERROR(Status) && !AttemptSlotB) {
    Status = CmosWriteOptionByName("attempt_slot_b", 1);
    if (EFI_ERROR(Status))
      DEBUG((DEBUG_ERROR, "%a(): Failed to set Attempt Slot B flag! Status = %r\n", __FUNCTION__, Status));
  }

  //
  // After the firmware on system flash was successfully updated working with
  // variable store on the flash isn't safe.  Switch to the use of stubs which
  // do nothing.
  //
  // New firmware will not report result of flashing in any way unless some
  // kind of communication mechanism is implemented for this purpose.
  //
  // If there was an error, it's unclear whether these stubs would be of any
  // help, so they are employed only on successful flashing.
  //

  gRT->GetVariable         = GetVariableHook;
  gRT->GetNextVariableName = GetNextVariableNameHook;
  gRT->SetVariable         = SetVariableHook;
  gRT->QueryVariableInfo   = QueryVariableInfoHook;

  gRT->Hdr.CRC32 = 0;
  gBS->CalculateCrc32 (
         (UINT8 *) &gRT->Hdr,
         gRT->Hdr.HeaderSize,
         &gRT->Hdr.CRC32
         );

  return EFI_SUCCESS;

IoError:
  DEBUG ((DEBUG_ERROR, "%a(): flashing has failed at block 0x%x/0x%x\n",
          __FUNCTION__, Block, BlockCount));

  //
  // Note that the recovery is performed from the start of the image and up to
  // and including the block at which write error has occurred.  Not considering
  // blocks that should not have changed to avoid doing any more damage if
  // region ranges or something similar is badly messed up and could cause
  // writing data to wrong places.
  //
  if (AttemptRecovery (CurrentImage, ImageSize, BlockSize, Block + 1, DescriptorLocked)) {
    DEBUG ((DEBUG_INFO, "%a(): successfully reverted to the previous firmware\n", __FUNCTION__));

    Status      = EFI_ABORTED;
    ErrorString = L"The firmware was recovered after a write failure during the update.";
  } else {
    DEBUG ((DEBUG_WARN, "%a(): failed to revert to the previous firmware\n", __FUNCTION__));

    Status      = EFI_DEVICE_ERROR;
    ErrorString = L"Failed to recover after a write failure.";
  }

  if (AbortReason != NULL) {
    *AbortReason = AllocateCopyPool (StrSize (ErrorString), ErrorString);
  }

  FreePool (CurrentImage);
  FreePool (UpdatedImage);
  return Status;
}

/**
  Lock the firmware device that contains a firmware image.  Once a firmware
  device is locked, any attempts to modify the firmware image contents in the
  firmware device must fail.

  @note It is recommended that all firmware devices support a lock method to
        prevent modifications to a stored firmware image.

  @note A firmware device lock mechanism is typically only cleared by a full
        system reset (not just sleep state/low power mode).

  @retval  EFI_SUCCESS      The firmware device was locked.
  @retval  EFI_UNSUPPORTED  The firmware device does not support locking
**/
EFI_STATUS
EFIAPI
FmpDeviceLock (
  VOID
  )
{
  return EFI_UNSUPPORTED;
}

/**
  Constructor that performs required initialization.

  @param ImageHandle  The image handle of the process.
  @param SystemTable  The EFI System Table pointer.

  @retval EFI_SUCCESS  Initialization was successful.
**/
EFI_STATUS
EFIAPI
FmpDeviceSmmLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  SmmStoreLibInitialize ();
  return EFI_SUCCESS;
}
