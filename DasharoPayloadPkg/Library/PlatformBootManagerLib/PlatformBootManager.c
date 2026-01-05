/** @file
  This file include all platform action which can be customized
  by IBV/OEM.

Copyright (c) 2015 - 2018, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "PlatformBootManager.h"
#include "PlatformConsole.h"
#include <Protocol/FirmwareVolume2.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Guid/GlobalVariable.h>
#include <Guid/Tcg2PhysicalPresenceData.h>
#include <Library/CustomizedDisplayLib.h>
#include <Library/BlParseLib.h>
#include <Library/CapsuleLib.h>
#include <Library/HobLib.h>
#include <Library/Tpm2CommandLib.h>
#include <Library/Tcg2PhysicalPresenceLib.h>
#include <Coreboot.h>
#include <DasharoOptions.h>
#include <Protocol/UsbIo.h>

EFI_GUID mBootMenuFile = {
  0xEEC25BDC, 0x67F2, 0x4D95, { 0xB1, 0xD5, 0xF8, 0x1B, 0x20, 0x39, 0xD1, 0x1D }
};

// Defined and initialized in BdsDxe
extern BOOLEAN mQuietBoot;
extern BOOLEAN mFastBoot;

STATIC
EFI_STATUS
RegisterFtdiUsbUart (
  OUT EFI_DEVICE_PATH_PROTOCOL **OutDevicePath
)
{
  EFI_STATUS                Status;
  EFI_HANDLE                *UsbHandles = NULL;
  UINTN                     UsbHandleCount = 0;
  UINTN                     UsbIndex;

  EFI_USB_IO_PROTOCOL       *UsbIo;
  USB_DEVICE_DESCRIPTOR     DeviceDescriptor;
  EFI_DEVICE_PATH_PROTOCOL  *UsbDevicePath;

  EFI_HANDLE                *ConsoleHandles = NULL;
  UINTN                     ConsoleHandleCount = 0;
  UINTN                     Index;
  EFI_DEVICE_PATH_PROTOCOL  *FullDevicePath;
  EFI_DEVICE_PATH_PROTOCOL  *Node;
  CHAR16                    *DevPathStr;
  UINTN                     PrefixSize;

  BOOLEAN                   FtdiFound = FALSE;

  Status = gBS->LocateHandleBuffer (
    ByProtocol,
    &gEfiUsbIoProtocolGuid,
    NULL,
    &UsbHandleCount,
    &UsbHandles
  );
  if (EFI_ERROR (Status) || UsbHandleCount == 0) {
    DEBUG ((DEBUG_INFO, "No USB handles found\n"));
    return EFI_NOT_FOUND;
  }

  for (UsbIndex = 0; UsbIndex < UsbHandleCount; UsbIndex++) {
    Status = gBS->HandleProtocol (
      UsbHandles[UsbIndex],
      &gEfiUsbIoProtocolGuid,
      (VOID **)&UsbIo
    );
    if (EFI_ERROR (Status)) continue;

    Status = UsbIo->UsbGetDeviceDescriptor (UsbIo, &DeviceDescriptor);
    if (EFI_ERROR (Status)) continue;

    if (DeviceDescriptor.IdVendor != 0x0403 ||
        (DeviceDescriptor.IdProduct != 0x6001 && DeviceDescriptor.IdProduct != 0x6010)) {
      continue;
    }

    Status = gBS->HandleProtocol (
      UsbHandles[UsbIndex],
      &gEfiDevicePathProtocolGuid,
      (VOID **)&UsbDevicePath
    );
    if (EFI_ERROR (Status)) continue;

    PrefixSize = GetDevicePathSize (UsbDevicePath) - END_DEVICE_PATH_LENGTH;

    Status = gBS->LocateHandleBuffer (
      ByProtocol,
      &gEfiSimpleTextInProtocolGuid,
      NULL,
      &ConsoleHandleCount,
      &ConsoleHandles
    );
    if (EFI_ERROR (Status)) continue;

    for (Index = 0; Index < ConsoleHandleCount; Index++) {
      Status = gBS->HandleProtocol (
        ConsoleHandles[Index],
        &gEfiDevicePathProtocolGuid,
        (VOID **)&FullDevicePath
      );
      if (EFI_ERROR (Status)) continue;

      if (CompareMem (FullDevicePath, UsbDevicePath, PrefixSize) != 0) {
        continue;
      }

      Node = FullDevicePath;
      while (!IsDevicePathEnd (NextDevicePathNode (Node))) {
        Node = NextDevicePathNode (Node);
      }

      if (Node->Type != MESSAGING_DEVICE_PATH || Node->SubType != MSG_VENDOR_DP) {
        continue;
      }

      DevPathStr = ConvertDevicePathToText (FullDevicePath, FALSE, FALSE);
      if (DevPathStr != NULL) {
        DEBUG ((DEBUG_INFO, "Registering FTDI terminal path: %s\n", DevPathStr));
        FreePool (DevPathStr);
      }

      EfiBootManagerUpdateConsoleVariable (ConIn, FullDevicePath, NULL);
      EfiBootManagerUpdateConsoleVariable (ConOut, FullDevicePath, NULL);
      EfiBootManagerUpdateConsoleVariable (ErrOut, FullDevicePath, NULL);

      *OutDevicePath = FullDevicePath;

      FtdiFound = TRUE;
      break;
    }

    if (ConsoleHandles != NULL) {
      FreePool (ConsoleHandles);
      ConsoleHandles = NULL;
    }
  }

  if (UsbHandles != NULL) {
    FreePool (UsbHandles);
  }

  return FtdiFound ? EFI_SUCCESS : EFI_NOT_FOUND;
}

VOID
InstallReadyToLock (
  VOID
  )
{
  EFI_STATUS                            Status;
  EFI_HANDLE                            Handle;
  EFI_SMM_ACCESS2_PROTOCOL              *SmmAccess;

  DEBUG((DEBUG_INFO,"InstallReadyToLock  entering......\n"));
  //
  // Inform the SMM infrastructure that we're entering BDS and may run 3rd party code hereafter
  // Since PI1.2.1, we need signal EndOfDxe as ExitPmAuth
  //
  EfiEventGroupSignal (&gEfiEndOfDxeEventGroupGuid);
  DEBUG((DEBUG_INFO,"All EndOfDxe callbacks have returned successfully\n"));

  //
  // Install DxeSmmReadyToLock protocol in order to lock SMM
  //
  Status = gBS->LocateProtocol (&gEfiSmmAccess2ProtocolGuid, NULL, (VOID **) &SmmAccess);
  if (!EFI_ERROR (Status)) {
    Handle = NULL;
    Status = gBS->InstallProtocolInterface (
                    &Handle,
                    &gEfiDxeSmmReadyToLockProtocolGuid,
                    EFI_NATIVE_INTERFACE,
                    NULL
                    );
    ASSERT_EFI_ERROR (Status);
  }

  DEBUG((DEBUG_INFO,"InstallReadyToLock  end\n"));
  return;
}

/**
  Return the index of the load option in the load option array.

  The function consider two load options are equal when the
  OptionType, Attributes, Description, FilePath and OptionalData are equal.

  @param Key    Pointer to the load option to be found.
  @param Array  Pointer to the array of load options to be found.
  @param Count  Number of entries in the Array.

  @retval -1          Key wasn't found in the Array.
  @retval 0 ~ Count-1 The index of the Key in the Array.
**/
INTN
PlatformFindLoadOption (
  IN CONST EFI_BOOT_MANAGER_LOAD_OPTION *Key,
  IN CONST EFI_BOOT_MANAGER_LOAD_OPTION *Array,
  IN UINTN                              Count
)
{
  UINTN                             Index;

  for (Index = 0; Index < Count; Index++) {
    if ((Key->OptionType == Array[Index].OptionType) &&
        (Key->Attributes == Array[Index].Attributes) &&
        (StrCmp (Key->Description, Array[Index].Description) == 0) &&
        (CompareMem (Key->FilePath, Array[Index].FilePath, GetDevicePathSize (Key->FilePath)) == 0) &&
        (Key->OptionalDataSize == Array[Index].OptionalDataSize) &&
        (CompareMem (Key->OptionalData, Array[Index].OptionalData, Key->OptionalDataSize) == 0)) {
      return (INTN) Index;
    }
  }

  return -1;
}

