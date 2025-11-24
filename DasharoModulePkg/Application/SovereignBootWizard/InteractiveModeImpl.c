/** @file
Sovereign Boot Wizard Interactive Mode implementation.

Copyright (c) 2025, 3mdeb Sp z o.o. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "SovereignBootWizard.h"
#include "InteractiveModeImpl.h"

EFI_STATUS
FormatHelpInfo (
  IN     SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *PrivateData,
  IN     EFI_SIGNATURE_LIST                  *ListEntry,
  IN     EFI_SIGNATURE_DATA                  *DataEntry,
  OUT EFI_STRING_ID                          *StringId
  );

//
// Variable Definitions
//
WIN_CERTIFICATE                      *mCertificate = NULL;
EFI_IMAGE_SECURITY_DATA_DIRECTORY    *mSecDataDir  = NULL;
UINT8 *mImageBase = NULL;
UINTN mImageSize  = 0;
//
// Possible DER-encoded certificate file suffixes, end with NULL pointer.
//
CHAR16  *mDerEncodedSuffix[] = {
  L".cer",
  L".der",
  L".crt",
  NULL
};

CHAR16  *mSupportX509Suffix = L"*.cer/der/crt";

//
// Prompt strings during certificate enrollment.
//
CHAR16  *mX509EnrollPromptTitle[] = {
  L"",
  L"ERROR: Unsupported file type!",
  L"ERROR: Unsupported certificate!",
  NULL
};
CHAR16  *mX509EnrollPromptString[] = {
  L"",
  L"Only DER encoded certificate file (*.cer/der/crt) is supported.",
  L"Public key length should be equal to or greater than 2048 bits.",
  NULL
};

/**
  Read file content into BufferPtr, the size of the allocate buffer
  is *FileSize plus AdditionAllocateSize.

  @param[in]       FileHandle            The file to be read.
  @param[in, out]  BufferPtr             Pointers to the pointer of allocated buffer.
  @param[out]      FileSize              Size of input file
  @param[in]       AdditionAllocateSize   Addition size the buffer need to be allocated.
                                         In case the buffer need to contain others besides the file content.

  @retval   EFI_SUCCESS                  The file was read into the buffer.
  @retval   EFI_INVALID_PARAMETER        A parameter was invalid.
  @retval   EFI_OUT_OF_RESOURCES         A memory allocation failed.
  @retval   others                       Unexpected error.

**/
EFI_STATUS
ReadFile (
  IN      EFI_FILE_HANDLE  FileHandle,
  IN OUT  VOID             **BufferPtr,
  OUT  UINTN               *FileSize,
  IN      UINTN            AdditionAllocateSize
  )

