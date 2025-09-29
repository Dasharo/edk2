/** @file
  Implement image verification services for secure boot service

Copyright (c) 2009 - 2018, Intel Corporation. All rights reserved.<BR>
(C) Copyright 2016 Hewlett Packard Enterprise Development LP<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "ImageVerificationLibInternal.h"

/**
  Get the image type.

  @param[in]    File       This is a pointer to the device path of the file that is
                           being dispatched.

  @return UINT32           Image Type

**/
UINT32
GetImageType (
  IN  CONST EFI_DEVICE_PATH_PROTOCOL  *File
  )
{
  EFI_STATUS                Status;
  EFI_HANDLE                DeviceHandle;
  EFI_DEVICE_PATH_PROTOCOL  *TempDevicePath;
  EFI_BLOCK_IO_PROTOCOL     *BlockIo;

  if (File == NULL) {
    return IMAGE_UNKNOWN;
  }

  //
  // First check to see if File is from a Firmware Volume
  //
  DeviceHandle   = NULL;
  TempDevicePath = (EFI_DEVICE_PATH_PROTOCOL *)File;
  Status         = gBS->LocateDevicePath (
                          &gEfiFirmwareVolume2ProtocolGuid,
                          &TempDevicePath,
                          &DeviceHandle
                          );
  if (!EFI_ERROR (Status)) {
    Status = gBS->OpenProtocol (
                    DeviceHandle,
                    &gEfiFirmwareVolume2ProtocolGuid,
                    NULL,
                    NULL,
                    NULL,
                    EFI_OPEN_PROTOCOL_TEST_PROTOCOL
                    );
    if (!EFI_ERROR (Status)) {
      return IMAGE_FROM_FV;
    }
  }

  //
  // Next check to see if File is from a Block I/O device
  //
  DeviceHandle   = NULL;
  TempDevicePath = (EFI_DEVICE_PATH_PROTOCOL *)File;
  Status         = gBS->LocateDevicePath (
                          &gEfiBlockIoProtocolGuid,
                          &TempDevicePath,
                          &DeviceHandle
                          );
  if (!EFI_ERROR (Status)) {
    BlockIo = NULL;
    Status  = gBS->OpenProtocol (
                     DeviceHandle,
                     &gEfiBlockIoProtocolGuid,
                     (VOID **)&BlockIo,
                     NULL,
                     NULL,
                     EFI_OPEN_PROTOCOL_GET_PROTOCOL
                     );
    if (!EFI_ERROR (Status) && (BlockIo != NULL)) {
      if (BlockIo->Media != NULL) {
        if (BlockIo->Media->RemovableMedia) {
          //
          // Block I/O is present and specifies the media is removable
          //
          return IMAGE_FROM_REMOVABLE_MEDIA;
        } else {
          //
          // Block I/O is present and specifies the media is not removable
          //
          return IMAGE_FROM_FIXED_MEDIA;
        }
      }
    }
  }

  //
  // File is not in a Firmware Volume or on a Block I/O device, so check to see if
  // the device path supports the Simple File System Protocol.
  //
  DeviceHandle   = NULL;
  TempDevicePath = (EFI_DEVICE_PATH_PROTOCOL *)File;
  Status         = gBS->LocateDevicePath (
                          &gEfiSimpleFileSystemProtocolGuid,
                          &TempDevicePath,
                          &DeviceHandle
                          );
  if (!EFI_ERROR (Status)) {
    //
    // Simple File System is present without Block I/O, so assume media is fixed.
    //
    return IMAGE_FROM_FIXED_MEDIA;
  }

  //
  // File is not from an FV, Block I/O or Simple File System, so the only options
  // left are a PCI Option ROM and a Load File Protocol such as a PXE Boot from a NIC.
  //
  TempDevicePath = (EFI_DEVICE_PATH_PROTOCOL *)File;
  while (!IsDevicePathEndType (TempDevicePath)) {
    switch (DevicePathType (TempDevicePath)) {
      case MEDIA_DEVICE_PATH:
        if (DevicePathSubType (TempDevicePath) == MEDIA_RELATIVE_OFFSET_RANGE_DP) {
          return IMAGE_FROM_OPTION_ROM;
        }

        break;

      case MESSAGING_DEVICE_PATH:
        if (DevicePathSubType (TempDevicePath) == MSG_MAC_ADDR_DP) {
          return IMAGE_FROM_REMOVABLE_MEDIA;
        }

        break;

      default:
        break;
    }

    TempDevicePath = NextDevicePathNode (TempDevicePath);
  }

  return IMAGE_UNKNOWN;
}


