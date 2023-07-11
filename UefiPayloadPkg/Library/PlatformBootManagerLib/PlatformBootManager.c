/** @file
  This file include all platform action which can be customized
  by IBV/OEM.

Copyright (c) 2015 - 2018, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "PlatformBootManager.h"
#include "PlatformConsole.h"
#include <Library/BaseLib.h>
#include <Library/FrameBufferBltLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Protocol/FirmwareVolume2.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Guid/BoardSettingsGuid.h>
#include <Guid/GlobalVariable.h>
#include <Library/CustomizedDisplayLib.h>
#include <Library/BlParseLib.h>

EFI_GUID mBootMenuFile = {
  0xEEC25BDC, 0x67F2, 0x4D95, { 0xB1, 0xD5, 0xF8, 0x1B, 0x20, 0x39, 0xD1, 0x1D }
};

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
PlatformRegisterFvBootOption (
  EFI_GUID                         *FileGuid,
  CHAR16                           *Description,
  UINT32                           Attributes
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

  Status = EfiBootManagerInitializeLoadOption (
             &NewOption,
             LoadOptionNumberUnassigned,
             LoadOptionTypeBoot,
             Attributes,
             Description,
             DevicePath,
             NULL,
             0
             );
  ASSERT_EFI_ERROR (Status);
  FreePool (DevicePath);

  BootOptions = EfiBootManagerGetLoadOptions (
                  &BootOptionCount, LoadOptionTypeBoot
                  );

  OptionIndex = EfiBootManagerFindLoadOption (
                  &NewOption, BootOptions, BootOptionCount
                  );

  if (OptionIndex == -1) {
    Status = EfiBootManagerAddLoadOptionVariable (&NewOption, MAX_UINTN);
    ASSERT_EFI_ERROR (Status);
  }
  EfiBootManagerFreeLoadOption (&NewOption);
  EfiBootManagerFreeLoadOptions (BootOptions, BootOptionCount);
}

VOID
PlatformUnregisterFvBootOption (
  EFI_GUID                         *FileGuid,
  CHAR16                           *Description,
  UINT32                           Attributes
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

  Status = EfiBootManagerInitializeLoadOption (
             &NewOption,
             LoadOptionNumberUnassigned,
             LoadOptionTypeBoot,
             Attributes,
             Description,
             DevicePath,
             NULL,
             0
             );
  ASSERT_EFI_ERROR (Status);
  FreePool (DevicePath);

  BootOptions = EfiBootManagerGetLoadOptions (
                  &BootOptionCount, LoadOptionTypeBoot
                  );

  OptionIndex = EfiBootManagerFindLoadOption (
                  &NewOption, BootOptions, BootOptionCount
                  );

  if (OptionIndex >= 0 && OptionIndex < BootOptionCount) {
    Status = EfiBootManagerDeleteLoadOptionVariable (BootOptions[OptionIndex].OptionNumber,
                                                     BootOptions[OptionIndex].OptionType);
    ASSERT_EFI_ERROR (Status);
  }
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

  @param  FileGuid          Input file guid for the BootManagerMenuApp.
  @param  Description       Description of the BootManagerMenuApp boot option.
  @param  Position          Position of the new load option to put in the ****Order variable.
  @param  IsBootCategory    Whether this is a boot category.


  @retval OptionNumber      Return the option number info.

**/
UINTN
RegisterBootManagerMenuAppBootOption (
  EFI_GUID                         *FileGuid,
  CHAR16                           *Description,
  UINTN                            Position,
  BOOLEAN                          IsBootCategory
  )
{
  EFI_STATUS                       Status;
  EFI_BOOT_MANAGER_LOAD_OPTION     NewOption;
  EFI_DEVICE_PATH_PROTOCOL         *DevicePath;
  UINTN                            OptionNumber;

  DevicePath = FvFilePath (FileGuid);
  Status = EfiBootManagerInitializeLoadOption (
             &NewOption,
             LoadOptionNumberUnassigned,
             LoadOptionTypeBoot,
             IsBootCategory ? LOAD_OPTION_ACTIVE : LOAD_OPTION_CATEGORY_APP,
             Description,
             DevicePath,
             NULL,
             0
             );
  ASSERT_EFI_ERROR (Status);
  FreePool (DevicePath);

  DEBUG((EFI_D_INFO, "Registering Boot Manager app option\n"));
  Status = EfiBootManagerAddLoadOptionVariable (&NewOption, Position);
  ASSERT_EFI_ERROR (Status);

  OptionNumber = NewOption.OptionNumber;

  EfiBootManagerFreeLoadOption (&NewOption);

  return OptionNumber;
}