{
  UINTN       BufferSize;
  UINT64      SourceFileSize;
  VOID        *Buffer;
  EFI_STATUS  Status;

  if ((FileHandle == NULL) || (FileSize == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Buffer = NULL;

  //
  // Get the file size
  //
  Status = FileHandle->SetPosition (FileHandle, (UINT64)-1);
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  Status = FileHandle->GetPosition (FileHandle, &SourceFileSize);
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  Status = FileHandle->SetPosition (FileHandle, 0);
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  BufferSize = (UINTN)SourceFileSize + AdditionAllocateSize;
  Buffer     =  AllocateZeroPool (BufferSize);
  if (Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  BufferSize = (UINTN)SourceFileSize;
  *FileSize  = BufferSize;

  Status = FileHandle->Read (FileHandle, &BufferSize, Buffer);
  if (EFI_ERROR (Status) || (BufferSize != *FileSize)) {
    FreePool (Buffer);
    Buffer = NULL;
    Status = EFI_BAD_BUFFER_SIZE;
    goto ON_EXIT;
  }

ON_EXIT:

  *BufferPtr = Buffer;
  return Status;
}

/**
  Close an open file handle.

  @param[in] FileHandle           The file handle to close.

**/
VOID
CloseFile (
  IN EFI_FILE_HANDLE  FileHandle
  )
{
  if (FileHandle != NULL) {
    FileHandle->Close (FileHandle);
  }
}

/**
  Convert a nonnegative integer to an octet string of a specified length.

  @param[in]   Integer          Pointer to the nonnegative integer to be converted
  @param[in]   IntSizeInWords   Length of integer buffer in words
  @param[out]  OctetString      Converted octet string of the specified length
  @param[in]   OSSizeInBytes    Intended length of resulting octet string in bytes

Returns:

  @retval   EFI_SUCCESS            Data conversion successfully
  @retval   EFI_BUFFER_TOOL_SMALL  Buffer is too small for output string

**/
EFI_STATUS
EFIAPI
Int2OctStr (
  IN     CONST UINTN  *Integer,
  IN     UINTN        IntSizeInWords,
  OUT UINT8           *OctetString,
  IN     UINTN        OSSizeInBytes
  )
{
  CONST UINT8  *Ptr1;
  UINT8        *Ptr2;

  for (Ptr1 = (CONST UINT8 *)Integer, Ptr2 = OctetString + OSSizeInBytes - 1;
       Ptr1 < (UINT8 *)(Integer + IntSizeInWords) && Ptr2 >= OctetString;
       Ptr1++, Ptr2--)
  {
    *Ptr2 = *Ptr1;
  }

  for ( ; Ptr1 < (CONST UINT8 *)(Integer + IntSizeInWords) && *Ptr1 == 0; Ptr1++) {
  }

  if (Ptr1 < (CONST UINT8 *)(Integer + IntSizeInWords)) {
    return EFI_BUFFER_TOO_SMALL;
  }

  if (Ptr2 >= OctetString) {
    ZeroMem (OctetString, Ptr2 - OctetString + 1);
  }

  return EFI_SUCCESS;
}

/**
  Worker function that prints an EFI_GUID into specified Buffer.

  @param[in]     Guid          Pointer to GUID to print.
  @param[in]     Buffer        Buffer to print Guid into.
  @param[in]     BufferSize    Size of Buffer.

  @retval    Number of characters printed.

**/
UINTN
GuidToString (
  IN  EFI_GUID  *Guid,
  IN  CHAR16    *Buffer,
  IN  UINTN     BufferSize
  )
{
  UINTN  Size;

  Size = UnicodeSPrint (
           Buffer,
           BufferSize,
           L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           (UINTN)Guid->Data1,
           (UINTN)Guid->Data2,
           (UINTN)Guid->Data3,
           (UINTN)Guid->Data4[0],
           (UINTN)Guid->Data4[1],
           (UINTN)Guid->Data4[2],
           (UINTN)Guid->Data4[3],
           (UINTN)Guid->Data4[4],
           (UINTN)Guid->Data4[5],
           (UINTN)Guid->Data4[6],
           (UINTN)Guid->Data4[7]
           );

  //
  // SPrint will null terminate the string. The -1 skips the null
  //
  return Size - 1;
}


VOID                *mStartOpCodeHandle = NULL;
VOID                *mEndOpCodeHandle   = NULL;
EFI_IFR_GUID_LABEL  *mStartLabel        = NULL;
EFI_IFR_GUID_LABEL  *mEndLabel          = NULL;

/**
  Refresh the global UpdateData structure.

**/
VOID
RefreshUpdateData (
  VOID
  )
{
  //
  // Free current updated date
  //
  if (mStartOpCodeHandle != NULL) {
    HiiFreeOpCodeHandle (mStartOpCodeHandle);
  }

  //
  // Create new OpCode Handle
  //
  mStartOpCodeHandle = HiiAllocateOpCodeHandle ();

  //
  // Create Hii Extend Label OpCode as the start opcode
  //
  mStartLabel = (EFI_IFR_GUID_LABEL *)HiiCreateGuidOpCode (
                                        mStartOpCodeHandle,
                                        &gEfiIfrTianoGuid,
                                        NULL,
                                        sizeof (EFI_IFR_GUID_LABEL)
                                        );
  mStartLabel->ExtendOpCode = EFI_IFR_EXTEND_OP_LABEL;
}

/**
  Clean up the dynamic opcode at label and form specified by both LabelId.

  @param[in] LabelId         It is both the Form ID and Label ID for opcode deletion.
  @param[in] PrivateData     Module private data.

**/
VOID
CleanUpPage (
  IN UINT16                              LabelId,
  IN SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *PrivateData
  )
{
  RefreshUpdateData ();

  //
  // Remove all op-codes from dynamic page
  //
  mStartLabel->Number = LabelId;
  HiiUpdateForm (
    PrivateData->HiiHandle,
    &gSovereignBootWizardFormSetGuid,
    LabelId,
    mStartOpCodeHandle, // Label LabelId
    mEndOpCodeHandle    // LABEL_END
    );
}

/**
  Extract filename from device path. The returned buffer is allocated using AllocateCopyPool.
  The caller is responsible for freeing the allocated buffer using FreePool(). If return NULL
  means not enough memory resource.

  @param DevicePath       Device path.

  @retval NULL            Not enough memory resource for AllocateCopyPool.
  @retval Other           A new allocated string that represents the file name.

**/
CHAR16 *
ExtractFileNameFromDevicePath (
  IN   EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  )
{
  CHAR16  *String;
  CHAR16  *MatchString;
  CHAR16  *LastMatch;
  CHAR16  *FileName;
  UINTN   Length;

  ASSERT (DevicePath != NULL);

  String      = UiDevicePathToStr (gPrivateData->DevPathToText, DevicePath);
  MatchString = String;
  LastMatch   = String;
  FileName    = NULL;

  while (MatchString != NULL) {
    LastMatch   = MatchString + 1;
    MatchString = StrStr (LastMatch, L"\\");
  }

  Length   = StrLen (LastMatch);
  FileName = AllocateCopyPool ((Length + 1) * sizeof (CHAR16), LastMatch);
  if (FileName != NULL) {
    *(FileName + Length) = 0;
  }

  FreePool (String);

  return FileName;
}

/**
  Update  the form base on the selected file.

  @param FilePath   Point to the file path.
  @param FormId     The form need to display.

  @retval TRUE   Exit caller function.
  @retval FALSE  Not exit caller function.

**/
BOOLEAN
UpdatePage (
  IN  EFI_DEVICE_PATH_PROTOCOL  *FilePath,
  IN  EFI_FORM_ID               FormId
  )
{
  CHAR16         *FileName;
  EFI_STRING_ID  StringToken;

  FileName = NULL;

  if (FilePath != NULL) {
    FileName = ExtractFileNameFromDevicePath (FilePath);
  }

  if (FileName == NULL) {
    //
    // FileName = NULL has two case:
    // 1. FilePath == NULL, not select file.
    // 2. FilePath != NULL, but ExtractFileNameFromDevicePath return NULL not enough memory resource.
    // In these two case, no need to update the form, and exit the caller function.
    //
    return TRUE;
  }

  StringToken =  HiiSetString (gPrivateData->HiiHandle, 0, FileName, NULL);

  gPrivateData->FileContext->FileName = FileName;

  EfiOpenFileByDevicePath (
    &FilePath,
    &gPrivateData->FileContext->FHandle,
    EFI_FILE_MODE_READ,
    0
    );
  //
  // Create Subtitle op-code for the display string of the option.
  //
  RefreshUpdateData ();
  mStartLabel->Number = FormId;

  HiiCreateSubTitleOpCode (
    mStartOpCodeHandle,
    StringToken,
    0,
    0,
    0
    );

  HiiUpdateForm (
    gPrivateData->HiiHandle,
    &gSovereignBootWizardFormSetGuid,
    FormId,
    mStartOpCodeHandle, // Label FormId
    mEndOpCodeHandle    // LABEL_END
    );

  return TRUE;
}

/**
  Update the DB form base on the input file path info.

  @param FilePath    Point to the file path.

  @retval TRUE   Exit caller function.
  @retval FALSE  Not exit caller function.
**/
BOOLEAN
EFIAPI
UpdateDBFromFile (
  IN EFI_DEVICE_PATH_PROTOCOL  *FilePath
  )
{
  return UpdatePage (FilePath, SOVEREIGN_BOOT_ENROLL_SIGNATURE_TO_DB);
}

/**
  Update the DBX form base on the input file path info.

  @param FilePath    Point to the file path.

  @retval TRUE   Exit caller function.
  @retval FALSE  Not exit caller function.
**/
BOOLEAN
EFIAPI
UpdateDBXFromFile (
  IN EFI_DEVICE_PATH_PROTOCOL  *FilePath
  )
{
  return UpdatePage (FilePath, SOVEREIGN_BOOT_ENROLL_SIGNATURE_TO_DBX);
}

/**
  This code cleans up enrolled file by closing file & free related resources attached to
  enrolled file.

  @param[in] FileContext            FileContext cached in Sovereign Boot Wizard driver

**/
VOID
CloseEnrolledFile (
  IN SOVEREIGNBOOT_FILE_CONTEXT  *FileContext
  )
{
  if (FileContext->FHandle != NULL) {
    CloseFile (FileContext->FHandle);
    FileContext->FHandle = NULL;
  }

  if (FileContext->FileName != NULL) {
    FreePool (FileContext->FileName);
    FileContext->FileName = NULL;
  }

  FileContext->FileType = UNKNOWN_FILE_TYPE;
}

/**
  This code checks if the FileSuffix is one of the possible DER-encoded certificate suffix.

  @param[in] FileSuffix            The suffix of the input certificate file

  @retval    TRUE           It's a DER-encoded certificate.
  @retval    FALSE          It's NOT a DER-encoded certificate.

**/
BOOLEAN
IsDerEncodeCertificate (
  IN CONST CHAR16  *FileSuffix
  )
{
  UINTN  Index;

  for (Index = 0; mDerEncodedSuffix[Index] != NULL; Index++) {
    if (StrCmp (FileSuffix, mDerEncodedSuffix[Index]) == 0) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
  This code checks if the file content complies with EFI_VARIABLE_AUTHENTICATION_2 format
The function reads file content but won't open/close given FileHandle.

  @param[in] FileHandle            The FileHandle to be checked

  @retval    TRUE            The content is EFI_VARIABLE_AUTHENTICATION_2 format.
  @retval    FALSE          The content is NOT a EFI_VARIABLE_AUTHENTICATION_2 format.

**/
BOOLEAN
IsAuthentication2Format (
  IN   EFI_FILE_HANDLE  FileHandle
  )
{
  EFI_STATUS                     Status;
  EFI_VARIABLE_AUTHENTICATION_2  *Auth2;
  BOOLEAN                        IsAuth2Format;

  IsAuth2Format = FALSE;

  //
  // Read the whole file content
  //
  Status = ReadFile (
             FileHandle,
             (VOID **)&mImageBase,
             &mImageSize,
             0
             );
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  Auth2 = (EFI_VARIABLE_AUTHENTICATION_2 *)mImageBase;
  if (Auth2->AuthInfo.Hdr.wCertificateType != WIN_CERT_TYPE_EFI_GUID) {
    goto ON_EXIT;
  }

  if (CompareGuid (&gEfiCertPkcs7Guid, &Auth2->AuthInfo.CertType)) {
    IsAuth2Format = TRUE;
  }

ON_EXIT:
  //
  // Do not close File. simply check file content
  //
  if (mImageBase != NULL) {
    FreePool (mImageBase);
    mImageBase = NULL;
  }

  return IsAuth2Format;
}

/**
  This code checks if the encode type and key strength of X.509
  certificate is qualified.

  @param[in]  X509FileContext     FileContext of X.509 certificate storing
                                  file.
  @param[out] Error               Error type checked in the certificate.

  @return EFI_SUCCESS             The certificate checked successfully.
  @return EFI_INVALID_PARAMETER   The parameter is invalid.
  @return EFI_OUT_OF_RESOURCES    Memory allocation failed.

**/
EFI_STATUS
CheckX509Certificate (
  IN    SOVEREIGNBOOT_FILE_CONTEXT  *X509FileContext,
  OUT   ENROLL_KEY_ERROR         *Error
  )
{
  EFI_STATUS  Status;
  UINT16      *FilePostFix;
  UINTN       NameLength;
  UINT8       *X509Data;
  UINTN       X509DataSize;
  void        *X509PubKey;
  UINTN       PubKeyModSize;

  if (X509FileContext->FileName == NULL) {
    *Error = Unsupported_Type;
    return EFI_INVALID_PARAMETER;
  }

  X509Data      = NULL;
  X509DataSize  = 0;
  X509PubKey    = NULL;
  PubKeyModSize = 0;

  //
  // Parse the file's postfix. Only support DER encoded X.509 certificate files.
  //
  NameLength = StrLen (X509FileContext->FileName);
  if (NameLength <= 4) {
    DEBUG ((DEBUG_ERROR, "Wrong X509 NameLength\n"));
    *Error = Unsupported_Type;
    return EFI_INVALID_PARAMETER;
  }

  FilePostFix = X509FileContext->FileName + NameLength - 4;
  if (!IsDerEncodeCertificate (FilePostFix)) {
    DEBUG ((DEBUG_ERROR, "Unsupported file type, only DER encoded certificate (%s) is supported.\n", mSupportX509Suffix));
    *Error = Unsupported_Type;
    return EFI_INVALID_PARAMETER;
  }

  DEBUG ((DEBUG_INFO, "FileName= %s\n", X509FileContext->FileName));
  DEBUG ((DEBUG_INFO, "FilePostFix = %s\n", FilePostFix));

  //
  // Read the certificate file content
  //
  Status = ReadFile (X509FileContext->FHandle, (VOID **)&X509Data, &X509DataSize, 0);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Error occurred while reading the file.\n"));
    goto ON_EXIT;
  }

  //
  // Parse the public key context.
  //
  if (RsaGetPublicKeyFromX509 (X509Data, X509DataSize, &X509PubKey) == FALSE) {
    DEBUG ((DEBUG_ERROR, "Error occurred while parsing the pubkey from certificate.\n"));
    Status = EFI_INVALID_PARAMETER;
    *Error = Unsupported_Type;
    goto ON_EXIT;
  }

  //
  // Parse Module size of public key using interface provided by CryptoPkg, which is
  // actually the size of public key.
  //
  if (X509PubKey != NULL) {
    RsaGetKey (X509PubKey, RsaKeyN, NULL, &PubKeyModSize);
    if (PubKeyModSize < CER_PUBKEY_MIN_SIZE) {
      DEBUG ((DEBUG_ERROR, "Unqualified PK size, key size should be equal to or greater than 2048 bits.\n"));
      Status = EFI_INVALID_PARAMETER;
      *Error = Unqualified_Key;
    }

    RsaFree (X509PubKey);
  }

ON_EXIT:
  if (X509Data != NULL) {
    FreePool (X509Data);
  }

  return Status;
}

/**
  Enroll a new X509 certificate into Signature Database (DB or DBX or DBT) without
  KEK's authentication.

  @param[in] PrivateData     The module's private data.
  @param[in] VariableName    Variable name of signature database, must be
                             EFI_IMAGE_SECURITY_DATABASE or EFI_IMAGE_SECURITY_DATABASE1.

  @retval   EFI_SUCCESS            New X509 is enrolled successfully.
  @retval   EFI_OUT_OF_RESOURCES   Could not allocate needed resources.

**/
EFI_STATUS
EnrollX509toSigDB (
  IN SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN CHAR16                              *VariableName
  )
{
  EFI_STATUS          Status;
  UINTN               X509DataSize;
  VOID                *X509Data;
  EFI_SIGNATURE_LIST  *SigDBCert;
  EFI_SIGNATURE_DATA  *SigDBCertData;
  VOID                *Data;
  UINTN               DataSize;
  UINTN               SigDBSize;
  UINT32              Attr;
  EFI_TIME            Time;

  X509DataSize  = 0;
  SigDBSize     = 0;
  DataSize      = 0;
  X509Data      = NULL;
  SigDBCert     = NULL;
  SigDBCertData = NULL;
  Data          = NULL;

  Status = ReadFile (
             Private->FileContext->FHandle,
             &X509Data,
             &X509DataSize,
             0
             );
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  ASSERT (X509Data != NULL);

  SigDBSize = sizeof (EFI_SIGNATURE_LIST) + sizeof (EFI_SIGNATURE_DATA) - 1 + X509DataSize;

  Data = AllocateZeroPool (SigDBSize);
  if (Data == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  //
  // Fill Certificate Database parameters.
  //
  SigDBCert                      = (EFI_SIGNATURE_LIST *)Data;
  SigDBCert->SignatureListSize   = (UINT32)SigDBSize;
  SigDBCert->SignatureHeaderSize = 0;
  SigDBCert->SignatureSize       = (UINT32)(sizeof (EFI_SIGNATURE_DATA) - 1 + X509DataSize);
  CopyGuid (&SigDBCert->SignatureType, &gEfiCertX509Guid);

  SigDBCertData = (EFI_SIGNATURE_DATA *)((UINT8 *)SigDBCert + sizeof (EFI_SIGNATURE_LIST));
  CopyGuid (&SigDBCertData->SignatureOwner, &gSovereignBootWizardFormSetGuid);
  CopyMem ((UINT8 *)(SigDBCertData->SignatureData), X509Data, X509DataSize);

  //
  // Check if signature database entry has been already existed.
  // If true, use EFI_VARIABLE_APPEND_WRITE attribute to append the
  // new signature data to original variable
  //
  Attr = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_RUNTIME_ACCESS
         | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS;
  Status = GetCurrentTime (&Time);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Fail to fetch valid time data: %r", Status));
    goto ON_EXIT;
  }

  Status = CreateTimeBasedPayload (&SigDBSize, (UINT8 **)&Data, &Time);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Fail to create time-based data payload: %r", Status));
    goto ON_EXIT;
  }

  Status = gRT->GetVariable (
                  VariableName,
                  &gEfiImageSecurityDatabaseGuid,
                  NULL,
                  &DataSize,
                  NULL
                  );
  if (Status == EFI_BUFFER_TOO_SMALL) {
    Attr |= EFI_VARIABLE_APPEND_WRITE;
  } else if (Status != EFI_NOT_FOUND) {
    goto ON_EXIT;
  }

  Status = gRT->SetVariable (
                  VariableName,
                  &gEfiImageSecurityDatabaseGuid,
                  Attr,
                  SigDBSize,
                  Data
                  );
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

ON_EXIT:

  CloseEnrolledFile (Private->FileContext);

  if (Data != NULL) {
    FreePool (Data);
  }

  if (X509Data != NULL) {
    FreePool (X509Data);
  }

  return Status;
}

/**
  Check whether the signature list exists in given variable data.

  It searches the signature list for the certificate hash by CertType.
  If the signature list is found, get the offset of Database for the
  next hash of a certificate.

  @param[in]  Database      Variable data to save signature list.
  @param[in]  DatabaseSize  Variable size.
  @param[in]  SignatureType The type of the signature.
  @param[out] Offset        The offset to save a new hash of certificate.

  @return TRUE       The signature list is found in the forbidden database.
  @return FALSE      The signature list is not found in the forbidden database.
**/
BOOLEAN
GetSignaturelistOffset (
  IN  EFI_SIGNATURE_LIST  *Database,
  IN  UINTN               DatabaseSize,
  IN  EFI_GUID            *SignatureType,
  OUT UINTN               *Offset
  )
{
  EFI_SIGNATURE_LIST  *SigList;
  UINTN               SiglistSize;

  if ((Database == NULL) || (DatabaseSize == 0)) {
    *Offset = 0;
    return FALSE;
  }

  SigList     = Database;
  SiglistSize = DatabaseSize;
  while ((SiglistSize > 0) && (SiglistSize >= SigList->SignatureListSize)) {
    if (CompareGuid (&SigList->SignatureType, SignatureType)) {
      *Offset = DatabaseSize - SiglistSize;
      return TRUE;
    }

    SiglistSize -= SigList->SignatureListSize;
    SigList      = (EFI_SIGNATURE_LIST *)((UINT8 *)SigList + SigList->SignatureListSize);
  }

  *Offset = 0;
  return FALSE;
}

/**
  Enroll a new X509 certificate hash into Signature Database (dbx) without
  KEK's authentication.

  @param[in] PrivateData      The module's private data.
  @param[in] HashAlg          The hash algorithm to enroll the certificate.
  @param[in] RevocationDate   The revocation date of the certificate.
  @param[in] RevocationTime   The revocation time of the certificate.
  @param[in] AlwaysRevocation Indicate whether the certificate is always revoked.

  @retval   EFI_SUCCESS            New X509 is enrolled successfully.
  @retval   EFI_INVALID_PARAMETER  The parameter is invalid.
  @retval   EFI_OUT_OF_RESOURCES   Could not allocate needed resources.

**/
EFI_STATUS
EnrollX509HashtoSigDB (
  IN SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN UINT32                              HashAlg,
  IN EFI_HII_DATE                        *RevocationDate,
  IN EFI_HII_TIME                        *RevocationTime,
  IN BOOLEAN                             AlwaysRevocation
  )
{
  EFI_STATUS          Status;
  UINTN               X509DataSize;
  VOID                *X509Data;
  EFI_SIGNATURE_LIST  *SignatureList;
  UINTN               SignatureListSize;
  UINT8               *Data;
  UINT8               *NewData;
  UINTN               DataSize;
  UINTN               DbSize;
  UINT32              Attr;
  EFI_SIGNATURE_DATA  *SignatureData;
  UINTN               SignatureSize;
  EFI_GUID            SignatureType;
  UINTN               Offset;
  UINT8               CertHash[MAX_DIGEST_SIZE];
  UINT16              *FilePostFix;
  UINTN               NameLength;
  EFI_TIME            *Time;
  EFI_TIME            NewTime;

  X509DataSize  = 0;
  DbSize        = 0;
  X509Data      = NULL;
  SignatureData = NULL;
  SignatureList = NULL;
  Data          = NULL;
  NewData       = NULL;

  if ((Private->FileContext->FileName == NULL) || (Private->FileContext->FHandle == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Status = SetSecureBootMode (CUSTOM_SECURE_BOOT_MODE);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Parse the file's postfix.
  //
  NameLength = StrLen (Private->FileContext->FileName);
  if (NameLength <= 4) {
    return EFI_INVALID_PARAMETER;
  }

  FilePostFix = Private->FileContext->FileName + NameLength - 4;
  if (!IsDerEncodeCertificate (FilePostFix)) {
    //
    // Only supports DER-encoded X509 certificate.
    //
    return EFI_INVALID_PARAMETER;
  }

  //
  // Get the certificate from file and calculate its hash.
  //
  Status = ReadFile (
             Private->FileContext->FHandle,
             &X509Data,
             &X509DataSize,
             0
             );
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  ASSERT (X509Data != NULL);

  if (!CalculateCertHash (X509Data, X509DataSize, HashAlg, CertHash)) {
    goto ON_EXIT;
  }

  //
  // Get the variable for enrollment.
  //
  DataSize = 0;
  Status   = gRT->GetVariable (EFI_IMAGE_SECURITY_DATABASE1, &gEfiImageSecurityDatabaseGuid, NULL, &DataSize, NULL);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    Data = (UINT8 *)AllocateZeroPool (DataSize);
    if (Data == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    Status = gRT->GetVariable (EFI_IMAGE_SECURITY_DATABASE1, &gEfiImageSecurityDatabaseGuid, NULL, &DataSize, Data);
    if (EFI_ERROR (Status)) {
      goto ON_EXIT;
    }
  }

  //
  // Allocate memory for Signature and fill the Signature
  //
  SignatureSize = sizeof (EFI_SIGNATURE_DATA) - 1 + sizeof (EFI_TIME) + GetDigestLength(HashAlg);
  SignatureData = (EFI_SIGNATURE_DATA *)AllocateZeroPool (SignatureSize);
  if (SignatureData == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  CopyGuid (&SignatureData->SignatureOwner, &gSovereignBootWizardFormSetGuid);
  CopyMem (SignatureData->SignatureData, CertHash, GetDigestLength(HashAlg));

  //
  // Fill the time.
  //
  if (!AlwaysRevocation) {
    Time         = (EFI_TIME *)(&SignatureData->SignatureData + GetDigestLength(HashAlg));
    Time->Year   = RevocationDate->Year;
    Time->Month  = RevocationDate->Month;
    Time->Day    = RevocationDate->Day;
    Time->Hour   = RevocationTime->Hour;
    Time->Minute = RevocationTime->Minute;
    Time->Second = RevocationTime->Second;
  }

  //
  // Determine the GUID for certificate hash.
  //
  switch (HashAlg) {
    case HASHALG_SHA256:
      SignatureType = gEfiCertX509Sha256Guid;
      break;
    case HASHALG_SHA384:
      SignatureType = gEfiCertX509Sha384Guid;
      break;
    case HASHALG_SHA512:
      SignatureType = gEfiCertX509Sha512Guid;
      break;
    default:
      return FALSE;
  }

  //
  // Add signature into the new variable data buffer
  //
  if (GetSignaturelistOffset ((EFI_SIGNATURE_LIST *)Data, DataSize, &SignatureType, &Offset)) {
    //
    // Add the signature to the found signaturelist.
    //
    DbSize  = DataSize + SignatureSize;
    NewData = AllocateZeroPool (DbSize);
    if (NewData == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      goto ON_EXIT;
    }

    SignatureList     = (EFI_SIGNATURE_LIST *)(Data + Offset);
    SignatureListSize = (UINTN)ReadUnaligned32 ((UINT32 *)&SignatureList->SignatureListSize);
    CopyMem (NewData, Data, Offset + SignatureListSize);

    SignatureList = (EFI_SIGNATURE_LIST *)(NewData + Offset);
    WriteUnaligned32 ((UINT32 *)&SignatureList->SignatureListSize, (UINT32)(SignatureListSize + SignatureSize));

    Offset += SignatureListSize;
    CopyMem (NewData + Offset, SignatureData, SignatureSize);
    CopyMem (NewData + Offset + SignatureSize, Data + Offset, DataSize - Offset);

    FreePool (Data);
    Data     = NewData;
    DataSize = DbSize;
  } else {
    //
    // Create a new signaturelist, and add the signature into the signaturelist.
    //
    DbSize  = DataSize + sizeof (EFI_SIGNATURE_LIST) + SignatureSize;
    NewData = AllocateZeroPool (DbSize);
    if (NewData == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      goto ON_EXIT;
    }

    //
    // Fill Certificate Database parameters.
    //
    SignatureList     = (EFI_SIGNATURE_LIST *)(NewData + DataSize);
    SignatureListSize = sizeof (EFI_SIGNATURE_LIST) + SignatureSize;
    WriteUnaligned32 ((UINT32 *)&SignatureList->SignatureListSize, (UINT32)SignatureListSize);
    WriteUnaligned32 ((UINT32 *)&SignatureList->SignatureSize, (UINT32)SignatureSize);
    CopyGuid (&SignatureList->SignatureType, &SignatureType);
    CopyMem ((UINT8 *)SignatureList + sizeof (EFI_SIGNATURE_LIST), SignatureData, SignatureSize);
    if ((DataSize != 0) && (Data != NULL)) {
      CopyMem (NewData, Data, DataSize);
      FreePool (Data);
    }

    Data     = NewData;
    DataSize = DbSize;
  }

  Status = GetCurrentTime (&NewTime);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Fail to fetch valid time data: %r", Status));
    goto ON_EXIT;
  }

  Status = CreateTimeBasedPayload (&DataSize, (UINT8 **)&Data, &NewTime);
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  Attr = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_RUNTIME_ACCESS
         | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS;
  Status = gRT->SetVariable (
                  EFI_IMAGE_SECURITY_DATABASE1,
                  &gEfiImageSecurityDatabaseGuid,
                  Attr,
                  DataSize,
                  Data
                  );
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

ON_EXIT:

  CloseEnrolledFile (Private->FileContext);

  if (Data != NULL) {
    FreePool (Data);
  }

  if (SignatureData != NULL) {
    FreePool (SignatureData);
  }

  if (X509Data != NULL) {
    FreePool (X509Data);
  }

  return Status;
}

/**
  Check whether a certificate from a file exists in dbx.

  @param[in] PrivateData     The module's private data.

  @retval   TRUE             The X509 certificate is found in dbx successfully.
  @retval   FALSE            The X509 certificate is not found in dbx.
**/
BOOLEAN
IsX509CertInDbx (
  IN SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private
  )
{
  EFI_STATUS  Status;
  UINTN       X509DataSize;
  VOID        *X509Data;
  BOOLEAN     IsFound;

  //
  //  Read the certificate from file
  //
  X509DataSize = 0;
  X509Data     = NULL;
  Status       = ReadFile (
                   Private->FileContext->FHandle,
                   &X509Data,
                   &X509DataSize,
                   0
                   );
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  //
  // Check the raw certificate.
  //
  IsFound = FALSE;
  Status = IsSignatureFoundInDatabase (
            EFI_IMAGE_SECURITY_DATABASE1,
            X509Data,
            &gEfiCertX509Guid,
            X509DataSize,
            &IsFound);
  if (EFI_ERROR (Status)) {
    IsFound = FALSE;
    goto ON_EXIT;
  }

  if (IsFound) {
    goto ON_EXIT;
  }

  //
  // Check the hash of certificate.
  //
  IsFound = FALSE;
  Status = IsCertHashFoundInDbx (
             X509Data,
             X509DataSize,
             NULL,
             0,
             NULL,
             &IsFound);
  if (EFI_ERROR (Status)) {
    IsFound = FALSE;
    goto ON_EXIT;
  }

ON_EXIT:
  if (X509Data != NULL) {
    FreePool (X509Data);
  }

  return IsFound;
}

/**
  Enroll a new signature of executable into Signature Database.

  @param[in] PrivateData     The module's private data.
  @param[in] VariableName    Variable name of signature database, must be
                             EFI_IMAGE_SECURITY_DATABASE, EFI_IMAGE_SECURITY_DATABASE1
                             or EFI_IMAGE_SECURITY_DATABASE2.

  @retval   EFI_SUCCESS            New signature is enrolled successfully.
  @retval   EFI_INVALID_PARAMETER  The parameter is invalid.
  @retval   EFI_UNSUPPORTED        Unsupported command.
  @retval   EFI_OUT_OF_RESOURCES   Could not allocate needed resources.

**/
EFI_STATUS
EnrollAuthentication2Descriptor (
  IN SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN CHAR16                              *VariableName
  )
{
  EFI_STATUS  Status;
  VOID        *Data;
  UINTN       DataSize;
  UINT32      Attr;

  Data = NULL;

  //
  // DBT only support DER-X509 Cert Enrollment
  //
  if (StrCmp (VariableName, EFI_IMAGE_SECURITY_DATABASE2) == 0) {
    return EFI_UNSUPPORTED;
  }

  //
  // Read the whole file content
  //
  Status = ReadFile (
             Private->FileContext->FHandle,
             (VOID **)&mImageBase,
             &mImageSize,
             0
             );
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  ASSERT (mImageBase != NULL);

  Attr = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_RUNTIME_ACCESS
         | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS;

  //
  // Check if SigDB variable has been already existed.
  // If true, use EFI_VARIABLE_APPEND_WRITE attribute to append the
  // new signature data to original variable
  //
  DataSize = 0;
  Status   = gRT->GetVariable (
                    VariableName,
                    &gEfiImageSecurityDatabaseGuid,
                    NULL,
                    &DataSize,
                    NULL
                    );
  if (Status == EFI_BUFFER_TOO_SMALL) {
    Attr |= EFI_VARIABLE_APPEND_WRITE;
  } else if (Status != EFI_NOT_FOUND) {
    goto ON_EXIT;
  }

  //
  // Directly set AUTHENTICATION_2 data to SetVariable
  //
  Status = gRT->SetVariable (
                  VariableName,
                  &gEfiImageSecurityDatabaseGuid,
                  Attr,
                  mImageSize,
                  mImageBase
                  );

  DEBUG ((DEBUG_INFO, "Enroll AUTH_2 data to Var:%s Status: %x\n", VariableName, Status));

ON_EXIT:

  CloseEnrolledFile (Private->FileContext);

  if (Data != NULL) {
    FreePool (Data);
  }

  if (mImageBase != NULL) {
    FreePool (mImageBase);
    mImageBase = NULL;
  }

  return Status;
}

/**
  Enroll a new signature of executable into Signature Database.

  @param[in] PrivateData     The module's private data.
  @param[in] VariableName    Variable name of signature database, must be
                             EFI_IMAGE_SECURITY_DATABASE, EFI_IMAGE_SECURITY_DATABASE1
                             or EFI_IMAGE_SECURITY_DATABASE2.

  @retval   EFI_SUCCESS            New signature is enrolled successfully.
  @retval   EFI_INVALID_PARAMETER  The parameter is invalid.
  @retval   EFI_UNSUPPORTED        Unsupported command.
  @retval   EFI_OUT_OF_RESOURCES   Could not allocate needed resources.

**/
EFI_STATUS
EnrollImageSignatureToSigDB (
  IN SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN CHAR16                              *VariableName
  )
{
  EFI_STATUS                 Status;
  EFI_SIGNATURE_LIST         *SigDBCert;
  EFI_SIGNATURE_DATA         *SigDBCertData;
  VOID                       *Data;
  UINTN                      DataSize;
  UINTN                      SigDBSize;
  UINT32                     Attr;
  WIN_CERTIFICATE_UEFI_GUID  *GuidCertData;
  WIN_CERTIFICATE_EFI_PKCS   *PkcsCertData;
  EFI_TIME                   Time;
  UINT32                     HashAlg;
  EFI_GUID                   CertType;
  UINTN                      ImageDigestSize;
  UINT8                      ImageDigest[MAX_DIGEST_SIZE];

  Data         = NULL;
  GuidCertData = NULL;

  if (StrCmp (VariableName, EFI_IMAGE_SECURITY_DATABASE2) == 0) {
    return EFI_UNSUPPORTED;
  }

  //
  // Form the SigDB certificate list.
  // Format the data item into EFI_SIGNATURE_LIST type.
  //
  // We need to parse signature data of executable from specified signed executable file.
  // In current implementation, we simply trust the pass-in signed executable file.
  // In reality, it's OS's responsibility to verify the signed executable file.
  //

  //
  // Read the whole file content
  //
  Status = ReadFile (
             Private->FileContext->FHandle,
             (VOID **)&mImageBase,
             &mImageSize,
             0
             );
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  ASSERT (mImageBase != NULL);

  Status = GetImageSecDataDir (mImageBase, mImageSize, (EFI_IMAGE_DATA_DIRECTORY **)&mSecDataDir);
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  if (mSecDataDir->SizeOfCert == 0) {
    Status  = EFI_SECURITY_VIOLATION;
    HashAlg = HASHALG_MAX;
    while (HashAlg > 0) {
      HashAlg--;

      if (HashPeImage (mImageBase, mImageSize, HashAlg, ImageDigest, &ImageDigestSize, &CertType)) {
        Status = EFI_SUCCESS;
        break;
      }

    }

    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "Fail to get hash digest: %r", Status));
      goto ON_EXIT;
    }
  } else {
    //
    // Read the certificate data
    //
    mCertificate = (WIN_CERTIFICATE *)(mImageBase + mSecDataDir->Offset);

    if (mCertificate->wCertificateType == WIN_CERT_TYPE_EFI_GUID) {
      GuidCertData = (WIN_CERTIFICATE_UEFI_GUID *)mCertificate;
      if (CompareMem (&GuidCertData->CertType, &gEfiCertTypeRsa2048Sha256Guid, sizeof (EFI_GUID)) != 0) {
        Status = EFI_ABORTED;
        goto ON_EXIT;
      }

      if (!HashPeImage (mImageBase, mImageSize, HASHALG_SHA256, ImageDigest, &ImageDigestSize, &CertType)) {
        Status = EFI_ABORTED;
        goto ON_EXIT;
      }
    } else if (mCertificate->wCertificateType == WIN_CERT_TYPE_PKCS_SIGNED_DATA) {
      PkcsCertData = (WIN_CERTIFICATE_EFI_PKCS *)mCertificate;
      if (PkcsCertData->Hdr.dwLength <= sizeof (PkcsCertData->Hdr)) {
        Status = EFI_ABORTED;
        goto ON_EXIT;
      }
      Status =  HashPeImageByType (
                  mImageBase,
                  mImageSize,
                  PkcsCertData->CertData,
                  PkcsCertData->Hdr.dwLength - sizeof (PkcsCertData->Hdr),
                  ImageDigest,
                  &ImageDigestSize,
                  &CertType);
      if (EFI_ERROR (Status)) {
        goto ON_EXIT;
      }
    } else {
      Status = EFI_ABORTED;
      goto ON_EXIT;
    }
  }

  //
  // Create a new SigDB entry.
  //
  SigDBSize = sizeof (EFI_SIGNATURE_LIST)
              + sizeof (EFI_SIGNATURE_DATA) - 1
              + (UINT32)ImageDigestSize;

  Data = (UINT8 *)AllocateZeroPool (SigDBSize);
  if (Data == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  //
  // Adjust the Certificate Database parameters.
  //
  SigDBCert                      = (EFI_SIGNATURE_LIST *)Data;
  SigDBCert->SignatureListSize   = (UINT32)SigDBSize;
  SigDBCert->SignatureHeaderSize = 0;
  SigDBCert->SignatureSize       = sizeof (EFI_SIGNATURE_DATA) - 1 + (UINT32)ImageDigestSize;
  CopyGuid (&SigDBCert->SignatureType, &CertType);

  SigDBCertData = (EFI_SIGNATURE_DATA *)((UINT8 *)SigDBCert + sizeof (EFI_SIGNATURE_LIST));
  CopyGuid (&SigDBCertData->SignatureOwner, &gSovereignBootWizardFormSetGuid);
  CopyMem (SigDBCertData->SignatureData, ImageDigest, ImageDigestSize);

  Attr = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_RUNTIME_ACCESS
         | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS;
  Status = GetCurrentTime (&Time);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Fail to fetch valid time data: %r", Status));
    goto ON_EXIT;
  }

  Status = CreateTimeBasedPayload (&SigDBSize, (UINT8 **)&Data, &Time);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Fail to create time-based data payload: %r", Status));
    goto ON_EXIT;
  }

  //
  // Check if SigDB variable has been already existed.
  // If true, use EFI_VARIABLE_APPEND_WRITE attribute to append the
  // new signature data to original variable
  //
  DataSize = 0;
  Status   = gRT->GetVariable (
                    VariableName,
                    &gEfiImageSecurityDatabaseGuid,
                    NULL,
                    &DataSize,
                    NULL
                    );
  if (Status == EFI_BUFFER_TOO_SMALL) {
    Attr |= EFI_VARIABLE_APPEND_WRITE;
  } else if (Status != EFI_NOT_FOUND) {
    goto ON_EXIT;
  }

  //
  // Enroll the variable.
  //
  Status = gRT->SetVariable (
                  VariableName,
                  &gEfiImageSecurityDatabaseGuid,
                  Attr,
                  SigDBSize,
                  Data
                  );
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

ON_EXIT:

  CloseEnrolledFile (Private->FileContext);

  if (Data != NULL) {
    FreePool (Data);
  }

  if (mImageBase != NULL) {
    FreePool (mImageBase);
    mImageBase = NULL;
  }

  return Status;
}

/**
  Enroll signature into DB/DBX/DBT without KEK's authentication.
  The SignatureOwner GUID will be SovereignBootWizardFormsetGuid.

  @param[in] PrivateData     The module's private data.
  @param[in] VariableName    Variable name of signature database, must be
                             EFI_IMAGE_SECURITY_DATABASE or EFI_IMAGE_SECURITY_DATABASE1.

  @retval   EFI_SUCCESS            New signature enrolled successfully.
  @retval   EFI_INVALID_PARAMETER  The parameter is invalid.
  @retval   others                 Fail to enroll signature data.

**/
EFI_STATUS
EnrollSignatureDatabase (
  IN SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN CHAR16                              *VariableName
  )
{
  UINT16      *FilePostFix;
  EFI_STATUS  Status;
  UINTN       NameLength;

  if ((Private->FileContext->FileName == NULL) || (Private->FileContext->FHandle == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Status = SetSecureBootMode (CUSTOM_SECURE_BOOT_MODE);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Parse the file's postfix.
  //
  NameLength = StrLen (Private->FileContext->FileName);
  if (NameLength <= 4) {
    return EFI_INVALID_PARAMETER;
  }

  FilePostFix = Private->FileContext->FileName + NameLength - 4;
  if (IsDerEncodeCertificate (FilePostFix)) {
    //
    // Supports DER-encoded X509 certificate.
    //
    return EnrollX509toSigDB (Private, VariableName);
  } else if (IsAuthentication2Format (Private->FileContext->FHandle)) {
    return EnrollAuthentication2Descriptor (Private, VariableName);
  } else {
    return EnrollImageSignatureToSigDB (Private, VariableName);
  }
}

/**
  List all signatures in specified signature database (e.g. KEK/DB/DBX/DBT)
  by GUID in the page for user to select and delete as needed.

  @param[in]    PrivateData         Module's private data.
  @param[in]    VariableName        The variable name of the vendor's signature database.
  @param[in]    VendorGuid          A unique identifier for the vendor.
  @param[in]    LabelNumber         Label number to insert opcodes.
  @param[in]    FormId              Form ID of current page.
  @param[in]    QuestionIdBase      Base question id of the signature list.

  @retval   EFI_SUCCESS             Success to update the signature list page
  @retval   EFI_OUT_OF_RESOURCES    Unable to allocate required resources.

**/
EFI_STATUS
UpdateDeletePage (
  IN SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *PrivateData,
  IN CHAR16                              *VariableName,
  IN EFI_GUID                            *VendorGuid,
  IN UINT16                              LabelNumber,
  IN EFI_FORM_ID                         FormId,
  IN EFI_QUESTION_ID                     QuestionIdBase
  )
{
  EFI_STATUS          Status;
  UINT32              Index;
  UINTN               CertCount;
  UINTN               GuidIndex;
  VOID                *StartOpCodeHandle;
  VOID                *EndOpCodeHandle;
  EFI_IFR_GUID_LABEL  *StartLabel;
  EFI_IFR_GUID_LABEL  *EndLabel;
  UINTN               DataSize;
  UINT8               *Data;
  EFI_SIGNATURE_LIST  *CertList;
  EFI_SIGNATURE_DATA  *Cert;
  UINT32              ItemDataSize;
  CHAR16              *CertificateInfoStr;
  CHAR8               *CertificateInfoStr8;
  EFI_STRING_ID       CertificateInfoID;
  EFI_STRING_ID       Help;
  CHAR16              *FormatNameString;
  UINTN               CertificateInfoStrSize;
  CHAR16              *UnknownCert;
  RETURN_STATUS       RStatus;
  CHAR16              *NameString;
  EFI_STRING_ID       NameStringId;

  Data                    = NULL;
  CertList                = NULL;
  Cert                    = NULL;
  CertificateInfoStr      = NULL;
  CertificateInfoStr8     = NULL;
  StartOpCodeHandle       = NULL;
  EndOpCodeHandle         = NULL;
  NameString              = NULL;
  FormatNameString        = NULL;
  CertificateInfoStrSize  = 0;
  UnknownCert             = L"Unknown Certificate: No Common Name, No Issuer";
  RStatus                 = RETURN_SUCCESS;

  //
  // Initialize the container for dynamic opcodes.
  //
  StartOpCodeHandle = HiiAllocateOpCodeHandle ();
  if (StartOpCodeHandle == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  EndOpCodeHandle = HiiAllocateOpCodeHandle ();
  if (EndOpCodeHandle == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  //
  // Create Hii Extend Label OpCode.
  //
  StartLabel = (EFI_IFR_GUID_LABEL *)HiiCreateGuidOpCode (
                                       StartOpCodeHandle,
                                       &gEfiIfrTianoGuid,
                                       NULL,
                                       sizeof (EFI_IFR_GUID_LABEL)
                                       );
  StartLabel->ExtendOpCode = EFI_IFR_EXTEND_OP_LABEL;
  StartLabel->Number       = LabelNumber;

  EndLabel = (EFI_IFR_GUID_LABEL *)HiiCreateGuidOpCode (
                                     EndOpCodeHandle,
                                     &gEfiIfrTianoGuid,
                                     NULL,
                                     sizeof (EFI_IFR_GUID_LABEL)
                                     );
  EndLabel->ExtendOpCode = EFI_IFR_EXTEND_OP_LABEL;
  EndLabel->Number       = LABEL_END;

  //
  // Read Variable.
  //
  DataSize = 0;
  Status   = gRT->GetVariable (VariableName, VendorGuid, NULL, &DataSize, Data);
  if (EFI_ERROR (Status) && (Status != EFI_BUFFER_TOO_SMALL)) {
    goto ON_EXIT;
  }

  Data = (UINT8 *)AllocateZeroPool (DataSize);
  if (Data == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  Status = gRT->GetVariable (VariableName, VendorGuid, NULL, &DataSize, Data);
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  //
  // Enumerate all KEK pub data.
  //
  ItemDataSize = (UINT32)DataSize;
  CertList     = (EFI_SIGNATURE_LIST *)Data;
  GuidIndex    = 0;

  while ((ItemDataSize > 0) && (ItemDataSize >= CertList->SignatureListSize)) {
    if (CompareGuid (&CertList->SignatureType, &gEfiCertX509Guid)) {
      /* Handle certificates */
      CertCount = (CertList->SignatureListSize - sizeof (EFI_SIGNATURE_LIST) - CertList->SignatureHeaderSize) / CertList->SignatureSize;
      for (Index = 0; Index < CertCount; Index++) {
        Cert = (EFI_SIGNATURE_DATA *)((UINT8 *)CertList
                                      + sizeof (EFI_SIGNATURE_LIST)
                                      + CertList->SignatureHeaderSize
                                      + Index * CertList->SignatureSize);

        // Get the required buffer size
        X509GetCommonName (
          (UINT8*)Cert->SignatureData,
          (UINTN)CertList->SignatureSize,
          NULL,
          &CertificateInfoStrSize
          );

        CertificateInfoStr8 = AllocateZeroPool (CertificateInfoStrSize * sizeof(CHAR8));
        if (CertificateInfoStr8 == NULL) {
          Status = EFI_OUT_OF_RESOURCES;
          goto ON_EXIT;
        }
        CertificateInfoStr = AllocateZeroPool (CertificateInfoStrSize * sizeof(CHAR16));
        if (CertificateInfoStr == NULL) {
          Status = EFI_OUT_OF_RESOURCES;
          goto ON_EXIT;
        }

        //
        // Display GUID and help
        //
        RStatus = X509GetCommonName (
          (UINT8*)Cert->SignatureData,
          (UINTN)CertList->SignatureSize,
          CertificateInfoStr8,
          &CertificateInfoStrSize
          );

        if (!EFI_ERROR (RStatus)) {
          AsciiStrToUnicodeStrS (CertificateInfoStr8, CertificateInfoStr, CertificateInfoStrSize);
          CertificateInfoID = HiiSetString (PrivateData->HiiHandle, 0, CertificateInfoStr, NULL);
        } else {
          CertificateInfoID = HiiSetString (PrivateData->HiiHandle, 0, UnknownCert, NULL);
        }

        Status = FormatHelpInfo (PrivateData, CertList, Cert, &Help);
        if (!EFI_ERROR (Status)) {
          HiiCreateCheckBoxOpCode (
            StartOpCodeHandle,
            (EFI_QUESTION_ID)(QuestionIdBase + GuidIndex++),
            0,
            0,
            CertificateInfoID,
            Help,
            EFI_IFR_FLAG_CALLBACK | EFI_IFR_FLAG_RESET_REQUIRED,
            0,
            NULL
            );
        }

        FreePool (CertificateInfoStr8);
        FreePool (CertificateInfoStr);
        CertificateInfoStr8 = NULL;
        CertificateInfoStr = NULL;
      }
    } else if (CompareGuid (&CertList->SignatureType, &gEfiCertSha1Guid) ||
               CompareGuid (&CertList->SignatureType, &gEfiCertSha224Guid) ||
               CompareGuid (&CertList->SignatureType, &gEfiCertSha256Guid) ||
               CompareGuid (&CertList->SignatureType, &gEfiCertSha384Guid) ||
               CompareGuid (&CertList->SignatureType, &gEfiCertSha512Guid) ||
               CompareGuid (&CertList->SignatureType, &gEfiCertSm3Guid) ||
               CompareGuid (&CertList->SignatureType, &gEfiCertX509Sha256Guid) ||
               CompareGuid (&CertList->SignatureType, &gEfiCertX509Sha384Guid) ||
               CompareGuid (&CertList->SignatureType, &gEfiCertX509Sha512Guid) ||
               CompareGuid (&CertList->SignatureType, &gEfiCertX509Sm3Guid) ||
               CompareGuid (&CertList->SignatureType, &gEfiCertRsa2048Guid) ||
               CompareGuid (&CertList->SignatureType, &gEfiCertRsa2048Sha256Guid)
              )
    {
      /* Handle image hashes or certificate hashes */
      CertCount = (CertList->SignatureListSize - sizeof (EFI_SIGNATURE_LIST) - CertList->SignatureHeaderSize) / CertList->SignatureSize;
      for (Index = 0; Index < CertCount; Index++) {
        Cert = (EFI_SIGNATURE_DATA *)((UINT8 *)CertList
                                      + sizeof (EFI_SIGNATURE_LIST)
                                      + CertList->SignatureHeaderSize
                                      + Index * CertList->SignatureSize);

        if (CompareGuid (&CertList->SignatureType, &gEfiCertX509Sha256Guid) ||
            CompareGuid (&CertList->SignatureType, &gEfiCertX509Sha384Guid) ||
            CompareGuid (&CertList->SignatureType, &gEfiCertX509Sha512Guid) ||
            CompareGuid (&CertList->SignatureType, &gEfiCertX509Sm3Guid)
           )
        {
          FormatNameString = HiiGetString (PrivateData->HiiHandle, STRING_TOKEN (STR_SIGNATURE_CERT_HASH_NAME_FORMAT), NULL);
        } else if (CompareGuid (&CertList->SignatureType, &gEfiCertRsa2048Sha256Guid)) {
          FormatNameString = HiiGetString (PrivateData->HiiHandle, STRING_TOKEN (STR_SIGNATURE_DATA_RSA_HASH_NAME_FORMAT), NULL);
        } else if (CompareGuid (&CertList->SignatureType, &gEfiCertRsa2048Guid)) {
          FormatNameString = HiiGetString (PrivateData->HiiHandle, STRING_TOKEN (STR_SIGNATURE_DATA_RSA_NAME_FORMAT), NULL);
        } else {
          FormatNameString = HiiGetString (PrivateData->HiiHandle, STRING_TOKEN (STR_SIGNATURE_DATA_HASH_NAME_FORMAT), NULL);
        }

        if (FormatNameString == NULL) {
          goto ON_EXIT;
        }

        NameString = AllocateZeroPool (BUFFER_MAX_SIZE + StrLen(FormatNameString) * sizeof(CHAR16));
        if (NameString == NULL) {
          Status = EFI_OUT_OF_RESOURCES;
          goto ON_EXIT;
        }

        UnicodeSPrint (NameString, BUFFER_MAX_SIZE + StrLen(FormatNameString) * sizeof(CHAR16), FormatNameString, &Cert->SignatureOwner),
        NameStringId = HiiSetString (PrivateData->HiiHandle, 0, NameString, NULL);

        Status = FormatHelpInfo (PrivateData, CertList, Cert, &Help);
        if (!EFI_ERROR (Status)) {
          HiiCreateCheckBoxOpCode (
            StartOpCodeHandle,
            (EFI_QUESTION_ID)(QuestionIdBase + GuidIndex++),
            0,
            0,
            NameStringId,
            Help,
            EFI_IFR_FLAG_CALLBACK | EFI_IFR_FLAG_RESET_REQUIRED,
            0,
            NULL
            );
        }
      }
    } else {
      DEBUG ((EFI_D_WARN, "%a(): Unhandled Cert Type: %g\n", __FUNCTION__, &CertList->SignatureType));
    }

    ItemDataSize -= CertList->SignatureListSize;
    CertList      = (EFI_SIGNATURE_LIST *)((UINT8 *)CertList + CertList->SignatureListSize);
  }

ON_EXIT:
  HiiUpdateForm (
    PrivateData->HiiHandle,
    &gSovereignBootWizardFormSetGuid,
    FormId,
    StartOpCodeHandle,
    EndOpCodeHandle
    );

  if (StartOpCodeHandle != NULL) {
    HiiFreeOpCodeHandle (StartOpCodeHandle);
  }

  if (EndOpCodeHandle != NULL) {
    HiiFreeOpCodeHandle (EndOpCodeHandle);
  }

  if (Data != NULL) {
    FreePool (Data);
  }

  if (CertificateInfoStr != NULL) {
    FreePool (CertificateInfoStr);
  }

  if (CertificateInfoStr8 != NULL) {
    FreePool (CertificateInfoStr8);
  }

  if (NameString != NULL) {
    FreePool (NameString);
  }

  return EFI_SUCCESS;
}

/**
  Delete a signature entry from signature database.

  @param[in]    PrivateData         Module's private data.
  @param[in]    VariableName        The variable name of the vendor's signature database.
  @param[in]    VendorGuid          A unique identifier for the vendor.
  @param[in]    LabelNumber         Label number to insert opcodes.
  @param[in]    FormId              Form ID of current page.
  @param[in]    QuestionIdBase      Base question id of the signature list.
  @param[in]    DeleteIndex         Signature index to delete.

  @retval   EFI_SUCCESS             Delete signature successfully.
  @retval   EFI_NOT_FOUND           Can't find the signature item,
  @retval   EFI_OUT_OF_RESOURCES    Could not allocate needed resources.
**/
EFI_STATUS
DeleteSignature (
  IN SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *PrivateData,
  IN CHAR16                              *VariableName,
  IN EFI_GUID                            *VendorGuid,
  IN UINT16                              LabelNumber,
  IN EFI_FORM_ID                         FormId,
  IN EFI_QUESTION_ID                     QuestionIdBase,
  IN UINTN                               DeleteIndex
  )
{
  EFI_STATUS          Status;
  UINTN               DataSize;
  UINT8               *Data;
  UINT8               *OldData;
  UINT32              Attr;
  UINT32              Index;
  EFI_SIGNATURE_LIST  *CertList;
  EFI_SIGNATURE_LIST  *NewCertList;
  EFI_SIGNATURE_DATA  *Cert;
  UINTN               CertCount;
  UINT32              Offset;
  BOOLEAN             IsItemFound;
  UINT32              ItemDataSize;
  UINTN               GuidIndex;
  EFI_TIME            Time;

  Data     = NULL;
  OldData  = NULL;
  CertList = NULL;
  Cert     = NULL;
  Attr     = 0;

  Status = SetSecureBootMode (CUSTOM_SECURE_BOOT_MODE);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Get original signature list data.
  //
  DataSize = 0;
  Status   = gRT->GetVariable (VariableName, VendorGuid, NULL, &DataSize, NULL);
  if (EFI_ERROR (Status) && (Status != EFI_BUFFER_TOO_SMALL)) {
    goto ON_EXIT;
  }

  OldData = (UINT8 *)AllocateZeroPool (DataSize);
  if (OldData == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  Status = gRT->GetVariable (VariableName, VendorGuid, &Attr, &DataSize, OldData);
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  //
  // Allocate space for new variable.
  //
  Data = (UINT8 *)AllocateZeroPool (DataSize);
  if (Data == NULL) {
    Status =  EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  //
  // Enumerate all signature data and erasing the target item.
  //
  IsItemFound  = FALSE;
  ItemDataSize = (UINT32)DataSize;
  CertList     = (EFI_SIGNATURE_LIST *)OldData;
  Offset       = 0;
  GuidIndex    = 0;
  while ((ItemDataSize > 0) && (ItemDataSize >= CertList->SignatureListSize)) {
    if (CompareGuid (&CertList->SignatureType, &gEfiCertRsa2048Guid) ||
        CompareGuid (&CertList->SignatureType, &gEfiCertX509Guid) ||
        CompareGuid (&CertList->SignatureType, &gEfiCertSha1Guid) ||
        CompareGuid (&CertList->SignatureType, &gEfiCertSha224Guid) ||
        CompareGuid (&CertList->SignatureType, &gEfiCertSha384Guid) ||
        CompareGuid (&CertList->SignatureType, &gEfiCertSha256Guid) ||
        CompareGuid (&CertList->SignatureType, &gEfiCertSha512Guid) ||
        CompareGuid (&CertList->SignatureType, &gEfiCertSm3Guid) ||
        CompareGuid (&CertList->SignatureType, &gEfiCertX509Sha256Guid) ||
        CompareGuid (&CertList->SignatureType, &gEfiCertX509Sha384Guid) ||
        CompareGuid (&CertList->SignatureType, &gEfiCertX509Sha512Guid) ||
        CompareGuid (&CertList->SignatureType, &gEfiCertX509Sm3Guid)
        )
    {
      //
      // Copy EFI_SIGNATURE_LIST header then calculate the signature count in this list.
      //
      CopyMem (Data + Offset, CertList, (sizeof (EFI_SIGNATURE_LIST) + CertList->SignatureHeaderSize));
      NewCertList = (EFI_SIGNATURE_LIST *)(Data + Offset);
      Offset     += (sizeof (EFI_SIGNATURE_LIST) + CertList->SignatureHeaderSize);
      Cert        = (EFI_SIGNATURE_DATA *)((UINT8 *)CertList + sizeof (EFI_SIGNATURE_LIST) + CertList->SignatureHeaderSize);
      CertCount   = (CertList->SignatureListSize - sizeof (EFI_SIGNATURE_LIST) - CertList->SignatureHeaderSize) / CertList->SignatureSize;
      for (Index = 0; Index < CertCount; Index++) {
        if (GuidIndex == DeleteIndex) {
          //
          // Find it! Skip it!
          //
          NewCertList->SignatureListSize -= CertList->SignatureSize;
          IsItemFound                     = TRUE;
        } else {
          //
          // This item doesn't match. Copy it to the Data buffer.
          //
          CopyMem (Data + Offset, (UINT8 *)(Cert), CertList->SignatureSize);
          Offset += CertList->SignatureSize;
        }

        GuidIndex++;
        Cert = (EFI_SIGNATURE_DATA *)((UINT8 *)Cert + CertList->SignatureSize);
      }
    } else {
      //
      // This List doesn't match. Just copy it to the Data buffer.
      //
      CopyMem (Data + Offset, (UINT8 *)(CertList), CertList->SignatureListSize);
      Offset += CertList->SignatureListSize;
    }

    ItemDataSize -= CertList->SignatureListSize;
    CertList      = (EFI_SIGNATURE_LIST *)((UINT8 *)CertList + CertList->SignatureListSize);
  }

  if (!IsItemFound) {
    //
    // Doesn't find the signature Item!
    //
    Status = EFI_NOT_FOUND;
    goto ON_EXIT;
  }

  //
  // Delete the EFI_SIGNATURE_LIST header if there is no signature in the list.
  //
  ItemDataSize = Offset;
  CertList     = (EFI_SIGNATURE_LIST *)Data;
  Offset       = 0;
  ZeroMem (OldData, ItemDataSize);
  while ((ItemDataSize > 0) && (ItemDataSize >= CertList->SignatureListSize)) {
    CertCount = (CertList->SignatureListSize - sizeof (EFI_SIGNATURE_LIST) - CertList->SignatureHeaderSize) / CertList->SignatureSize;
    DEBUG ((DEBUG_INFO, "       CertCount = %x\n", CertCount));
    if (CertCount != 0) {
      CopyMem (OldData + Offset, (UINT8 *)(CertList), CertList->SignatureListSize);
      Offset += CertList->SignatureListSize;
    }

    ItemDataSize -= CertList->SignatureListSize;
    CertList      = (EFI_SIGNATURE_LIST *)((UINT8 *)CertList + CertList->SignatureListSize);
  }

  DataSize = Offset;
  if ((Attr & EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS) != 0) {
    Status = GetCurrentTime (&Time);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "Fail to fetch valid time data: %r", Status));
      goto ON_EXIT;
    }

    Status = CreateTimeBasedPayload (&DataSize, &OldData, &Time);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "Fail to create time-based data payload: %r", Status));
      goto ON_EXIT;
    }
  }

  Status = gRT->SetVariable (
                  VariableName,
                  VendorGuid,
                  Attr,
                  DataSize,
                  OldData
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to set variable, Status = %r\n", Status));
    goto ON_EXIT;
  }

ON_EXIT:
  if (Data != NULL) {
    FreePool (Data);
  }

  if (OldData != NULL) {
    FreePool (OldData);
  }

  return UpdateDeletePage (
           PrivateData,
           VariableName,
           VendorGuid,
           LabelNumber,
           FormId,
           QuestionIdBase
           );
}

/**
  This function to delete signature list or data, according by DelType.

  @param[in]  PrivateData           Module's private data.
  @param[in]  DelType               Indicate delete signature list or data.
  @param[in]  CheckedCount          Indicate how many signature data have
                                    been checked in current signature list.

  @retval   EFI_SUCCESS             Success to update the signature list page
  @retval   EFI_OUT_OF_RESOURCES    Unable to allocate required resources.
**/
EFI_STATUS
DeleteSignatureEx (
  IN SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *PrivateData,
  IN SIGNATURE_DELETE_TYPE               DelType,
  IN UINT32                              CheckedCount
  )
{
  EFI_STATUS          Status;
  EFI_SIGNATURE_LIST  *ListWalker;
  EFI_SIGNATURE_LIST  *NewCertList;
  EFI_SIGNATURE_DATA  *DataWalker;
  CHAR16              VariableName[BUFFER_MAX_SIZE];
  UINT32              VariableAttr;
  UINTN               VariableDataSize;
  UINTN               RemainingSize;
  UINTN               ListIndex;
  UINTN               Index;
  UINTN               Offset;
  UINT8               *VariableData;
  UINT8               *NewVariableData;
  EFI_TIME            Time;

  Status           = EFI_SUCCESS;
  VariableAttr     = 0;
  VariableDataSize = 0;
  ListIndex        = 0;
  Offset           = 0;
  VariableData     = NULL;
  NewVariableData  = NULL;

  if (PrivateData->VariableName == Variable_DB) {
    UnicodeSPrint (VariableName, sizeof (VariableName), EFI_IMAGE_SECURITY_DATABASE);
  } else if (PrivateData->VariableName == Variable_DBX) {
    UnicodeSPrint (VariableName, sizeof (VariableName), EFI_IMAGE_SECURITY_DATABASE1);
  } else {
    goto ON_EXIT;
  }

  Status = gRT->GetVariable (
                  VariableName,
                  &gEfiImageSecurityDatabaseGuid,
                  &VariableAttr,
                  &VariableDataSize,
                  VariableData
                  );
  if (EFI_ERROR (Status) && (Status != EFI_BUFFER_TOO_SMALL)) {
    goto ON_EXIT;
  }

  VariableData = AllocateZeroPool (VariableDataSize);
  if (VariableData == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  Status = gRT->GetVariable (
                  VariableName,
                  &gEfiImageSecurityDatabaseGuid,
                  &VariableAttr,
                  &VariableDataSize,
                  VariableData
                  );
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  Status = SetSecureBootMode (CUSTOM_SECURE_BOOT_MODE);
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  NewVariableData = AllocateZeroPool (VariableDataSize);
  if (NewVariableData == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  RemainingSize = VariableDataSize;
  ListWalker    = (EFI_SIGNATURE_LIST *)(VariableData);
  if (DelType == Delete_Signature_List_All) {
    VariableDataSize = 0;
  } else {
    //
    //  Traverse to target EFI_SIGNATURE_LIST but others will be skipped.
    //
    while ((RemainingSize > 0) && (RemainingSize >= ListWalker->SignatureListSize) && ListIndex < PrivateData->ListIndex) {
      CopyMem ((UINT8 *)NewVariableData + Offset, ListWalker, ListWalker->SignatureListSize);
      Offset += ListWalker->SignatureListSize;

      RemainingSize -= ListWalker->SignatureListSize;
      ListWalker     = (EFI_SIGNATURE_LIST *)((UINT8 *)ListWalker + ListWalker->SignatureListSize);
      ListIndex++;
    }

    //
    //  Handle the target EFI_SIGNATURE_LIST.
    //  If CheckedCount == SIGNATURE_DATA_COUNTS (ListWalker) or DelType == Delete_Signature_List_One
    //  it means delete the whole EFI_SIGNATURE_LIST, So we just skip this EFI_SIGNATURE_LIST.
    //
    if ((CheckedCount < SIGNATURE_DATA_COUNTS (ListWalker)) && (DelType == Delete_Signature_Data)) {
      NewCertList = (EFI_SIGNATURE_LIST *)(NewVariableData + Offset);
      //
      // Copy header.
      //
      CopyMem ((UINT8 *)NewVariableData + Offset, ListWalker, sizeof (EFI_SIGNATURE_LIST) + ListWalker->SignatureHeaderSize);
      Offset += sizeof (EFI_SIGNATURE_LIST) + ListWalker->SignatureHeaderSize;

      DataWalker = (EFI_SIGNATURE_DATA *)((UINT8 *)ListWalker + sizeof (EFI_SIGNATURE_LIST) + ListWalker->SignatureHeaderSize);
      for (Index = 0; Index < SIGNATURE_DATA_COUNTS (ListWalker); Index = Index + 1) {
        if (PrivateData->CheckArray[Index]) {
          //
          // Delete checked signature data, and update the size of whole signature list.
          //
          NewCertList->SignatureListSize -= NewCertList->SignatureSize;
        } else {
          //
          // Remain the unchecked signature data.
          //
          CopyMem ((UINT8 *)NewVariableData + Offset, DataWalker, ListWalker->SignatureSize);
          Offset += ListWalker->SignatureSize;
        }

        DataWalker = (EFI_SIGNATURE_DATA *)((UINT8 *)DataWalker + ListWalker->SignatureSize);
      }
    }

    RemainingSize -= ListWalker->SignatureListSize;
    ListWalker     = (EFI_SIGNATURE_LIST *)((UINT8 *)ListWalker + ListWalker->SignatureListSize);

    //
    // Copy remaining data, maybe 0.
    //
    CopyMem ((UINT8 *)NewVariableData + Offset, ListWalker, RemainingSize);
    Offset += RemainingSize;

    VariableDataSize = Offset;
  }

  if ((VariableAttr & EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS) != 0) {
    Status = GetCurrentTime (&Time);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "Fail to fetch valid time data: %r", Status));
      goto ON_EXIT;
    }

    Status = CreateTimeBasedPayload (&VariableDataSize, &NewVariableData, &Time);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "Fail to create time-based data payload: %r", Status));
      goto ON_EXIT;
    }
  }

  Status = gRT->SetVariable (
                  VariableName,
                  &gEfiImageSecurityDatabaseGuid,
                  VariableAttr,
                  VariableDataSize,
                  NewVariableData
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to set variable, Status = %r", Status));
    goto ON_EXIT;
  }

