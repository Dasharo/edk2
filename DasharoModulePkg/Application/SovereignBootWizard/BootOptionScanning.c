/** @file
Sovereign Boot Wizard bootloader parsing.

Copyright (c) 2025, 3mdeb Sp z o.o. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "SovereignBootWizard.h"

#include <Guid/FileInfo.h>


/**

  Get file type base on the file name.
  Just cut the file name, from the ".". eg ".efi"

  @param FileName  File need to be checked.

  @retval the file type string.

**/
CHAR16 *
GetTypeFromName (
  IN CHAR16  *FileName
  )
{
  UINTN  Index;

  Index = StrLen (FileName) - 1;
  while ((FileName[Index] != L'.') && (Index != 0)) {
    Index--;
  }

  return Index == 0 ? NULL : &FileName[Index];
}

/**
  Converts the unicode character of the string from uppercase to lowercase.
  This is a internal function.

  @param ConfigString  String to be converted

**/
VOID
ToLowerString (
  IN CHAR16  *String
  )
{
  CHAR16  *TmpStr;

  for (TmpStr = String; *TmpStr != L'\0'; TmpStr++) {
    if ((*TmpStr >= L'A') && (*TmpStr <= L'Z')) {
      *TmpStr = (CHAR16)(*TmpStr - L'A' + L'a');
    }
  }
}

/**

  Check whether current FileName point to a valid
  Efi Image File.

  @param FileName  File need to be checked.

  @retval TRUE  Is Efi Image
  @retval FALSE Not a valid Efi Image

**/
BOOLEAN
IsSupportedFileType (
  IN UINT16  *FileName
  )
{
  CHAR16   *InputFileType;
  CHAR16   *TmpStr;
  BOOLEAN  IsSupported;

  InputFileType = GetTypeFromName (FileName);
  //
  // If the file not has *.* style, always return FALSE.
  //
  if (InputFileType == NULL) {
    return FALSE;
  }

  TmpStr = AllocateCopyPool (StrSize (InputFileType), InputFileType);
  ASSERT (TmpStr != NULL);
  ToLowerString (TmpStr);

  IsSupported = (StrStr (L".efi", TmpStr) == NULL ? FALSE : TRUE);

  FreePool (TmpStr);
  return IsSupported;
}

/**

  Append file name to existing file name.

  @param Str1  The existing file name
  @param Str2  The file name to be appended

  @return Allocate a new string to hold the appended result.
          Caller is responsible to free the returned string.

**/
CHAR16 *
AppendFileName (
  IN  CHAR16  *Str1,
  IN  CHAR16  *Str2
  )
{
  UINTN   Size1;
  UINTN   Size2;
  UINTN   MaxLen;
  CHAR16  *Str;
  CHAR16  *TmpStr;
  CHAR16  *Ptr;
  CHAR16  *LastSlash;

  Size1 = StrSize (Str1);
  Size2 = StrSize (Str2);

  //
  // Check overflow
  //
  if (((MAX_UINTN - Size1) < Size2) || ((MAX_UINTN - Size1 - Size2) < sizeof (CHAR16))) {
    return NULL;
  }

  MaxLen = (Size1 + Size2 + sizeof (CHAR16))/ sizeof (CHAR16);
  Str    = AllocateZeroPool (Size1 + Size2 + sizeof (CHAR16));
  ASSERT (Str != NULL);

  TmpStr = AllocateZeroPool (Size1 + Size2 + sizeof (CHAR16));
  ASSERT (TmpStr != NULL);

  StrCpyS (Str, MaxLen, Str1);
  if (!((*Str == '\\') && (*(Str + 1) == 0))) {
    StrCatS (Str, MaxLen, L"\\");
  }

  StrCatS (Str, MaxLen, Str2);

  Ptr       = Str;
  LastSlash = Str;
  while (*Ptr != 0) {
    if ((*Ptr == '\\') && (*(Ptr + 1) == '.') && (*(Ptr + 2) == '.') && (*(Ptr + 3) == L'\\')) {
      //
      // Convert "\Name\..\" to "\"
      // DO NOT convert the .. if it is at the end of the string. This will
      // break the .. behavior in changing directories.
      //

      //
      // Use TmpStr as a backup, as StrCpyS in BaseLib does not handle copy of two strings
      // that overlap.
      //
      StrCpyS (TmpStr, MaxLen, Ptr + 3);
      StrCpyS (LastSlash, MaxLen - ((UINTN)LastSlash - (UINTN)Str) / sizeof (CHAR16), TmpStr);
      Ptr = LastSlash;
    } else if ((*Ptr == '\\') && (*(Ptr + 1) == '.') && (*(Ptr + 2) == '\\')) {
      //
      // Convert a "\.\" to a "\"
      //

      //
      // Use TmpStr as a backup, as StrCpyS in BaseLib does not handle copy of two strings
      // that overlap.
      //
      StrCpyS (TmpStr, MaxLen, Ptr + 2);
      StrCpyS (Ptr, MaxLen - ((UINTN)Ptr - (UINTN)Str) / sizeof (CHAR16), TmpStr);
      Ptr = LastSlash;
    } else if (*Ptr == '\\') {
      LastSlash = Ptr;
    }

    Ptr++;
  }

  FreePool (TmpStr);

  return Str;
}

