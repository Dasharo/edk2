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
#include <Guid/BoardSettingsGuid.h>

#define PCIE_SLOT1  L"PciRoot(0x0)/Pci(0x1B" //Wildcard
#define PCIE_SLOT2  L"PciRoot(0x0)/Pci(0x1,0x0)"
#define PCIE_SLOT3  L"PciRoot(0x0)/Pci(0x1C,0x0)"
#define PCIE_SLOT4  L"PciRoot(0x0)/Pci(0x1,0x2)"
#define PCIE_SLOT5  L"PciRoot(0x0)/Pci(0x1D,0x0)"
#define PCIE_SLOT6  L"PciRoot(0x0)/Pci(0x1,0x1)"
#define GRAPHICS_IGD_OUTPUT   L"PciRoot(0x0)/Pci(0x2,0x0)"
#define GRAPHICS_KVM_OUTPUT   L"PciRoot(0x0)/Pci(0x1D,0x6)"

#define DP_NODE_LEN(Type) { (UINT8)sizeof (Type), (UINT8)(sizeof (Type) >> 8) }

#pragma pack (1)
typedef struct {
  VENDOR_DEVICE_PATH         SerialDxe;
  UART_DEVICE_PATH           Uart;
  VENDOR_DEFINED_DEVICE_PATH TermType;
  EFI_DEVICE_PATH_PROTOCOL   End;
} PLATFORM_SERIAL_CONSOLE;
#pragma pack ()

#define SERIAL_DXE_FILE_GUID { \
          0xD3987D4B, 0x971A, 0x435F, \
          { 0x8C, 0xAF, 0x49, 0x67, 0xEB, 0x62, 0x72, 0x41 } \
          }