ON_EXIT:
  FREE_NON_NULL (VariableData);
  FREE_NON_NULL (NewVariableData);

  return Status;
}

/**
  This function extracts configuration from variable.

  @param[in]       Private      Point to Sovereign Boot configuration driver private data.
  @param[in, out]  FormData     Point to Sovereign Boot form private data.

**/
VOID
InteractiveModeExtractConfig (
  IN     SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN OUT SOVEREIGN_BOOT_WIZARD_FORM_DATA     *FormData
  )
{
  EFI_TIME  CurrTime;

  //
  // Initialize the Date and Time using system time.
  //
  FormData->CertificateFormat = HASHALG_RAW;
  FormData->AlwaysRevocation  = TRUE;
  gRT->GetTime (&CurrTime, NULL);
  FormData->RevocationDate.Year   = CurrTime.Year;
  FormData->RevocationDate.Month  = CurrTime.Month;
  FormData->RevocationDate.Day    = CurrTime.Day;
  FormData->RevocationTime.Hour   = CurrTime.Hour;
  FormData->RevocationTime.Minute = CurrTime.Minute;
  FormData->RevocationTime.Second = 0;
  if (Private->FileContext->FHandle != NULL) {
    FormData->FileEnrollType = Private->FileContext->FileType;
  } else {
    FormData->FileEnrollType = UNKNOWN_FILE_TYPE;
  }

  FormData->ListCount = Private->ListCount;
}