EFI_HANDLE
DiskHandleByFsHandle (
  EFI_HANDLE   FsHandle
)
{
  UINTN                                 HandleCount;
  EFI_HANDLE                            *Handles;
  EFI_HANDLE                            DiskHandle;
  UINTN                                 Index;
  EFI_DEVICE_PATH_PROTOCOL              *DiskDevicePath;
  EFI_DEVICE_PATH_PROTOCOL              *FileSystemDevicePath;
  EFI_DEVICE_PATH_PROTOCOL              *TempFileSystemDevicePath;
  BOOLEAN                               FoundMatch;

  FoundMatch = FALSE;
  FileSystemDevicePath = DevicePathFromHandle (FsHandle);

  gBS->LocateHandleBuffer (
         ByProtocol,
         &gEfiBlockIoProtocolGuid,
         NULL,
         &HandleCount,
         &Handles
         );
  for (Index = 0; Index < HandleCount; Index++) {

    DiskDevicePath = DevicePathFromHandle (Handles[Index]);
    TempFileSystemDevicePath = FileSystemDevicePath;

    while (!IsDevicePathEnd (DiskDevicePath) && !IsDevicePathEnd (TempFileSystemDevicePath)) {

      if (!CompareMem(TempFileSystemDevicePath, DiskDevicePath, DevicePathNodeLength(TempFileSystemDevicePath))) {
        if ((DevicePathType (DiskDevicePath) == MEDIA_DEVICE_PATH) &&
            (DevicePathSubType (DiskDevicePath) == MEDIA_HARDDRIVE_DP)) {
          // If DiskDevicePath has HardDrive DP, it is not the one we look for
          break;
        }
        // Continue search
        TempFileSystemDevicePath = NextDevicePathNode (TempFileSystemDevicePath);
        DiskDevicePath = NextDevicePathNode (DiskDevicePath);

        // If we reached the end, check for a match, because the loop will not check it on next iteration
        if (IsDevicePathEnd (DiskDevicePath)) {
          if ((DevicePathType (TempFileSystemDevicePath) == MEDIA_DEVICE_PATH) &&
              (DevicePathSubType (TempFileSystemDevicePath) == MEDIA_HARDDRIVE_DP)) {
            FoundMatch = TRUE;
            DiskHandle = Handles[Index];
          }
        }
      } else {
        // If we found first uncommon node and it is HardDrive DP, then we have a match
        if ((DevicePathType (TempFileSystemDevicePath) == MEDIA_DEVICE_PATH) &&
            (DevicePathSubType (TempFileSystemDevicePath) == MEDIA_HARDDRIVE_DP)) {
          FoundMatch = TRUE;
          DiskHandle = Handles[Index];
        }
        break;
      }
    }

    if (FoundMatch) {
      if (HandleCount != 0)
        FreePool (Handles);

      return DiskHandle;
    }

  }

  if (HandleCount != 0)
    FreePool (Handles);

  // No match, return the FS handle. Description will not be the one we would like to be though.
  return FsHandle;
}