STATIC PLATFORM_SERIAL_CONSOLE mSerialConsole = {
  //
  // VENDOR_DEVICE_PATH SerialDxe
  //
  {
    { HARDWARE_DEVICE_PATH, HW_VENDOR_DP, DP_NODE_LEN (VENDOR_DEVICE_PATH) },
    SERIAL_DXE_FILE_GUID
  },

  //
  // UART_DEVICE_PATH Uart
  //
  {
    { MESSAGING_DEVICE_PATH, MSG_UART_DP, DP_NODE_LEN (UART_DEVICE_PATH) },
    0, // Reserved
    0, // BaudRate
    0, // DataBits
    0, // Parity
    0  // StopBits
  },

  //
  // VENDOR_DEFINED_DEVICE_PATH TermType
  //
  {
    {
      MESSAGING_DEVICE_PATH, MSG_VENDOR_DP,
      DP_NODE_LEN (VENDOR_DEFINED_DEVICE_PATH)
    }
    //
    // Guid to be filled in dynamically
    //
  },

  //
  // EFI_DEVICE_PATH_PROTOCOL End
  //
  {
    END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE,
    DP_NODE_LEN (EFI_DEVICE_PATH_PROTOCOL)
  }
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

EFI_GRAPHICS_OUTPUT_PROTOCOL  *mGop = NULL;

VOID
EFIAPI
SetPrimaryVideoOutput(
  VOID
)
{
  EFI_STATUS                      Status;
  UINTN                           HandleCount;
  EFI_HANDLE                      *Handle;
  UINTN                           Index;
  EFI_DEVICE_PATH_PROTOCOL        *TempDevicePath;
  CHAR16                          *Str;
  BOARD_SETTINGS                  BoardSettings;
  UINTN                           BoardSettingsSize;

  Handle = NULL;
  Index = 0;
  BoardSettingsSize = sizeof(BOARD_SETTINGS);

  DEBUG ((EFI_D_ERROR, "SetPrimaryVideoOutput\n"));

  // Fetch Board Settings
  Status = gRT->GetVariable(BOARD_SETTINGS_NAME,
    &gEfiBoardSettingsVariableGuid,
    NULL,
    &BoardSettingsSize,
    &BoardSettings);

  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "Fetching Board Settings errored with %x\n", Status));
    return;
  }

  // Locate all GOPs
  Status = gBS->LocateHandleBuffer(ByProtocol,
                  &gEfiGraphicsOutputProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handle);

  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "Fetching handles errored with %x\n", Status));
    return;
  }

  DEBUG ((EFI_D_INFO, "Amount of Handles: %x\n", HandleCount));

  // Loop through all GOPs to find the primary one
  for (Index=0; Index < HandleCount; Index++) {
    // Get Device Path of GOP
    Status = gBS->HandleProtocol (Handle[Index], &gEfiDevicePathProtocolGuid,   (VOID*)&TempDevicePath);
    if (EFI_ERROR (Status)) {
      continue;
    }
    // Get Protocol of GOP
    Status = gBS->HandleProtocol (Handle[Index], &gEfiGraphicsOutputProtocolGuid, (VOID**)&mGop);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Str = ConvertDevicePathToText(TempDevicePath, FALSE, TRUE);
    DEBUG ((EFI_D_INFO, "Current Device: %s\n", Str));
    // Check which GOP should be enabled
    if ((!StrnCmp(Str, GRAPHICS_KVM_OUTPUT, StrLen(GRAPHICS_KVM_OUTPUT))) && (HandleCount > 2))  {
      DEBUG ((EFI_D_INFO, "Found the KVM Device.."));
      // If Primary Video not KVM - disable.
      if (BoardSettings.PrimaryVideo != 0) {
        DEBUG ((EFI_D_INFO, "Disabling"));
        Status = gBS->UninstallProtocolInterface(Handle[Index], &gEfiGraphicsOutputProtocolGuid, mGop);
        if (EFI_ERROR (Status)) {
          DEBUG((DEBUG_ERROR, "Uninstalling Handle errored with %x\n", Status));
        }
      }
      DEBUG ((EFI_D_INFO, "\n"));
    } else if (!StrnCmp(Str, GRAPHICS_IGD_OUTPUT, StrLen(GRAPHICS_IGD_OUTPUT))) {
      DEBUG ((EFI_D_INFO, "Found the IGD Device.."));
      // If Primary Video not IGD - disable.
      if (BoardSettings.PrimaryVideo != 1) {
        DEBUG ((EFI_D_INFO, "Disabling"));
        Status = gBS->UninstallProtocolInterface(Handle[Index], &gEfiGraphicsOutputProtocolGuid, mGop);
        if (EFI_ERROR (Status)) {
          DEBUG((DEBUG_ERROR, "Uninstalling Handle errored with %x\n", Status));
        }
      }
      DEBUG ((EFI_D_INFO, "\n"));
    } else if
    (!StrnCmp(Str, PCIE_SLOT1, StrLen(PCIE_SLOT1)))
    {
      DEBUG ((EFI_D_INFO, "Found PCI SLOT1 Graphics Device.."));
      // If Primary Video not PCI - disable.
      if (BoardSettings.PrimaryVideo != 2) {
        DEBUG ((EFI_D_INFO, "Disabling"));
        Status = gBS->UninstallProtocolInterface(Handle[Index], &gEfiGraphicsOutputProtocolGuid, mGop);
        if (EFI_ERROR (Status)) {
          DEBUG((DEBUG_ERROR, "Uninstalling Handle errored with %x\n", Status));
        }
      }
      DEBUG ((EFI_D_INFO, "\n"));
    } else if
    (!StrnCmp(Str, PCIE_SLOT2, StrLen(PCIE_SLOT2)))
    {
      DEBUG ((EFI_D_INFO, "Found PCI SLOT2 Graphics Device.."));
      // If Primary Video not PEG - disable.
      if (BoardSettings.PrimaryVideo != 3) {
        DEBUG ((EFI_D_INFO, "Disabling"));
        Status = gBS->UninstallProtocolInterface(Handle[Index], &gEfiGraphicsOutputProtocolGuid, mGop);
        if (EFI_ERROR (Status)) {
          DEBUG((DEBUG_ERROR, "Uninstalling Handle errored with %x\n", Status));
        }
      }

      DEBUG ((EFI_D_INFO, "\n"));
    } else if
    (!StrnCmp(Str, PCIE_SLOT3, StrLen(PCIE_SLOT3)))
    {
      DEBUG ((EFI_D_INFO, "Found PCI SLOT3 Graphics Device.."));
      // If Primary Video not PEG - disable.
      if (BoardSettings.PrimaryVideo != 4) {
        DEBUG ((EFI_D_INFO, "Disabling"));
        Status = gBS->UninstallProtocolInterface(Handle[Index], &gEfiGraphicsOutputProtocolGuid, mGop);
        if (EFI_ERROR (Status)) {
          DEBUG((DEBUG_ERROR, "Uninstalling Handle errored with %x\n", Status));
        }
      }

      DEBUG ((EFI_D_INFO, "\n"));
    } else if
    (!StrnCmp(Str, PCIE_SLOT4, StrLen(PCIE_SLOT4)))
    {
      DEBUG ((EFI_D_INFO, "Found PCI SLOT4 Graphics Device.."));
      // If Primary Video not PEG - disable.
      if (BoardSettings.PrimaryVideo != 5) {
        DEBUG ((EFI_D_INFO, "Disabling"));
        Status = gBS->UninstallProtocolInterface(Handle[Index], &gEfiGraphicsOutputProtocolGuid, mGop);
        if (EFI_ERROR (Status)) {
          DEBUG((DEBUG_ERROR, "Uninstalling Handle errored with %x\n", Status));
        }
      }

      DEBUG ((EFI_D_INFO, "\n"));
    } else if
    (!StrnCmp(Str, PCIE_SLOT5, StrLen(PCIE_SLOT5)))
    {
      DEBUG ((EFI_D_INFO, "Found PCI SLOT5 Graphics Device.."));
      // If Primary Video not PEG - disable.
      if (BoardSettings.PrimaryVideo != 6) {
        DEBUG ((EFI_D_INFO, "Disabling"));
        Status = gBS->UninstallProtocolInterface(Handle[Index], &gEfiGraphicsOutputProtocolGuid, mGop);
        if (EFI_ERROR (Status)) {
          DEBUG((DEBUG_ERROR, "Uninstalling Handle errored with %x\n", Status));
        }
      }

      DEBUG ((EFI_D_INFO, "\n"));
    } else if
    (!StrnCmp(Str, PCIE_SLOT6, StrLen(PCIE_SLOT6)))
    {
      DEBUG ((EFI_D_INFO, "Found PCI SLOT2 Graphics Device.."));
      // If Primary Video not PEG - disable.
      if (BoardSettings.PrimaryVideo != 7) {
        DEBUG ((EFI_D_INFO, "Disabling"));
        Status = gBS->UninstallProtocolInterface(Handle[Index], &gEfiGraphicsOutputProtocolGuid, mGop);
        if (EFI_ERROR (Status)) {
          DEBUG((DEBUG_ERROR, "Uninstalling Handle errored with %x\n", Status));
        }
      }

      DEBUG ((EFI_D_INFO, "\n"));
    }

  } // for loop

  return;
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
  EFI_INPUT_KEY                Escape;
  EFI_BOOT_MANAGER_LOAD_OPTION BootOption;

  VisitAllInstancesOfProtocol (&gEfiPciRootBridgeIoProtocolGuid,
    ConnectRootBridge, NULL);

  PlatformConsoleInit ();

  //
  // Map Escape to Boot Manager Menu
  //
  Escape.ScanCode    = SCAN_ESC;
  Escape.UnicodeChar = CHAR_NULL;
  EfiBootManagerGetBootManagerMenu (&BootOption);
  EfiBootManagerAddKeyOptionVariable (NULL, (UINT16) BootOption.OptionNumber, 0, &Escape, NULL);

  mSerialConsole.Uart.BaudRate = PcdGet64 (PcdUartDefaultBaudRate);
  mSerialConsole.Uart.DataBits = PcdGet8 (PcdUartDefaultDataBits);
  mSerialConsole.Uart.Parity = PcdGet8 (PcdUartDefaultParity);
  mSerialConsole.Uart.StopBits = PcdGet8 (PcdUartDefaultStopBits);
  //
  // Add the hardcoded serial console device path to ConIn, ConOut, ErrOut.
  //
  CopyGuid (&mSerialConsole.TermType.Guid,
    PcdGetPtr (PcdTerminalTypeGuidBuffer));
  EfiBootManagerUpdateConsoleVariable (ConIn,
    (EFI_DEVICE_PATH_PROTOCOL *)&mSerialConsole, NULL);
  EfiBootManagerUpdateConsoleVariable (ConOut,
    (EFI_DEVICE_PATH_PROTOCOL *)&mSerialConsole, NULL);
  EfiBootManagerUpdateConsoleVariable (ErrOut,
    (EFI_DEVICE_PATH_PROTOCOL *)&mSerialConsole, NULL);


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

  Black.Blue = Black.Green = Black.Red = Black.Reserved = 0;
  White.Blue = White.Green = White.Red = White.Reserved = 0xFF;

  gST->ConOut->ClearScreen (gST->ConOut);
  BootLogoEnableLogo ();

  // FIXME: USB devices are not being detected unless we wait a bit.
  gBS->Stall (100 * 1000);

  EfiBootManagerConnectAll ();
  EfiBootManagerRefreshAllBootOption ();

  //
  // Register UEFI Shell
  //
  //DEBUG((DEBUG_INFO, "Registering UEFI Shell boot option\n"));
  //PlatformRegisterFvBootOption (PcdGetPtr (PcdShellFile), L"UEFI Shell", LOAD_OPTION_ACTIVE);

  //
  // Register iPXE
  //
  //DEBUG((DEBUG_INFO, "Registering iPXE boot option\n"));
  //PlatformRegisterFvBootOption (PcdGetPtr (PcdiPXEFile), L"iPXE Network boot", LOAD_OPTION_ACTIVE);

  Print (L"Press ESC to enter Boot Manager Menu.\n");
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
  return;
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
  EFI_INPUT_KEY                Key;
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

    //
    // Drain any queued keys.
    //
    while (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
      //
      // just throw away Key
      //
    }
  }

  for (;;) {
    EfiBootManagerBoot (&BootManagerMenu);
  }
}