/**
  Returns the size of a given image execution info table in bytes.

  This function returns the size, in bytes, of the image execution info table specified by
  ImageExeInfoTable. If ImageExeInfoTable is NULL, then 0 is returned.

  @param  ImageExeInfoTable          A pointer to a image execution info table structure.

  @retval 0       If ImageExeInfoTable is NULL.
  @retval Others  The size of a image execution info table in bytes.

**/
UINTN
GetImageExeInfoTableSize (
  EFI_IMAGE_EXECUTION_INFO_TABLE  *ImageExeInfoTable
  )
{
  UINTN                     Index;
  EFI_IMAGE_EXECUTION_INFO  *ImageExeInfoItem;
  UINTN                     TotalSize;

  if (ImageExeInfoTable == NULL) {
    return 0;
  }

  ImageExeInfoItem = (EFI_IMAGE_EXECUTION_INFO *)((UINT8 *)ImageExeInfoTable + sizeof (EFI_IMAGE_EXECUTION_INFO_TABLE));
  TotalSize        = sizeof (EFI_IMAGE_EXECUTION_INFO_TABLE);
  for (Index = 0; Index < ImageExeInfoTable->NumberOfImages; Index++) {
    TotalSize       += ReadUnaligned32 ((UINT32 *)&ImageExeInfoItem->InfoSize);
    ImageExeInfoItem = (EFI_IMAGE_EXECUTION_INFO *)((UINT8 *)ImageExeInfoItem + ReadUnaligned32 ((UINT32 *)&ImageExeInfoItem->InfoSize));
  }

  return TotalSize;
}

