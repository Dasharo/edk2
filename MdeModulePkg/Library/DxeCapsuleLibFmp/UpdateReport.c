#include "UpdateReport.h"

#include <Guid/FmpCapsule.h>
#include <Guid/SystemResourceTable.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CustomizedDisplayLib.h>
#include <Library/DebugLib.h>
#include <Library/FmpDependencyLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PopUpLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#pragma pack(1)

typedef struct {
  UINT32  Signature;
  UINT32  HeaderSize;
  UINT32  FwVersion;
  UINT32  LowestSupportedVersion;
} FMP_PAYLOAD_HEADER;

#pragma pack()

#define FMP_PAYLOAD_HEADER_SIGNATURE  SIGNATURE_32 ('M', 'S', 'S', '1')

/**
  Return if this CapsuleGuid is a FMP capsule GUID or not.

  @param[in] CapsuleGuid A pointer to EFI_GUID

  @retval TRUE  It is a FMP capsule GUID.
  @retval FALSE It is not a FMP capsule GUID.
**/
BOOLEAN
IsFmpCapsuleGuid (
  IN EFI_GUID  *CapsuleGuid
  );

/**
  Return if this FMP is a system FMP or a device FMP, based upon CapsuleHeader.

  @param[in] CapsuleHeader A pointer to EFI_CAPSULE_HEADER

  @retval TRUE  It is a system FMP.
  @retval FALSE It is a device FMP.
**/
BOOLEAN
IsFmpCapsule (
  IN EFI_CAPSULE_HEADER  *CapsuleHeader
  );