VOID
SyncFvBootOption (
  EFI_GUID                         *FileGuid,
  CHAR16                           *Description,
  BOOLEAN                          BootNow,
  BOOLEAN                          Remove
  )
{
  EFI_STATUS                        Status;
  INTN                              OptionIndex;
  EFI_BOOT_MANAGER_LOAD_OPTION      NewOption;
  EFI_BOOT_MANAGER_LOAD_OPTION      *BootOptions;
  UINTN                             BootOptionCount;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH FileNode;
  EFI_LOADED_IMAGE_PROTOCOL         *LoadedImage;
  EFI_DEVICE_PATH_PROTOCOL          *DevicePath;
  EFI_DEVICE_PATH_PROTOCOL          *FileDevicePath;
  BOOLEAN                           FileExists;

  Status = gBS->HandleProtocol (
                  gImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **) &LoadedImage
                  );
  ASSERT_EFI_ERROR (Status);

  EfiInitializeFwVolDevicepathNode (&FileNode, FileGuid);
  DevicePath = DevicePathFromHandle (LoadedImage->DeviceHandle);
  ASSERT (DevicePath != NULL);
  DevicePath = AppendDevicePathNode (
                 DevicePath,
                 (EFI_DEVICE_PATH_PROTOCOL *) &FileNode
                 );
  ASSERT (DevicePath != NULL);

  // Search if given file exists anywhere
  Status = GetFileDevicePathFromAnyFv (
             FileGuid,
             EFI_SECTION_ALL,
             0,
             &FileDevicePath
             );

  FileExists = !EFI_ERROR (Status);

  Status = EfiBootManagerInitializeLoadOption (
             &NewOption,
             LoadOptionNumberUnassigned,
             LoadOptionTypeBoot,
             LOAD_OPTION_ACTIVE,
             Description,
             DevicePath,
             NULL,
             0
             );
  ASSERT_EFI_ERROR (Status);
  FreePool (DevicePath);

  // No need to check if the file exists here, because EfiBootManager will
  // print a message on the screen if file was not found.
  if (BootNow)
    EfiBootManagerBoot (&NewOption);

  BootOptions = EfiBootManagerGetLoadOptions (
                  &BootOptionCount, LoadOptionTypeBoot
                  );

  OptionIndex = EfiBootManagerFindLoadOption (
                  &NewOption, BootOptions, BootOptionCount
                  );

  if (OptionIndex == -1 && FileExists && !Remove) {
    // Option does not exist yet and the file exists, so add new option
    Status = EfiBootManagerAddLoadOptionVariable (&NewOption, MAX_UINTN);
    ASSERT_EFI_ERROR (Status);
  } else if (OptionIndex != -1) {
    if (Remove || !FileExists) {
      // Option exists and the file does not exists, or we explicitly asked to
      // remove it from boot option list.
      Status = EfiBootManagerDeleteLoadOptionVariable (BootOptions[OptionIndex].OptionNumber,
                                                     BootOptions[OptionIndex].OptionType);
    }
  }

  ASSERT_EFI_ERROR (Status);

  EfiBootManagerFreeLoadOption (&NewOption);
  EfiBootManagerFreeLoadOptions (BootOptions, BootOptionCount);
}

EFI_DEVICE_PATH *
FvFilePath (
  EFI_GUID                     *FileGuid
  )
{

  EFI_STATUS                         Status;
  EFI_LOADED_IMAGE_PROTOCOL          *LoadedImage;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH  FileNode;

  EfiInitializeFwVolDevicepathNode (&FileNode, FileGuid);

  Status = gBS->HandleProtocol (
                  gImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **) &LoadedImage
                  );
  ASSERT_EFI_ERROR (Status);
  return AppendDevicePathNode (
           DevicePathFromHandle (LoadedImage->DeviceHandle),
           (EFI_DEVICE_PATH_PROTOCOL *) &FileNode
           );
}

/**
  Create one boot option for BootManagerMenuApp.

  @retval OptionNumber      Return the option number info.

**/
UINTN
RegisterBootManagerMenuAppBootOption (
  VOID
  )
{
  EFI_STATUS                       Status;
  EFI_BOOT_MANAGER_LOAD_OPTION     NewOption;
  EFI_DEVICE_PATH_PROTOCOL         *DevicePath;
  UINTN                            OptionNumber;

  DevicePath = FvFilePath (&mBootMenuFile);
  // Use LOAD_OPTION_HIDDEN to not display Boot Manager Menu App in
  // "One Time Boot" menu.
  Status = EfiBootManagerInitializeLoadOption (
             &NewOption,
             LoadOptionNumberUnassigned,
             LoadOptionTypeBoot,
             LOAD_OPTION_CATEGORY_APP | LOAD_OPTION_HIDDEN,
             L"UEFI BootManagerMenuApp",
             DevicePath,
             NULL,
             0
             );
  ASSERT_EFI_ERROR (Status);
  FreePool (DevicePath);

  DEBUG((EFI_D_INFO, "Registering Boot Manager app option\n"));
  Status = EfiBootManagerAddLoadOptionVariable (&NewOption, MAX_UINTN);
  ASSERT_EFI_ERROR (Status);

  OptionNumber = NewOption.OptionNumber;

  EfiBootManagerFreeLoadOption (&NewOption);

  return OptionNumber;
}

/**
  Delete one boot option for BootManagerMenuApp.

  @retval OptionNumber      Return the option number info.

**/
EFI_STATUS
UnregisterBootManagerMenuAppBootOption (
  VOID
  )
{
  EFI_STATUS                       Status;
  EFI_BOOT_MANAGER_LOAD_OPTION     NewOption;
  EFI_DEVICE_PATH_PROTOCOL         *DevicePath;
  UINTN                            BootOptionCount;
  INTN                             OptionIndex;
  EFI_BOOT_MANAGER_LOAD_OPTION     *BootOptions;

  DevicePath = FvFilePath (&mBootMenuFile);
  // Use LOAD_OPTION_HIDDEN to not display Boot Manager Menu App in
  // "One Time Boot" menu.
  Status = EfiBootManagerInitializeLoadOption (
             &NewOption,
             LoadOptionNumberUnassigned,
             LoadOptionTypeBoot,
             LOAD_OPTION_CATEGORY_APP | LOAD_OPTION_HIDDEN,
             L"UEFI BootManagerMenuApp",
             DevicePath,
             NULL,
             0
             );
  ASSERT_EFI_ERROR (Status);
  FreePool (DevicePath);

  DEBUG((EFI_D_INFO, "Unregistering Boot Manager app option\n"));
  BootOptions = EfiBootManagerGetLoadOptions (
                &BootOptionCount, LoadOptionTypeBoot
                );

  OptionIndex = EfiBootManagerFindLoadOption (
                &NewOption, BootOptions, BootOptionCount
                );

  if (OptionIndex >= 0 && OptionIndex < BootOptionCount) {
    Status = EfiBootManagerDeleteLoadOptionVariable (BootOptions[OptionIndex].OptionNumber,
                                                     BootOptions[OptionIndex].OptionType);
  }

  return Status;
}

/**
  Check if it's a Device Path pointing to BootManagerMenuApp.

  @param  DevicePath     Input device path.

  @retval TRUE   The device path is BootManagerMenuApp File Device Path.
  @retval FALSE  The device path is NOT BootManagerMenuApp File Device Path.
**/
BOOLEAN
IsBootManagerMenuAppFilePath (
  EFI_DEVICE_PATH_PROTOCOL     *DevicePath
)
{
  EFI_HANDLE                      FvHandle;
  VOID                            *NameGuid;
  EFI_STATUS                      Status;

  Status = gBS->LocateDevicePath (&gEfiFirmwareVolume2ProtocolGuid, &DevicePath, &FvHandle);
  if (!EFI_ERROR (Status)) {
    NameGuid = EfiGetNameGuidFromFwVolDevicePathNode ((CONST MEDIA_FW_VOL_FILEPATH_DEVICE_PATH *) DevicePath);
    if (NameGuid != NULL) {
      return CompareGuid (NameGuid, &mBootMenuFile);
    }
  }

  return FALSE;
}

/**
  Return the boot option number to the BootManagerMenuApp.

  If not found it in the current boot option, create a new one.

  @retval OptionNumber   Return the boot option number to the BootManagerMenuApp.

**/
UINTN
GetBootManagerMenuAppOption (
  VOID
  )
{
  UINTN                        BootOptionCount;
  EFI_BOOT_MANAGER_LOAD_OPTION *BootOptions;
  UINTN                        Index;
  UINTN                        OptionNumber;

  OptionNumber = 0;

  BootOptions = EfiBootManagerGetLoadOptions (&BootOptionCount, LoadOptionTypeBoot);

  for (Index = 0; Index < BootOptionCount; Index++) {
    if (IsBootManagerMenuAppFilePath (BootOptions[Index].FilePath)) {
      OptionNumber = BootOptions[Index].OptionNumber;
      break;
    }
  }

  EfiBootManagerFreeLoadOptions (BootOptions, BootOptionCount);

  if (Index >= BootOptionCount) {
    //
    // If not found the BootManagerMenuApp, create it.
    //
    DEBUG((EFI_D_INFO, "Creating Boot Manager option\n"));
    OptionNumber = (UINT16) RegisterBootManagerMenuAppBootOption ();
  } else {
    DEBUG((EFI_D_INFO, "Boot Manager option number %d\n", OptionNumber));
  }

  return OptionNumber;
}


/**
  Check if the handle satisfies a particular condition.

  @param[in] Handle      The handle to check.
  @param[in] ReportText  A caller-allocated string passed in for reporting
                         purposes. It must never be NULL.

  @retval TRUE   The condition is satisfied.
  @retval FALSE  Otherwise. This includes the case when the condition could not
                 be fully evaluated due to an error.
**/
typedef
BOOLEAN
(EFIAPI *FILTER_FUNCTION) (
  IN EFI_HANDLE   Handle,
  IN CONST CHAR16 *ReportText
  );


/**
  Process a handle.

  @param[in] Handle      The handle to process.
  @param[in] ReportText  A caller-allocated string passed in for reporting
                         purposes. It must never be NULL.
**/
typedef
VOID
(EFIAPI *CALLBACK_FUNCTION)  (
  IN EFI_HANDLE   Handle,
  IN CONST CHAR16 *ReportText
  );