/**
  Create an Image Execution Information Table entry and add it to system configuration table.

  @param[in]  Action          Describes the action taken by the firmware regarding this image.
  @param[in]  Name            Input a null-terminated, user-friendly name.
  @param[in]  DevicePath      Input device path pointer.
  @param[in]  Signature       Input signature info in EFI_SIGNATURE_LIST data structure.
  @param[in]  SignatureSize   Size of signature. Must be zero if Signature is NULL.

**/
VOID
AddImageExeInfo (
  IN       EFI_IMAGE_EXECUTION_ACTION  Action,
  IN       CHAR16                      *Name OPTIONAL,
  IN CONST EFI_DEVICE_PATH_PROTOCOL    *DevicePath,
  IN       EFI_SIGNATURE_LIST          *Signature OPTIONAL,
  IN       UINTN                       SignatureSize
  )
{
  EFI_IMAGE_EXECUTION_INFO_TABLE  *ImageExeInfoTable;
  EFI_IMAGE_EXECUTION_INFO_TABLE  *NewImageExeInfoTable;
  EFI_IMAGE_EXECUTION_INFO        *ImageExeInfoEntry;
  UINTN                           ImageExeInfoTableSize;
  UINTN                           NewImageExeInfoEntrySize;
  UINTN                           NameStringLen;
  UINTN                           DevicePathSize;
  CHAR16                          *NameStr;

  ImageExeInfoTable    = NULL;
  NewImageExeInfoTable = NULL;
  ImageExeInfoEntry    = NULL;
  NameStringLen        = 0;
  NameStr              = NULL;

  if (DevicePath == NULL) {
    return;
  }

  if (Name != NULL) {
    NameStringLen = StrSize (Name);
  } else {
    NameStringLen = sizeof (CHAR16);
  }

  EfiGetSystemConfigurationTable (&gEfiImageSecurityDatabaseGuid, (VOID **)&ImageExeInfoTable);
  if (ImageExeInfoTable != NULL) {
    //
    // The table has been found!
    // We must enlarge the table to accommodate the new exe info entry.
    //
    ImageExeInfoTableSize = GetImageExeInfoTableSize (ImageExeInfoTable);
  } else {
    //
    // Not Found!
    // We should create a new table to append to the configuration table.
    //
    ImageExeInfoTableSize = sizeof (EFI_IMAGE_EXECUTION_INFO_TABLE);
  }

  DevicePathSize = GetDevicePathSize (DevicePath);

  //
  // Signature size can be odd. Pad after signature to ensure next EXECUTION_INFO entry align
  //
  ASSERT (Signature != NULL || SignatureSize == 0);
  NewImageExeInfoEntrySize = sizeof (EFI_IMAGE_EXECUTION_INFO) + NameStringLen + DevicePathSize + SignatureSize;

  NewImageExeInfoTable = (EFI_IMAGE_EXECUTION_INFO_TABLE *)AllocateRuntimePool (ImageExeInfoTableSize + NewImageExeInfoEntrySize);
  if (NewImageExeInfoTable == NULL) {
    return;
  }

  if (ImageExeInfoTable != NULL) {
    CopyMem (NewImageExeInfoTable, ImageExeInfoTable, ImageExeInfoTableSize);
  } else {
    NewImageExeInfoTable->NumberOfImages = 0;
  }

  NewImageExeInfoTable->NumberOfImages++;
  ImageExeInfoEntry = (EFI_IMAGE_EXECUTION_INFO *)((UINT8 *)NewImageExeInfoTable + ImageExeInfoTableSize);
  //
  // Update new item's information.
  //
  WriteUnaligned32 ((UINT32 *)ImageExeInfoEntry, Action);
  WriteUnaligned32 ((UINT32 *)((UINT8 *)ImageExeInfoEntry + sizeof (EFI_IMAGE_EXECUTION_ACTION)), (UINT32)NewImageExeInfoEntrySize);

  NameStr = (CHAR16 *)(ImageExeInfoEntry + 1);
  if (Name != NULL) {
    CopyMem ((UINT8 *)NameStr, Name, NameStringLen);
  } else {
    ZeroMem ((UINT8 *)NameStr, sizeof (CHAR16));
  }

  CopyMem (
    (UINT8 *)NameStr + NameStringLen,
    DevicePath,
    DevicePathSize
    );
  if (Signature != NULL) {
    CopyMem (
      (UINT8 *)NameStr + NameStringLen + DevicePathSize,
      Signature,
      SignatureSize
      );
  }

  //
  // Update/replace the image execution table.
  //
  gBS->InstallConfigurationTable (&gEfiImageSecurityDatabaseGuid, (VOID *)NewImageExeInfoTable);

  //
  // Free Old table data!
  //
  if (ImageExeInfoTable != NULL) {
    FreePool (ImageExeInfoTable);
  }
}

