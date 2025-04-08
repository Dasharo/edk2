/** @file

  Execute pending TPM requests from OS or BIOS and Lock TPM.

  Caution: This module requires additional review when modified.
  This driver will have external input - variable.
  This external input must be validated carefully to avoid security issue.

  ExecutePendingTpmRequest() will receive untrusted input and do validation.

Copyright (c) 2006 - 2018, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/TcgPhysicalPresenceLib.h>

/**
  Check and execute the pending TPM request and Lock TPM.

  The TPM request may come from OS or BIOS. This API will display request information and wait
  for user confirmation if TPM request exists. The TPM request will be sent to TPM device after
  the TPM request is confirmed, and one or more reset may be required to make TPM request to
  take effect. At last, it will lock TPM to prevent TPM state change by malware.

  This API should be invoked after console in and console out are all ready as they are required
  to display request information and get user input to confirm the request. This API should also
  be invoked as early as possible as TPM is locked in this function.

**/
VOID
EFIAPI
TcgPhysicalPresenceLibProcessRequest (
  VOID
  )
{
  // Do nothing
}

/**
  Check if the pending TPM request needs user input to confirm.

  The TPM request may come from OS. This API will check if TPM request exists and need user
  input to confirmation.

  @retval    TRUE        TPM needs input to confirm user physical presence.
  @retval    FALSE       TPM doesn't need input to confirm user physical presence.

**/
BOOLEAN
EFIAPI
TcgPhysicalPresenceLibNeedUserConfirm (
  VOID
  )
{
  return FALSE;
}

/**
  The handler for TPM physical presence function:
  Submit TPM Operation Request to Pre-OS Environment.

  Caution: This function may receive untrusted input.

  @param[in]      PpRequest TPM physical presence operation request.

  @return Return Code for Submit TPM Operation Request to Pre-OS Environment.
**/
EFI_STATUS
EFIAPI
TcgPhysicalPresenceLibSubmitRequestToPreOSFunction (
  IN UINT8  PpRequest
  )
{
  return EFI_SUCCESS;
}
