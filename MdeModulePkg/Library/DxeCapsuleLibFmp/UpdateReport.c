#include "UpdateReport.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CustomizedDisplayLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PopUpLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>

VOID
EFIAPI
ReportFree (
  IN OUT UpdateReport  *Report
  )
{
  UINTN          CapsuleIdx;
  CapsuleResult  *Capsule;

  for (CapsuleIdx = 0; CapsuleIdx < Report->CapsuleCount; ++CapsuleIdx) {
    Capsule = &Report->Capsules[CapsuleIdx];
    if (Capsule->Payloads != NULL) {
      FreePool (Capsule->Payloads);
    }
  }

  if (Report->Capsules != NULL) {
    FreePool (Report->Capsules);
  }
}

CapsuleResult *
EFIAPI
ReportAddCapsule (
  IN UpdateReport  *Report,
  IN UINTN         Index
  )
{
  VOID           *Capsules;
  CapsuleResult  *CapsuleResult;

  Capsules = ReallocatePool (Report->CapsuleCount * sizeof(*Report->Capsules),
                             (Report->CapsuleCount + 1) * sizeof(*Report->Capsules),
                             Report->Capsules);
  if (Capsules == NULL) {
    return NULL;
  }

  Report->Capsules = Capsules;

  CapsuleResult = &Report->Capsules[Report->CapsuleCount++];

  CapsuleResult->Index   = Index;
  CapsuleResult->Outcome = CAPSULE_UNKNOWN;
  CapsuleResult->Status  = EFI_SUCCESS;

  return CapsuleResult;
}

VOID
EFIAPI
ReportPopCapsule (
  IN UpdateReport  *Report
  )
{
  CapsuleResult  *Capsule;

  if (Report->CapsuleCount == 0) {
    return;
  }

  Capsule = &Report->Capsules[--Report->CapsuleCount];
  if (Capsule->Payloads != NULL) {
    FreePool (Capsule->Payloads);
  }
}

VOID
EFIAPI
ReportCapsuleOutcome (
  IN CapsuleResult  *CapsuleResult OPTIONAL,
  IN UpdateOutcome  Outcome,
  IN EFI_STATUS     Status
  )
{
  if (CapsuleResult == NULL) {
    return;
  }

  //
  // Permit code deeper in the call stack to set a more specific failure reason
  // in which case this will do nothing when a higher-level function tries to
  // set a generic failure.
  //
  if (CapsuleResult->Outcome == CAPSULE_UNKNOWN) {
    CapsuleResult->Outcome = Outcome;
    CapsuleResult->Status  = Status;
  }
}

VOID
EFIAPI
ReportAddPayload (
  IN OUT CapsuleResult  *Capsule OPTIONAL,
  IN EFI_STATUS         Status
  )
{
  VOID           *Payloads;
  PayloadResult  *PayloadResult;

  if (Capsule == NULL) {
    return;
  }

  Payloads = ReallocatePool (Capsule->PayloadCount * sizeof(*Capsule->Payloads),
                             (Capsule->PayloadCount + 1) * sizeof(*Capsule->Payloads),
                             Capsule->Payloads);
  if (Payloads == NULL) {
    return;
  }

  Capsule->Payloads = Payloads;

  PayloadResult = &Capsule->Payloads[Capsule->PayloadCount++];

  PayloadResult->Status = Status;
}

STATIC
BOOLEAN
EFIAPI
AllUpdatesSuccessful (
  IN CONST UpdateReport  *Report
  )
{
  UINTN  CapsuleIdx;

  for (CapsuleIdx = 0; CapsuleIdx < Report->CapsuleCount; ++CapsuleIdx) {
    if (Report->Capsules[CapsuleIdx].Outcome != CAPSULE_SUCCESS) {
      return FALSE;
    }
  }

  return TRUE;
}

STATIC
CONST CHAR16 *
OutcomeToString (
  IN UpdateOutcome  Outcome
  )
{
  switch (Outcome) {
    case CAPSULE_UNKNOWN:  return L"BUG: not set";
    case CAPSULE_REJECTED: return L"Firmware GUID wasn't recognized";
    case CAPSULE_REFUSED:  return L"Capsule has failed FMP validation";
    case CAPSULE_NONFMP:   return L"Capsule is not of FMP type";
    case CAPSULE_FAILED:   return L"Update process hit an error";
    case CAPSULE_DRIVER:   return L"Failed to start a driver";
    case CAPSULE_PAYLOAD:  return L"Failed to set a payload image";
    case CAPSULE_SUCCESS:  return L"Applied successfully";
  }

  return L"BUG: unhandled value of a CAPSULE_* constant!";
}