/**
  Provide verification service for signed images, which include both signature validation
  and platform policy control. For signature types, both UEFI WIN_CERTIFICATE_UEFI_GUID and
  MSFT Authenticode type signatures are supported.

  In this implementation, only verify external executables when in USER MODE.
  Executables from FV is bypass, so pass in AuthenticationStatus is ignored.

  The image verification policy is:
    If the image is signed,
      At least one valid signature or at least one hash value of the image must match a record
      in the security database "db", and no valid signature nor any hash value of the image may
      be reflected in the security database "dbx".
    Otherwise, the image is not signed,
      The hash value of the image must match a record in the security database "db", and
      not be reflected in the security data base "dbx".

  Caution: This function may receive untrusted input.
  PE/COFF image is external input, so this function will validate its data structure
  within this image buffer before use.

  @param[in]    AuthenticationStatus
                           This is the authentication status returned from the security
                           measurement services for the input file.
  @param[in]    File       This is a pointer to the device path of the file that is
                           being dispatched. This will optionally be used for logging.
  @param[in]    FileBuffer File buffer matches the input file device path.
  @param[in]    FileSize   Size of File buffer matches the input file device path.
  @param[in]    BootPolicy A boot policy that was used to call LoadImage() UEFI service.

  @retval EFI_SUCCESS            The file specified by DevicePath and non-NULL
                                 FileBuffer did authenticate, and the platform policy dictates
                                 that the DXE Foundation may use the file.
  @retval EFI_SUCCESS            The device path specified by NULL device path DevicePath
                                 and non-NULL FileBuffer did authenticate, and the platform
                                 policy dictates that the DXE Foundation may execute the image in
                                 FileBuffer.
  @retval EFI_SECURITY_VIOLATION The file specified by File did not authenticate, and
                                 the platform policy dictates that File should be placed
                                 in the untrusted state. The image has been added to the file
                                 execution table.
  @retval EFI_ACCESS_DENIED      The file specified by File and FileBuffer did not
                                 authenticate, and the platform policy dictates that the DXE
                                 Foundation may not use File. The image has
                                 been added to the file execution table.

**/
EFI_STATUS
EFIAPI
DxeImageVerificationHandler (
  IN  UINT32                          AuthenticationStatus,
  IN  CONST EFI_DEVICE_PATH_PROTOCOL  *File  OPTIONAL,
  IN  VOID                            *FileBuffer,
  IN  UINTN                           FileSize,
  IN  BOOLEAN                         BootPolicy
  )
{
  BOOLEAN                       IsVerified;
  EFI_SIGNATURE_LIST            *SignatureList;
  UINTN                         SignatureListSize;
  EFI_SIGNATURE_DATA            *Signature;
  EFI_IMAGE_EXECUTION_ACTION    Action;
  WIN_CERTIFICATE               *WinCertificate;
  UINT32                        Policy;
  UINT8                         SecureBoot;
  UINTN                         SecureBootSize;
  WIN_CERTIFICATE_EFI_PKCS      *PkcsCertData;
  WIN_CERTIFICATE_UEFI_GUID     *WinCertUefiGuid;
  UINT8                         *AuthData;
  UINTN                         AuthDataSize;
  EFI_IMAGE_DATA_DIRECTORY      *SecDataDir;
  UINT32                        SecDataDirEnd;
  UINT32                        SecDataDirLeft;
  UINT32                        OffSet;
  CHAR16                        *NameStr;
  EFI_STATUS                    HashStatus;
  EFI_STATUS                    DbStatus;
  EFI_STATUS                    VarStatus;
  EFI_STATUS                    ImageStatus;
  UINT32                        VarAttr;
  BOOLEAN                       IsFound;
  UINT8                         HashAlg;
  BOOLEAN                       IsFoundInDatabase;
  UINT8                         ImageDigest[MAX_DIGEST_SIZE];
  UINTN                         ImageDigestSize;
  EFI_GUID                      CertType;

  SignatureList     = NULL;
  SignatureListSize = 0;
  WinCertificate    = NULL;
  SecDataDir        = NULL;
  PkcsCertData      = NULL;
  Action            = EFI_IMAGE_EXECUTION_AUTH_UNTESTED;
  IsVerified        = FALSE;
  IsFound           = FALSE;
  IsFoundInDatabase = FALSE;

  //
  // Check the image type and get policy setting.
  //
  switch (GetImageType (File)) {
    case IMAGE_FROM_FV:
      Policy = ALWAYS_EXECUTE;
      break;

    case IMAGE_FROM_OPTION_ROM:
      Policy = PcdGet32 (PcdOptionRomImageVerificationPolicy);
      break;

    case IMAGE_FROM_REMOVABLE_MEDIA:
      Policy = PcdGet32 (PcdRemovableMediaImageVerificationPolicy);
      break;

    case IMAGE_FROM_FIXED_MEDIA:
      Policy = PcdGet32 (PcdFixedMediaImageVerificationPolicy);
      break;

    default:
      Policy = DENY_EXECUTE_ON_SECURITY_VIOLATION;
      break;
  }

  //
  // If policy is always/never execute, return directly.
  //
  if (Policy == ALWAYS_EXECUTE) {
    return EFI_SUCCESS;
  }

  if (Policy == NEVER_EXECUTE) {
    return EFI_ACCESS_DENIED;
  }

  //
  // The policy QUERY_USER_ON_SECURITY_VIOLATION and ALLOW_EXECUTE_ON_SECURITY_VIOLATION
  // violates the UEFI spec and has been removed.
  //
  ASSERT (Policy != QUERY_USER_ON_SECURITY_VIOLATION && Policy != ALLOW_EXECUTE_ON_SECURITY_VIOLATION);
  if ((Policy == QUERY_USER_ON_SECURITY_VIOLATION) || (Policy == ALLOW_EXECUTE_ON_SECURITY_VIOLATION)) {
    CpuDeadLoop ();
  }

  SecureBootSize = sizeof (SecureBoot);
  VarStatus      = gRT->GetVariable (EFI_SECURE_BOOT_MODE_NAME, &gEfiGlobalVariableGuid, &VarAttr, &SecureBootSize, &SecureBoot);
  //
  // Skip verification if SecureBoot variable doesn't exist.
  //
  if (VarStatus == EFI_NOT_FOUND) {
    return EFI_SUCCESS;
  }

  //
  // Skip verification if SecureBoot is disabled but not AuditMode
  //
  if ((VarStatus == EFI_SUCCESS) &&
      (VarAttr == (EFI_VARIABLE_BOOTSERVICE_ACCESS |
                   EFI_VARIABLE_RUNTIME_ACCESS)) &&
      (SecureBoot == SECURE_BOOT_MODE_DISABLE))
  {
    return EFI_SUCCESS;
  }

  //
  // Read the Dos header.
  //
  if (FileBuffer == NULL) {
    return EFI_ACCESS_DENIED;
  }

  ImageStatus = GetImageSecDataDir (FileBuffer, FileSize, &SecDataDir);
  if (EFI_ERROR (ImageStatus)) {
    goto Failed;
  }

  //
  // Start Image Validation.
  //
  if ((SecDataDir == NULL) || (SecDataDir->Size == 0)) {
    //
    // This image is not signed. The hash value of the image must match a record in the security database "db",
    // and not be reflected in the security data base "dbx".
    //
    HashAlg = HASHALG_MAX;
    while (HashAlg > 0) {
      HashAlg--;

      if (!HashPeImage (FileBuffer, FileSize, HashAlg, ImageDigest, &ImageDigestSize, &CertType)) {
        continue;
      }

      DbStatus = IsSignatureFoundInDatabase (
                   EFI_IMAGE_SECURITY_DATABASE1,
                   ImageDigest,
                   &CertType,
                   ImageDigestSize,
                   &IsFound
                   );
      if (EFI_ERROR (DbStatus) || IsFound) {
        //
        // Image Hash is in forbidden database (DBX).
        //
        DEBUG ((DEBUG_INFO, "DxeImageVerificationLib: Image is not signed and hash of image is forbidden by DBX.\n"));
        goto Failed;
      }

      DbStatus = IsSignatureFoundInDatabase (
                   EFI_IMAGE_SECURITY_DATABASE,
                   ImageDigest,
                   &CertType,
                   ImageDigestSize,
                   &IsFound
                   );
      if (!EFI_ERROR (DbStatus) && IsFound) {
        //
        // Image Hash is in allowed database (DB).
        //
        IsFoundInDatabase = TRUE;
      }
    }

    if (IsFoundInDatabase) {
      return EFI_SUCCESS;
    }

    //
    // Image Hash is not found in both forbidden and allowed database.
    //
    DEBUG ((DEBUG_INFO, "DxeImageVerificationLib: Image is not signed and hash of image is not found in DB/DBX.\n"));
    goto Failed;
  }

  //
  // Verify the signature of the image, multiple signatures are allowed as per PE/COFF Section 4.7
  // "Attribute Certificate Table".
  // The first certificate starts at offset (SecDataDir->VirtualAddress) from the start of the file.
  //
  SecDataDirEnd = SecDataDir->VirtualAddress + SecDataDir->Size;
  for (OffSet = SecDataDir->VirtualAddress;
       OffSet < SecDataDirEnd;
       OffSet += (WinCertificate->dwLength + ALIGN_SIZE (WinCertificate->dwLength)))
  {
    SecDataDirLeft = SecDataDirEnd - OffSet;
    if (SecDataDirLeft <= sizeof (WIN_CERTIFICATE)) {
      break;
    }

    WinCertificate = (WIN_CERTIFICATE *)(FileBuffer + OffSet);
    if ((SecDataDirLeft < WinCertificate->dwLength) ||
        (SecDataDirLeft - WinCertificate->dwLength <
         ALIGN_SIZE (WinCertificate->dwLength)))
    {
      break;
    }

    //
    // Verify the image's Authenticode signature, only DER-encoded PKCS#7 signed data is supported.
    //
    if (WinCertificate->wCertificateType == WIN_CERT_TYPE_PKCS_SIGNED_DATA) {
      //
      // The certificate is formatted as WIN_CERTIFICATE_EFI_PKCS which is described in the
      // Authenticode specification.
      //
      PkcsCertData = (WIN_CERTIFICATE_EFI_PKCS *)WinCertificate;
      if (PkcsCertData->Hdr.dwLength <= sizeof (PkcsCertData->Hdr)) {
        break;
      }

      AuthData     = PkcsCertData->CertData;
      AuthDataSize = PkcsCertData->Hdr.dwLength - sizeof (PkcsCertData->Hdr);
    } else if (WinCertificate->wCertificateType == WIN_CERT_TYPE_EFI_GUID) {
      //
      // The certificate is formatted as WIN_CERTIFICATE_UEFI_GUID which is described in UEFI Spec.
      //
      WinCertUefiGuid = (WIN_CERTIFICATE_UEFI_GUID *)WinCertificate;
      if (WinCertUefiGuid->Hdr.dwLength <= OFFSET_OF (WIN_CERTIFICATE_UEFI_GUID, CertData)) {
        break;
      }

      if (!CompareGuid (&WinCertUefiGuid->CertType, &gEfiCertPkcs7Guid)) {
        continue;
      }

      AuthData     = WinCertUefiGuid->CertData;
      AuthDataSize = WinCertUefiGuid->Hdr.dwLength - OFFSET_OF (WIN_CERTIFICATE_UEFI_GUID, CertData);
    } else {
      if (WinCertificate->dwLength < sizeof (WIN_CERTIFICATE)) {
        break;
      }

      continue;
    }

    HashStatus = HashPeImageByType (
                   FileBuffer,
                   FileSize,
                   AuthData,
                   AuthDataSize,
                   ImageDigest,
                   &ImageDigestSize,
                   &CertType);
    if (EFI_ERROR (HashStatus)) {
      continue;
    }

    //
    // Check the digital signature against the revoked certificate in forbidden database (dbx).
    //
    if (IsForbiddenByDbx (AuthData, AuthDataSize, ImageDigest, ImageDigestSize)) {
      Action     = EFI_IMAGE_EXECUTION_AUTH_SIG_FAILED;
      IsVerified = FALSE;
      break;
    }

    //
    // Check the digital signature against the valid certificate in allowed database (db).
    //
    if (!IsVerified) {
      if (IsAllowedByDb (AuthData, AuthDataSize, ImageDigest, ImageDigestSize)) {
        IsVerified = TRUE;
      }
    }

    //
    // Check the image's hash value.
    //
    DbStatus = IsSignatureFoundInDatabase (
                 EFI_IMAGE_SECURITY_DATABASE1,
                 ImageDigest,
                 &CertType,
                 ImageDigestSize,
                 &IsFound
                 );
    if (EFI_ERROR (DbStatus) || IsFound) {
      Action = EFI_IMAGE_EXECUTION_AUTH_SIG_FOUND;
      DEBUG ((DEBUG_INFO, "DxeImageVerificationLib: Image is signed but hash of image is found in DBX.\n"));
      IsVerified = FALSE;
      break;
    }

    if (!IsVerified) {
      DbStatus = IsSignatureFoundInDatabase (
                   EFI_IMAGE_SECURITY_DATABASE,
                   ImageDigest,
                   &CertType,
                   ImageDigestSize,
                   &IsFound
                   );
      if (!EFI_ERROR (DbStatus) && IsFound) {
        IsVerified = TRUE;
      } else {
        Action = EFI_IMAGE_EXECUTION_AUTH_SIG_NOT_FOUND;
        DEBUG ((DEBUG_INFO, "DxeImageVerificationLib: Image is signed but signature is not allowed by DB and hash of image is not found in DB/DBX.\n"));
      }
    }
  }

  if (OffSet != SecDataDirEnd) {
    //
    // The Size in Certificate Table or the attribute certificate table is corrupted.
    //
    IsVerified = FALSE;
  }

  if (IsVerified) {
    return EFI_SUCCESS;
  }

  if ((Action == EFI_IMAGE_EXECUTION_AUTH_SIG_FAILED) || (Action == EFI_IMAGE_EXECUTION_AUTH_SIG_FOUND)) {
    //
    // Get image hash value as signature of executable.
    //
    SignatureListSize = sizeof (EFI_SIGNATURE_LIST) + sizeof (EFI_SIGNATURE_DATA) - 1 + ImageDigestSize;
    SignatureList     = (EFI_SIGNATURE_LIST *)AllocateZeroPool (SignatureListSize);
    if (SignatureList == NULL) {
      SignatureListSize = 0;
      goto Failed;
    }

    SignatureList->SignatureHeaderSize = 0;
    SignatureList->SignatureListSize   = (UINT32)SignatureListSize;
    SignatureList->SignatureSize       = (UINT32)(sizeof (EFI_SIGNATURE_DATA) - 1 + ImageDigestSize);
    CopyMem (&SignatureList->SignatureType, &CertType, sizeof (EFI_GUID));
    Signature = (EFI_SIGNATURE_DATA *)((UINT8 *)SignatureList + sizeof (EFI_SIGNATURE_LIST));
    CopyMem (Signature->SignatureData, ImageDigest, ImageDigestSize);
  }

Failed:
  //
  // Policy decides to defer or reject the image; add its information in image
  // executable information table in either case.
  //
  NameStr = ConvertDevicePathToText (File, FALSE, TRUE);
  AddImageExeInfo (Action, NameStr, File, SignatureList, SignatureListSize);
  if (NameStr != NULL) {
    DEBUG ((DEBUG_INFO, "The image doesn't pass verification: %s\n", NameStr));
    FreePool (NameStr);
  }

  if (SignatureList != NULL) {
    FreePool (SignatureList);
  }

  if (Policy == DEFER_EXECUTE_ON_SECURITY_VIOLATION) {
    return EFI_SECURITY_VIOLATION;
  }

  return EFI_ACCESS_DENIED;
}

