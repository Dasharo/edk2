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

STATIC CONST UINT8 DevicePathAllowList [][2] = {
  { MEDIA_DEVICE_PATH, MEDIA_HARDDRIVE_DP },
  { MEDIA_DEVICE_PATH, MEDIA_CDROM_DP },
  { MESSAGING_DEVICE_PATH, MSG_USB_DP },
  { MESSAGING_DEVICE_PATH, MSG_SATA_DP },
  { MESSAGING_DEVICE_PATH, MSG_NVME_NAMESPACE_DP },
  { MESSAGING_DEVICE_PATH, MSG_SD_DP },
  { MESSAGING_DEVICE_PATH, MSG_EMMC_DP }
};

BOOLEAN
IsPathAllowed (
  IN EFI_DEVICE_PATH_PROTOCOL  *Path
  )
{
  UINTN Idx;

  for (Idx = 0; Idx < sizeof (DevicePathAllowList) / 2; Idx++) {
    if ((DevicePathType (Path) == DevicePathAllowList[Idx][0]) &&
        (DevicePathSubType (Path) == DevicePathAllowList[Idx][1])) {
        return TRUE;
    }
  }

  return FALSE;
}

/**
  Check if it's a Device Path pointing to HDD.

  @param  DevicePath     Input device path.

  @retval TRUE   The device path is a HDD Device Path.
  @retval FALSE  The device path is NOT a HDD File Device Path.
**/
BOOLEAN
IsHddFilePath (
  IN EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *FullPath = NULL;
  EFI_DEVICE_PATH_PROTOCOL  *Path;

  // Awlays try to expand the path. We may get a shortened Device Path here.
  FullPath = EfiBootManagerGetNextLoadOptionDevicePath (DevicePath, FullPath);

  Path = FullPath;
  while (!IsDevicePathEnd (Path)) {
    if (IsPathAllowed (Path)) {
        FREE_NON_NULL (FullPath);
        return TRUE;
    }

    Path = NextDevicePathNode (Path);
  }

  FREE_NON_NULL (FullPath);

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
  FREE_NON_NULL (Path);
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

STATIC EFI_STATUS
FillMenuEntryFromBootOption (
  IN     SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN     UINT16                              BootOptionIndex,
  IN OUT SV_MENU_ENTRY                       **MenuEntry
  )
{
  CHAR16                        BootString[10];
  UINT8                         *LoadOptionFromVar;
  UINTN                         BootOptionSize;
  UINT8                         *LoadOptionPtr;
  UINT8                         *LoadOptionEnd;
  SV_MENU_ENTRY                 *NewMenuEntry;
  SV_LOAD_CONTEXT               *NewLoadContext;
  UINTN                         OptionalDataSize;
  UINTN                         StringSize;
  UINTN                         DescriptionSize;
  EFI_DEVICE_PATH_PROTOCOL      *DevicePath;
  EFI_DEVICE_PATH_PROTOCOL      *HwDevicePath;
  UINT8                         *Ptr;
  CHAR16                        *PathString;

  if ((Private == NULL) || (MenuEntry == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *MenuEntry = NULL;

  UnicodeSPrint (BootString, sizeof (BootString), L"Boot%04x", BootOptionIndex);
  //
  //  Get all loadoptions from the VAR
  //
  GetEfiGlobalVariable2 (BootString, (VOID **)&LoadOptionFromVar, &BootOptionSize);
  if (LoadOptionFromVar == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

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

  // Skip boot options that do not point to disks
  if (!IsHddFilePath (DevicePath)) {
    DEBUG ((DEBUG_INFO, "Boot option does not contain a HD path\n"));
    FreePool (LoadOptionFromVar);
    return EFI_ABORTED;
  }

  NewMenuEntry = CreateMenuEntry (SOVEREIGN_BOOT_LOAD_CONTEXT_SELECT);
  if (NewMenuEntry == NULL) {
    FreePool (LoadOptionFromVar);
    return EFI_OUT_OF_RESOURCES;
  }

  NewLoadContext = (SV_LOAD_CONTEXT *)NewMenuEntry->VariableContext;

  LoadOptionPtr = LoadOptionFromVar;
  LoadOptionEnd = LoadOptionFromVar + BootOptionSize;

  NewMenuEntry->OptionNumber = BootOptionIndex;

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

  NewLoadContext->Description = AllocateZeroPool (DescriptionSize);
  if (NewLoadContext->Description == NULL) {
    FreePool (LoadOptionFromVar);
    return EFI_OUT_OF_RESOURCES;
  }
  StrCpyS (NewLoadContext->Description, DescriptionSize / sizeof(CHAR16), (CONST CHAR16 *)LoadOptionPtr);

  StringSize = StrSize (L"Description: ") + DescriptionSize;
  NewMenuEntry->DisplayString = AllocateZeroPool (StringSize);
  if (NewMenuEntry->DisplayString == NULL) {
    FreePool (LoadOptionFromVar);
    return EFI_OUT_OF_RESOURCES;
  }

  UnicodeSPrint (NewMenuEntry->DisplayString, StringSize, L"Description: %s", LoadOptionPtr);
  NewMenuEntry->DisplayStringToken = HiiSetString (
                                       Private->HiiHandle,
                                       0,
                                       NewMenuEntry->DisplayString,
                                       NULL);

  LoadOptionPtr += StrSize ((UINT16 *)LoadOptionPtr);

  NewLoadContext->FilePath = AllocateZeroPool (NewLoadContext->FilePathLength);
  if (NewLoadContext->FilePath == NULL) {
    FreePool (LoadOptionFromVar);
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
      FreePool (LoadOptionFromVar);
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
    NewLoadContext->NeedsPathExpansion = TRUE;
  } else {
    PathString = UiDevicePathToStr (Private->DevPathToText, HwDevicePath);
    FREE_NON_NULL (HwDevicePath);
  }
  ASSERT (PathString != NULL);
  StringSize = StrSize (L"Hardware path: ") + StrSize (PathString) + sizeof(CHAR16);
  NewMenuEntry->DevicePathString = AllocateZeroPool (StringSize);
  ASSERT (NewMenuEntry->DevicePathString != NULL);
  UnicodeSPrint (NewMenuEntry->DevicePathString, StringSize, L"Hardware path: %s", PathString);
  FREE_NON_NULL (PathString);
  NewMenuEntry->DevicePathStringToken = HiiSetString (
                                          Private->HiiHandle,
                                          0,
                                          NewMenuEntry->DevicePathString,
                                          NULL);

  // File path on the disk
  if (!NewLoadContext->NeedsPathExpansion) {
    PathString = UiDevicePathToStr (
                   Private->DevPathToText,
                   ExtractFilePath (NewLoadContext->FilePath));
    if (PathString == NULL) {
      FREE_NON_NULL (LoadOptionFromVar);
      return EFI_OUT_OF_RESOURCES;
    }
  } else {
    // In case there is no file device path, it means it is /EFI/BOOT/BOOTX64.efi
    // and is automatically expanded by UEFI boot manager. However when reading
    // the file in the application, we have to expand it ourselves.
    PathString = EFI_REMOVABLE_MEDIA_FILE_NAME;
  }
  StringSize = StrSize (L"File path: ") + StrSize (PathString) + sizeof(CHAR16);
  NewMenuEntry->FilePathString = AllocateZeroPool (StringSize);
  if (NewMenuEntry->FilePathString == NULL) {
    if (!NewLoadContext->NeedsPathExpansion) {
      FREE_NON_NULL (PathString);
    }
    return EFI_OUT_OF_RESOURCES;
  }

  UnicodeSPrint (NewMenuEntry->FilePathString, StringSize, L"File path: %s", PathString);
  if (!NewLoadContext->NeedsPathExpansion) {
    FREE_NON_NULL (PathString);
  }
  NewMenuEntry->FilePathStringToken = HiiSetString (
                                        Private->HiiHandle,
                                        0,
                                        NewMenuEntry->FilePathString,
                                        NULL);

  FREE_NON_NULL (LoadOptionFromVar);

  *MenuEntry = NewMenuEntry;

  return EFI_SUCCESS;
}

/**
  Checks if two device paths are the same.

  @param[in] Entry1         First entry to compare
  @param[in] Entry2         Second entry to compare

  @retval TRUE              The device paths share the same nodes and values
  @retval FALSE             The device paths differ
**/
STATIC BOOLEAN
MenuPathsAreEqual (
  IN CONST SV_MENU_ENTRY *Entry1,
  IN CONST SV_MENU_ENTRY *Entry2
  )
{
  CHAR16 *Str1;
  CHAR16 *Str2;

  if ((Entry1->DevicePathString == NULL) || (Entry1->FilePathString == NULL) ||
      (Entry2->DevicePathString == NULL) || (Entry2->FilePathString == NULL)) {
    return FALSE;
  }

  // Check if this is the same full disk device path. Sometimes paths can be
  // shorter and do not include only the HD device path and file path. Should
  // we use StrStr then?
  if (StrCmp (Entry1->DevicePathString, Entry2->DevicePathString) != 0) {
    return FALSE;
  }

  // Compare the paths in lower case, as case does not matter in UEFI BM
  Str1 = AllocateCopyPool(StrSize(Entry1->FilePathString), Entry1->FilePathString);
  Str2 = AllocateCopyPool(StrSize(Entry2->FilePathString), Entry2->FilePathString);

  ToLowerString(Str1);
  ToLowerString(Str2);

  if (StrCmp (Str1, Str2) != 0) {
    FREE_NON_NULL (Str1);
    FREE_NON_NULL (Str2);
    return FALSE;
  }

  FREE_NON_NULL (Str1);
  FREE_NON_NULL (Str2);

  return TRUE;
}

BOOLEAN
CheckIfEntryIsDuplicate (
  IN SV_MENU_ENTRY *MenuEntry
  )
{
  SV_MENU_ENTRY            *BootloaderEntry;
  UINTN                    Index;

  if (MenuEntry == NULL) {
    DEBUG ((DEBUG_WARN, "Null entry, assume duplicate\n"));
    return TRUE;
  }

  Index = 0;
  while (Index < BootOptionMenu.MenuNumber) {
    BootloaderEntry = GetMenuEntry (&BootOptionMenu, Index++);
    if (BootloaderEntry == NULL) {
      DEBUG ((DEBUG_WARN, "Bootloader entry is NULL\n"));
      continue;
    }

    DEBUG ((DEBUG_INFO, "Comparing:\n\t%s %s\n\t%s %s\n",
            MenuEntry->DevicePathString, MenuEntry->FilePathString,
            BootloaderEntry->DevicePathString, BootloaderEntry->FilePathString
            ));

    if (MenuPathsAreEqual (MenuEntry, BootloaderEntry)) {
      DEBUG ((DEBUG_WARN, "Found duplicate entry\n"));
      return TRUE;
    }
  }

  DEBUG ((DEBUG_WARN, "Not a duplicate entry\n"));
  return FALSE;
}

EFI_STATUS
FillMenuEntryFromDevicePath (
  IN     SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN     EFI_HANDLE                          DeviceHandle,
  IN     EFI_DEVICE_PATH_PROTOCOL            *DevicePath,
  IN OUT SV_MENU_ENTRY                       **MenuEntry
  )
{
  SV_MENU_ENTRY                 *NewMenuEntry;
  SV_LOAD_CONTEXT               *NewLoadContext;
  UINTN                         StringSize;
  EFI_DEVICE_PATH_PROTOCOL      *HwDevicePath;
  CHAR16                        *PathString;
  CHAR16                        *Description;

  if ((Private == NULL) || (MenuEntry == NULL) || (DevicePath == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *MenuEntry = NULL;

  // Skip boot options that do not point to disks
  if (!IsHddFilePath (DevicePath)) {
    DEBUG ((DEBUG_INFO, "Boot option does not contain a HD path\n"));
    return EFI_ABORTED;
  }

  NewMenuEntry = CreateMenuEntry (SOVEREIGN_BOOT_LOAD_CONTEXT_SELECT);
  if (NewMenuEntry == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  NewLoadContext = (SV_LOAD_CONTEXT *)NewMenuEntry->VariableContext;

  NewMenuEntry->OptionNumber = LoadOptionNumberUnassigned;

  if ((BBS_DEVICE_PATH == DevicePath->Type) && (BBS_BBS_DP == DevicePath->SubType)) {
    NewLoadContext->IsLegacy = TRUE;
  } else {
    NewLoadContext->IsLegacy = FALSE;
  }

  NewLoadContext->Attributes = LOAD_OPTION_ACTIVE;
  NewLoadContext->FilePathLength = GetDevicePathSize(DevicePath);
  NewLoadContext->FilePath = DuplicateDevicePath (DevicePath);
  if (NewLoadContext->FilePath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  NewLoadContext->OptionalData = NULL;
  NewLoadContext->OptionalDataSize = 0;

  Description = EfiBootManagerGetBootDescription(DeviceHandle);
  NewLoadContext->Description = AllocateCopyPool(StrSize(Description), Description);
  if (NewLoadContext->Description != NULL) {
    StringSize = StrSize (L"Description: ") + StrSize(NewLoadContext->Description);
    NewMenuEntry->DisplayString = AllocateZeroPool (StringSize);
    if (NewMenuEntry->DisplayString == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    UnicodeSPrint (NewMenuEntry->DisplayString,
                   StringSize,
                   L"Description: %s",
                   NewLoadContext->Description);
    NewMenuEntry->DisplayStringToken = HiiSetString (
                                        Private->HiiHandle,
                                        0,
                                        NewMenuEntry->DisplayString,
                                        NULL);
  }

  // Hardware Path to the disk
  HwDevicePath = StripFilePath (NewLoadContext->FilePath);
  if (HwDevicePath == NULL) {
    // In case there is no file device path, it means it is /EFI/BOOT/BOOTX64.efi
    // and is automatically expanded by UEFI boot manager
    PathString = UiDevicePathToStr (Private->DevPathToText, NewLoadContext->FilePath);
    NewLoadContext->NeedsPathExpansion = TRUE;
  } else {
    PathString = UiDevicePathToStr (Private->DevPathToText, HwDevicePath);
    FREE_NON_NULL (HwDevicePath);
  }
  ASSERT (PathString != NULL);
  StringSize = StrSize (L"Hardware path: ") + StrSize (PathString) + sizeof(CHAR16);
  NewMenuEntry->DevicePathString = AllocateZeroPool (StringSize);
  ASSERT (NewMenuEntry->DevicePathString != NULL);
  UnicodeSPrint (NewMenuEntry->DevicePathString, StringSize, L"Hardware path: %s", PathString);
  FREE_NON_NULL (PathString);
  NewMenuEntry->DevicePathStringToken = HiiSetString (
                                          Private->HiiHandle,
                                          0,
                                          NewMenuEntry->DevicePathString,
                                          NULL);

  // File path on the disk
  if (!NewLoadContext->NeedsPathExpansion) {
    PathString = UiDevicePathToStr (
                   Private->DevPathToText,
                   ExtractFilePath (NewLoadContext->FilePath));
    if (PathString == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
  } else {
    // In case there is no file device path, it means it is /EFI/BOOT/BOOTX64.efi
    // and is automatically expanded by UEFI boot manager. However when reading
    // the file in the application, we have to expand it ourselves.
    PathString = EFI_REMOVABLE_MEDIA_FILE_NAME;
  }
  StringSize = StrSize (L"File path: ") + StrSize (PathString) + sizeof(CHAR16);
  NewMenuEntry->FilePathString = AllocateZeroPool (StringSize);
  if (NewMenuEntry->FilePathString == NULL) {
    if (!NewLoadContext->NeedsPathExpansion) {
      FREE_NON_NULL (PathString);
    }
    return EFI_OUT_OF_RESOURCES;
  }

  UnicodeSPrint (NewMenuEntry->FilePathString, StringSize, L"File path: %s", PathString);
  if (!NewLoadContext->NeedsPathExpansion) {
    FREE_NON_NULL (PathString);
  }
  NewMenuEntry->FilePathStringToken = HiiSetString (
                                        Private->HiiHandle,
                                        0,
                                        NewMenuEntry->FilePathString,
                                        NULL);

  *MenuEntry = NewMenuEntry;

  return EFI_SUCCESS;
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
  UINT16                        *BootOrderList;
  UINTN                         BootOrderListSize;
  SV_MENU_ENTRY                 *NewMenuEntry;
  UINTN                         MenuCount;
  EFI_BOOT_MANAGER_LOAD_OPTION  *BootOption;
  UINTN                         BootOptionCount;
  EFI_STATUS                    Status;

  MenuCount         = 0;
  BootOrderListSize = 0;
  BootOrderList     = NULL;
  InitializeListHead (&BootOptionMenu.Head);

  DEBUG ((DEBUG_INFO, "Locating boot options\n"));

  if (Private->ConfigData.AppLaunchCause == SV_BOOT_LAUNCH_IMAGE_VERIFICATION_FAILED) {
    // Some boot options may not have entries returned by EfiBootManagerGetLoadOptions
    // Query the Boot#### variable directly using BootCurrent index.
    Status = FillMenuEntryFromBootOption (Private, Private->ConfigData.BootCurrent, &NewMenuEntry);
    if (EFI_ERROR (Status)) {
      FreeBootMenuEntry (NewMenuEntry);
      return Status;
    }

    InsertTailList (&BootOptionMenu.Head, &NewMenuEntry->Link);
    BootOptionMenu.MenuNumber = 1;
    return EFI_SUCCESS;
  }

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

    NewMenuEntry = NULL;
    Status = FillMenuEntryFromBootOption (Private, BootOption[Index].OptionNumber, &NewMenuEntry);
    if (EFI_ERROR (Status)) {
      FreeBootMenuEntry (NewMenuEntry);
      if (Status == EFI_ABORTED) {
        continue;
      }
      return Status;
    }

    InsertTailList (&BootOptionMenu.Head, &NewMenuEntry->Link);
    MenuCount++;
  }

  EfiBootManagerFreeLoadOptions (BootOption, BootOptionCount);

  FREE_NON_NULL (BootOrderList);

  BootOptionMenu.MenuNumber = MenuCount;

  Status = ScanFileSystemsForBootOptions (Private, &BootOptionMenu);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Scanning of filesystems failed with %r\n", Status));
  }

  DEBUG ((DEBUG_INFO, "Found %d boot options \n", BootOptionMenu.MenuNumber));

  if (BootOptionMenu.MenuNumber == 0) {
    return EFI_NOT_FOUND;
  }

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
  EFI_STRING       NewString;
  EFI_STATUS       Status;

  while (mBootloaderIndex < BootOptionMenu.MenuNumber) {
    BootloaderEntry   = GetMenuEntry (&BootOptionMenu, mBootloaderIndex);
    if (BootloaderEntry == NULL) {
      return EFI_NO_MEDIA;
    }

    // Filling security context is time-consuming and may cause unnecessary
    // delays. Also we should parse the certificates dynamically as user gives
    // their choices, so that the keys appearing will always have up to date
    // state (trusted/untrusted), so they can be skipped.
    if (BootloaderEntry->SecurityContext == NULL) {
      Status = FillSecurityContext(BootloaderEntry);
      if (EFI_ERROR (Status)) {
        DEBUG ((DEBUG_ERROR, "Failed to fill security context for bootloader %u\n", mBootloaderIndex));
        return EFI_NO_MEDIA;
      }
    }

    Status = UpdateCertInfo (Private, mBootloaderIndex);
    if (Status == EFI_NO_MEDIA) {
      DEBUG ((DEBUG_INFO, "No more keys/certificates for bootloader %u\n", mBootloaderIndex));
      // No more keys/certs to show for this bootloader, proceed to the next one
      mCertIndex = 0;
      mBootloaderIndex++;
      continue;
    }

    break;
  }

  if (mBootloaderIndex >= BootOptionMenu.MenuNumber) {
    DEBUG ((DEBUG_INFO, "No more keys/certificates/bootloaders to show\n"));
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

  return Status;
}

STATIC VOID
FreeLoadContext (
  SV_LOAD_CONTEXT  *LoadCtx
  )
{
  if (LoadCtx == NULL) {
    return;
  }

  FREE_NON_NULL (LoadCtx->Description);
  FREE_NON_NULL (LoadCtx->FilePath);
  FREE_NON_NULL (LoadCtx->OptionalData);
}

VOID
FreeBootMenuEntry (
  SV_MENU_ENTRY    *BootloaderEntry
  )
{
  if (BootloaderEntry == NULL) {
    return;
  }

  FreeLoadContext (BootloaderEntry->VariableContext);
  FreeSecurityContext (BootloaderEntry->SecurityContext);

  FREE_NON_NULL (BootloaderEntry->SecurityContext);
  FREE_NON_NULL (BootloaderEntry->VariableContext);
  FREE_NON_NULL (BootloaderEntry->DisplayString);
  FREE_NON_NULL (BootloaderEntry->DevicePathString);
  FREE_NON_NULL (BootloaderEntry->FilePathString);
}


VOID
FreeBootMenuEntries (
  VOID
  )
{
  SV_MENU_ENTRY    *BootloaderEntry;

  if (BootOptionMenu.MenuNumber == 0) {
    return;
  }

  while (!IsListEmpty (&BootOptionMenu.Head)) {
    BootloaderEntry = CR (
                        BootOptionMenu.Head.ForwardLink,
                        SV_MENU_ENTRY,
                        Link,
                        SOVEREIGN_BOOT_MENU_ENTRY_SIGNATURE);
    RemoveEntryList (&BootloaderEntry->Link);
    FreeBootMenuEntry (BootloaderEntry);
  }

  BootOptionMenu.MenuNumber = 0;
}