STATIC
VOID
EFIAPI
ReportResults (
  IN OUT PopUpData       *PopUp,
  IN CONST UpdateReport  *Report
  )
{
  UINTN                CapsuleIdx;
  UINTN                PayloadIdx;
  CONST CapsuleResult  *Capsule;
  CONST PayloadResult  *Payload;

  for (CapsuleIdx = 0; CapsuleIdx < Report->CapsuleCount; ++CapsuleIdx) {
    Capsule = &Report->Capsules[CapsuleIdx];

    //
    // Note that lines can't start with a space as then they are drawn as
    // input fields.
    //

    if (Report->CapsuleCount > 1) {
      AddFullLineF (PopUp, L"Capsule #%d", Capsule->Index + 1);
    }
    AddFullLineF (PopUp, L"- Result: %s", OutcomeToString (Capsule->Outcome));
    if (Capsule->Status != EFI_SUCCESS) {
      AddFullLineF (PopUp, L"- Status: %r", Capsule->Status);
    }

    for (PayloadIdx = 0; PayloadIdx < Capsule->PayloadCount; ++PayloadIdx) {
      Payload = &Capsule->Payloads[PayloadIdx];
      if (Capsule->PayloadCount > 1) {
        AddFullLineF (PopUp, L"- Status of payload #%d: %r", PayloadIdx + 1, Payload->Status);
      } else {
        AddFullLineF (PopUp, L"- Status of payload: %r", Payload->Status);
      }
    }

    if (Capsule->PayloadCount == 0) {
      AddFullLine (PopUp, L"- No payloads were processed.");
    }
  }
}

STATIC
VOID
WaitForEnterKey (
  IN UINTN  TimeoutSeconds
  )
{
  EFI_STATUS     Status;
  EFI_EVENT      Events[2];
  UINTN          EventCount;
  UINTN          EventIndex;
  EFI_INPUT_KEY  Key;
  EFI_EVENT      TimerEvent;

  EventCount = 0;
  Events[EventCount++] = gST->ConIn->WaitForKey;
  if (TimeoutSeconds > 0) {
    Status = gBS->CreateEvent (
        EVT_TIMER,
        TPL_CALLBACK,
        NULL,
        NULL,
        &TimerEvent
        );
    if (EFI_ERROR (Status) ||
        EFI_ERROR (gBS->SetTimer (TimerEvent, TimerRelative, TimeoutSeconds * 1000 * 1000 * 10))) {
      if (!EFI_ERROR (Status)) {
        gBS->CloseEvent (TimerEvent);
      }
      // It was just a notice anyway, give up.
      return;
    }

    Events[EventCount++] = TimerEvent;
  }

  DrainInput ();

  while (TRUE) {
    Status = gBS->WaitForEvent (EventCount, Events, &EventIndex);
    if (EFI_ERROR (Status)) {
      continue;
    }

    // Reached a timeout.
    if (EventIndex == 1) {
      break;
    }

    // Received a key press.
    if (EventIndex == 0) {
      Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      if (!EFI_ERROR (Status) && Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
        break;
      }
    }
  }

  if (TimeoutSeconds > 0) {
    gBS->CloseEvent (TimerEvent);
  }
}

VOID
EFIAPI
ReportDisplay (
  IN CONST UpdateReport  *Report
  )
{
  PopUpData  PopUp;
  BOOLEAN    Success;

  if (!FixedPcdGetBool (PcdShowCapsuleReport)) {
    return;
  }

  PopUpInit (&PopUp, /*Width=*/78);

  if (Report->CapsuleCount == 0) {
    Success = FALSE;
    AddTitle (&PopUp, L"Firmware Update Skipped");
    AddLine (&PopUp, L"The firmware is in capsule update mode, yet no update capsules were");
    AddLine (&PopUp, L"discovered. The update was cancelled without modifying the firmware.");
    AddLine (&PopUp, L"");
    AddLine (&PopUp, L"This situation is unexpected. Please consider contacting us at");
    AddLine (&PopUp, L"support@dasharo.com describing how this situation occurred.");
  } else if (AllUpdatesSuccessful (Report)) {
    Success = TRUE;
    AddTitle (&PopUp, L"Firmware Update Succeeded");
    if (Report->CapsuleCount == 1) {
      AddLine (&PopUp, L"Successfully applied a firmware update.");
    } else {
      AddLineF (&PopUp, L"Successfully applied all firmware updates (%d capsules).", Report->CapsuleCount);
    }
  } else {
    Success = FALSE;
    AddTitle (&PopUp, L"Firmware Update Failed");
    if (Report->CapsuleCount == 1) {
      AddLine (&PopUp, L"A firmware update was not applied successfully. Depending on the severity of");
      AddLine (&PopUp, L"the error the device may be unbootable and require external firmware flashing.");
    } else {
      AddLine (&PopUp, L"Not all firmware updates were applied successfully. Depending on the severity");
      AddLine (&PopUp, L"of the error the device may be unbootable and require external firmware");
      AddLine (&PopUp, L"flashing.");
    }
    AddLine (&PopUp, L"");
    AddLine (&PopUp, L"Please contact us at support@dasharo.com with a photo of the screen and refer");
    AddLine (&PopUp, L"to the device-specific recovery instructions at <https://docs.dasharo.com>.");
    AddLine (&PopUp, L"");
    AddLine (&PopUp, L"Details about the update:");
    ReportResults (&PopUp, Report);
  }

  AddLine (&PopUp, L"");
  if (Success) {
    AddLine (&PopUp, L"The system will reboot in 30 seconds, press ENTER to reboot immediately.");
  } else {
    AddLine (&PopUp, L"Press ENTER to reboot.");
  }

  PopUpDraw (&PopUp, Success ? SuccessPopUp : ErrorPopUp);

  WaitForEnterKey (Success ? 30 : 0);
}
