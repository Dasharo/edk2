/** @file
  Top-level navigation tabs for the graphical setup menu.

  The display engine receives forms one at a time via FormBrowser2; it has
  no inherent notion of "siblings" or a tab strip. To present persistent
  tabs we snapshot the REF statements from the first form that contains
  them (typically the front page) and render that snapshot on every form.

  Statement pointers are owned by the form browser and are only valid for
  the duration of one FormDisplay call, so we cache by FormSet GUID +
  Form ID + HII handle + prompt StringId — all of which remain stable.

  Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "GraphicsDisplayEngine.h"

STATIC TAB_ENTRY  mTabs[MAX_TABS];
STATIC UINTN      mTabCount;

// Tabs that should always be pinned to the right end of the strip, in the
// order listed here. Matched against the destination FormSetGuid of each
// captured REF. Add new GUIDs (e.g. for additional "wrap-up" tabs) at the
// end — entries are moved to the rightmost slots in this order.
STATIC EFI_GUID  mTrailingTabs[] = {
  // DasharoSaveExitUiLib: { 0x5b7f9a21, 0x3c44, 0x4e89, ... }
  { 0x5b7f9a21, 0x3c44, 0x4e89, { 0xb1, 0xa6, 0xd8, 0xc7, 0xf2, 0xe5, 0xa9, 0x01 } },
};

STATIC
VOID
PinTrailingTabs (
  VOID
  )
{
  UINTN  Pin;
  UINTN  i;
  UINTN  TargetSlot;

  if (mTabCount < 2) {
    return;
  }
  // For each "trailing" GUID in registration order, find the tab in the
  // current array (skipping Main at index 0) and rotate it into the
  // appropriate slot near the end. We process front-to-back through
  // mTrailingTabs so multiple trailing tabs preserve their relative order.
  for (Pin = 0; Pin < ARRAY_SIZE (mTrailingTabs); Pin++) {
    TargetSlot = mTabCount - 1 - (ARRAY_SIZE (mTrailingTabs) - 1 - Pin);
    if (TargetSlot >= mTabCount) {
      continue;
    }
    for (i = 1; i < mTabCount; i++) {
      if (!CompareGuid (&mTabs[i].FormSetGuid, &mTrailingTabs[Pin])) {
        continue;
      }
      if (i == TargetSlot) {
        break;
      }
      // Rotate mTabs[i] to TargetSlot. If TargetSlot > i, shift the
      // intermediate range left; otherwise shift right. (For our cases
      // TargetSlot is always >= i so we only ever shift left.)
      {
        TAB_ENTRY  Tmp = mTabs[i];
        if (TargetSlot > i) {
          UINTN  k;
          for (k = i; k < TargetSlot; k++) {
            mTabs[k] = mTabs[k + 1];
          }
        } else {
          UINTN  k;
          for (k = i; k > TargetSlot; k--) {
            mTabs[k] = mTabs[k - 1];
          }
        }
        mTabs[TargetSlot] = Tmp;
      }
      break;
    }
  }
}

STATIC
BOOLEAN
IsRefOpcode (
  IN UINT8  OpCode
  )
{
  return (OpCode == EFI_IFR_REF_OP);
}

STATIC
VOID
ExtractRef (
  IN  FORM_DISPLAY_ENGINE_STATEMENT  *Stmt,
  IN  EFI_HII_HANDLE                 HiiHandle,
  IN  EFI_GUID                       *CurrentFormSetGuid,
  OUT TAB_ENTRY                      *Out
  )
{
  EFI_IFR_REF  *Ref = (EFI_IFR_REF *)Stmt->OpCode;
  CHAR16       *Resolved;

  Out->FormId    = Ref->FormId;
  Out->HiiHandle = HiiHandle;
  Out->Prompt    = Ref->Question.Header.Prompt;

  // Resolve the prompt string NOW and cache the literal — UiApp rebuilds
  // the Front Page on every entry, which can retire the StringIds we
  // captured. Holding the string itself keeps tabs stable across rebuilds.
  Resolved = HiiGetString (HiiHandle, Out->Prompt, NULL);
  if (Resolved != NULL) {
    StrCpyS (Out->Name, ARRAY_SIZE (Out->Name), Resolved);
    FreePool (Resolved);
  } else {
    Out->Name[0] = L'\0';
  }

  // REF3 and REF4 carry an explicit destination FormSetId — that's how
  // cross-formset entries on the Front Page are encoded (HiiCreateGotoExOpCode
  // emits REF3 with FormId=0 and a FormSetId pointing at the target formset).
  if (Ref->Header.Length >= sizeof (EFI_IFR_REF3)) {
    EFI_IFR_REF3  *Ref3 = (EFI_IFR_REF3 *)Stmt->OpCode;
    CopyMem (&Out->FormSetGuid, &Ref3->FormSetId, sizeof (EFI_GUID));
  } else {
    CopyMem (&Out->FormSetGuid, CurrentFormSetGuid, sizeof (EFI_GUID));
  }
}

VOID
TabsCaptureFromForm (
  IN FORM_DISPLAY_ENGINE_FORM  *Form
  )
{
  LIST_ENTRY                       *Link;
  FORM_DISPLAY_ENGINE_STATEMENT    *Stmt;
  UINTN                            RefsTotal;

  // One-shot capture: the first form we see with enough REFs becomes the
  // tab source for the rest of the session. We deliberately do NOT replace
  // the cache when the user navigates into a sub-menu (which itself may
  // have several REFs); doing so would make the tab strip mutate as the
  // user drills down, which is the opposite of what tabs are for.
  if (mTabCount > 0) {
    return;
  }

  RefsTotal = 0;
  for (Link = GetFirstNode (&Form->StatementListHead);
       !IsNull (&Form->StatementListHead, Link);
       Link = GetNextNode (&Form->StatementListHead, Link))
  {
    Stmt = FORM_DISPLAY_ENGINE_STATEMENT_FROM_LINK (Link);
    if ((Stmt->OpCode != NULL) && IsRefOpcode (Stmt->OpCode->OpCode)) {
      RefsTotal++;
    }
  }

  DEBUG ((
    DEBUG_ERROR,
    "Tabs: form FormId=0x%x has %u REFs\n",
    Form->FormId, (UINT32)RefsTotal
    ));

  if (RefsTotal < 2) {
    return;  // not a tab-source form
  }

  // Record this form as the canonical "Main" form so the rendering path
  // can recognize when we're on it and switch into the system-info card
  // layout (vs the plain REF list).
  CopyMem (&mState.MainFormSetGuid, &Form->FormSetGuid, sizeof (EFI_GUID));
  mState.MainFormId         = Form->FormId;
  mState.MainFormIdentified = TRUE;

  // Tab 01 is a synthetic "Main" entry pointing at the form we're capturing
  // from. It carries the actual FormSetGuid + FormId so navigation back to
  // it via L/R works through the same FindRefForTab + multi-step exit
  // path; the literal "Main" label sidesteps the form's own title.
  CopyMem (&mTabs[0].FormSetGuid, &mState.MainFormSetGuid, sizeof (EFI_GUID));
  mTabs[0].FormId    = mState.MainFormId;
  mTabs[0].HiiHandle = Form->HiiHandle;
  mTabs[0].Prompt    = 0;
  StrCpyS (mTabs[0].Name, ARRAY_SIZE (mTabs[0].Name), L"Main");
  mTabCount = 1;

  for (Link = GetFirstNode (&Form->StatementListHead);
       !IsNull (&Form->StatementListHead, Link) && (mTabCount < MAX_TABS);
       Link = GetNextNode (&Form->StatementListHead, Link))
  {
    Stmt = FORM_DISPLAY_ENGINE_STATEMENT_FROM_LINK (Link);
    if ((Stmt->OpCode == NULL) || !IsRefOpcode (Stmt->OpCode->OpCode)) {
      continue;
    }
    ExtractRef (Stmt, Form->HiiHandle, &Form->FormSetGuid, &mTabs[mTabCount]);
    mTabCount++;
  }

  PinTrailingTabs ();
}

UINTN
TabsCount (
  VOID
  )
{
  return mTabCount;
}

TAB_ENTRY *
TabsGet (
  IN UINTN  Index
  )
{
  if (Index >= mTabCount) {
    return NULL;
  }
  return &mTabs[Index];
}

INTN
TabsFindActive (
  IN FORM_DISPLAY_ENGINE_FORM  *Form
  )
{
  UINTN  i;

  // Match on FormSetGuid first; that anchors us to the right top-level
  // section even when the user has drilled into a sub-form. The cached
  // FormId is only required to match if it's non-zero — cross-formset
  // tab entries store FormId=0 (meaning "the formset's main form") and
  // we want them to highlight while the user is anywhere inside that
  // formset, not just on its main form.
  for (i = 0; i < mTabCount; i++) {
    if (!CompareGuid (&mTabs[i].FormSetGuid, &Form->FormSetGuid)) {
      continue;
    }
    if ((mTabs[i].FormId == 0) || (mTabs[i].FormId == Form->FormId)) {
      return (INTN)i;
    }
  }
  return -1;
}

VOID
TabsRenderHorizontal (
  IN UINTN  TopY,
  IN UINTN  BarH,
  IN INTN   ActiveIndex
  )
{
  UINTN   TabW;
  UINTN   x;
  UINTN   yText;
  UINTN   i;
  CHAR16  NumPrefix[8];
  UINTN   PrefixW;

  if (mTabCount == 0) {
    return;
  }

  TabW  = mState.ScreenW / mTabCount;
  yText = TopY + (BarH - mState.FontPx) / 2;

  for (i = 0; i < mTabCount; i++) {
    x = i * TabW;

    // Number prefix is always dim — it's purely for orientation, the
    // emphasis belongs on the name.
    UnicodeSPrint (NumPrefix, sizeof (NumPrefix), L"%02u  ", (UINT32)(i + 1));
    GfxSetTextColor (TC_DIMMER);
    GfxDrawTextW    (x + 2 * GFX_PADDING_PX, yText, NumPrefix);
    PrefixW = GfxMeasureTextW (NumPrefix);

    // Active: bright white. Inactive: dim — visible but recedes.
    if ((INTN)i == ActiveIndex) {
      GfxSetTextColor (TC_WHITE);
    } else {
      GfxSetTextColor (TC_DIM);
    }
    GfxDrawTextWClipped (
      x + 2 * GFX_PADDING_PX + PrefixW,
      yText,
      TabW - 3 * GFX_PADDING_PX - PrefixW,
      mTabs[i].Name[0] != L'\0' ? mTabs[i].Name : L"?"
      );

    if ((INTN)i == ActiveIndex) {
      GfxFillRect (
        x + GFX_PADDING_PX,
        TopY + BarH - 3,
        TabW - 2 * GFX_PADDING_PX, 3,
        GFX_ACCENT
        );
    }
  }
  GfxSetTextColor (TC_WHITE);
}
