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

STATIC
EFI_STATUS
VisitAllInstancesOfProtocol (
  IN EFI_GUID                    *Id,
  IN PROTOCOL_INSTANCE_CALLBACK  CallBackFunction,
  IN VOID                        *Context
  )
{
  EFI_STATUS                Status;
  UINTN                     HandleCount;
  EFI_HANDLE                *HandleBuffer;
  UINTN                     Index;
  VOID                      *Instance;

  //
  // Start to check all the PciIo to find all possible device
  //
  HandleCount = 0;
  HandleBuffer = NULL;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  Id,
                  NULL,
                  &HandleCount,
                  &HandleBuffer
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (HandleBuffer[Index], Id, &Instance);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = (*CallBackFunction) (
               HandleBuffer[Index],
               Instance,
               Context
               );
  }

  gBS->FreePool (HandleBuffer);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ConnectRootBridge (
  IN EFI_HANDLE  RootBridgeHandle,
  IN VOID        *Instance,
  IN VOID        *Context
  )
{
  EFI_STATUS Status;

  //
  // Make the PCI bus driver connect the root bridge, non-recursively. This
  // will produce a number of child handles with PciIo on them.
  //
  Status = gBS->ConnectController (
                  RootBridgeHandle, // ControllerHandle
                  NULL,             // DriverImageHandle
                  NULL,             // RemainingDevicePath -- produce all
                                    //   children
                  FALSE             // Recursive
                  );
  return Status;
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

  Status = EfiBootManagerAddLoadOptionVariable (&NewOption, MAX_UINTN);
  ASSERT_EFI_ERROR (Status);

  OptionNumber = NewOption.OptionNumber;

  EfiBootManagerFreeLoadOption (&NewOption);

  return OptionNumber;
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
    OptionNumber = (UINT16) RegisterBootManagerMenuAppBootOption ();
  }

  return OptionNumber;
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
  EFI_INPUT_KEY                  Enter;
  EFI_INPUT_KEY                  Esc;
  EFI_INPUT_KEY                  F12;
  EFI_BOOT_MANAGER_LOAD_OPTION   BootOption;
  UINTN                          OptionNumber;

  VisitAllInstancesOfProtocol (&gEfiPciRootBridgeIoProtocolGuid,
    ConnectRootBridge, NULL);

  PlatformConsoleInit ();

  //
  // Register ENTER as CONTINUE key
  //
  Enter.ScanCode    = SCAN_NULL;
  Enter.UnicodeChar = CHAR_CARRIAGE_RETURN;
  EfiBootManagerRegisterContinueKeyOption (0, &Enter, NULL);
  //
  // Map ESC to Boot Manager Menu
  //
  Esc.ScanCode    = FixedPcdGet16(PcdSetupMenuKey);;
  Esc.UnicodeChar = CHAR_NULL;
  EfiBootManagerGetBootManagerMenu (&BootOption);
  EfiBootManagerAddKeyOptionVariable (NULL, (UINT16) BootOption.OptionNumber, 0, &Esc, NULL);

  //
  // Map F12 to Boot Device List menu
  //
  F12.ScanCode    = FixedPcdGet16(PcdBootMenuKey);
  F12.UnicodeChar = CHAR_NULL;
  OptionNumber    = GetBootManagerMenuAppOption ();
  EfiBootManagerAddKeyOptionVariable (NULL, (UINT16)OptionNumber, 0, &F12, NULL);

  //
  // Install ready to lock.
  // This needs to be done before option rom dispatched.
  //
  InstallReadyToLock ();

  //
  // Dispatch deferred images after EndOfDxe event and ReadyToLock installation.
  //
  EfiBootManagerDispatchDeferredImages ();
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
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Black;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  White;
  CHAR16                         *BootMenuKey;
  CHAR16                         *SetupMenuKey;

  Black.Blue = Black.Green = Black.Red = Black.Reserved = 0;
  White.Blue = White.Green = White.Red = White.Reserved = 0xFF;

  gST->ConOut->ClearScreen (gST->ConOut);
  BootLogoEnableLogo ();

  // FIXME: USB devices are not being detected unless we wait a bit.
  gBS->Stall (100 * 1000);

  EfiBootManagerConnectAll ();
  EfiBootManagerRefreshAllBootOption ();

  WarnIfRecoveryBoot ();

  //
  // Process TPM PPI request
  //
  Tcg2PhysicalPresenceLibProcessRequest (NULL);

  //
  // Register iPXE
  //
  DEBUG((DEBUG_INFO, "Registering iPXE boot option\n"));
  PlatformRegisterFvBootOption (PcdGetPtr (PcdiPXEFile), L"iPXE Network boot", LOAD_OPTION_ACTIVE);

  //
  // Register UEFI Shell
  //
  DEBUG((DEBUG_INFO, "Registering UEFI Shell boot option\n"));
  PlatformRegisterFvBootOption (PcdGetPtr (PcdShellFile), L"UEFI Shell", LOAD_OPTION_ACTIVE);

  BootMenuKey = GetKeyStringFromScanCode (FixedPcdGet16(PcdBootMenuKey), L"F12");
  SetupMenuKey = GetKeyStringFromScanCode (FixedPcdGet16(PcdSetupMenuKey), L"ESC");

  Print (L"%-5s to enter Setup\n%-5s to enter Boot Manager Menu\nENTER to boot directly",
         SetupMenuKey, BootMenuKey);
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