/**
  This function to load signature list, the update the menu page.

  @param[in]  PrivateData         Module's private data.
  @param[in]  LabelId             Label number to insert opcodes.
  @param[in]  FormId              Form ID of current page.
  @param[in]  QuestionIdBase      Base question id of the signature list.

  @retval   EFI_SUCCESS           Success to update the signature list page
  @retval   EFI_OUT_OF_RESOURCES  Unable to allocate required resources.
**/
EFI_STATUS
LoadSignatureList (
  IN SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *PrivateData,
  IN UINT16                              LabelId,
  IN EFI_FORM_ID                         FormId,
  IN EFI_QUESTION_ID                     QuestionIdBase
  )
{
  EFI_STATUS          Status;
  EFI_STRING_ID       ListType;
  EFI_STRING          FormatNameString;
  EFI_STRING          FormatHelpString;
  EFI_STRING          FormatTypeString;
  EFI_SIGNATURE_LIST  *ListWalker;
  EFI_IFR_GUID_LABEL  *StartLabel;
  EFI_IFR_GUID_LABEL  *EndLabel;
  EFI_IFR_GUID_LABEL  *StartGoto;
  EFI_IFR_GUID_LABEL  *EndGoto;
  EFI_FORM_ID         DstFormId;
  VOID                *StartOpCodeHandle;
  VOID                *EndOpCodeHandle;
  VOID                *StartGotoHandle;
  VOID                *EndGotoHandle;
  UINTN               DataSize;
  UINTN               RemainingSize;
  UINT16              Index;
  UINT8               *VariableData;
  CHAR16              VariableName[BUFFER_MAX_SIZE];
  CHAR16              NameBuffer[BUFFER_MAX_SIZE];
  CHAR16              HelpBuffer[BUFFER_MAX_SIZE];

  Status            = EFI_SUCCESS;
  FormatNameString  = NULL;
  FormatHelpString  = NULL;
  StartOpCodeHandle = NULL;
  EndOpCodeHandle   = NULL;
  StartGotoHandle   = NULL;
  EndGotoHandle     = NULL;
  Index             = 0;
  VariableData      = NULL;

  //
  // Initialize the container for dynamic opcodes.
  //
  StartOpCodeHandle = HiiAllocateOpCodeHandle ();
  if (StartOpCodeHandle == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  EndOpCodeHandle = HiiAllocateOpCodeHandle ();
  if (EndOpCodeHandle == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  StartGotoHandle = HiiAllocateOpCodeHandle ();
  if (StartGotoHandle == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  EndGotoHandle = HiiAllocateOpCodeHandle ();
  if (EndGotoHandle == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  //
  // Create Hii Extend Label OpCode.
  //
  StartLabel = (EFI_IFR_GUID_LABEL *)HiiCreateGuidOpCode (
                                       StartOpCodeHandle,
                                       &gEfiIfrTianoGuid,
                                       NULL,
                                       sizeof (EFI_IFR_GUID_LABEL)
                                       );
  StartLabel->ExtendOpCode = EFI_IFR_EXTEND_OP_LABEL;
  StartLabel->Number       = LabelId;

  EndLabel = (EFI_IFR_GUID_LABEL *)HiiCreateGuidOpCode (
                                     EndOpCodeHandle,
                                     &gEfiIfrTianoGuid,
                                     NULL,
                                     sizeof (EFI_IFR_GUID_LABEL)
                                     );
  EndLabel->ExtendOpCode = EFI_IFR_EXTEND_OP_LABEL;
  EndLabel->Number       = LABEL_END;

  StartGoto = (EFI_IFR_GUID_LABEL *)HiiCreateGuidOpCode (
                                      StartGotoHandle,
                                      &gEfiIfrTianoGuid,
                                      NULL,
                                      sizeof (EFI_IFR_GUID_LABEL)
                                      );
  StartGoto->ExtendOpCode = EFI_IFR_EXTEND_OP_LABEL;
  StartGoto->Number       = LABEL_DELETE_ALL_LIST_BUTTON;

  EndGoto = (EFI_IFR_GUID_LABEL *)HiiCreateGuidOpCode (
                                    EndGotoHandle,
                                    &gEfiIfrTianoGuid,
                                    NULL,
                                    sizeof (EFI_IFR_GUID_LABEL)
                                    );
  EndGoto->ExtendOpCode = EFI_IFR_EXTEND_OP_LABEL;
  EndGoto->Number       = LABEL_END;

  if (PrivateData->VariableName == Variable_DB) {
    UnicodeSPrint (VariableName, sizeof (VariableName), EFI_IMAGE_SECURITY_DATABASE);
    DstFormId = FORMID_SOVEREIGN_BOOT_DB_OPTION_FORM;
  } else if (PrivateData->VariableName == Variable_DBX) {
    UnicodeSPrint (VariableName, sizeof (VariableName), EFI_IMAGE_SECURITY_DATABASE1);
    DstFormId = FORMID_SOVEREIGN_BOOT_DBX_OPTION_FORM;
  } else {
    goto ON_EXIT;
  }

  HiiCreateGotoOpCode (
    StartGotoHandle,
    DstFormId,
    STRING_TOKEN (STR_SOVEREIGN_BOOT_DELETE_ALL_LIST),
    STRING_TOKEN (STR_SOVEREIGN_BOOT_DELETE_ALL_LIST),
    EFI_IFR_FLAG_CALLBACK | EFI_IFR_FLAG_RESET_REQUIRED,
    KEY_SOVEREIGN_BOOT_DELETE_ALL_LIST
    );

  //
  // Read Variable, the variable name save in the PrivateData->VariableName.
  //
  DataSize = 0;
  Status   = gRT->GetVariable (VariableName, &gEfiImageSecurityDatabaseGuid, NULL, &DataSize, VariableData);
  if (EFI_ERROR (Status) && (Status != EFI_BUFFER_TOO_SMALL)) {
    goto ON_EXIT;
  }

  VariableData = AllocateZeroPool (DataSize);
  if (VariableData == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  Status = gRT->GetVariable (VariableName, &gEfiImageSecurityDatabaseGuid, NULL, &DataSize, VariableData);
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  FormatNameString = HiiGetString (PrivateData->HiiHandle, STRING_TOKEN (STR_SIGNATURE_LIST_NAME_FORMAT), NULL);
  FormatHelpString = HiiGetString (PrivateData->HiiHandle, STRING_TOKEN (STR_SIGNATURE_LIST_HELP_FORMAT), NULL);
  if ((FormatNameString == NULL) || (FormatHelpString == NULL)) {
    goto ON_EXIT;
  }

  RemainingSize = DataSize;
  ListWalker    = (EFI_SIGNATURE_LIST *)VariableData;
  while ((RemainingSize > 0) && (RemainingSize >= ListWalker->SignatureListSize)) {
    if (CompareGuid (&ListWalker->SignatureType, &gEfiCertRsa2048Guid)) {
      ListType = STRING_TOKEN (STR_LIST_TYPE_RSA2048);
    } else if (CompareGuid (&ListWalker->SignatureType, &gEfiCertRsa2048Sha256Guid)) {
      ListType = STRING_TOKEN (STR_LIST_TYPE_RSA2048_SHA256);
    } else if (CompareGuid (&ListWalker->SignatureType, &gEfiCertX509Guid)) {
      ListType = STRING_TOKEN (STR_LIST_TYPE_X509);
    } else if (CompareGuid (&ListWalker->SignatureType, &gEfiCertSha1Guid)) {
      ListType = STRING_TOKEN (STR_LIST_TYPE_SHA1);
    } else if (CompareGuid (&ListWalker->SignatureType, &gEfiCertSha224Guid)) {
      ListType = STRING_TOKEN (STR_LIST_TYPE_SHA224);
    } else if (CompareGuid (&ListWalker->SignatureType, &gEfiCertSha256Guid)) {
      ListType = STRING_TOKEN (STR_LIST_TYPE_SHA256);
    } else if (CompareGuid (&ListWalker->SignatureType, &gEfiCertSha384Guid)) {
      ListType = STRING_TOKEN (STR_LIST_TYPE_SHA384);
    } else if (CompareGuid (&ListWalker->SignatureType, &gEfiCertSha512Guid)) {
      ListType = STRING_TOKEN (STR_LIST_TYPE_SHA512);
    } else if (CompareGuid (&ListWalker->SignatureType, &gEfiCertSm3Guid)) {
      ListType = STRING_TOKEN (STR_LIST_TYPE_SM3);
    } else if (CompareGuid (&ListWalker->SignatureType, &gEfiCertX509Sha256Guid)) {
      ListType = STRING_TOKEN (STR_LIST_TYPE_X509_SHA256);
    } else if (CompareGuid (&ListWalker->SignatureType, &gEfiCertX509Sha384Guid)) {
      ListType = STRING_TOKEN (STR_LIST_TYPE_X509_SHA384);
    } else if (CompareGuid (&ListWalker->SignatureType, &gEfiCertX509Sha512Guid)) {
      ListType = STRING_TOKEN (STR_LIST_TYPE_X509_SHA512);
    } else if (CompareGuid (&ListWalker->SignatureType, &gEfiCertX509Sm3Guid)) {
      ListType = STRING_TOKEN (STR_LIST_TYPE_X509_SM3);
    } else {
      ListType = STRING_TOKEN (STR_LIST_TYPE_UNKNOWN);
    }

    FormatTypeString = HiiGetString (PrivateData->HiiHandle, ListType, NULL);
    if (FormatTypeString == NULL) {
      goto ON_EXIT;
    }

    ZeroMem (NameBuffer, sizeof (NameBuffer));
    UnicodeSPrint (NameBuffer, sizeof (NameBuffer), FormatNameString, Index + 1);

    ZeroMem (HelpBuffer, sizeof (HelpBuffer));
    UnicodeSPrint (
      HelpBuffer,
      sizeof (HelpBuffer),
      FormatHelpString,
      FormatTypeString,
      SIGNATURE_DATA_COUNTS (ListWalker)
      );
    FREE_NON_NULL (FormatTypeString);
    FormatTypeString = NULL;

    HiiCreateGotoOpCode (
      StartOpCodeHandle,
      SOVEREIGN_BOOT_DELETE_SIGNATURE_DATA_FORM,
      HiiSetString (PrivateData->HiiHandle, 0, NameBuffer, NULL),
      HiiSetString (PrivateData->HiiHandle, 0, HelpBuffer, NULL),
      EFI_IFR_FLAG_CALLBACK | EFI_IFR_FLAG_RESET_REQUIRED,
      QuestionIdBase + Index++
      );

    RemainingSize -= ListWalker->SignatureListSize;
    ListWalker     = (EFI_SIGNATURE_LIST *)((UINT8 *)ListWalker + ListWalker->SignatureListSize);
  }

ON_EXIT:
  HiiUpdateForm (
    PrivateData->HiiHandle,
    &gSovereignBootWizardFormSetGuid,
    FormId,
    StartOpCodeHandle,
    EndOpCodeHandle
    );

  HiiUpdateForm (
    PrivateData->HiiHandle,
    &gSovereignBootWizardFormSetGuid,
    FormId,
    StartGotoHandle,
    EndGotoHandle
    );

  FREE_NON_OPCODE (StartOpCodeHandle);
  FREE_NON_OPCODE (EndOpCodeHandle);
  FREE_NON_OPCODE (StartGotoHandle);
  FREE_NON_OPCODE (EndGotoHandle);

  FREE_NON_NULL (VariableData);
  FREE_NON_NULL (FormatNameString);
  FREE_NON_NULL (FormatHelpString);

  PrivateData->ListCount = Index;

  return Status;
}

/**
  Parse hash value from EFI_SIGNATURE_DATA, and save in the CHAR16 type array.
  The buffer is callee allocated and should be freed by the caller.

  @param[in]    ListEntry                 The pointer point to the signature list.
  @param[in]    DataEntry                 The signature data we are processing.
  @param[out]   BufferToReturn            Buffer to save the hash value.

  @retval       EFI_INVALID_PARAMETER     Invalid List or Data or Buffer.
  @retval       EFI_OUT_OF_RESOURCES      A memory allocation failed.
  @retval       EFI_SUCCESS               Operation success.
**/
EFI_STATUS
ParseHelpHashValue (
  IN     EFI_SIGNATURE_LIST  *ListEntry,
  IN     EFI_SIGNATURE_DATA  *DataEntry,
  OUT CHAR16                 **BufferToReturn
  )
{
  UINTN  Index;
  UINTN  BufferIndex;
  UINTN  TotalSize;
  UINTN  DataSize;
  UINTN  Line;
  UINTN  OneLineBytes;

  //
  //  Assume that, display 8 bytes in one line.
  //
  OneLineBytes = 8;

  if ((ListEntry == NULL) || (DataEntry == NULL) || (BufferToReturn == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  DataSize = ListEntry->SignatureSize - sizeof (EFI_GUID);
  Line     = (DataSize + OneLineBytes - 1) / OneLineBytes;

  //
  // Each byte will split two Hex-number, and each line need additional memory to save '\r\n'.
  //
  TotalSize = ((DataSize + Line) * 2 * sizeof (CHAR16));

  *BufferToReturn = AllocateZeroPool (TotalSize);
  if (*BufferToReturn == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  for (Index = 0, BufferIndex = 0; Index < DataSize; Index = Index + 1) {
    if ((Index > 0) && (Index % OneLineBytes == 0)) {
      BufferIndex += UnicodeSPrint (&(*BufferToReturn)[BufferIndex], TotalSize - sizeof (CHAR16) * BufferIndex, L"\n");
    }

    BufferIndex += UnicodeSPrint (&(*BufferToReturn)[BufferIndex], TotalSize - sizeof (CHAR16) * BufferIndex, L"%02x", DataEntry->SignatureData[Index]);
  }

  BufferIndex += UnicodeSPrint (&(*BufferToReturn)[BufferIndex], TotalSize - sizeof (CHAR16) * BufferIndex, L"\n");

  return EFI_SUCCESS;
}

/**
  Function to get the common name from the X509 format certificate.
  The buffer is callee allocated and should be freed by the caller.

  @param[in]    ListEntry                 The pointer point to the signature list.
  @param[in]    DataEntry                 The signature data we are processing.
  @param[out]   BufferToReturn            Buffer to save the CN of X509 certificate.

  @retval       EFI_INVALID_PARAMETER     Invalid List or Data or Buffer.
  @retval       EFI_OUT_OF_RESOURCES      A memory allocation failed.
  @retval       EFI_SUCCESS               Operation success.
  @retval       EFI_NOT_FOUND             Not found CN field in the X509 certificate.
**/
EFI_STATUS
GetCommonNameFromX509 (
  IN     EFI_SIGNATURE_LIST  *ListEntry,
  IN     EFI_SIGNATURE_DATA  *DataEntry,
  OUT CHAR16                 **BufferToReturn
  )
{
  EFI_STATUS  Status;
  CHAR8       *CNBuffer;
  UINTN       CNBufferSize;

  Status   = EFI_SUCCESS;
  CNBuffer = NULL;

  CNBuffer = AllocateZeroPool (256);
  if (CNBuffer == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  CNBufferSize = 256;
  X509GetCommonName (
    (UINT8 *)DataEntry + sizeof (EFI_GUID),
    ListEntry->SignatureSize - sizeof (EFI_GUID),
    CNBuffer,
    &CNBufferSize
    );

  *BufferToReturn = AllocateZeroPool (256 * sizeof (CHAR16));
  if (*BufferToReturn == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  AsciiStrToUnicodeStrS (CNBuffer, *BufferToReturn, 256);

ON_EXIT:
  FREE_NON_NULL (CNBuffer);

  return Status;
}

/**
  Format the help info for the signature data, each help info contain 3 parts.
  1. Owner Guid.
  2. Content, depends on the type of the signature list.
  3. Revocation time.

  @param[in]      PrivateData             Module's private data.
  @param[in]      ListEntry               Point to the signature list.
  @param[in]      DataEntry               Point to the signature data we are processing.
  @param[out]     StringId                Save the string id of help info.

  @retval         EFI_SUCCESS             Operation success.
  @retval         EFI_OUT_OF_RESOURCES    Unable to allocate required resources.
**/
EFI_STATUS
FormatHelpInfo (
  IN     SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *PrivateData,
  IN     EFI_SIGNATURE_LIST              *ListEntry,
  IN     EFI_SIGNATURE_DATA              *DataEntry,
  OUT EFI_STRING_ID                      *StringId
  )
{
  EFI_STATUS     Status;
  EFI_TIME       *Time;
  EFI_STRING_ID  ListTypeId;
  EFI_STRING     FormatHelpString;
  EFI_STRING     FormatTypeString;
  UINTN          DataSize;
  UINTN          HelpInfoIndex;
  UINTN          TotalSize;
  CHAR16         GuidString[BUFFER_MAX_SIZE];
  CHAR16         TimeString[BUFFER_MAX_SIZE];
  CHAR16         *DataString;
  CHAR16         *HelpInfoString;
  BOOLEAN        IsCert;

  Status           = EFI_SUCCESS;
  Time             = NULL;
  FormatTypeString = NULL;
  HelpInfoIndex    = 0;
  DataString       = NULL;
  HelpInfoString   = NULL;
  IsCert           = FALSE;

  if (CompareGuid (&ListEntry->SignatureType, &gEfiCertRsa2048Guid)) {
    ListTypeId = STRING_TOKEN (STR_LIST_TYPE_RSA2048);
    DataSize   = ListEntry->SignatureSize - sizeof (EFI_GUID);
  } else if (CompareGuid (&ListEntry->SignatureType, &gEfiCertRsa2048Sha256Guid)) {
    ListTypeId = STRING_TOKEN (STR_LIST_TYPE_RSA2048_SHA256);
    DataSize   = ListEntry->SignatureSize - sizeof (EFI_GUID);
  } else if (CompareGuid (&ListEntry->SignatureType, &gEfiCertX509Guid)) {
    ListTypeId = STRING_TOKEN (STR_LIST_TYPE_X509);
    DataSize   = ListEntry->SignatureSize - sizeof (EFI_GUID);
    IsCert     = TRUE;
  } else if (CompareGuid (&ListEntry->SignatureType, &gEfiCertSha1Guid)) {
    ListTypeId = STRING_TOKEN (STR_LIST_TYPE_SHA1);
    DataSize   = 20;
  } else if (CompareGuid (&ListEntry->SignatureType, &gEfiCertSha224Guid)) {
    ListTypeId = STRING_TOKEN (STR_LIST_TYPE_SHA224);
    DataSize   = 28;
  } else if (CompareGuid (&ListEntry->SignatureType, &gEfiCertSha256Guid)) {
    ListTypeId = STRING_TOKEN (STR_LIST_TYPE_SHA256);
    DataSize   = 32;
  } else if (CompareGuid (&ListEntry->SignatureType, &gEfiCertSha384Guid)) {
    ListTypeId = STRING_TOKEN (STR_LIST_TYPE_SHA384);
    DataSize   = 48;
  } else if (CompareGuid (&ListEntry->SignatureType, &gEfiCertSha512Guid)) {
    ListTypeId = STRING_TOKEN (STR_LIST_TYPE_SHA512);
    DataSize   = 64;
  } else if (CompareGuid (&ListEntry->SignatureType, &gEfiCertSm3Guid)) {
    ListTypeId = STRING_TOKEN (STR_LIST_TYPE_SM3);
    DataSize   = 32;
  } else if (CompareGuid (&ListEntry->SignatureType, &gEfiCertX509Sha256Guid)) {
    ListTypeId = STRING_TOKEN (STR_LIST_TYPE_X509_SHA256);
    DataSize   = 32;
    Time       = (EFI_TIME *)(DataEntry->SignatureData + DataSize);
  } else if (CompareGuid (&ListEntry->SignatureType, &gEfiCertX509Sha384Guid)) {
    ListTypeId = STRING_TOKEN (STR_LIST_TYPE_X509_SHA384);
    DataSize   = 48;
    Time       = (EFI_TIME *)(DataEntry->SignatureData + DataSize);
  } else if (CompareGuid (&ListEntry->SignatureType, &gEfiCertX509Sha512Guid)) {
    ListTypeId = STRING_TOKEN (STR_LIST_TYPE_X509_SHA512);
    DataSize   = 64;
    Time       = (EFI_TIME *)(DataEntry->SignatureData + DataSize);
  } else if (CompareGuid (&ListEntry->SignatureType, &gEfiCertX509Sm3Guid)) {
    ListTypeId = STRING_TOKEN (STR_LIST_TYPE_X509_SM3);
    DataSize   = 32;
    Time       = (EFI_TIME *)(DataEntry->SignatureData + DataSize);
  } else {
    Status = EFI_UNSUPPORTED;
    goto ON_EXIT;
  }

  FormatTypeString = HiiGetString (PrivateData->HiiHandle, ListTypeId, NULL);
  if (FormatTypeString == NULL) {
    goto ON_EXIT;
  }

  TotalSize      = 1024;
  HelpInfoString = AllocateZeroPool (TotalSize);
  if (HelpInfoString == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  //
  // Format GUID part.
  //
  ZeroMem (GuidString, sizeof (GuidString));
  GuidToString (&DataEntry->SignatureOwner, GuidString, BUFFER_MAX_SIZE);
  FormatHelpString = HiiGetString (PrivateData->HiiHandle, STRING_TOKEN (STR_SIGNATURE_DATA_HELP_FORMAT_GUID), NULL);
  if (FormatHelpString == NULL) {
    goto ON_EXIT;
  }

  HelpInfoIndex += UnicodeSPrint (
                     &HelpInfoString[HelpInfoIndex],
                     TotalSize - sizeof (CHAR16) * HelpInfoIndex,
                     FormatHelpString,
                     GuidString
                     );
  FREE_NON_NULL (FormatHelpString);
  FormatHelpString = NULL;

  //
  // Format content part, it depends on the type of signature list, hash value or CN.
  //
  if (IsCert) {
    GetCommonNameFromX509 (ListEntry, DataEntry, &DataString);
    FormatHelpString = HiiGetString (PrivateData->HiiHandle, STRING_TOKEN (STR_SIGNATURE_DATA_HELP_FORMAT_CN), NULL);
  } else {
    //
    //  Format hash value for each signature data entry.
    //
    ParseHelpHashValue (ListEntry, DataEntry, &DataString);
    FormatHelpString = HiiGetString (PrivateData->HiiHandle, STRING_TOKEN (STR_SIGNATURE_DATA_HELP_FORMAT_HASH), NULL);
  }

  if (FormatHelpString == NULL) {
    goto ON_EXIT;
  }

  HelpInfoIndex += UnicodeSPrint (
                     &HelpInfoString[HelpInfoIndex],
                     TotalSize - sizeof (CHAR16) * HelpInfoIndex,
                     FormatHelpString,
                     FormatTypeString,
                     DataSize,
                     DataString
                     );
  FREE_NON_NULL (FormatHelpString);
  FormatHelpString = NULL;

  //
  // Format revocation time part.
  //
  if (Time != NULL) {
    ZeroMem (TimeString, sizeof (TimeString));
    UnicodeSPrint (
      TimeString,
      sizeof (TimeString),
      L"%04d-%02d-%02d %02d:%02d:%02d",
      Time->Year,
      Time->Month,
      Time->Day,
      Time->Hour,
      Time->Minute,
      Time->Second
      );
    FormatHelpString = HiiGetString (PrivateData->HiiHandle, STRING_TOKEN (STR_SIGNATURE_DATA_HELP_FORMAT_TIME), NULL);
    if (FormatHelpString == NULL) {
      goto ON_EXIT;
    }

    UnicodeSPrint (
      &HelpInfoString[HelpInfoIndex],
      TotalSize - sizeof (CHAR16) * HelpInfoIndex,
      FormatHelpString,
      TimeString
      );
    FREE_NON_NULL (FormatHelpString);
    FormatHelpString = NULL;
  }

  *StringId = HiiSetString (PrivateData->HiiHandle, 0, HelpInfoString, NULL);
ON_EXIT:
  FREE_NON_NULL (DataString);
  FREE_NON_NULL (HelpInfoString);

  FREE_NON_NULL (FormatTypeString);

  return Status;
}

/**
  This function to load signature data under the signature list.

  @param[in]  PrivateData         Module's private data.
  @param[in]  LabelId             Label number to insert opcodes.
  @param[in]  FormId              Form ID of current page.
  @param[in]  QuestionIdBase      Base question id of the signature list.
  @param[in]  ListIndex           Indicate to load which signature list.

  @retval   EFI_SUCCESS           Success to update the signature list page
  @retval   EFI_OUT_OF_RESOURCES  Unable to allocate required resources.
**/
EFI_STATUS
LoadSignatureData (
  IN SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *PrivateData,
  IN UINT16                              LabelId,
  IN EFI_FORM_ID                         FormId,
  IN EFI_QUESTION_ID                     QuestionIdBase,
  IN UINT16                              ListIndex
  )
{
  EFI_STATUS          Status;
  EFI_SIGNATURE_LIST  *ListWalker;
  EFI_SIGNATURE_DATA  *DataWalker;
  EFI_IFR_GUID_LABEL  *StartLabel;
  EFI_IFR_GUID_LABEL  *EndLabel;
  EFI_STRING_ID       HelpStringId;
  EFI_STRING          FormatNameString;
  VOID                *StartOpCodeHandle;
  VOID                *EndOpCodeHandle;
  UINTN               DataSize;
  UINTN               RemainingSize;
  UINT16              Index;
  UINT8               *VariableData;
  CHAR16              VariableName[BUFFER_MAX_SIZE];
  CHAR16              NameBuffer[BUFFER_MAX_SIZE];

  Status            = EFI_SUCCESS;
  FormatNameString  = NULL;
  StartOpCodeHandle = NULL;
  EndOpCodeHandle   = NULL;
  Index             = 0;
  VariableData      = NULL;

  //
  // Initialize the container for dynamic opcodes.
  //
  StartOpCodeHandle = HiiAllocateOpCodeHandle ();
  if (StartOpCodeHandle == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  EndOpCodeHandle = HiiAllocateOpCodeHandle ();
  if (EndOpCodeHandle == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  //
  // Create Hii Extend Label OpCode.
  //
  StartLabel = (EFI_IFR_GUID_LABEL *)HiiCreateGuidOpCode (
                                       StartOpCodeHandle,
                                       &gEfiIfrTianoGuid,
                                       NULL,
                                       sizeof (EFI_IFR_GUID_LABEL)
                                       );
  StartLabel->ExtendOpCode = EFI_IFR_EXTEND_OP_LABEL;
  StartLabel->Number       = LabelId;

  EndLabel = (EFI_IFR_GUID_LABEL *)HiiCreateGuidOpCode (
                                     EndOpCodeHandle,
                                     &gEfiIfrTianoGuid,
                                     NULL,
                                     sizeof (EFI_IFR_GUID_LABEL)
                                     );
  EndLabel->ExtendOpCode = EFI_IFR_EXTEND_OP_LABEL;
  EndLabel->Number       = LABEL_END;

  if (PrivateData->VariableName == Variable_DB) {
    UnicodeSPrint (VariableName, sizeof (VariableName), EFI_IMAGE_SECURITY_DATABASE);
  } else if (PrivateData->VariableName == Variable_DBX) {
    UnicodeSPrint (VariableName, sizeof (VariableName), EFI_IMAGE_SECURITY_DATABASE1);
  } else {
    goto ON_EXIT;
  }

  //
  // Read Variable, the variable name save in the PrivateData->VariableName.
  //
  DataSize = 0;
  Status   = gRT->GetVariable (VariableName, &gEfiImageSecurityDatabaseGuid, NULL, &DataSize, VariableData);
  if (EFI_ERROR (Status) && (Status != EFI_BUFFER_TOO_SMALL)) {
    goto ON_EXIT;
  }

  VariableData = AllocateZeroPool (DataSize);
  if (VariableData == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  Status = gRT->GetVariable (VariableName, &gEfiImageSecurityDatabaseGuid, NULL, &DataSize, VariableData);
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  RemainingSize = DataSize;
  ListWalker    = (EFI_SIGNATURE_LIST *)VariableData;

  //
  // Skip signature list.
  //
  while ((RemainingSize > 0) && (RemainingSize >= ListWalker->SignatureListSize) && ListIndex-- > 0) {
    RemainingSize -= ListWalker->SignatureListSize;
    ListWalker     = (EFI_SIGNATURE_LIST *)((UINT8 *)ListWalker + ListWalker->SignatureListSize);
  }

  FormatNameString = HiiGetString (PrivateData->HiiHandle, STRING_TOKEN (STR_SIGNATURE_DATA_NAME_FORMAT), NULL);
  if (FormatNameString == NULL) {
    goto ON_EXIT;
  }

  DataWalker = (EFI_SIGNATURE_DATA *)((UINT8 *)ListWalker + sizeof (EFI_SIGNATURE_LIST) + ListWalker->SignatureHeaderSize);
  for (Index = 0; Index < SIGNATURE_DATA_COUNTS (ListWalker); Index = Index + 1) {
    //
    // Format name buffer.
    //
    ZeroMem (NameBuffer, sizeof (NameBuffer));
    UnicodeSPrint (NameBuffer, sizeof (NameBuffer), FormatNameString, Index + 1);

    //
    // Format help info buffer.
    //
    Status = FormatHelpInfo (PrivateData, ListWalker, DataWalker, &HelpStringId);
    if (EFI_ERROR (Status)) {
      goto ON_EXIT;
    }

    HiiCreateCheckBoxOpCode (
      StartOpCodeHandle,
      (EFI_QUESTION_ID)(QuestionIdBase + Index),
      0,
      0,
      HiiSetString (PrivateData->HiiHandle, 0, NameBuffer, NULL),
      HelpStringId,
      EFI_IFR_FLAG_CALLBACK,
      0,
      NULL
      );

    ZeroMem (NameBuffer, BUFFER_MAX_SIZE);
    DataWalker = (EFI_SIGNATURE_DATA *)((UINT8 *)DataWalker + ListWalker->SignatureSize);
  }

  //
  // Allocate a buffer to record which signature data will be checked.
  // This memory buffer will be freed when exit from the SOVEREIGN_BOOT_DELETE_SIGNATURE_DATA_FORM form.
  //
  PrivateData->CheckArray = AllocateZeroPool (SIGNATURE_DATA_COUNTS (ListWalker) * sizeof (BOOLEAN));
ON_EXIT:
  HiiUpdateForm (
    PrivateData->HiiHandle,
    &gSovereignBootWizardFormSetGuid,
    FormId,
    StartOpCodeHandle,
    EndOpCodeHandle
    );

  FREE_NON_OPCODE (StartOpCodeHandle);
  FREE_NON_OPCODE (EndOpCodeHandle);

  FREE_NON_NULL (VariableData);
  FREE_NON_NULL (FormatNameString);

  return Status;
}

/**
  This function publish the Sovereign Boot Wizrd Interactive Mode form.

  @param[in, out]  PrivateData   Points to Sovereign Boot private data.

  @retval EFI_SUCCESS            HII Form is installed successfully.
  @retval EFI_OUT_OF_RESOURCES   Not enough resource for HII Form installation.
  @retval Others                 Other errors as indicated.

**/
EFI_STATUS
InstallInteractiveModeForm (
  IN OUT SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *PrivateData
  )
{
  PrivateData->FileContext = AllocateZeroPool (sizeof (SOVEREIGNBOOT_FILE_CONTEXT));

  if (PrivateData->FileContext == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Init OpCode Handle and Allocate space for creation of Buffer
  //
  mStartOpCodeHandle = HiiAllocateOpCodeHandle ();
  if (mStartOpCodeHandle == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  mEndOpCodeHandle = HiiAllocateOpCodeHandle ();
  if (mEndOpCodeHandle == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Create Hii Extend Label OpCode as the start opcode
  //
  mStartLabel = (EFI_IFR_GUID_LABEL *)HiiCreateGuidOpCode (
                                        mStartOpCodeHandle,
                                        &gEfiIfrTianoGuid,
                                        NULL,
                                        sizeof (EFI_IFR_GUID_LABEL)
                                        );
  mStartLabel->ExtendOpCode = EFI_IFR_EXTEND_OP_LABEL;

  //
  // Create Hii Extend Label OpCode as the end opcode
  //
  mEndLabel = (EFI_IFR_GUID_LABEL *)HiiCreateGuidOpCode (
                                      mEndOpCodeHandle,
                                      &gEfiIfrTianoGuid,
                                      NULL,
                                      sizeof (EFI_IFR_GUID_LABEL)
                                      );
  mEndLabel->ExtendOpCode = EFI_IFR_EXTEND_OP_LABEL;
  mEndLabel->Number       = LABEL_END;

  return EFI_SUCCESS;
}

/**
  This function removes Sovereign Boot Wizrd Interactive Mode form.

  @param[in, out]  PrivateData   Points to Sovereign Boot configuration private data.

**/
VOID
UninstallInteractiveModeForm (
  IN OUT SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *PrivateData
  )
{
  FREE_NON_NULL (PrivateData->FileContext);

  if (mStartOpCodeHandle != NULL) {
    HiiFreeOpCodeHandle (mStartOpCodeHandle);
  }

  if (mEndOpCodeHandle != NULL) {
    HiiFreeOpCodeHandle (mEndOpCodeHandle);
  }
}