/**
  Find files under current directory.

  All files and sub-directories in current directory
  will be stored in DirectoryMenu for future use.

  @param MenuOptions   Current list of menu options.
  @param MenuCount     Current count of menu options.
  @param FileHandle    Parent file handle.
  @param FileName      Parent file name.
  @param DeviceHandle  Driver handle for this partition.

  @retval EFI_SUCCESS         Get files from current dir successfully.
  @return Other value if can't get files from current dir.

**/
EFI_STATUS
FindEfiFiles (
  IN SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA *Private,
  IN SV_MENU_OPTION                     *MenuOptions,
  IN EFI_FILE_HANDLE                    FileHandle,
  IN CHAR16                             *FileName,
  IN EFI_HANDLE                         DeviceHandle,
  IN BOOLEAN                            IsRoot
  )
{
  EFI_FILE_INFO             *DirInfo;
  EFI_FILE_HANDLE           NewDir;
  UINTN                     BufferSize;
  UINTN                     DirBufferSize;
  SV_MENU_ENTRY             *NewMenuEntry;
  EFI_DEVICE_PATH_PROTOCOL  *FileDevPath;
  CHAR16                    *File;
  CHAR16                    *Path;
  UINTN                     Pass;
  EFI_STATUS                Status;
  BOOLEAN                   IsDir;

  DirBufferSize = sizeof (EFI_FILE_INFO) + 1024;
  DirInfo       = AllocateZeroPool (DirBufferSize);
  if (DirInfo == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Get all files in current directory
  // Pass 1 to get Directories
  // Pass 2 to get files that are EFI images
  //
  Status = EFI_SUCCESS;
  for (Pass = 1; Pass <= 2; Pass++) {
    FileHandle->SetPosition (FileHandle, 0);
    for ( ; ;) {
      BufferSize = DirBufferSize;
      Status     = FileHandle->Read (FileHandle, &BufferSize, DirInfo);
      if (EFI_ERROR (Status) || (BufferSize == 0)) {
        Status = EFI_SUCCESS;
        break;
      }
      if ((((DirInfo->Attribute & EFI_FILE_DIRECTORY) != 0) && (Pass == 2)) ||
          (((DirInfo->Attribute & EFI_FILE_DIRECTORY) == 0) && (Pass == 1))
          )
      {
        //
        // Pass 1 is for Directories
        // Pass 2 is for file names
        //
        continue;
      }

      if (!(((DirInfo->Attribute & EFI_FILE_DIRECTORY) != 0) || IsSupportedFileType (DirInfo->FileName))) {
        //
        // Skip file unless it is a directory entry or a .EFI file
        //
        continue;
      }

      IsDir = (BOOLEAN)((DirInfo->Attribute & EFI_FILE_DIRECTORY) == EFI_FILE_DIRECTORY);

      Path = AppendFileName(FileName, DirInfo->FileName);
      if (Path == NULL) {
        DEBUG ((DEBUG_ERROR, "Failed to append file name for file %s\n",  DirInfo->FileName));
        continue;
      }
      FileDevPath = FileDevicePath (DeviceHandle, Path);
      if (FileDevPath == NULL) {
        DEBUG ((DEBUG_ERROR, "Failed to get device path for file %s\n",  DirInfo->FileName));
        continue;
      }
      File = ConvertDevicePathToText (FileDevPath, FALSE, FALSE);
      if (File == NULL) {
        FreePool (FileDevPath);
        DEBUG ((DEBUG_ERROR, "Failed to convert device path for file %s\n", DirInfo->FileName));
        continue;
      }
      if (IsDir) {
        // Skip current and parent directory entries
        if ((StrCmp(DirInfo->FileName, L".") == 0) ||
            (StrCmp(DirInfo->FileName, L"..") == 0)) {
          continue;
        }
        //
        // Open directory to get files from it
        //
        Status = FileHandle->Open (
                            FileHandle,
                            &NewDir,
                            DirInfo->FileName,
                            EFI_FILE_READ_ONLY,
                            0
                            );
        if (!EFI_ERROR (Status)) {
          Status = FindEfiFiles (
                     Private,
                     MenuOptions,
                     NewDir,
                     Path,
                     DeviceHandle,
                     FALSE
                     );
          if (!IsRoot) {
            NewDir->Close (NewDir);
          }
        }
      } else {
        NewMenuEntry = NULL;
        Status = FillMenuEntryFromDevicePath (
                   Private,
                   DiskHandleByFsHandle(DeviceHandle),
                   FileDevPath,
                   &NewMenuEntry
                   );
        if (!EFI_ERROR (Status)) {
          if (!CheckIfEntryIsDuplicate (NewMenuEntry)) {
            InsertTailList (&MenuOptions->Head, &NewMenuEntry->Link);
            MenuOptions->MenuNumber++;
          } else {
            FreeBootMenuEntry (NewMenuEntry);
          }
        } else {
          DEBUG ((DEBUG_ERROR, "Failed to fill menu entry info from device path: %r\n", Status));
          FreeBootMenuEntry (NewMenuEntry);
        }

      }
      FREE_NON_NULL (Path);
      FREE_NON_NULL (File);
      FREE_NON_NULL (FileDevPath);
    }
  }

  if (IsRoot) {
    FileHandle->Close (FileHandle);
  }

  FreePool (DirInfo);

  return Status;
}

/**
  Find the root file handle from the input filesystem handle.

  @param  Esphandle        Handle with the gEfiPartTypeSystemPartGuid installed on it.
  @param  RetFileHandle    Return the file handle for the input device path.

  @retval EFI_SUCESS       Find the root file handle success.
  @retval Other            Find the root file handle failure.
**/
EFI_STATUS
GetRootFileHandleFromEspHandle (
  IN  EFI_HANDLE                Fshandle,
  OUT EFI_FILE_HANDLE           *RetFileHandle
  )
{
  EFI_STATUS                       Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Volume;
  EFI_FILE_HANDLE                  FileHandle;

  Status = gBS->HandleProtocol (Fshandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Volume);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Simple file system protocol not found\n"));
    return Status;
  }

  //
  // Open the Volume to get the File System handle
  //
  Status = Volume->OpenVolume (Volume, &FileHandle);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Could not open volume\n"));
    return Status;
  }

  *RetFileHandle  = FileHandle;
  return EFI_SUCCESS;
}

EFI_STATUS
ScanFileSystemsForBootOptions (
  IN     SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN OUT SV_MENU_OPTION                      *MenuOption
  )
{
  EFI_HANDLE       *HandleBuffer = NULL;
  UINTN            HandleCount;
  EFI_FILE_HANDLE  FileHandle;
  EFI_STATUS       Status;
  UINTN            i;

  // Locate all the ESP devices in the system
  Status = gBS->LocateHandleBuffer(
             ByProtocol,
             &gEfiSimpleFileSystemProtocolGuid,
             NULL,
             &HandleCount,
             &HandleBuffer);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Could not locate ESP handle buffer\n"));
    return Status;
  }

  for (i = 0; i < HandleCount; i++) {
    Status = GetRootFileHandleFromEspHandle (HandleBuffer[i], &FileHandle);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "Could not open root file for ESP handle %u\n", i));
      continue;
    }

    FindEfiFiles (Private, MenuOption, FileHandle, L"\\", HandleBuffer[i], TRUE);
  }

  return EFI_SUCCESS;
}