/**
  Delete one boot option for BootManagerMenuApp.

  @param  FileGuid          Input file guid for the BootManagerMenuApp.
  @param  Description       Description of the BootManagerMenuApp boot option.
  @param  IsBootCategory    Whether this is a boot category.

  @retval OptionNumber      Return the option number info.

**/
EFI_STATUS
UnregisterBootManagerMenuAppBootOption (
  EFI_GUID                         *FileGuid,
  CHAR16                           *Description,
  BOOLEAN                          IsBootCategory
  )
{
  EFI_STATUS                       Status;
  EFI_BOOT_MANAGER_LOAD_OPTION     NewOption;
  EFI_DEVICE_PATH_PROTOCOL         *DevicePath;
  UINTN                            BootOptionCount;
  INTN                             OptionIndex;
  EFI_BOOT_MANAGER_LOAD_OPTION     *BootOptions;

  DevicePath = FvFilePath (FileGuid);
  Status = EfiBootManagerInitializeLoadOption (
             &NewOption,
             LoadOptionNumberUnassigned,
             LoadOptionTypeBoot,
             IsBootCategory ? LOAD_OPTION_ACTIVE : LOAD_OPTION_CATEGORY_APP,
             Description,
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
    OptionNumber = (UINT16) RegisterBootManagerMenuAppBootOption (&mBootMenuFile, L"UEFI BootManagerMenuApp", (UINTN) -1, FALSE);
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
    DEBUG ((EFI_D_VERBOSE, "%a: Processing %s: %r\n", __FUNCTION__, DevicePathText));
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
  DEBUG ((EFI_ERROR (Status) ? EFI_D_ERROR : EFI_D_INFO, "%a: %s: %r\n",
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

  if (ReportText != NULL) {
        DEBUG ((EFI_D_VERBOSE, "%a: Adding output %s: %r\n", __FUNCTION__, ReportText));
  }

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
          L"BootManagerEnabled",
          &gDasharoSystemFeaturesGuid,
          NULL,
          &VarSize,
          &BootMenuEnable
        );

  DEBUG((EFI_D_ERROR, "Boot Manager option: %r, Size: %x, Enabled: %d\n",
                      Status, VarSize, BootMenuEnable));

  if ((Status == EFI_SUCCESS) && (VarSize == sizeof(BootMenuEnable)) && !BootMenuEnable) {
    DEBUG((EFI_D_INFO, "Unregistering Boot Manager key option\n"));
    EfiBootManagerDeleteKeyOptionVariable(NULL, 0, &F12, NULL);
    UnregisterBootManagerMenuAppBootOption(&mBootMenuFile, L"UEFI BootManagerMenuApp", FALSE);
  } else {
    DEBUG((EFI_D_INFO, "Registering Boot Manager key option\n"));
    OptionNumber = GetBootManagerMenuAppOption ();
    EfiBootManagerAddKeyOptionVariable (NULL, (UINT16)OptionNumber, 0, &F12, NULL);
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

  PlatformConsoleInit ();
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

    CreateMultiStringPopUp (
        78,
        12,
        L"!!! WARNING !!!",
        L"",
        L"This message is displayed, because the platform booted from the recovery",
        L"firmware partition. If you have just updated firmware, it is likely that",
        L"the signature verification process failed. Please verify again that",
        L"firmware was downloaded from the proper source and try updating again.",
        L"",
        RecoveryCodeLine,
        RecoveryMsgLine,
        L"",
        L"Press ENTER key to continue.",
        DelayLine
        );

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
  BOOLEAN                        BootMenuEnable;
  UINTN                          VarSize;
  EFI_EVENT                      Event;
  EFI_INPUT_KEY                  Enter;

  Black.Blue = Black.Green = Black.Red = Black.Reserved = 0;
  White.Blue = White.Green = White.Red = White.Reserved = 0xFF;

  gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  gST->ConOut->ClearScreen (gST->ConOut);

  WarnIfRecoveryBoot ();

  BootLogoEnableLogo ();

  //
  // Register ENTER as CONTINUE key
  //
  Enter.ScanCode    = SCAN_NULL;
  Enter.UnicodeChar = CHAR_CARRIAGE_RETURN;
  EfiBootManagerRegisterContinueKeyOption (0, &Enter, NULL);

  // FIXME: USB devices are not being detected unless we wait a bit.
  gBS->Stall (100 * 1000);
  EfiBootManagerConnectAll ();
  EfiBootManagerRefreshAllBootOption ();

  //
  // Process TPM PPI request
  //
  Tcg2PhysicalPresenceLibProcessRequest (NULL);

  VarSize = sizeof (NetBootEnabled);
  Status = gRT->GetVariable (
      L"NetworkBoot",
      &gDasharoSystemFeaturesGuid,
      NULL,
      &VarSize,
      &NetBootEnabled
      );

  //
  // Register iPXE
  //
  if ((Status != EFI_NOT_FOUND) && (VarSize == sizeof(NetBootEnabled))) {
    if (NetBootEnabled) {
      DEBUG((DEBUG_INFO, "Registering iPXE boot option by variable\n"));
      PlatformRegisterFvBootOption (PcdGetPtr (PcdiPXEFile),
                                    (CHAR16 *) PcdGetPtr(PcdiPXEOptionName),
                                    LOAD_OPTION_ACTIVE);
    } else {
        DEBUG((DEBUG_INFO, "Unregistering iPXE boot option by variable\n"));
        PlatformUnregisterFvBootOption (PcdGetPtr (PcdiPXEFile),
                                        (CHAR16 *) PcdGetPtr(PcdiPXEOptionName),
                                        LOAD_OPTION_ACTIVE);
    }
  } else if ((Status == EFI_NOT_FOUND) && FixedPcdGetBool(PcdDefaultNetworkBootEnable)) {
    DEBUG((DEBUG_INFO, "Registering iPXE boot option by policy\n"));
    PlatformRegisterFvBootOption (PcdGetPtr (PcdiPXEFile),
                                  (CHAR16 *) PcdGetPtr(PcdiPXEOptionName),
                                  LOAD_OPTION_ACTIVE);
  } else {
    DEBUG((DEBUG_INFO, "Unregistering iPXE boot option\n"));
    PlatformUnregisterFvBootOption (PcdGetPtr (PcdiPXEFile),
                                    (CHAR16 *) PcdGetPtr(PcdiPXEOptionName),
                                    LOAD_OPTION_ACTIVE);
  }
  //
  // Register UEFI Shell
  //
  DEBUG((DEBUG_INFO, "Registering UEFI Shell boot option\n"));
  PlatformRegisterFvBootOption (PcdGetPtr (PcdShellFile), L"UEFI Shell", LOAD_OPTION_ACTIVE);

  BootMenuKey = GetKeyStringFromScanCode (FixedPcdGet16(PcdBootMenuKey), L"F12");
  SetupMenuKey = GetKeyStringFromScanCode (FixedPcdGet16(PcdSetupMenuKey), L"ESC");

  VarSize = sizeof (BootMenuEnable);
  Status = gRT->GetVariable (
          L"BootManagerEnabled",
          &gDasharoSystemFeaturesGuid,
          NULL,
          &VarSize,
          &BootMenuEnable
        );

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

  Timeout = PcdGet16 (PcdPlatformBootTimeOut);

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

