/** @file
  This driver init default Secure Boot variables

Copyright (c) 2021, ARM Ltd. All rights reserved.<BR>
Copyright (c) 2021, Semihalf All rights reserved.<BR>
Copyright (c) 2021, Ampere Computing LLC. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/
#include <Guid/AuthenticatedVariableFormat.h>
#include <Guid/ImageAuthentication.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <UefiSecureBoot.h>
#include <Library/SecureBootVariableLib.h>
#include <Library/SecureBootVariableProvisionLib.h>
#include <Pi/PiBootMode.h>

STATIC
EFI_STATUS
EnrollKeysFromDefaults (
  VOID
)
{
  EFI_STATUS  Status;
  UINT8       SetupMode;

  Status = EFI_SUCCESS;

  // After PK clear, Setup Mode shall be enabled
  Status = GetSetupMode (&SetupMode);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Cannot get SetupMode variable: %r\n",
      Status));
    return Status;
  }

  if (SetupMode == USER_MODE) {
    DEBUG((DEBUG_INFO, "Skipped - USER_MODE\n"));
    return EFI_SUCCESS;
  }

  Status = SetSecureBootMode (CUSTOM_SECURE_BOOT_MODE);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Cannot set CUSTOM_SECURE_BOOT_MODE: %r\n",
      Status));
    return EFI_SUCCESS;
  }

  // Enroll all the keys from default variables
  Status = EnrollDbFromDefault ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Cannot enroll db: %r\n", Status));
    goto error;
  }

  Status = EnrollDbxFromDefault ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Cannot enroll dbx: %r\n", Status));
  }

  Status = EnrollDbtFromDefault ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Cannot enroll dbt: %r\n", Status));
  }

  Status = EnrollKEKFromDefault ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Cannot enroll KEK: %r\n", Status));
    goto cleardbs;
  }

  Status = EnrollPKFromDefault ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Cannot enroll PK: %r\n", Status));
    goto clearKEK;
  }

  DEBUG ((DEBUG_INFO, "Setting Secure Boot state to: %d\n", FixedPcdGet8(PcdSecureBootDefaultEnable)));
  Status = SetSecureBootState (FixedPcdGet8(PcdSecureBootDefaultEnable));
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Cannot set Secure Boot state: %r\n", Status));
  }

  Status = SetSecureBootMode (STANDARD_SECURE_BOOT_MODE);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Cannot set CustomMode to STANDARD_SECURE_BOOT_MODE\n"
      "Please do it manually, otherwise system can be easily compromised\n"));
  }

  return Status;

clearKEK:
  DeleteKEK ();

cleardbs:
  DeleteDbt ();
  DeleteDbx ();
  DeleteDb ();

error:
  if (SetSecureBootMode (STANDARD_SECURE_BOOT_MODE) != EFI_SUCCESS) {
    DEBUG ((DEBUG_ERROR, "Cannot set mode to Secure: %r\n", Status));
  }
  return Status;
}

/**
  The entry point for SecureBootDefaultKeys driver.

  @param[in]  ImageHandle        The image handle of the driver.
  @param[in]  SystemTable        The system table.

  @retval EFI_SUCCESS            The secure default keys are initialized successfully.
  @retval EFI_UNSUPPORTED        One of the secure default keys already exists.
  @retval EFI_NOT_FOUND          One of the PK, KEK, or DB default keys is not found.
  @retval Others                 Fail to initialize the secure default keys.

**/
EFI_STATUS
EFIAPI
SecureBootDefaultKeysEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS     Status;
  EFI_BOOT_MODE  BootMode;

  Status = SecureBootInitPKDefault ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Cannot initialize PKDefault: %r\n", __func__, Status));
    return Status;
  }

  Status = SecureBootInitKEKDefault ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Cannot initialize KEKDefault: %r\n", __func__, Status));
    return Status;
  }

  Status = SecureBootInitDbDefault ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Cannot initialize dbDefault: %r\n", __func__, Status));
    return Status;
  }

  Status = SecureBootInitDbtDefault ();
  if (Status == EFI_NOT_FOUND) {
    DEBUG ((DEBUG_INFO, "%a: dbtDefault not initialized\n", __func__));
  } else if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Cannot initialize dbtDefault: %r\n", __func__, Status));
    return Status;
  }

  Status = SecureBootInitDbxDefault ();
  if (Status == EFI_NOT_FOUND) {
    DEBUG ((DEBUG_INFO, "%a: dbxDefault not initialized\n", __func__));
  } else if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Cannot initialize dbxDefault: %r\n", __func__, Status));
    return Status;
  }

  BootMode = GetBootModeHob ();
  if (BootMode == BOOT_WITH_DEFAULT_SETTINGS || BootMode == BOOT_WITH_MFG_MODE_SETTINGS) {
    Status = EnrollKeysFromDefaults ();
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Cannot enroll keys from defaults: %r\n", __func__, Status));
      return Status;
    }
  }

  return EFI_SUCCESS;
}