/**
  Gets pointer to the image data from payload's header.

  @param[in]  ImageHeader   The payload image header.

  @return The pointer to the start of the image data.
**/
VOID *
GetFmpImage (
  IN EFI_FIRMWARE_MANAGEMENT_CAPSULE_IMAGE_HEADER  *ImageHeader
  );

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
  IN UpdateReport        *Report,
  IN UINTN               Index,
  IN EFI_CAPSULE_HEADER  *Header
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
  CapsuleResult->Header  = Header;
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
BOOLEAN
FindEsre (
  IN OUT CONST EFI_SYSTEM_RESOURCE_ENTRY  **Entry
  )
{
  EFI_STATUS                 Status;
  UINTN                      Index;
  EFI_SYSTEM_RESOURCE_ENTRY  *Esre;
  EFI_SYSTEM_RESOURCE_TABLE  *Esrt;

  Status = EfiGetSystemConfigurationTable (&gEfiSystemResourceTableGuid, (VOID **)&Esrt);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  Esre = (VOID *)&Esrt[1];
  for (Index = 0; Index < Esrt->FwResourceCount; ++Index) {
    if (Esre[Index].FwType == ESRT_FW_TYPE_SYSTEMFIRMWARE) {
      *Entry = &Esre[Index];
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
BOOLEAN
BiosUpdateFailed (
  IN CONST UpdateReport  *Report
  )
{
  UINTN                                         CapsuleIdx;
  UINTN                                         PayloadIdx;
  UINTN                                         ItemCount;
  CONST EFI_SYSTEM_RESOURCE_ENTRY               *Esre;
  CONST CapsuleResult                           *Capsule;
  EFI_CAPSULE_HEADER                            *Header;
  EFI_FIRMWARE_MANAGEMENT_CAPSULE_HEADER        *FmpHdr;
  EFI_FIRMWARE_MANAGEMENT_CAPSULE_IMAGE_HEADER  *FmpImageHdr;
  UINT64                                        *ItemOffsetList;

  if (!FindEsre (&Esre)) {
    // Assume the worst.
    return TRUE;
  }

  // A successful later update overrides any previous failure, so visit the
  // updates in reverse.
  for (CapsuleIdx = Report->CapsuleCount; CapsuleIdx > 0; --CapsuleIdx) {
    Capsule = &Report->Capsules[CapsuleIdx - 1];

    Header = Capsule->Header;
    if (!IsFmpCapsule (Header)) {
      continue;
    }

    // An FMP capsule can be nested which is checked for in IsFmpCapsule().
    if (!IsFmpCapsuleGuid (&Header->CapsuleGuid)) {
      Header = (VOID *)((UINT8 *)Header + Header->HeaderSize);
    }

    FmpHdr         = (VOID *)((UINT8 *)Header + Header->HeaderSize);
    ItemCount      = FmpHdr->EmbeddedDriverCount + FmpHdr->PayloadItemCount;
    ItemOffsetList = (UINT64 *)&FmpHdr[1];

    for (PayloadIdx = FmpHdr->EmbeddedDriverCount; PayloadIdx < ItemCount; ++PayloadIdx) {
      FmpImageHdr = (VOID *)((UINT8 *)FmpHdr + ItemOffsetList[PayloadIdx]);

      if (CompareGuid (&Esre->FwClass, &FmpImageHdr->UpdateImageTypeId)) {
        return (Capsule->Outcome != CAPSULE_SUCCESS);
      }
    }
  }

  return FALSE;
}

STATIC
BOOLEAN
GetNewBiosVersion (
  IN CONST UpdateReport               *Report,
  IN CONST EFI_SYSTEM_RESOURCE_ENTRY  *Esre,
  OUT UINT32                          *NewVersion
  )
{
  UINTN                                         CapsuleIdx;
  UINTN                                         PayloadIdx;
  UINTN                                         ItemCount;
  UINT32                                        DepExSize;
  CONST CapsuleResult                           *Capsule;
  EFI_CAPSULE_HEADER                            *Header;
  EFI_FIRMWARE_MANAGEMENT_CAPSULE_HEADER        *FmpHdr;
  EFI_FIRMWARE_MANAGEMENT_CAPSULE_IMAGE_HEADER  *FmpImageHdr;
  EFI_FIRMWARE_IMAGE_AUTHENTICATION             *FmpImage;
  FMP_PAYLOAD_HEADER                            *FmpPayloadHdr;
  UINT64                                        *ItemOffsetList;

  // We want to get the version from the last capsule in case more than one was
  // applied in a succession.
  for (CapsuleIdx = Report->CapsuleCount; CapsuleIdx > 0; --CapsuleIdx) {
    Capsule = &Report->Capsules[CapsuleIdx - 1];

    if (Capsule->Outcome != CAPSULE_SUCCESS) {
      continue;
    }

    Header = Capsule->Header;
    if (!IsFmpCapsule (Header)) {
      continue;
    }

    // An FMP capsule can be nested which is checked for in IsFmpCapsule().
    if (!IsFmpCapsuleGuid (&Header->CapsuleGuid)) {
      Header = (VOID *)((UINT8 *)Header + Header->HeaderSize);
    }

    FmpHdr         = (VOID *)((UINT8 *)Header + Header->HeaderSize);
    ItemCount      = FmpHdr->EmbeddedDriverCount + FmpHdr->PayloadItemCount;
    ItemOffsetList = (UINT64 *)&FmpHdr[1];

    for (PayloadIdx = FmpHdr->EmbeddedDriverCount; PayloadIdx < ItemCount; ++PayloadIdx) {
      FmpImageHdr = (VOID *)((UINT8 *)FmpHdr + ItemOffsetList[PayloadIdx]);

      if (!CompareGuid (&Esre->FwClass, &FmpImageHdr->UpdateImageTypeId)) {
        continue;
      }

      FmpImage = GetFmpImage (FmpImageHdr);

      DepExSize = 0;
      GetImageDependency (FmpImage, FmpImageHdr->UpdateImageSize, &DepExSize, NULL);

      FmpPayloadHdr = (VOID *)((UINT8 *)FmpImage +
                               sizeof(FmpImage->MonotonicCount) +
                               FmpImage->AuthInfo.Hdr.dwLength +
                               DepExSize);
      if (FmpPayloadHdr->Signature == FMP_PAYLOAD_HEADER_SIGNATURE) {
        *NewVersion = FmpPayloadHdr->FwVersion;
        return TRUE;
      }
    }
  }

  return FALSE;
}

STATIC
VOID
EFIAPI
VersionToStr (
  IN UINT32      Version,
  IN OUT CHAR16  *Buffer,
  IN UINTN       BufferSizeBytes
  )
{
  UINTN  Major;
  UINTN  Minor;
  UINTN  Patch;
  UINTN  RcBuild;

  Major   = Version >> 24;
  Minor   = (Version >> 16) & 0xff;
  Patch   = (Version >> 8) & 0xff;
  RcBuild = Version & 0xff;

  if (RcBuild < 0x80) {
    UnicodeSPrint (Buffer, BufferSizeBytes, L"v%ld.%ld.%ld-rc%ld", Major, Minor, Patch, RcBuild);
  } else if (RcBuild == 0x80) {
    UnicodeSPrint (Buffer, BufferSizeBytes, L"v%ld.%ld.%ld", Major, Minor, Patch);
  } else {
    UnicodeSPrint (Buffer, BufferSizeBytes, L"v%ld.%ld.%ld.%ld", Major, Minor, Patch, RcBuild - 0x80);
  }
}

STATIC
VOID
ReportVersions (
  IN OUT PopUpData       *PopUp,
  IN CONST UpdateReport  *Report
  )
{
  CONST EFI_SYSTEM_RESOURCE_ENTRY  *Esre;
  UINT32                           NewVersion;
  CHAR16                           OldVersionStr[32];
  CHAR16                           NewVersionStr[32];

  AddLine (PopUp, L"");

  if (!FindEsre (&Esre)) {
    AddLine (PopUp, L"Failed to determine version of the running firmware!");
    return;
  }

  VersionToStr (Esre->FwVersion, OldVersionStr, sizeof(OldVersionStr));

  // It's possible there were no BIOS updates.
  if (GetNewBiosVersion (Report, Esre, &NewVersion)) {
    VersionToStr (NewVersion, NewVersionStr, sizeof(NewVersionStr));
    AddLineF (PopUp, L"Updated from %s to %s.", OldVersionStr, NewVersionStr);
  } else {
    AddLineF (PopUp, L"Version of the running firmware: %s", OldVersionStr);
  }
}

STATIC
CONST EFI_GUID *
GetFirstPayloadFwClass (
  IN CONST CapsuleResult  *Capsule
  )
{
  UINTN                                         PayloadIdx;
  UINTN                                         ItemCount;
  EFI_CAPSULE_HEADER                            *Header;
  EFI_FIRMWARE_MANAGEMENT_CAPSULE_HEADER        *FmpHdr;
  EFI_FIRMWARE_MANAGEMENT_CAPSULE_IMAGE_HEADER  *FmpImageHdr;
  UINT64                                        *ItemOffsetList;

  Header = Capsule->Header;
  if (!IsFmpCapsule (Header)) {
    return NULL;
  }

  // An FMP capsule can be nested which is checked for in IsFmpCapsule().
  if (!IsFmpCapsuleGuid (&Header->CapsuleGuid)) {
    Header = (VOID *)((UINT8 *)Header + Header->HeaderSize);
  }

  FmpHdr         = (VOID *)((UINT8 *)Header + Header->HeaderSize);
  ItemCount      = FmpHdr->EmbeddedDriverCount + FmpHdr->PayloadItemCount;
  ItemOffsetList = (UINT64 *)&FmpHdr[1];

  PayloadIdx = FmpHdr->EmbeddedDriverCount;
  if (PayloadIdx < ItemCount) {
    FmpImageHdr = (VOID *)((UINT8 *)FmpHdr + ItemOffsetList[PayloadIdx]);
    return &FmpImageHdr->UpdateImageTypeId;
  }

  return NULL;
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
  CONST EFI_GUID       *FwClass;

  for (CapsuleIdx = 0; CapsuleIdx < Report->CapsuleCount; ++CapsuleIdx) {
    Capsule = &Report->Capsules[CapsuleIdx];

    //
    // Note that lines can't start with a space as then they are drawn as
    // input fields.
    //

    if (Report->CapsuleCount > 1) {
      AddFullLineF (PopUp, L"Capsule #%d", Capsule->Index + 1);
    }

    // Can print GUIDs of all payloads if we'll ever use such capsules.
    FwClass = GetFirstPayloadFwClass (Capsule);
    if (FwClass == NULL) {
      AddFullLine (PopUp, L"- GUID: unknown");
    } else {
      AddFullLineF (PopUp, L"- GUID: %g", FwClass);
    }

    if (Capsule->Status == EFI_SUCCESS) {
      AddFullLineF (PopUp, L"- Result: %s", OutcomeToString (Capsule->Outcome));
    } else {
      AddFullLineF (PopUp, L"- Result: %s (error: %r)", OutcomeToString (Capsule->Outcome), Capsule->Status);
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
    ReportVersions (&PopUp, Report);
  } else if (AllUpdatesSuccessful (Report)) {
    Success = TRUE;
    AddTitle (&PopUp, L"Firmware Update Succeeded");
    if (Report->CapsuleCount == 1) {
      AddLine (&PopUp, L"Successfully applied a firmware update.");
    } else {
      AddLineF (&PopUp, L"Successfully applied all firmware updates (%d capsules).", Report->CapsuleCount);
    }
    ReportVersions (&PopUp, Report);
  } else {
    Success = FALSE;
    AddTitle (&PopUp, L"Firmware Update Failed");
    if (BiosUpdateFailed (Report)) {
      if (Report->CapsuleCount == 1) {
        AddLine (&PopUp, L"A firmware update was not applied successfully.");
      } else {
        AddLine (&PopUp, L"Not all firmware updates were applied successfully.");
      }
      AddLine (&PopUp, L"Depending on the severity of the error the device may be");
      AddLine (&PopUp, L"unbootable and require external firmware flashing!");
      AddLine (&PopUp, L"");
      AddLine (&PopUp, L"Please contact us at support@dasharo.com with a photo of the screen and refer");
      AddLine (&PopUp, L"to the device-specific recovery instructions at <https://docs.dasharo.com>.");
    } else {
      if (Report->CapsuleCount == 1) {
        AddLine (&PopUp, L"A firmware update was not applied successfully.");
      } else {
        AddLine (&PopUp, L"Not all firmware updates were applied successfully.");
      }
      AddLine (&PopUp, L"The system firmware should be intact.");
      AddLine (&PopUp, L"");
      AddLine (&PopUp, L"Please contact us at support@dasharo.com with a photo of the screen.");
    }
    ReportVersions (&PopUp, Report);
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
