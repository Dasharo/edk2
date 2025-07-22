/** @file
Sovereign Boot Wizard bootloader parsing.

Copyright (c) 2025, 3mdeb Sp z o.o. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "SovereignBootWizard.h"

///
/// Boot Option from variable Menu
///
SV_MENU_OPTION  BootOptionMenu = {
  SOVEREIGN_BOOT_MENU_OPTION_SIGNATURE,
  { NULL },
  0
};

/**
  This function converts an input device structure to a Unicode string.

  @param DevPath      A pointer to the device path structure.

  @return             A new allocated Unicode string that represents the device path.

**/
CHAR16 *
UiDevicePathToStr (
  IN EFI_DEVICE_PATH_TO_TEXT_PROTOCOL  *DevPathToText,
  IN EFI_DEVICE_PATH_PROTOCOL  *DevPath
  )
{
  CHAR16                            *ToText;

  if (DevPath == NULL || DevPathToText == NULL) {
    return NULL;
  }

  ToText = DevPathToText->ConvertDevicePathToText (
                            DevPath,
                            FALSE,
                            TRUE
                            );
  ASSERT (ToText != NULL);
  return ToText;
}

/**
  Check if it's a Device Path pointing to FV file.

  The function doesn't guarantee the device path points to existing FV file.

  @param  DevicePath     Input device path.

  @retval TRUE   The device path is a FV File Device Path.
  @retval FALSE  The device path is NOT a FV File Device Path.
**/
BOOLEAN
IsFvFilePath (
  IN EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  )
{
  EFI_STATUS                Status;
  EFI_HANDLE                Handle;
  EFI_DEVICE_PATH_PROTOCOL  *Node;
  EFI_DEVICE_PATH_PROTOCOL  *Path;

  Node   = DevicePath;
  Status = gBS->LocateDevicePath (&gEfiFirmwareVolume2ProtocolGuid, &Node, &Handle);
  if (!EFI_ERROR (Status)) {
    return TRUE;
  }

  Path = DevicePath;

  if ((DevicePathType (Path) == HARDWARE_DEVICE_PATH) && (DevicePathSubType (Path) == HW_MEMMAP_DP)) {
    Path = NextDevicePathNode (Path);
    if ((DevicePathType (Path) == MEDIA_DEVICE_PATH) && (DevicePathSubType (Path) == MEDIA_PIWG_FW_FILE_DP)) {
      return IsDevicePathEnd (NextDevicePathNode (Path));
    }
  }

  return FALSE;
}

EFI_DEVICE_PATH_PROTOCOL *
StripFilePath (
  IN EFI_DEVICE_PATH_PROTOCOL  *DevPath
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Path;
  EFI_DEVICE_PATH_PROTOCOL  *Node;

  Path = DuplicateDevicePath(DevPath);

  if (Path == NULL) {
    return NULL;
  }

  for (Node = Path; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if ((DevicePathType (Node) == MEDIA_DEVICE_PATH) &&
        ((DevicePathSubType (Node) == MEDIA_FILEPATH_DP)))
    {
      SetDevicePathEndNode(Node);
      return Path;
    }
  }

  // In case we did not found file path, free the duplicated path
  FreePool (Path);
  return NULL;
}

EFI_DEVICE_PATH_PROTOCOL *
ExtractFilePath (
  IN EFI_DEVICE_PATH_PROTOCOL  *DevPath
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Node;

  for (Node = DevPath; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if ((DevicePathType (Node) == MEDIA_DEVICE_PATH) &&
        ((DevicePathSubType (Node) == MEDIA_FILEPATH_DP)))
    {
      return Node;
    }
  }

  return NULL;
}

/**
  Create a menu entry by given menu type.

  @param MenuType        The Menu type to be created.

  @retval NULL           If failed to create the menu.
  @return the new menu entry.

**/
SV_MENU_ENTRY *
CreateMenuEntry (
  UINTN  MenuType
  )
{
  SV_MENU_ENTRY  *MenuEntry;
  UINTN          ContextSize;

  //
  // Get context size according to menu type
  //
  switch (MenuType) {
    case SOVEREIGN_BOOT_LOAD_CONTEXT_SELECT:
      ContextSize = sizeof (SV_LOAD_CONTEXT);
      break;

    default:
      ContextSize = 0;
      break;
  }

  if (ContextSize == 0) {
    return NULL;
  }

  //
  // Create new menu entry
  //
  MenuEntry = AllocateZeroPool (sizeof (SV_MENU_ENTRY));
  if (MenuEntry == NULL) {
    return NULL;
  }

  MenuEntry->VariableContext = AllocateZeroPool (ContextSize);
  if (MenuEntry->VariableContext == NULL) {
    FreePool (MenuEntry);
    return NULL;
  }

  MenuEntry->Signature        = SOVEREIGN_BOOT_MENU_ENTRY_SIGNATURE;
  MenuEntry->ContextSelection = MenuType;
  return MenuEntry;
}