/**
  Locate all handles that carry the specified protocol, filter them with a
  callback function, and pass each handle that passes the filter to another
  callback.

  @param[in] ProtocolGuid  The protocol to look for.

  @param[in] Filter        The filter function to pass each handle to. If this
                           parameter is NULL, then all handles are processed.

  @param[in] Process       The callback function to pass each handle to that
                           clears the filter.
**/
STATIC
VOID
FilterAndProcess (
  IN EFI_GUID          *ProtocolGuid,
  IN FILTER_FUNCTION   Filter         OPTIONAL,
  IN CALLBACK_FUNCTION Process
  )
{
  EFI_STATUS Status;
  EFI_HANDLE *Handles;
  UINTN      NoHandles;
  UINTN      Idx;

  Status = gBS->LocateHandleBuffer (ByProtocol, ProtocolGuid,
                  NULL /* SearchKey */, &NoHandles, &Handles);
  if (EFI_ERROR (Status)) {
    //
    // This is not an error, just an informative condition.
    //
    DEBUG ((EFI_D_VERBOSE, "%a: %g: %r\n", __FUNCTION__, ProtocolGuid,
      Status));
    return;
  }

  ASSERT (NoHandles > 0);
  for (Idx = 0; Idx < NoHandles; ++Idx) {
    CHAR16        *DevicePathText;
    STATIC CHAR16 Fallback[] = L"<device path unavailable>";

    //
    // The ConvertDevicePathToText() function handles NULL input transparently.
    //
    DevicePathText = ConvertDevicePathToText (
                       DevicePathFromHandle (Handles[Idx]),
                       FALSE, // DisplayOnly
                       FALSE  // AllowShortcuts
                       );
    if (DevicePathText == NULL) {
      DevicePathText = Fallback;
    }

    if (Filter == NULL || Filter (Handles[Idx], DevicePathText)) {
      Process (Handles[Idx], DevicePathText);
    }

    if (DevicePathText != Fallback) {
      FreePool (DevicePathText);
    }
  }
  gBS->FreePool (Handles);
}


/**
  This FILTER_FUNCTION checks if a handle corresponds to a PCI display device.
**/
STATIC
BOOLEAN
EFIAPI
IsPciDisplay (
  IN EFI_HANDLE   Handle,
  IN CONST CHAR16 *ReportText
  )
{
  EFI_STATUS          Status;
  EFI_PCI_IO_PROTOCOL *PciIo;
  PCI_TYPE00          Pci;

  Status = gBS->HandleProtocol (Handle, &gEfiPciIoProtocolGuid,
                  (VOID**)&PciIo);
  if (EFI_ERROR (Status)) {
    //
    // This is not an error worth reporting.
    //
    return FALSE;
  }

  Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, 0 /* Offset */,
                        sizeof Pci / sizeof (UINT32), &Pci);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: %s: %r\n", __FUNCTION__, ReportText, Status));
    return FALSE;
  }

  return IS_PCI_DISPLAY (&Pci);
}

/**
  This FILTER_FUNCTION checks if a handle corresponds to a PCI display device.
**/
STATIC
BOOLEAN
EFIAPI
IsPciMassStorage (
  IN EFI_HANDLE   Handle,
  IN CONST CHAR16 *ReportText
  )
{
  EFI_STATUS          Status;
  EFI_PCI_IO_PROTOCOL *PciIo;
  PCI_TYPE00          Pci;

  Status = gBS->HandleProtocol (Handle, &gEfiPciIoProtocolGuid,
                  (VOID**)&PciIo);
  if (EFI_ERROR (Status)) {
    //
    // This is not an error worth reporting.
    //
    return FALSE;
  }

  Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, 0 /* Offset */,
                        sizeof Pci / sizeof (UINT32), &Pci);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: %s: %r\n", __FUNCTION__, ReportText, Status));
    return FALSE;
  }

  return IS_PCI_MASS_STORAGE (&Pci);
}

/**
  This CALLBACK_FUNCTION attempts to connect a handle non-recursively, asking
  the matching driver to produce all first-level child handles.
**/
STATIC
VOID
EFIAPI
Connect (
  IN EFI_HANDLE   Handle,
  IN CONST CHAR16 *ReportText
  )
{
  EFI_STATUS Status;

  Status = gBS->ConnectController (
                  Handle, // ControllerHandle
                  NULL,   // DriverImageHandle
                  NULL,   // RemainingDevicePath -- produce all children
                  FALSE   // Recursive
                  );
  DEBUG ((EFI_ERROR (Status) ? EFI_D_ERROR : EFI_D_VERBOSE, "%a: %s: %r\n",
    __FUNCTION__, ReportText, Status));
}

STATIC
VOID
EFIAPI
ConnectRecursive (
  IN EFI_HANDLE   Handle,
  IN CONST CHAR16 *ReportText
  )
{
  EFI_STATUS Status;

  Status = gBS->ConnectController (
                  Handle, // ControllerHandle
                  NULL,   // DriverImageHandle
                  NULL,   // RemainingDevicePath -- produce all children
                  TRUE    // Recursive
                  );
  DEBUG ((EFI_ERROR (Status) ? EFI_D_ERROR : EFI_D_VERBOSE, "%a: %s: %r\n",
    __FUNCTION__, ReportText, Status));
}

/**
  This CALLBACK_FUNCTION retrieves the EFI_DEVICE_PATH_PROTOCOL from the
  handle, and adds it to ConOut and ErrOut.
**/
STATIC
VOID
EFIAPI
AddOutput (
  IN EFI_HANDLE   Handle,
  IN CONST CHAR16 *ReportText
  )
{
  EFI_STATUS               Status;
  EFI_DEVICE_PATH_PROTOCOL *DevicePath;

  DevicePath = DevicePathFromHandle (Handle);
  if (DevicePath == NULL) {
    DEBUG ((EFI_D_ERROR, "%a: %s: handle %p: device path not found\n",
      __FUNCTION__, ReportText, Handle));
    return;
  }

  Status = EfiBootManagerUpdateConsoleVariable (ConOut, DevicePath, NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: %s: adding to ConOut: %r\n", __FUNCTION__,
      ReportText, Status));
    return;
  }

  Status = EfiBootManagerUpdateConsoleVariable (ErrOut, DevicePath, NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: %s: adding to ErrOut: %r\n", __FUNCTION__,
      ReportText, Status));
    return;
  }

  DEBUG ((EFI_D_VERBOSE, "%a: %s: added to ConOut and ErrOut\n", __FUNCTION__,
    ReportText));
}

STATIC
BOOLEAN
IsFumEnabled (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       VarSize;
  BOOLEAN     FUMEnabled;

  if (!PcdGetBool (PcdShowFum)) {
    return FALSE;
  }

  VarSize = sizeof (FUMEnabled);

  Status = gRT->GetVariable (
      DASHARO_VAR_FIRMWARE_UPDATE_MODE,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &VarSize,
      &FUMEnabled
      );
  if (EFI_ERROR(Status) || VarSize != sizeof(FUMEnabled)) {
    return FALSE;
  }

  return FUMEnabled;
}

STATIC
VOID
DropDasharoVar (
  IN CHAR16  *VarName
  )
{
  // Don't really care about the return value.
  gRT->SetVariable (VarName, &gDasharoSystemFeaturesGuid, 0, 0, NULL);
}

STATIC
VOID
DropFum (
  VOID
  )
{
  // Remove the variable to disable FUM on the next boot.
  DropDasharoVar (DASHARO_VAR_FIRMWARE_UPDATE_MODE);
}

// Replace non-volatile FUM variable with a volatile runtime one so applications
// can detect FUM.
STATIC
EFI_STATUS
PromoteFum (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       VarSize;
  BOOLEAN     FUMEnabled;

  DropFum ();

  FUMEnabled = TRUE;
  VarSize = sizeof (FUMEnabled);
  Status = gRT->SetVariable (
      DASHARO_VAR_FIRMWARE_UPDATE_MODE,
      &gDasharoSystemFeaturesGuid,
      EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
      VarSize,
      &FUMEnabled
      );

  return Status;
}

STATIC
EFI_STATUS
RequestDiskCapsulesBoot (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       VarSize;
  BOOLEAN     Value;

  Value   = TRUE;
  VarSize = sizeof (Value);

  // This requests FUM as well.
  Status = gRT->SetVariable (
      DASHARO_VAR_DISK_CAPSULES_BOOT,
      &gDasharoSystemFeaturesGuid,
      EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
      VarSize,
      &Value
      );

  return Status;
}

STATIC
BOOLEAN
HaveCapsules (
  VOID
  )
{
  EFI_STATUS  Status;

  if (GetBootModeHob () == BOOT_ON_FLASH_UPDATE) {
    //
    // Two possibilities:
    //  - there is an in-RAM capsule(s) and we're ignoring on-disk capsules if
    //    any are present
    //  - on-disk capsules were detected by this function on the previous boot
    //    and we'll try to process them during this boot
    //
    return TRUE;
  }

  if (!CoDCheckCapsuleOnDiskFlag ()) {
    return FALSE;
  }

  DEBUG ((DEBUG_INFO, "On-disk capsules: processing request from an OS.\n"));

  //
  // Worth noting that CoDPresent() always returns FALSE in
  // PlatformBootManagerBeforeConsole() because storage devices haven't been
  // initialized yet.
  //
  if (!CoDPresent (/*MaxRetry=*/3)) {
    DEBUG ((DEBUG_INFO, "On-disk capsules: found no capsules.\n"));
    return FALSE;
  }

  DEBUG ((
    DEBUG_INFO,
    "On-disk capsules: at least one is present on boot device.\n"
    ));

  Status = RequestDiskCapsulesBoot ();
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "On-disk capsules: failed requesting disk capsules boot: %r.\n",
      Status
      ));
    return FALSE;
  }

  DEBUG ((
    DEBUG_INFO,
    "On-disk capsules: rebooting to enable handling of capsules in coreboot.\n"
    ));
  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);

  // Should be unreachable.
  return FALSE;
}

