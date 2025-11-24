/** @file
The header file of Interactive Mode implementation of Sovereign Boot
Provisioning Wizard.

Copyright (c) 2025, 3mdeb Sp z o.o. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef __SOVEREIGNBOOT_INTERACTIVE_MODE_IMPL_H__
#define __SOVEREIGNBOOT_INTERACTIVE_MODE_IMPL_H__

//
// Shared IFR form update data
//
extern  VOID                *mStartOpCodeHandle;
extern  VOID                *mEndOpCodeHandle;
extern  EFI_IFR_GUID_LABEL  *mStartLabel;
extern  EFI_IFR_GUID_LABEL  *mEndLabel;

#define MAX_CHAR         480
#define TWO_BYTE_ENCODE  0x82
#define BUFFER_MAX_SIZE  100

#define WIN_CERT_UEFI_RSA2048_SIZE  256
#define WIN_CERT_UEFI_RSA3072_SIZE  384
#define WIN_CERT_UEFI_RSA4096_SIZE  512

#define UNKNOWN_FILE_TYPE           0
#define X509_CERT_FILE_TYPE         1
#define PE_IMAGE_FILE_TYPE          2
#define AUTHENTICATION_2_FILE_TYPE  3

//
// Certificate public key minimum size (bytes)
//
#define CER_PUBKEY_MIN_SIZE  256

//
// Define KeyType for public key storing file
//
#define KEY_TYPE_RSASSA  0

//
// Types of errors may occur during certificate enrollment.
//
typedef enum {
  None_Error = 0,
  //
  // Unsupported_type indicates the certificate type is not supported.
  //
  Unsupported_Type,
  //
  // Unqualified_key indicates the key strength of certificate is not
  // strong enough.
  //
  Unqualified_Key,
  Enroll_Error_Max
} ENROLL_KEY_ERROR;

typedef enum {
  Delete_Signature_List_All,
  Delete_Signature_List_One,
  Delete_Signature_Data
} SIGNATURE_DELETE_TYPE;

typedef struct {
  UINTN         Signature;
  LIST_ENTRY    Head;
  UINTN         MenuNumber;
} SOVEREIGNBOOT_MENU_OPTION;


#define SIGNATURE_DATA_COUNTS(List)         \
  (((List)->SignatureListSize - sizeof(EFI_SIGNATURE_LIST) - (List)->SignatureHeaderSize) / (List)->SignatureSize)

//
// We define another format of 5th directory entry: security directory
//
typedef struct {
  UINT32    Offset;                 // Offset of certificate
  UINT32    SizeOfCert;             // size of certificate appended
} EFI_IMAGE_SECURITY_DATA_DIRECTORY;

typedef enum {
  ImageType_IA32,
  ImageType_X64
} IMAGE_TYPE;

//
// Cryptographic Key Information
//
#pragma pack(1)
typedef struct _CPL_KEY_INFO {
  UINT32    KeyLengthInBits;        // Key Length In Bits
  UINT32    BlockSize;              // Operation Block Size in Bytes
  UINT32    CipherBlockSize;        // Output Cipher Block Size in Bytes
  UINT32    KeyType;                // Key Type
  UINT32    CipherMode;             // Cipher Mode for Symmetric Algorithm
  UINT32    Flags;                  // Additional Key Property Flags
} CPL_KEY_INFO;
#pragma pack()

/**
  This function extracts configuration from variable.

  @param[in]       Private      Point to Sovereign Boot configuration driver private data.
  @param[in, out]  FormData     Point to Sovereign Boot form private data.

**/
VOID
InteractiveModeExtractConfig (
  IN     SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private,
  IN OUT SOVEREIGN_BOOT_WIZARD_FORM_DATA     *FormData
  );

EFI_STATUS
InstallInteractiveModeForm (
  IN OUT SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *PrivateData
  );

VOID
UninstallInteractiveModeForm (
  IN OUT SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *PrivateData
  );

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
  );

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
  );

/**
  This code cleans up enrolled file by closing file & free related resources attached to
  enrolled file.

  @param[in] FileContext            FileContext cached in Sovereign Boot Wizard driver

**/
VOID
CloseEnrolledFile (
  IN SOVEREIGNBOOT_FILE_CONTEXT  *FileContext
  );

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
  );

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
  );

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
  );

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
  );

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
  );

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
  );

/**
  Check whether a certificate from a file exists in dbx.

  @param[in] PrivateData     The module's private data.

  @retval   TRUE             The X509 certificate is found in dbx successfully.
  @retval   FALSE            The X509 certificate is not found in dbx.
**/
BOOLEAN
IsX509CertInDbx (
  IN SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *Private
  );


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
  );

/**
  This code checks if the FileSuffix is one of the possible DER-encoded certificate suffix.

  @param[in] FileSuffix            The suffix of the input certificate file

  @retval    TRUE           It's a DER-encoded certificate.
  @retval    FALSE          It's NOT a DER-encoded certificate.

**/
BOOLEAN
IsDerEncodeCertificate (
  IN CONST CHAR16  *FileSuffix
  );

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
  );

/**
  Clean up the dynamic opcode at label and form specified by both LabelId.

  @param[in] LabelId         It is both the Form ID and Label ID for opcode deletion.
  @param[in] PrivateData     Module private data.

**/
VOID
CleanUpPage (
  IN UINT16                              LabelId,
  IN SOVEREIGN_BOOT_WIZARD_PRIVATE_DATA  *PrivateData
  );

#endif