/**
  On Ready To Boot Services Event notification handler.

  Add the image execution information table if it is not in system configuration table.

  @param[in]  Event     Event whose notification function is being invoked
  @param[in]  Context   Pointer to the notification function's context

**/
VOID
EFIAPI
OnReadyToBoot (
  IN      EFI_EVENT  Event,
  IN      VOID       *Context
  )
{
  EFI_IMAGE_EXECUTION_INFO_TABLE  *ImageExeInfoTable;
  UINTN                           ImageExeInfoTableSize;

  EfiGetSystemConfigurationTable (&gEfiImageSecurityDatabaseGuid, (VOID **)&ImageExeInfoTable);
  if (ImageExeInfoTable != NULL) {
    return;
  }

  ImageExeInfoTableSize = sizeof (EFI_IMAGE_EXECUTION_INFO_TABLE);
  ImageExeInfoTable     = (EFI_IMAGE_EXECUTION_INFO_TABLE *)AllocateRuntimePool (ImageExeInfoTableSize);
  if (ImageExeInfoTable == NULL) {
    return;
  }

  ImageExeInfoTable->NumberOfImages = 0;
  gBS->InstallConfigurationTable (&gEfiImageSecurityDatabaseGuid, (VOID *)ImageExeInfoTable);
}

/**
  Register security measurement handler.

  @param  ImageHandle   ImageHandle of the loaded driver.
  @param  SystemTable   Pointer to the EFI System Table.

  @retval EFI_SUCCESS   The handlers were registered successfully.
**/
EFI_STATUS
EFIAPI
DxeImageVerificationLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_EVENT  Event;

  //
  // Register the event to publish the image execution table.
  //
  EfiCreateEventReadyToBootEx (
    TPL_CALLBACK,
    OnReadyToBoot,
    NULL,
    &Event
    );

  return RegisterSecurity2Handler (
           DxeImageVerificationHandler,
           EFI_AUTH_OPERATION_VERIFY_IMAGE | EFI_AUTH_OPERATION_IMAGE_REQUIRED
           );
}