/**
  Do the platform specific action before the console is connected.

  Such as:
    Update console variable;
    Register new Driver#### or Boot####;
    Signal ReadyToLock event.
**/
VOID
EFIAPI
PlatformBootManagerBeforeConsole (
  VOID
)
{
  EFI_INPUT_KEY                  Esc;
  EFI_INPUT_KEY                  F12;
  EFI_BOOT_MANAGER_LOAD_OPTION   BootOption;
  UINTN                          OptionNumber;

  // For Boot Menu Enabled functionality
  EFI_STATUS                     Status;
  BOOLEAN                        BootMenuEnable;
  UINTN                          VarSize;

  //
  // This variable communicates EDK's intent to coreboot and shouldn't exist
  // longer than a single boot.
  //
  DropDasharoVar (DASHARO_VAR_DISK_CAPSULES_BOOT);

  //
  // Map ESC to Boot Manager Menu
  //
  Esc.ScanCode    = FixedPcdGet16(PcdSetupMenuKey);
  Esc.UnicodeChar = CHAR_NULL;
  EfiBootManagerGetBootManagerMenu (&BootOption);
  EfiBootManagerAddKeyOptionVariable (NULL, (UINT16) BootOption.OptionNumber, 0, &Esc, NULL);

  //
  // Map F12 to Boot Device List menu
  //
  F12.ScanCode    = FixedPcdGet16(PcdBootMenuKey);
  F12.UnicodeChar = CHAR_NULL;

  VarSize = sizeof (BootMenuEnable);
  Status = gRT->GetVariable (
          DASHARO_VAR_BOOT_MANAGER_ENABLED,
          &gDasharoSystemFeaturesGuid,
          NULL,
          &VarSize,
          &BootMenuEnable
        );

  DEBUG((EFI_D_ERROR, "Boot Manager option: %r, Size: %x, Enabled: %d\n",
                      Status, VarSize, BootMenuEnable));

  if (EFI_ERROR(Status) || VarSize != sizeof(BootMenuEnable) || BootMenuEnable) {
    DEBUG((EFI_D_INFO, "Registering Boot Manager key option\n"));
    OptionNumber = GetBootManagerMenuAppOption ();
    EfiBootManagerAddKeyOptionVariable (NULL, (UINT16)OptionNumber, 0, &F12, NULL);
  } else {
    DEBUG((EFI_D_INFO, "Unregistering Boot Manager key option\n"));
    EfiBootManagerDeleteKeyOptionVariable(NULL, 0, &F12, NULL);
    UnregisterBootManagerMenuAppBootOption ();
  }

  //
  // Process system firmware update capsules and possibly device update
  // capsules that don't contain embedded drivers if those devices are already
  // available.
  //
  if (HaveCapsules ()) {
    // Capsules may have their own sanity checks, e.g. AC check on laptops.
    Status = ProcessCapsules ();
    if (EFI_ERROR (Status)) {
      DEBUG((DEBUG_ERROR, "%a(): ProcessCapsule() failed with: %r\n", __FUNCTION__, Status));
    }
  }

  //
  // Install ready to lock.
  // This needs to be done before option rom dispatched.
  //
  InstallReadyToLock ();

  //
  // Dispatch deferred images after EndOfDxe event and ReadyToLock installation.
  //
  EfiBootManagerDispatchDeferredImages ();

  //
  // Locate the PCI root bridges and make the PCI bus driver connect each,
  // non-recursively. This will produce a number of child handles with PciIo on
  // them.
  //
  FilterAndProcess (&gEfiPciRootBridgeIoProtocolGuid, NULL, Connect);

  //
  // Find all display class PCI devices (using the handles from the previous
  // step), and connect them non-recursively. This should produce a number of
  // child handles with GOPs on them.
  //
  FilterAndProcess (&gEfiPciIoProtocolGuid, IsPciDisplay, Connect);

  //
  // Now add the device path of all handles with GOP on them to ConOut and
  // ErrOut.
  //
  FilterAndProcess (&gEfiGraphicsOutputProtocolGuid, NULL, AddOutput);

  //
  // Connecting children of gEfiPciRootBridgeIoProtocolGuid should be sufficient
  // for the discovery and processing of consoles, but we intentionally add
  // serial consoles later to make sure they follow all graphical ones.
  //
  // This is because at least some bootloaders (e.g., one in FreeBSD) use the
  // first console of ConOut as the primary one. Putting serial console first
  // leads to interactive output going to serial where most users won't even
  // see it.
  //
  PlatformConsoleInit ();

  if (mFastBoot) {
    //
    // Find all mass storage class PCI devices and connect them
    // non-recursively (do we have to handle other cases than PCI?). This
    // should produce a number of child handles with storage-specific drivers
    // on them. Connecting the storages may be needed, e.g. if Linux is
    // installed on a separate driver than ESP. In such a case GRUB will not
    // be able to find the grub.cfg on rootfs. Otherwise the boot manager
    // would only connect the driver to the disk which is booted.
    //
    FilterAndProcess (&gEfiPciIoProtocolGuid, IsPciMassStorage, ConnectRecursive);
  }
}

CHAR16*
GetKeyStringFromScanCode (
  UINT16    ScanCode,
  CHAR16*   Default
)
{
  switch (ScanCode) {
  case SCAN_UP:     return L"UP";
  case SCAN_DOWN:   return L"DOWN";
  case SCAN_RIGHT:  return L"RIGHT";
  case SCAN_LEFT:   return L"LEFT";
  case SCAN_HOME:   return L"HOME";
  case SCAN_END:    return L"END";
  case SCAN_INSERT: return L"INS";
  case SCAN_DELETE: return L"DEL";
  case SCAN_F1:     return L"F1";
  case SCAN_F2:     return L"F2";
  case SCAN_F3:     return L"F3";
  case SCAN_F4:     return L"F4";
  case SCAN_F5:     return L"F5";
  case SCAN_F6:     return L"F6";
  case SCAN_F7:     return L"F7";
  case SCAN_F8:     return L"F8";
  case SCAN_F9:     return L"F9";
  case SCAN_F10:    return L"F10";
  case SCAN_F11:    return L"F11";
  case SCAN_F12:    return L"F12";
  case SCAN_ESC:    return L"ESC";
  default:          return Default;
  }
}

STATIC
VOID
DrainInput (
  VOID
)
{
  EFI_INPUT_KEY Key;

  //
  // Drain any queued keys.
  //
  while (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
    //
    // just throw away Key
    //
  }
}

STATIC
VOID
WarnIfRecoveryBoot (
  VOID
)
{
  EFI_STATUS     Status;
  EFI_EVENT      TimerEvent;
  EFI_EVENT      Events[2];
  UINTN          Index;
  EFI_INPUT_KEY  Key;
  RETURN_STATUS  RetStatus;
  UINT8          RecoveryCode;
  CONST CHAR8   *RecoveryReason;
  CHAR16         RecoveryCodeLine[81];
  CHAR16         RecoveryMsgLine[81];
  CHAR16         DelayLine[81];
  BOOLEAN        CursorVisible;
  UINTN          CurrentAttribute;
  UINTN          SecondsLeft;

  RetStatus = ParseVBootWorkbuf (&RecoveryCode, &RecoveryReason);

  if (RetStatus != RETURN_SUCCESS || RecoveryCode == 0) {
    return;
  }

  Status = gBS->CreateEvent (
      EVT_TIMER,
      TPL_CALLBACK,
      NULL,
      NULL,
      &TimerEvent
      );
  ASSERT_EFI_ERROR (Status);

  UnicodeSPrint (
      RecoveryCodeLine,
      sizeof (RecoveryCodeLine),
      L"Recovery reason code: 0x%02x",
      RecoveryCode
      );
  UnicodeSPrint (
      RecoveryMsgLine,
      sizeof (RecoveryMsgLine),
      L"Recovery reason: %a",
      RecoveryReason
      );

  CurrentAttribute = gST->ConOut->Mode->Attribute;
  CursorVisible    = gST->ConOut->Mode->CursorVisible;

  gST->ConOut->EnableCursor (gST->ConOut, FALSE);

  DrainInput ();
  gBS->SetTimer (TimerEvent, TimerPeriodic, 1 * 1000 * 1000 * 10);

  Events[0] = gST->ConIn->WaitForKey;
  Events[1] = TimerEvent;

  SecondsLeft = 30;
  while (SecondsLeft > 0) {
    UnicodeSPrint (
        DelayLine,
        sizeof (DelayLine),
        L"(The boot process will continue automatically in %d second%a.)",
        SecondsLeft,
        SecondsLeft == 1 ? "" : "s"
        );

    switch (RecoveryCode) {
      case VB2_RECOVERY_EC_SOFTWARE_SYNC:
        CreateMultiStringPopUp (
            78,
            10,
            L"!!! WARNING !!!",
            L"",
            L"Embedded Controller firmware update failed. Try rebooting the device",
            L"with an AC adapter connected.",
            L"",
            L"If the message persists, contact support or see docs.dasharo.com for",
            L"more information.",
            L"",
            L"Press ENTER key to continue.",
            DelayLine
            );
        break;
      default:
        CreateMultiStringPopUp (
            78,
            12,
            L"!!! WARNING !!!",
            L"",
            L"This message is displayed because the platform has booted from the recovery",
            L"firmware partition. If you have just updated firmware, it is likely that",
            L"the signature verification process failed. Please verify again that the",
            L"firmware was downloaded from the proper source and try updating again.",
            L"",
            RecoveryCodeLine,
            RecoveryMsgLine,
            L"",
            L"Press ENTER key to continue.",
            DelayLine
            );
        break;
    }

    Status = gBS->WaitForEvent (2, Events, &Index);
    ASSERT_EFI_ERROR (Status);

    if (Index == 0) {
      Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      ASSERT_EFI_ERROR (Status);

      if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
        break;
      }
    } else {
      SecondsLeft--;
    }
  }

  Status = gBS->CloseEvent (TimerEvent);
  ASSERT_EFI_ERROR (Status);

  gST->ConOut->EnableCursor (gST->ConOut, CursorVisible);
  gST->ConOut->SetAttribute (gST->ConOut, CurrentAttribute);

  gST->ConOut->ClearScreen (gST->ConOut);
  DrainInput ();
  BootLogoEnableLogo ();
}