/**

  Build the BootOptionMenu according to BootOrder Variable.
  This Routine will access the Boot#### to get EFI_LOAD_OPTION.

  @param CallbackData The BMM context data.

  @return EFI_NOT_FOUND Fail to find "BootOrder" variable.
  @return EFI_SUCESS    Success build boot option menu.

**/
EFI_STATUS
GetBootOptions (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private
  )
{
  UINTN                         Index;
  CHAR16                        BootString[10];
  UINT8                         *LoadOptionFromVar;
  UINTN                         BootOptionSize;
  UINT16                        *BootOrderList;
  UINTN                         BootOrderListSize;
  UINT8                         *LoadOptionPtr;
  UINT8                         *LoadOptionEnd;
  SV_MENU_ENTRY                 *NewMenuEntry;
  SV_LOAD_CONTEXT               *NewLoadContext;
  UINTN                         OptionalDataSize;
  UINTN                         StringSize;
  UINTN                         DescriptionSize;
  EFI_DEVICE_PATH_PROTOCOL      *DevicePath;
  EFI_DEVICE_PATH_PROTOCOL      *HwDevicePath;
  UINTN                         MenuCount;
  UINT8                         *Ptr;
  EFI_BOOT_MANAGER_LOAD_OPTION  *BootOption;
  UINTN                         BootOptionCount;
  CHAR16                        *PathString;

  MenuCount         = 0;
  BootOrderListSize = 0;
  BootOrderList     = NULL;
  LoadOptionFromVar = NULL;
  InitializeListHead (&BootOptionMenu.Head);

  //
  // Get the BootOrder from the Var
  //
  GetEfiGlobalVariable2 (L"BootOrder", (VOID **)&BootOrderList, &BootOrderListSize);
  if (BootOrderList == NULL) {
    return EFI_NOT_FOUND;
  }

  BootOption = EfiBootManagerGetLoadOptions (&BootOptionCount, LoadOptionTypeBoot);
  for (Index = 0; Index < BootOrderListSize / sizeof (UINT16); Index++) {
    //
    // Don't display the hidden/inactive boot option
    //
    if (((BootOption[Index].Attributes & LOAD_OPTION_HIDDEN) != 0) ||
        ((BootOption[Index].Attributes & LOAD_OPTION_ACTIVE) == 0)) {
      continue;
    }

    UnicodeSPrint (BootString, sizeof (BootString), L"Boot%04x", BootOrderList[Index]);
    //
    //  Get all loadoptions from the VAR
    //
    GetEfiGlobalVariable2 (BootString, (VOID **)&LoadOptionFromVar, &BootOptionSize);
    if (LoadOptionFromVar == NULL) {
      continue;
    }

    //
    // Is a Legacy Device?
    //
    Ptr = (UINT8 *)LoadOptionFromVar;

    //
    // Attribute = *(UINT32 *)Ptr;
    //
    Ptr += sizeof (UINT32);

    //
    // FilePathSize = *(UINT16 *)Ptr;
    //
    Ptr += sizeof (UINT16);

    //
    // Description = (CHAR16 *)Ptr;
    //
    DescriptionSize = StrSize ((CHAR16 *)Ptr);
    Ptr += DescriptionSize;

    //
    // Now Ptr point to Device Path
    //
    DevicePath = (EFI_DEVICE_PATH_PROTOCOL *)Ptr;

    // Skip boot options that point to FV
    if (IsFvFilePath (DevicePath)) {
      FreePool (LoadOptionFromVar);
      continue;
    }

    NewMenuEntry = CreateMenuEntry (SOVEREIGN_BOOT_LOAD_CONTEXT_SELECT);
    if (NewMenuEntry == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    NewLoadContext = (SV_LOAD_CONTEXT *)NewMenuEntry->VariableContext;

    LoadOptionPtr = LoadOptionFromVar;
    LoadOptionEnd = LoadOptionFromVar + BootOptionSize;

    NewMenuEntry->OptionNumber = BootOrderList[Index];

    if ((BBS_DEVICE_PATH == DevicePath->Type) && (BBS_BBS_DP == DevicePath->SubType)) {
      NewLoadContext->IsLegacy = TRUE;
    } else {
      NewLoadContext->IsLegacy = FALSE;
    }

    //
    // LoadOption is a pointer type of UINT8
    // for easy use with following LOAD_OPTION
    // embedded in this struct
    //
    NewLoadContext->Attributes = *(UINT32 *)LoadOptionPtr;

    LoadOptionPtr += sizeof (UINT32);

    NewLoadContext->FilePathLength = *(UINT16 *)LoadOptionPtr;
    LoadOptionPtr                 += sizeof (UINT16);

    StringSize = StrSize (L"Description: ") + DescriptionSize + sizeof(CHAR16);
    NewLoadContext->Description = AllocateZeroPool (StringSize);
    if (NewLoadContext->Description == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    UnicodeSPrint (NewLoadContext->Description, StringSize, L"Description: %s", LoadOptionPtr);

    NewMenuEntry->DisplayString      = NewLoadContext->Description;
    NewMenuEntry->DisplayStringToken = HiiSetString (Private->HiiHandle, 0, NewLoadContext->Description, NULL);

    LoadOptionPtr += StrSize ((UINT16 *)LoadOptionPtr);

    NewLoadContext->FilePath = AllocateZeroPool (NewLoadContext->FilePathLength);
    if (NewLoadContext->FilePath == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    CopyMem (
      NewLoadContext->FilePath,
      (EFI_DEVICE_PATH_PROTOCOL *)LoadOptionPtr,
      NewLoadContext->FilePathLength
      );

    LoadOptionPtr += NewLoadContext->FilePathLength;

    if (LoadOptionPtr < LoadOptionEnd) {
      OptionalDataSize = BootOptionSize -
                         sizeof (UINT32) -
                         sizeof (UINT16) -
                         DescriptionSize -
                         NewLoadContext->FilePathLength;
      NewLoadContext->OptionalData = AllocateZeroPool (OptionalDataSize);

      if (NewLoadContext->OptionalData == NULL) {
        return EFI_OUT_OF_RESOURCES;
      }

      CopyMem (
        NewLoadContext->OptionalData,
        LoadOptionPtr,
        OptionalDataSize
        );
      NewLoadContext->OptionalDataSize = OptionalDataSize;
    }

    // Hardware Path to the disk
    HwDevicePath = StripFilePath (NewLoadContext->FilePath);
    if (HwDevicePath == NULL) {
      // In case there is no file device path, it means it is /EFI/BOOT/BOOTX64.efi
      // and is automatically expanded by UEFI boot manager
      PathString = UiDevicePathToStr (Private->DevPathToText, NewLoadContext->FilePath);
    } else {
      PathString = UiDevicePathToStr (Private->DevPathToText, HwDevicePath);
      FreePool (HwDevicePath);
    }
    ASSERT (PathString != NULL);
    StringSize = StrSize (L"Hardware path: ") + StrSize (PathString) + sizeof(CHAR16);
    NewMenuEntry->DevicePathString = AllocateZeroPool (StringSize);
    ASSERT (NewMenuEntry->DevicePathString != NULL);
    UnicodeSPrint (NewMenuEntry->DevicePathString, StringSize, L"Hardware path: %s", PathString);
    FreePool (PathString);
    NewMenuEntry->DevicePathStringToken = HiiSetString (Private->HiiHandle, 0, NewMenuEntry->DevicePathString, NULL);

    // File path on the disk
    if (HwDevicePath != NULL) {
      PathString = UiDevicePathToStr (Private->DevPathToText, ExtractFilePath (NewLoadContext->FilePath));
      if (PathString == NULL) {
        return EFI_OUT_OF_RESOURCES;
      }
    } else {
      // In case there is no file device path, it means it is /EFI/BOOT/BOOTX64.efi
      // and is automatically expanded by UEFI boot manager. However when reading
      // the file in the application, we have to expand it ourselves.
      PathString = EFI_REMOVABLE_MEDIA_FILE_NAME;
      NewLoadContext->NeedsPathExpansion = TRUE;
    }
    StringSize = StrSize (L"File path: ") + StrSize (PathString) + sizeof(CHAR16);
    NewMenuEntry->FilePathString = AllocateZeroPool (StringSize);
    if (NewMenuEntry->FilePathString == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    UnicodeSPrint (NewMenuEntry->FilePathString, StringSize, L"File path: %s", PathString);
    if (HwDevicePath != NULL) {
      FreePool (PathString);
    }
    NewMenuEntry->FilePathStringToken = HiiSetString (Private->HiiHandle, 0, NewMenuEntry->FilePathString, NULL);

    InsertTailList (&BootOptionMenu.Head, &NewMenuEntry->Link);
    MenuCount++;
    FreePool (LoadOptionFromVar);
  }

  EfiBootManagerFreeLoadOptions (BootOption, BootOptionCount);

  if (BootOrderList != NULL) {
    FreePool (BootOrderList);
  }

  DEBUG ((EFI_D_INFO, "Found %d boot options \n", MenuCount));

  BootOptionMenu.MenuNumber = MenuCount;
  return EFI_SUCCESS;
}

/**
  Get the Menu Entry from the list in Menu Entry List.

  If MenuNumber is great or equal to the number of Menu
  Entry in the list, then ASSERT.

  @param MenuOption      The Menu Entry List to read the menu entry.
  @param MenuNumber      The index of Menu Entry.

  @return The Menu Entry.

**/
SV_MENU_ENTRY *
GetMenuEntry (
  SV_MENU_OPTION  *MenuOption,
  UINTN           MenuNumber
  )
{
  SV_MENU_ENTRY  *NewMenuEntry;
  UINTN          Index;
  LIST_ENTRY     *List;

  if (MenuNumber >= MenuOption->MenuNumber) {
    return NULL;
  }

  List = MenuOption->Head.ForwardLink;
  for (Index = 0; Index < MenuNumber; Index++) {
    List = List->ForwardLink;
  }

  NewMenuEntry = CR (List, SV_MENU_ENTRY, Link, SOVEREIGN_BOOT_MENU_ENTRY_SIGNATURE);

  return NewMenuEntry;
}

EFI_STATUS
UpdateBootloaderPage (
  IN  SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private
  )
{
  SV_MENU_ENTRY    *BootloaderEntry;
  SV_LOAD_CONTEXT  *BootloaderContext;
  EFI_STRING       NewString;
  EFI_STATUS       Status;

  BootloaderEntry   = GetMenuEntry (&BootOptionMenu, mBootloaderIndex);
  if (BootloaderEntry == NULL) {
    return EFI_NO_MEDIA;
  }

  BootloaderContext = (SV_LOAD_CONTEXT *)BootloaderEntry->VariableContext;
  if (BootloaderContext == NULL) {
    return EFI_NO_MEDIA;
  }

  // Update strings on bootloader page
  NewString = HiiGetString (Private->HiiHandle, BootloaderEntry->DisplayStringToken, NULL);
  if (NewString != NULL) {
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_BOOTOPT_DESCRIPTION), NewString, NULL);
  } else {
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_BOOTOPT_DESCRIPTION), L"Not Found!", NULL);
  }

  NewString = HiiGetString (Private->HiiHandle, BootloaderEntry->DevicePathStringToken, NULL);
  if (NewString != NULL) {
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_HW_PATH), NewString, NULL);
  } else {
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_BOOTOPT_DESCRIPTION), L"Not Found!", NULL);
  }

  NewString = HiiGetString (Private->HiiHandle, BootloaderEntry->FilePathStringToken, NULL);
  if (NewString != NULL) {
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_FILE_PATH), NewString, NULL);
  } else {
    HiiSetString (Private->HiiHandle, STRING_TOKEN (STR_BOOTOPT_DESCRIPTION), L"Not Found!", NULL);
  }

  // Filling security context is time-consuming and may cause unnecessary
  // delays. Also we should parse the certificates dynamically as user gives
  // their choices, so that the keys appearing will always have up to date
  // state (trusted/untrusted), so they can be skipped.
  if (BootloaderEntry->SecurityContext == NULL) {
    FillSecurityContext(BootloaderEntry);
  }

  Status = UpdateCertInfo (Private, mBootloaderIndex, mCertIndex);
  mCertIndex++;
  if (Status == EFI_NO_MEDIA || ((mCertIndex > 1) && Private->FormData.ImageUnsigned)) {
    DEBUG ((EFI_D_INFO, "No more keys/certs for bootloader %u (current certificate %u)\n",
       mBootloaderIndex, mCertIndex - 1));
    // No more keys/certs to show for this bootloader, proceed to the next one
    mCertIndex = 0;
    mBootloaderIndex++;
    Status = UpdateBootloaderPage(Private);
  }

  return Status;
}