typedef struct {
  TPM_ALG_ID AlgId;
  CHAR16    *Name;
} HASH_ALG_ENTRY;

STATIC CONST HASH_ALG_ENTRY mHashAlgs[] = {
  { TPM_ALG_SHA1,    L"SHA1"    },
  { TPM_ALG_SHA256,  L"SHA256"  },
  { TPM_ALG_SHA384,  L"SHA384"  },
  { TPM_ALG_SHA512,  L"SHA512"  },
  { TPM_ALG_SM3_256, L"SM3_256" },
};

STATIC
VOID
WarnIfSinglePCRBank (
  VOID
)
{
  EFI_STATUS     Status;
  EFI_EVENT      Events[1];
  BOOLEAN        CursorVisible;
  UINTN          CurrentAttribute;
  UINTN          Index;
  EFI_INPUT_KEY  Key;
  UINT32         TpmHashAlgorithmBitmap;
  UINT32         ActivePcrBanks;
  UINT32         RequestedActivePcrBanks;
  UINTN          Size;
  UINTN          i;
  UINTN          OptionCount = 0;
  UINTN          AvlIdx[ARRAY_SIZE(mHashAlgs)] = {0};
  CHAR16         OptLine[81];
  UINT32         SelectedMask;

  Size = sizeof(RequestedActivePcrBanks);
  Status = gRT->GetVariable(
            REQUESTED_ACTIVE_PCR_BANKS_VARIABLE_NAME,
            &gEfiTcg2PhysicalPresenceGuid,
            NULL,
            &Size,
            &RequestedActivePcrBanks
            );
  //
  // If the variable doesn't exist, there's been no request to change the
  // PCR banks.
  //
  if (EFI_ERROR(Status)) {
    return;
  }
  // Clear the variable after reading
  gRT->SetVariable(
        REQUESTED_ACTIVE_PCR_BANKS_VARIABLE_NAME,
        &gEfiTcg2PhysicalPresenceGuid,
        0,
        0,
        NULL
      );
  Status = Tpm2GetCapabilitySupportedAndActivePcrs(&TpmHashAlgorithmBitmap, &ActivePcrBanks);
  if (EFI_ERROR(Status)) {
      return;
  }
  // If they're equal, the requested bank selection was successful.
  if (RequestedActivePcrBanks == ActivePcrBanks) {
    return;
  }
  // Check if multiple PCR banks have in fact been selected
  if ((RequestedActivePcrBanks & (RequestedActivePcrBanks - 1)) == 0)
    return;
  //
  // If they're not equal, display the popup and switch to a single bank
  // of user's choice.
  //
  ASSERT_EFI_ERROR(Status);
  CurrentAttribute = gST->ConOut->Mode->Attribute;
  CursorVisible    = gST->ConOut->Mode->CursorVisible;
  gST->ConOut->EnableCursor(gST->ConOut, FALSE);
  DrainInput();
  Events[0] = gST->ConIn->WaitForKey;
  //
  // Parse obtained available PCR banks bitmap to get the names and create
  // an option for each available bank
  //
  for (i = 0; i < ARRAY_SIZE(mHashAlgs); i++) {
    if (TpmHashAlgorithmBitmap & (1U << i)) {
      AvlIdx[OptionCount++] = i;
    }
  }
  OptLine[0] = L'\0';
  UnicodeSPrint(OptLine, sizeof(OptLine), L"Select bank:");
  // Append each available bank
  for (i = 0; i < OptionCount; i++) {
    CHAR16 token[40];
    UnicodeSPrint(
      token, sizeof(token),
      L" %u) %s", (UINT32)(i + 1), mHashAlgs[AvlIdx[i]].Name
    );
    StrCatS(OptLine, ARRAY_SIZE(OptLine), token);
  }

  while (1) {
    CreateMultiStringPopUp(
        78,
        9,
        L"!!! WARNING !!!",
        L"",
        L"Multiple PCR banks have been selected, but the current TPM ",
        L"apparently supports only one active bank at a time.",
        L"",
        OptLine,
        L"",
        L"Press ESC to stay with the previously active bank.",
        L""
    );

    Status = gBS->WaitForEvent(1, Events, &Index);
    if (EFI_ERROR(Status)) {
      break;
    }

    Status = gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
    if (EFI_ERROR(Status)) {
      break;
    }
    if (Key.ScanCode == SCAN_ESC) {
      break;
    }

    if (Key.UnicodeChar >= L'1' && Key.UnicodeChar <= (L'0' + (CHAR16)OptionCount)) {
      UINTN sel = (UINTN)(Key.UnicodeChar - L'1');
      UINTN algIdx = AvlIdx[sel];
      SelectedMask = (1U << algIdx);

      if (SelectedMask == ActivePcrBanks) {
        break;
      }

      Tcg2PhysicalPresenceLibSubmitRequestToPreOSFunction(
          TCG2_PHYSICAL_PRESENCE_SET_PCR_BANKS,
          SelectedMask
      );

      gST->ConOut->EnableCursor(gST->ConOut, CursorVisible);
      gST->ConOut->SetAttribute(gST->ConOut, CurrentAttribute);
      gST->ConOut->ClearScreen(gST->ConOut);
      DrainInput();

      Tcg2PhysicalPresenceLibProcessRequest(NULL);
      return;
    }
  }

  ASSERT_EFI_ERROR(Status);
  gST->ConOut->EnableCursor(gST->ConOut, CursorVisible);
  gST->ConOut->SetAttribute(gST->ConOut, CurrentAttribute);
  gST->ConOut->ClearScreen(gST->ConOut);
  DrainInput();
  BootLogoEnableLogo();
}

STATIC
VOID
WarnIfBatteryLow (
  VOID
)
{
  EFI_STATUS     Status;
  EFI_EVENT      TimerEvent;
  EFI_EVENT      Events[2];
  UINTN          Index;
  EFI_INPUT_KEY  Key;
  RETURN_STATUS  RetStatus;
  UINT32         BatteryCapacity;
  BOOLEAN        AcConnected;
  BOOLEAN        BatteryConnected;
  BOOLEAN        BatteryTooLow;
  CHAR16         BatteryCapLine[81];
  CHAR16         DelayLine[81];
  BOOLEAN        CursorVisible;
  BOOLEAN        EcReadDataFailure;
  UINTN          CurrentAttribute;
  UINTN          SecondsLeft;
  EFI_TPL        OriginalTPL;

  BatteryTooLow = FALSE;
  EcReadDataFailure = FALSE;

  OriginalTPL = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  RetStatus = LaptopGetAcState(&AcConnected);

  if (RetStatus == RETURN_UNSUPPORTED) {
    gBS->RestoreTPL (OriginalTPL);
    return;
  }

  if (RetStatus != RETURN_SUCCESS)
    EcReadDataFailure = TRUE;

  RetStatus = LaptopGetBatState(&BatteryConnected);
  if (RetStatus != RETURN_SUCCESS)
    EcReadDataFailure = TRUE;

  /* We only need the baterry capacity if AC not connected */
  if (!EcReadDataFailure && !AcConnected && BatteryConnected) {
    RetStatus = LaptopGetBatteryCapacity(&BatteryCapacity);
    if (RetStatus != RETURN_SUCCESS)
      EcReadDataFailure = TRUE;
  }

  gBS->RestoreTPL (OriginalTPL);

  /* Check if there is a need to display a warning */
  if (!EcReadDataFailure && BatteryConnected) {
    if(AcConnected)
      return;
    if(!AcConnected && BatteryCapacity >= 5)
      return;
  }

  if (!EcReadDataFailure && !AcConnected &&
      BatteryConnected && BatteryCapacity < 5)
    BatteryTooLow = TRUE;

  Status = gBS->CreateEvent (
      EVT_TIMER,
      TPL_CALLBACK,
      NULL,
      NULL,
      &TimerEvent
      );
  ASSERT_EFI_ERROR (Status);

  CurrentAttribute = gST->ConOut->Mode->Attribute;
  CursorVisible    = gST->ConOut->Mode->CursorVisible;

  gST->ConOut->EnableCursor (gST->ConOut, FALSE);

  DrainInput ();
  gBS->SetTimer (TimerEvent, TimerPeriodic, 1 * 1000 * 1000 * 10);

  Events[0] = gST->ConIn->WaitForKey;
  Events[1] = TimerEvent;

  SecondsLeft = 10;
  while (SecondsLeft > 0) {
    if (BatteryTooLow) {
    UnicodeSPrint (
        BatteryCapLine,
        sizeof (BatteryCapLine),
        L"Current battery capacity: %d%%",
        BatteryCapacity
        );

      UnicodeSPrint (
          DelayLine,
          sizeof (DelayLine),
          L"(The laptop will shut down automatically in %d second%a.)",
          SecondsLeft,
          SecondsLeft == 1 ? "" : "s"
          );

      CreateMultiStringPopUp (
          78,
          11,
          L"!!! WARNING !!!",
          L"",
          L"The laptop's current battery is critically low (< 5%).",
          L"To protect your disk data from corruption due to abrupt shut down,",
          L"the laptop will power off now. Please plug the AC adapter and power",
          L"the laptop on again to boot.",
          L"",
          BatteryCapLine,
          L"",
          L"Press ENTER key to shut down immediately.",
          DelayLine
          );
    } else if (!EcReadDataFailure && AcConnected && !BatteryConnected) {
      UnicodeSPrint (
          DelayLine,
          sizeof (DelayLine),
          L"(The boot process will continue automatically in %d second%a.)",
          SecondsLeft,
          SecondsLeft == 1 ? "" : "s"
          );

      CreateMultiStringPopUp (
          78,
          7,
          L"!!! WARNING !!!",
          L"",
          L"The laptop's battery is not detected!",
          L"Please check the battery connection or contact the manufacturer.",
          L"",
          L"Press ENTER key to continue.",
          DelayLine
          );
    } else if (EcReadDataFailure) {
      UnicodeSPrint (
          DelayLine,
          sizeof (DelayLine),
          L"(The boot process will continue automatically in %d second%a.)",
          SecondsLeft,
          SecondsLeft == 1 ? "" : "s"
          );

      CreateMultiStringPopUp (
          78,
          7,
          L"!!! ERROR !!!",
          L"",
          L"Could not retrieve information about AC and battery state!",
          L"Please contact the manufacturer.",
          L"",
          L"Press ENTER key to continue.",
          DelayLine
          );
    }

    Status = gBS->WaitForEvent (2, Events, &Index);
    ASSERT_EFI_ERROR (Status);

    if (Index == 0) {
      Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      ASSERT_EFI_ERROR (Status);

      if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
        break;
      }
    } else {
      SecondsLeft--;
    }
  }

  Status = gBS->CloseEvent (TimerEvent);
  ASSERT_EFI_ERROR (Status);

  if (BatteryTooLow)
    gRT->ResetSystem (EfiResetShutdown, EFI_SUCCESS, 0, NULL);

  gST->ConOut->EnableCursor (gST->ConOut, CursorVisible);
  gST->ConOut->SetAttribute (gST->ConOut, CurrentAttribute);

  gST->ConOut->ClearScreen (gST->ConOut);
  DrainInput ();
  BootLogoEnableLogo ();
}

STATIC
VOID
WarnIfFirmwareUpdateMode (
  VOID
)
{
  EFI_STATUS     Status;
  EFI_EVENT      TimerEvent;
  EFI_EVENT      Events[2];
  UINTN          Index;
  EFI_INPUT_KEY  Key;
  EFI_TIME       Time;
  CHAR16         RandomDigit;
  CHAR16         DelayLine[81];
  CHAR16         PressKeyLine[81];
  BOOLEAN        CursorVisible;
  UINTN          CurrentAttribute;
  UINTN          SecondsLeft;

  if (!IsFumEnabled ()) {
    return;
  }

  PromoteFum ();

  Status = gBS->CreateEvent (
      EVT_TIMER,
      TPL_CALLBACK,
      NULL,
      NULL,
      &TimerEvent
      );
  ASSERT_EFI_ERROR (Status);

  // Don't bother checking user presence if FUM was triggered by capsule update
  if (HaveCapsules ()) {
    return;
  }

  Status = gRT->GetTime (&Time, NULL);
  //
  // Don't check status, even if the call failed we still have "random" data
  // from stack where Time is located. It is better than nothing, and we don't
  // need more.
  //
  RandomDigit = L'0' + (Time.Second % 10);

  UnicodeSPrint (
      PressKeyLine,
      sizeof (PressKeyLine),
      L"Press %c to continue.",
      RandomDigit
      );

  CurrentAttribute = gST->ConOut->Mode->Attribute;
  CursorVisible    = gST->ConOut->Mode->CursorVisible;

  gST->ConOut->EnableCursor (gST->ConOut, FALSE);

  DrainInput ();
  gBS->SetTimer (TimerEvent, TimerPeriodic, 1 * 1000 * 1000 * 10);

  Events[0] = gST->ConIn->WaitForKey;
  Events[1] = TimerEvent;

  SecondsLeft = 30;
  while (SecondsLeft > 0) {
    UnicodeSPrint (
        DelayLine,
        sizeof (DelayLine),
        L"automatically in %d second%a.)",
        SecondsLeft,
        SecondsLeft == 1 ? "" : "s"
        );

    CreateMultiStringPopUp (
        78,
        11,
        L"!!! WARNING !!!",
        L"",
        L"This message is displayed because the platform has booted in Firmware",
        L"Update Mode. All firmware write protections are disabled in this mode.",
        L"If you intend to update the firmware, press the key listed below to",
        L"proceed; otherwise, press any other key or wait for the timeout.",
        L"",
        PressKeyLine,
        L"",
        L"(The platform will automatically reboot and disable Firmware Update Mode",
        DelayLine
        );

    Status = gBS->WaitForEvent (2, Events, &Index);
    ASSERT_EFI_ERROR (Status);

    if (Index == 0) {
      Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      ASSERT_EFI_ERROR (Status);

      if (Key.UnicodeChar == RandomDigit) {
        break;
      } else {
        gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
      }
    } else {
      SecondsLeft--;
    }
  }

  if (SecondsLeft == 0) {
    gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  }

  Status = gBS->CloseEvent (TimerEvent);
  ASSERT_EFI_ERROR (Status);

  gST->ConOut->EnableCursor (gST->ConOut, CursorVisible);
  gST->ConOut->SetAttribute (gST->ConOut, CurrentAttribute);

  gST->ConOut->ClearScreen (gST->ConOut);
  DrainInput ();
  BootLogoEnableLogo ();
}

/**

  Acquire the string associated with the Index from smbios structure and return it.
  The caller is responsible for free the string buffer.

  @param    OptionalStrStart  The start position to search the string
  @param    Index             The index of the string to extract
  @param    String            The string that is extracted

  @retval   EFI_SUCCESS       The function returns EFI_SUCCESS always.

**/
EFI_STATUS
GetOptionalStringByIndex (
  IN      CHAR8                   *OptionalStrStart,
  IN      UINT8                   Index,
  OUT     CHAR16                  **String
  )
{
  UINTN          StrSize;

  if (Index == 0) {
    *String = AllocateZeroPool (sizeof (CHAR16));
    return EFI_SUCCESS;
  }

  StrSize = 0;
  do {
    Index--;
    OptionalStrStart += StrSize;
    StrSize           = AsciiStrSize (OptionalStrStart);
  } while (OptionalStrStart[StrSize] != 0 && Index != 0);

  if ((Index != 0) || (StrSize == 1)) {
    //
    // Meet the end of strings set but Index is non-zero, or
    // Find an empty string
    //
    *String = NULL;
    return EFI_NOT_FOUND;
  } else {
    *String = AllocatePool (StrSize * sizeof (CHAR16));
    AsciiStrToUnicodeStrS (OptionalStrStart, *String, StrSize);
  }

  return EFI_SUCCESS;
}

STATIC
VOID
PrintSolStrings (
  VOID
)
{
  UINT8                             StrIndex;
  CHAR16                            *FirmwareVersionString;
  CHAR16                            *EcVersionString;
  CHAR16                            *EcVariantString;
  EFI_STATUS                        Status;
  EFI_SMBIOS_HANDLE                 SmbiosHandle;
  EFI_SMBIOS_PROTOCOL               *Smbios;
  SMBIOS_TABLE_TYPE0                *Type0Record;
  SMBIOS_TABLE_TYPE11               *Type11Record;
  EFI_SMBIOS_TABLE_HEADER           *Record;
  BOOLEAN                           GotType0;
  BOOLEAN                           GotType11;
  UINTN                             CurrentAttribute;

  GotType0 = FALSE;
  GotType11 = FALSE;

  Status = gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL, (VOID **) &Smbios);

  if (EFI_ERROR(Status))
    return;

  SmbiosHandle = SMBIOS_HANDLE_PI_RESERVED;
  Status = Smbios->GetNext (Smbios, &SmbiosHandle, NULL, &Record, NULL);
  while (!EFI_ERROR(Status)) {
    if (Record->Type == SMBIOS_TYPE_BIOS_INFORMATION) {
      Type0Record = (SMBIOS_TABLE_TYPE0 *) Record;
      StrIndex = Type0Record->BiosVersion;
      Status = GetOptionalStringByIndex ((CHAR8*)((UINT8*)Type0Record + Type0Record->Hdr.Length), StrIndex, &FirmwareVersionString);

      if (!EFI_ERROR(Status) && (*FirmwareVersionString != 0x0000)) {
        Print (L"Firmware version: %s\n", FirmwareVersionString);
      } else {
        Print (L"Firmware version: ");
        CurrentAttribute = gST->ConOut->Mode->Attribute;
        gST->ConOut->SetAttribute (gST->ConOut, EFI_RED | EFI_BRIGHT | EFI_BACKGROUND_BLACK);
        Print (L"UNKNOWN\n");
        gST->ConOut->SetAttribute (gST->ConOut, CurrentAttribute);
      }
      GotType0 = TRUE;
    }

    if (Record->Type == SMBIOS_TYPE_OEM_STRINGS) {
      Type11Record = (SMBIOS_TABLE_TYPE11 *) Record;
      if (Type11Record->StringCount < 2) {
        DEBUG((EFI_D_ERROR, "Missing some EC strings\n"));
        Print (L"EC firmware version: ");
        CurrentAttribute = gST->ConOut->Mode->Attribute;
        gST->ConOut->SetAttribute (gST->ConOut, EFI_RED | EFI_BRIGHT | EFI_BACKGROUND_BLACK);
        Print (L"UNKNOWN\n");
        Print (L"Unable to detect EC firmware version!\n");
        Print (L"Please update your EC firmware per docs.dasharo.com instructions!\n");
        gST->ConOut->SetAttribute (gST->ConOut, CurrentAttribute);
      } else {
        // First string should be the EC variant
        Status = GetOptionalStringByIndex ((CHAR8*)((UINT8*)Type11Record + Type11Record->Hdr.Length), 1, &EcVariantString);
        // If string is not found or not open EC, print error straight away
        if (EFI_ERROR(Status) || StrStr(EcVariantString, L"EC: unknown")) {
          DEBUG((EFI_D_ERROR, "Missing EC variant string or EC variant reported as unknown\n"));
          Print (L"EC firmware version: ");
          CurrentAttribute = gST->ConOut->Mode->Attribute;
          gST->ConOut->SetAttribute (gST->ConOut, EFI_RED | EFI_BRIGHT | EFI_BACKGROUND_BLACK);
          Print (L"UNKNOWN\n");
          Print (L"Unable to detect EC firmware version!\n");
          Print (L"Please update your EC firmware per docs.dasharo.com instructions!\n");
          gST->ConOut->SetAttribute (gST->ConOut, CurrentAttribute);
        } else {
          // Second string should be the EC firmware version.
          // Print it without any error if found, because it has to be open EC now
          Status = GetOptionalStringByIndex ((CHAR8*)((UINT8*)Type11Record + Type11Record->Hdr.Length), 2, &EcVersionString);
          if (EFI_ERROR(Status) || StrStr(EcVersionString, L"EC firmware version: unknown")) {
            DEBUG((EFI_D_ERROR, "Missing EC version string or EC version reported as unknown\n"));
            CurrentAttribute = gST->ConOut->Mode->Attribute;
            gST->ConOut->SetAttribute (gST->ConOut, EFI_RED | EFI_BRIGHT | EFI_BACKGROUND_BLACK);
            Print (L"UNKNOWN\n");
            Print (L"Unable to detect EC firmware version!\n");
            Print (L"Please update your EC firmware per docs.dasharo.com instructions!\n");
            gST->ConOut->SetAttribute (gST->ConOut, CurrentAttribute);
          } else {
            Print (L"%s\n", EcVersionString);
            if (StrStr(EcVariantString, L"EC: proprietary")) {
              CurrentAttribute = gST->ConOut->Mode->Attribute;
              gST->ConOut->SetAttribute (gST->ConOut, EFI_RED | EFI_BRIGHT | EFI_BACKGROUND_BLACK);
              Print (L"Proprietary EC firmware detected!\n");
              Print (L"Please update your EC firmware per docs.dasharo.com instructions!\n");
              gST->ConOut->SetAttribute (gST->ConOut, CurrentAttribute);
            }
          }
        }
      }
    }

    if (GotType0 && GotType11)
      break;

    Status = Smbios->GetNext (Smbios, &SmbiosHandle, NULL, &Record, NULL);
  }
}

STATIC
VOID
SaveSmBiosFieldToEfiVar (
  IN  VOID                          *FieldValue,
  IN  UINTN                         FieldSize,
  IN  CHAR16*                       VarName
)
{
  VOID                              *CurrentValue;
  EFI_STATUS                        Status;
  UINTN                             CurrentSize;
  BOOLEAN                           NeedUpdate;

  NeedUpdate = FALSE;
  CurrentSize = FieldSize;
  CurrentValue = AllocatePool (FieldSize);

  if (!CurrentValue)
    return;

  Status = gRT->GetVariable (
             VarName,
             &gDasharoSystemFeaturesGuid,
             NULL,
             &CurrentSize,
             CurrentValue
             );

  if (EFI_ERROR (Status)) {
    NeedUpdate = TRUE;
  } else {
    if (CurrentSize != FieldSize)
      NeedUpdate = TRUE;
    else if (CompareMem (CurrentValue, FieldValue, FieldSize) != 0)
      NeedUpdate = TRUE;
  }

  if (NeedUpdate) {
    DEBUG ((EFI_D_INFO, "%s variable needs update\n", VarName));
    Status = gRT->SetVariable (
               VarName,
               &gDasharoSystemFeaturesGuid,
               EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_NON_VOLATILE,
               FieldSize,
               FieldValue
               );
  }

  FreePool(CurrentValue);
}

STATIC
VOID
SaveSMBIOSFields (
  VOID
)
{
  UINT8                             StrIndex;
  EFI_STATUS                        Status;
  EFI_SMBIOS_HANDLE                 SmbiosHandle;
  EFI_SMBIOS_PROTOCOL               *Smbios;
  SMBIOS_TABLE_TYPE1                *Type1Record;
  SMBIOS_TABLE_TYPE2                *Type2Record;
  EFI_SMBIOS_TABLE_HEADER           *Record;
  BOOLEAN                           GotType1;
  BOOLEAN                           GotType2;
  CHAR8                             *OptionalStrStart;
  UINTN                             StrSize;

  GotType1 = FALSE;
  GotType2 = FALSE;

  Status = gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL, (VOID **) &Smbios);

  if (EFI_ERROR(Status))
    return;


  SmbiosHandle = SMBIOS_HANDLE_PI_RESERVED;
  Status = Smbios->GetNext (Smbios, &SmbiosHandle, NULL, &Record, NULL);
  while (!EFI_ERROR(Status)) {
    if (Record->Type == SMBIOS_TYPE_SYSTEM_INFORMATION) {
      Type1Record = (SMBIOS_TABLE_TYPE1 *) Record;
      SaveSmBiosFieldToEfiVar((VOID *)&Type1Record->Uuid, sizeof(Type1Record->Uuid), DASHARO_VAR_SMBIOS_UUID);
      GotType1 = TRUE;
    }

    if (Record->Type == SMBIOS_TYPE_BASEBOARD_INFORMATION) {
      Type2Record = (SMBIOS_TABLE_TYPE2 *) Record;
      StrIndex = Type2Record->SerialNumber;
      OptionalStrStart = (CHAR8*)((UINT8*)Type2Record + Type2Record->Hdr.Length);
      StrSize = 0;
      do {
        StrIndex--;
        OptionalStrStart += StrSize;
        StrSize  = AsciiStrSize (OptionalStrStart);
      } while (OptionalStrStart[StrSize] != 0 && StrIndex != 0);

      if ((StrIndex != 0) || (StrSize == 1))
        DEBUG((EFI_D_INFO, "SMBIOS Type2 Serial Number missing\n"));
      else
        SaveSmBiosFieldToEfiVar((VOID *)OptionalStrStart, StrSize, DASHARO_VAR_SMBIOS_SN);

      GotType2 = TRUE;
    }

    if (GotType1 && GotType2)
      break;

    Status = Smbios->GetNext (Smbios, &SmbiosHandle, NULL, &Record, NULL);
  }
}

/**
  Refresh the logo on ReadyToBoot event. It will clear the screen from strings

  and progress bar when timeout is reached or continue key is pressed.

  @param    Event          Event pointer.
  @param    Context        Context pass to this function.
**/
VOID
EFIAPI
RefreshLogo (
  IN EFI_EVENT    Event,
  IN VOID         *Context
  )
{
  gBS->CloseEvent (Event);
  gST->ConOut->ClearScreen (gST->ConOut);
  BootLogoEnableLogo ();
}

/**
  Do the platform specific action after the console is connected.

  Such as:
    Dynamically switch output mode;
    Signal console ready platform customized event;
    Run diagnostics like memory testing;
    Connect certain devices;
    Dispatch additional option roms.
**/
VOID
EFIAPI
PlatformBootManagerAfterConsole (
  VOID
)
{
  EFI_STATUS                     Status;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Black;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  White;
  CHAR16                         *BootMenuKey;
  CHAR16                         *SetupMenuKey;
  BOOLEAN                        NetBootEnabled;
  BOOLEAN                        FUMEnabled;
  BOOLEAN                        BootMenuEnable;
  UINTN                          VarSize;
  EFI_EVENT                      Event;
  EFI_INPUT_KEY                  Enter;

  Black.Blue = Black.Green = Black.Red = Black.Reserved = 0;
  White.Blue = White.Green = White.Red = White.Reserved = 0xFF;

  FUMEnabled = IsFumEnabled ();

  gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  gST->ConOut->ClearScreen (gST->ConOut);

  BootLogoEnableLogo ();

  if (!mFastBoot || FUMEnabled) {
    // FIXME: USB devices are not being detected unless we wait a bit.
    // But don't wait with fastboot enabled. We typically don't boot a full blown OS from USB.
    gBS->Stall (100 * 1000);

    // With fast boot, we can't call ConnectAll as it would connect all consoles.
    EfiBootManagerConnectAll ();

    // Detect and register FTDI USB-UART converters
    // The FTDI can only be detected after all protocols are bound thanks to
    // the ConnectAll, but then it has to be connected separately
    EFI_DEVICE_PATH_PROTOCOL *FtdiPath;
    if (EFI_SUCCESS == RegisterFtdiUsbUart(&FtdiPath)) {
      // Connect just the FTDI path
      EfiBootManagerConnectDevicePath(FtdiPath, NULL);
    }
  }

  WarnIfSinglePCRBank ();
  WarnIfBatteryLow ();
  WarnIfRecoveryBoot ();
  WarnIfFirmwareUpdateMode ();

  EfiBootManagerRefreshAllBootOption ();

  //
  // Process device update capsules there weren't processed along with system
  // firmware capsules on first call to ProcessCapsules() in
  // PlatformBootManagerBeforeConsole().
  //
  if (HaveCapsules ()) {
    // Capsules may have their own sanity checks, e.g. AC check on laptops.
    Status = ProcessCapsules ();
    if (EFI_ERROR (Status)) {
      DEBUG((DEBUG_ERROR, "%a(): ProcessCapsule() failed with: %r\n", __FUNCTION__, Status));
    }

    //
    // This also clears BootNext variable which may be used to find on-disk
    // capsule, thus this needs to be done after ProcessCapsules().
    //
    CoDClearCapsuleOnDiskFlag ();

    //
    // Reset the system to disable SMI handler in order to exclude the
    // possibility of it being used outside of the firmware
    //
    // In practice, this will rarely execute because even the first
    // ProcessCapsules() invocation might do a reset if all capsules were
    // processed and at least one of them needed a reset.  This is just to catch
    // a case when this doesn't happen which is possible on error.
    //
    gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  } else {
    // Don't leave the flag in OsIndications set.
    CoDClearCapsuleOnDiskFlag ();
  }

  //
  // Ps2KeyboardDxe module requires the keyboard to be added to the ConIn and
  // the real i8042 controller and keyboard initialization happens when the
  // console is connected (using DriverBindingStart). That is why checking for
  // keyboard presence in PlatformBootManagerBeforeConsole is too early,
  // because the controller is not yet ready to issue commands to the
  // keyboard. When all consoles are connected on non-fastboot path, we can
  // check the keyboard presence and remove it from ConIn if it is not
  // detected. That way we avoid the huge delay when booting Ventoy
  // (https://github.com/Dasharo/dasharo-issues/issues/160), which called the
  // ConIn->Reset multiple times. The console reset on PS/2 keyboard is very
  // time-consuming.
  //
  if (!mFastBoot && !PcdGetBool (PcdSkipPs2Detect)) {
    UpdatePs2KeyboardConIn ();
  }

  //
  // Process TPM PPI request
  //
  Tcg2PhysicalPresenceLibProcessRequest (NULL);
  TcgPhysicalPresenceLibProcessRequest ();

  SaveSMBIOSFields();

  VarSize = sizeof (NetBootEnabled);
  Status = gRT->GetVariable (
      DASHARO_VAR_NETWORK_BOOT,
      &gDasharoSystemFeaturesGuid,
      NULL,
      &VarSize,
      &NetBootEnabled
      );

  //
  // Register iPXE
  //
  if (FUMEnabled) {
    DEBUG((DEBUG_INFO, "Registering iPXE boot option for FUM\n"));
    SyncFvBootOption (PcdGetPtr (PcdiPXEFile),
                      (CHAR16 *) PcdGetPtr(PcdiPXEOptionName),
                      PcdGetBool (PcdFumAutoIpxeBoot),
                      FALSE);
  } else if (mFastBoot) {
    DEBUG((DEBUG_INFO, "Unregistering iPXE boot option on fast boot path\n"));
    SyncFvBootOption (PcdGetPtr (PcdiPXEFile),
                      (CHAR16 *) PcdGetPtr(PcdiPXEOptionName),
                      FALSE,
                      TRUE);
  } else if (!EFI_ERROR (Status) && (VarSize == sizeof(NetBootEnabled))) {
    DEBUG((DEBUG_INFO, "Registering iPXE boot option by variable: %sbled\n",
                       NetBootEnabled ? L"en" : L"dis"));
    SyncFvBootOption (PcdGetPtr (PcdiPXEFile),
                      (CHAR16 *) PcdGetPtr(PcdiPXEOptionName),
                      FALSE,
                      !NetBootEnabled);

  } else {
    DEBUG((DEBUG_INFO, "Registering iPXE boot option by policy: %sbled\n",
                       FixedPcdGetBool(PcdDefaultNetworkBootEnable) ? L"en" : L"dis"));
    SyncFvBootOption (PcdGetPtr (PcdiPXEFile),
                      (CHAR16 *) PcdGetPtr(PcdiPXEOptionName),
                      FALSE,
                      !FixedPcdGetBool(PcdDefaultNetworkBootEnable));
  }
  //
  // Register UEFI Shell
  //
  DEBUG((DEBUG_INFO, "Registering UEFI Shell boot option\n"));
  SyncFvBootOption (PcdGetPtr (PcdShellFile),
                    L"UEFI Shell",
                    FALSE,
                    FALSE);

  BootMenuKey = GetKeyStringFromScanCode (FixedPcdGet16(PcdBootMenuKey), L"F12");
  SetupMenuKey = GetKeyStringFromScanCode (FixedPcdGet16(PcdSetupMenuKey), L"ESC");

  VarSize = sizeof (BootMenuEnable);
  Status = gRT->GetVariable (
          DASHARO_VAR_BOOT_MANAGER_ENABLED,
          &gDasharoSystemFeaturesGuid,
          NULL,
          &VarSize,
          &BootMenuEnable
        );

  //
  // Register ENTER as CONTINUE key
  //
  Enter.ScanCode    = SCAN_NULL;
  Enter.UnicodeChar = CHAR_CARRIAGE_RETURN;
  EfiBootManagerRegisterContinueKeyOption (0, &Enter, NULL);

  // Print the prompt and SOL strings only if Quiet Boot and Fast Boot are disabled.
  // Do not refresh the logo, it should stay intact.
  if (!mFastBoot && !mQuietBoot) {

    if (PcdGetBool (PcdPrintSolStrings))
      PrintSolStrings();

    Print (L"%-5s to enter Setup\n", SetupMenuKey);

    if (EFI_ERROR(Status) || VarSize != sizeof(BootMenuEnable) || BootMenuEnable)
      Print (L"%-5s to enter Boot Manager Menu\n", BootMenuKey);

    Print (L"ENTER to boot directly\n");

    EfiCreateEventReadyToBootEx (
               TPL_CALLBACK,
               RefreshLogo,
               NULL,
               &Event
               );
  }
}

/**
  This function is called each second during the boot manager waits the timeout.

  @param TimeoutRemain  The remaining timeout.
**/
VOID
EFIAPI
PlatformBootManagerWaitCallback (
  UINT16          TimeoutRemain
)
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL_UNION Black;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL_UNION White;
  UINT16                              Timeout;
  UINTN                               DataSize;
  EFI_STATUS                          Status;

  DEBUG ((EFI_D_INFO, "[Bds]BdsWait ...Zzzzzzzzzzzz...\n"));

  // Don't print progress BAR on quiet or fast boot
  if (mFastBoot || mQuietBoot)
    return;


  DataSize = sizeof(Timeout);
  Status = gRT->GetVariable(
                  EFI_TIME_OUT_VARIABLE_NAME,
                  &gEfiGlobalVariableGuid,
                  NULL,
                  &DataSize,
                  &Timeout
                  );
  if (EFI_ERROR (Status)) {
    Timeout = PcdGet16 (PcdPlatformBootTimeOut);
  }

  Black.Raw = 0x00000000;
  White.Raw = 0x00FFFFFF;

  BootLogoUpdateProgress (
    White.Pixel,
    Black.Pixel,
    L"",
    White.Pixel,
    (Timeout - TimeoutRemain) * 100 / Timeout,
    0
    );
}

/**
  The function is called when no boot option could be launched,
  including platform recovery options and options pointing to applications
  built into firmware volumes.

  If this function returns, BDS attempts to enter an infinite loop.
**/
VOID
EFIAPI
PlatformBootManagerUnableToBoot (
  VOID
  )
{
  EFI_STATUS                   Status;
  EFI_BOOT_MANAGER_LOAD_OPTION BootManagerMenu;
  UINTN                        Index;

  //
  // BootManagerMenu doesn't contain the correct information when return status
  // is EFI_NOT_FOUND.
  //
  Status = EfiBootManagerGetBootManagerMenu (&BootManagerMenu);
  if (EFI_ERROR (Status)) {
    return;
  }
  //
  // Normally BdsDxe does not print anything to the system console, but this is
  // a last resort -- the end-user will likely not see any DEBUG messages
  // logged in this situation.
  //
  // AsciiPrint() will NULL-check gST->ConOut internally. We check gST->ConIn
  // here to see if it makes sense to request and wait for a keypress.
  //
  if (gST->ConOut != NULL && gST->ConIn != NULL) {
    gST->ConOut->ClearScreen (gST->ConOut);
    AsciiPrint (
      "%a: No bootable option or device was found.\n"
      "%a: Press any key to enter the Boot Manager Menu.\n",
      gEfiCallerBaseName,
      gEfiCallerBaseName
      );
    Status = gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
    ASSERT_EFI_ERROR (Status);
    ASSERT (Index == 0);

    DrainInput ();
  }

  for (;;) {
    EfiBootManagerBoot (&BootManagerMenu);
  }
}

