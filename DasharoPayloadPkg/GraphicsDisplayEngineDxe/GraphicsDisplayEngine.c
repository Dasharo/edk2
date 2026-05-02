/** @file
  Dasharo graphical FORM_DISPLAY_ENGINE_PROTOCOL provider — driver entry,
  protocol installation, and the per-form display state machine.

  Phase 1: walks FormData->StatementListHead, renders prompt strings as a
  vertical list, supports Up/Down navigation + Enter/Esc, mirrors the
  list and the current selection to serial-only consoles in sync.
  Opcode-specific value rendering and edit popups arrive in later phases.

  Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "GraphicsDisplayEngine.h"

#define MAX_ITEMS  128

typedef struct {
  FORM_DISPLAY_ENGINE_STATEMENT    *Stmt;
  EFI_STRING_ID                    Prompt;
  EFI_STRING_ID                    Help;
  BOOLEAN                          Selectable;
  BOOLEAN                          HasContent;   // FALSE = empty subtitle /
                                                 // padding row, skipped by
                                                 // arrow-key navigation
} ITEM;

//
// Statement-handling helpers.
//

STATIC
EFI_STRING_ID
GetStatementPrompt (
  IN FORM_DISPLAY_ENGINE_STATEMENT  *Stmt
  )
{
  if ((Stmt == NULL) || (Stmt->OpCode == NULL)) {
    return 0;
  }

  // Every opcode that produces a visible row has either an
  // EFI_IFR_STATEMENT_HEADER or an EFI_IFR_QUESTION_HEADER (which starts
  // with the same Statement header) immediately after EFI_IFR_OP_HEADER.
  // Prompt is the first field in either case.
  switch (Stmt->OpCode->OpCode) {
    case EFI_IFR_SUBTITLE_OP:
    case EFI_IFR_TEXT_OP:
    case EFI_IFR_REF_OP:
    case EFI_IFR_ACTION_OP:
    case EFI_IFR_RESET_BUTTON_OP:
    case EFI_IFR_CHECKBOX_OP:
    case EFI_IFR_NUMERIC_OP:
    case EFI_IFR_ONE_OF_OP:
    case EFI_IFR_ORDERED_LIST_OP:
    case EFI_IFR_STRING_OP:
    case EFI_IFR_PASSWORD_OP:
    case EFI_IFR_DATE_OP:
    case EFI_IFR_TIME_OP:
      return ((EFI_IFR_STATEMENT_HEADER *)(Stmt->OpCode + 1))->Prompt;
    default:
      return 0;
  }
}

STATIC
EFI_STRING_ID
GetStatementHelp (
  IN FORM_DISPLAY_ENGINE_STATEMENT  *Stmt
  )
{
  if ((Stmt == NULL) || (Stmt->OpCode == NULL)) {
    return 0;
  }
  // Same opcode set as GetStatementPrompt — Help follows Prompt in
  // EFI_IFR_STATEMENT_HEADER.
  switch (Stmt->OpCode->OpCode) {
    case EFI_IFR_SUBTITLE_OP:
    case EFI_IFR_TEXT_OP:
    case EFI_IFR_REF_OP:
    case EFI_IFR_ACTION_OP:
    case EFI_IFR_RESET_BUTTON_OP:
    case EFI_IFR_CHECKBOX_OP:
    case EFI_IFR_NUMERIC_OP:
    case EFI_IFR_ONE_OF_OP:
    case EFI_IFR_ORDERED_LIST_OP:
    case EFI_IFR_STRING_OP:
    case EFI_IFR_PASSWORD_OP:
    case EFI_IFR_DATE_OP:
    case EFI_IFR_TIME_OP:
      return ((EFI_IFR_STATEMENT_HEADER *)(Stmt->OpCode + 1))->Help;
    default:
      return 0;
  }
}

STATIC
BOOLEAN
IsStatementSelectable (
  IN FORM_DISPLAY_ENGINE_STATEMENT  *Stmt
  )
{
  if ((Stmt->Attribute & (HII_DISPLAY_GRAYOUT | HII_DISPLAY_SUPPRESS)) != 0) {
    return FALSE;
  }
  switch (Stmt->OpCode->OpCode) {
    case EFI_IFR_SUBTITLE_OP:
    case EFI_IFR_TEXT_OP:
      return FALSE;  // labels only
    default:
      return TRUE;
  }
}

STATIC
BOOLEAN
IsMainForm (
  IN FORM_DISPLAY_ENGINE_FORM  *Form
  )
{
  return mState.MainFormIdentified
         && (Form->FormId == mState.MainFormId)
         && CompareGuid (&Form->FormSetGuid, &mState.MainFormSetGuid);
}

// Pending tab navigation. When the user presses L/R on a form that isn't
// the Main form (so it doesn't contain the target tab's REF), we exit back
// up the form stack one step at a time. Each FormDisplay call checks this
// stash; if the current form contains the target tab's REF we fire it and
// clear the pending state, otherwise we keep exiting.
STATIC INTN  mPendingTabIndex = -1;

// Walks the current form's StatementListHead looking for the REF that
// targets the given cached tab. Returns NULL if not found (caller has to
// keep walking the form stack to reach a form that has it).
STATIC
FORM_DISPLAY_ENGINE_STATEMENT *
FindRefForTab (
  IN FORM_DISPLAY_ENGINE_FORM  *Form,
  IN UINTN                     TabIndex
  )
{
  TAB_ENTRY                      *Tab;
  LIST_ENTRY                     *Link;
  FORM_DISPLAY_ENGINE_STATEMENT  *Stmt;
  EFI_IFR_REF                    *Ref;
  EFI_GUID                       *RefFormSet;

  Tab = TabsGet (TabIndex);
  if (Tab == NULL) {
    return NULL;
  }

  for (Link = GetFirstNode (&Form->StatementListHead);
       !IsNull (&Form->StatementListHead, Link);
       Link = GetNextNode (&Form->StatementListHead, Link))
  {
    Stmt = FORM_DISPLAY_ENGINE_STATEMENT_FROM_LINK (Link);
    if ((Stmt->OpCode == NULL) ||
        (Stmt->OpCode->OpCode != EFI_IFR_REF_OP))
    {
      continue;
    }
    Ref = (EFI_IFR_REF *)Stmt->OpCode;
    if (Ref->Header.Length >= sizeof (EFI_IFR_REF3)) {
      RefFormSet = &((EFI_IFR_REF3 *)Stmt->OpCode)->FormSetId;
    } else {
      RefFormSet = &Form->FormSetGuid;
    }
    if ((Ref->FormId == Tab->FormId) &&
        CompareGuid (RefFormSet, &Tab->FormSetGuid))
    {
      return Stmt;
    }
  }
  return NULL;
}

STATIC
UINTN
CollectItems (
  IN  FORM_DISPLAY_ENGINE_FORM  *Form,
  OUT ITEM                      *Items,
  IN  UINTN                     MaxItems
  )
{
  LIST_ENTRY                       *Link;
  FORM_DISPLAY_ENGINE_STATEMENT    *Stmt;
  UINTN                            Count = 0;
  // Filter REFs out of the Main form's item list — they're already
  // represented as tabs across the top, so showing them inline would
  // duplicate the navigation.
  BOOLEAN                          SkipRefs = IsMainForm (Form);

  for (Link = GetFirstNode (&Form->StatementListHead);
       !IsNull (&Form->StatementListHead, Link) && (Count < MaxItems);
       Link = GetNextNode (&Form->StatementListHead, Link))
  {
    Stmt = FORM_DISPLAY_ENGINE_STATEMENT_FROM_LINK (Link);
    if (SkipRefs && (Stmt->OpCode != NULL) &&
        (Stmt->OpCode->OpCode == EFI_IFR_REF_OP))
    {
      continue;
    }
    Items[Count].Stmt       = Stmt;
    Items[Count].Prompt     = GetStatementPrompt (Stmt);
    Items[Count].Help       = GetStatementHelp   (Stmt);
    Items[Count].Selectable = IsStatementSelectable (Stmt);
    Items[Count].HasContent = FALSE;
    if (Items[Count].Prompt != 0) {
      CHAR16  *Resolved = HiiGetString (Form->HiiHandle, Items[Count].Prompt, NULL);
      BOOLEAN  IsEmpty = TRUE;
      if (Resolved != NULL) {
        // Treat whitespace-only as empty too — some VFRs use a single
        // space token as a "spacer" and we want to drop those as well.
        UINTN  i;
        for (i = 0; Resolved[i] != L'\0'; i++) {
          if ((Resolved[i] != L' ') && (Resolved[i] != L'\t')) {
            IsEmpty = FALSE;
            break;
          }
        }
        FreePool (Resolved);
      }

      // Drop *any* row whose prompt resolves to nothing meaningful —
      // these are invariably visual padding rows that shouldn't take up
      // a navigation slot. Real questions always have a prompt.
      if (IsEmpty) {
        continue;
      }

      Items[Count].HasContent = TRUE;
      Count++;
    }
  }
  return Count;
}

// Returns the next row index whose prompt resolves to a non-empty string,
// wrapping around at the ends. Used for arrow-key navigation so empty
// padding rows (empty subtitles) are skipped while text labels and
// grayed-out questions remain reachable.
STATIC
INTN
NextNavigable (
  IN ITEM   *Items,
  IN UINTN  Count,
  IN INTN   From,
  IN INTN   Direction
  )
{
  INTN  i;

  if (Count == 0) {
    return -1;
  }
  for (i = From; (i >= 0) && ((UINTN)i < Count); i += Direction) {
    if (Items[i].HasContent) {
      return i;
    }
  }
  if (Direction > 0) {
    for (i = 0; (UINTN)i < Count; i++) {
      if (Items[i].HasContent) {
        return i;
      }
    }
  } else {
    for (i = (INTN)Count - 1; i >= 0; i--) {
      if (Items[i].HasContent) {
        return i;
      }
    }
  }
  return -1;
}

STATIC
INTN
NextSelectable (
  IN ITEM   *Items,
  IN UINTN  Count,
  IN INTN   From,
  IN INTN   Direction
  )
{
  INTN  i;

  if (Count == 0) {
    return -1;
  }
  for (i = From; (i >= 0) && ((UINTN)i < Count); i += Direction) {
    if (Items[i].Selectable) {
      return i;
    }
  }
  // Wrap around.
  if (Direction > 0) {
    for (i = 0; (UINTN)i < Count; i++) {
      if (Items[i].Selectable) {
        return i;
      }
    }
  } else {
    for (i = (INTN)Count - 1; i >= 0; i--) {
      if (Items[i].Selectable) {
        return i;
      }
    }
  }
  return -1;
}

//
// Rendering — main panel layout + serial mirror.
//
// Layout (recomputed every frame from current GOP geometry):
//
//   +----------------------------------------------------------+
//   | [logo]  <form title>                                     |  HEADER
//   +-------+----------------------------------+---------------+
//   | tabs  |  item 1                          |               |
//   |       |  item 2  (highlighted row)       |   help        |
//   |       |  item 3                          |               |
//   |       |  ...                             |               |
//   +-------+----------------------------------+---------------+
//

STATIC
VOID
DrawHeader (
  IN UINTN  HeaderH
  )
{
  UINTN   LogoSize = HeaderH - 2 * GFX_PADDING_PX;
  UINTN   TextX    = HeaderH + GFX_PADDING_PX;
  UINTN   TopY     = HeaderH / 2 - mState.FontPx - 2;
  UINTN   BotY     = HeaderH / 2 + 2;
  UINTN   RightX   = mState.ScreenW - 2 * GFX_PADDING_PX;
  CHAR16  Buf[256];

  GfxFillRect (0, 0, mState.ScreenW, HeaderH, GFX_PANEL);
  GfxFillRect (GFX_PADDING_PX, GFX_PADDING_PX, LogoSize, LogoSize, GFX_ACCENT);

  // Left: brand block.
  GfxSetTextColor (TC_TEAL);
  GfxDrawTextW    (TextX, TopY, L"DASHARO  \x00B7  SETUP");
  GfxSetTextColor (TC_DIM);
  GfxDrawTextW    (TextX, BotY, L"OPEN SOURCE FIRMWARE DISTRIBUTION");

  // Right: clock on top, system summary below. Cap each line by what's
  // actually free between the corresponding brand-block line and the right
  // edge — measured at runtime so we don't hard-code an assumption about
  // brand width.
  SystemInfoFormatTime (Buf, sizeof (Buf));
  GfxSetTextColor (TC_DIM);
  {
    UINTN  BrandRight = TextX + GfxMeasureTextW (L"DASHARO  \x00B7  SETUP");
    UINTN  AvailW     = (RightX > BrandRight + 2 * GFX_PADDING_PX)
                          ? RightX - BrandRight - 2 * GFX_PADDING_PX : 0;
    GfxDrawTextWRightAlignedClipped (RightX, TopY, AvailW, Buf);
  }

  SystemInfoFormatSummary (Buf, sizeof (Buf));
  GfxSetTextColor (TC_DIM);
  {
    UINTN  BrandRight = TextX + GfxMeasureTextW (L"OPEN SOURCE FIRMWARE DISTRIBUTION");
    UINTN  AvailW     = (RightX > BrandRight + 2 * GFX_PADDING_PX)
                          ? RightX - BrandRight - 2 * GFX_PADDING_PX : 0;
    GfxDrawTextWRightAlignedClipped (RightX, BotY, AvailW, Buf);
  }

  GfxSetTextColor (TC_WHITE);
}

STATIC
VOID
DrawTabBar (
  IN UINTN                          TopY,
  IN UINTN                          BarH,
  IN FORM_DISPLAY_ENGINE_FORM       *Form
  )
{
  // Slim divider line at the bottom of the tab bar so the active-tab
  // underline visibly "rests" on a baseline.
  GfxFillRect (0, TopY + BarH - 1, mState.ScreenW, 1, GFX_PANEL_ALT);
  TabsRenderHorizontal (TopY, BarH, TabsFindActive (Form));
}

STATIC
VOID
DrawPageHeader (
  IN UINTN                          Y,
  IN UINTN                          H,
  IN FORM_DISPLAY_ENGINE_FORM       *Form
  )
{
  CHAR16      Buf[256];
  CHAR16      *Title;
  CONST CHAR16 *TabName;
  INTN        ActiveTab;
  UINTN       LineY;
  UINTN       LineSpace;
  TAB_ENTRY   *Tab;

  Title     = HiiGetString (Form->HiiHandle, Form->FormTitle, NULL);
  ActiveTab = TabsFindActive (Form);
  TabName   = L"";
  if (ActiveTab >= 0) {
    Tab = TabsGet ((UINTN)ActiveTab);
    if ((Tab != NULL) && (Tab->Name[0] != L'\0')) {
      TabName = Tab->Name;
    }
  }

  LineSpace = mState.FontPx * 14 / 10;
  LineY     = Y + GFX_PADDING_PX;

  // Breadcrumb in dim text — quiet context line.
  GfxSetTextColor (TC_DIM);
  UnicodeSPrint (
    Buf, sizeof (Buf),
    L"SETUP  /  %s",
    Title != NULL ? Title : L"?"
    );
  GfxDrawTextW (2 * GFX_PADDING_PX, LineY, Buf);
  LineY += LineSpace;

  // Section subhead in teal — only render when we actually have a section
  // (i.e. an active tab); on the front page itself there's nothing to say.
  if (ActiveTab >= 0) {
    GfxSetTextColor (TC_TEAL);
    UnicodeSPrint (
      Buf, sizeof (Buf),
      L"SECTION %02u  \x2014  %s",
      (UINT32)(ActiveTab + 1),
      TabName
      );
    GfxDrawTextW (2 * GFX_PADDING_PX, LineY, Buf);
    LineY += LineSpace;
  }

  // Page title — primary white, the headline of this view.
  GfxSetTextColor (TC_WHITE);
  GfxDrawTextW (2 * GFX_PADDING_PX, LineY, Title != NULL ? Title : L"?");

  // No underline rule — readability without it is fine.

  if (Title != NULL) FreePool (Title);
}

STATIC
VOID
DrawKeyBox (
  IN OUT UINTN         *X,
  IN     UINTN         Y,
  IN     UINTN         BoxH,
  IN     CONST CHAR16  *Key,
  IN     CONST CHAR16  *Label
  )
{
  UINTN  KeyW   = GfxMeasureTextW (Key);
  UINTN  LabelW = GfxMeasureTextW (Label);
  UINTN  BoxW   = KeyW + GFX_PADDING_PX;
  UINTN  BoxHi  = mState.FontPx + 4;
  UINTN  TextY  = Y + (BoxH - mState.FontPx) / 2;
  UINTN  BoxY   = Y + (BoxH - BoxHi) / 2;

  GfxDrawRectOutline (*X, BoxY, BoxW, BoxHi, GFX_TEXT_DIM);
  GfxSetTextColor    (TC_WHITE);
  GfxDrawTextW       (*X + GFX_PADDING_PX / 2, TextY, Key);

  *X += BoxW + GFX_PADDING_PX;
  GfxSetTextColor (TC_DIM);
  GfxDrawTextW    (*X, TextY, Label);
  *X += LabelW + 2 * GFX_PADDING_PX;
}

STATIC
VOID
DrawFooter (
  IN UINTN  FooterH
  )
{
  STATIC CONST struct {
    CONST CHAR16 *Key;
    CONST CHAR16 *Label;
  } Hints[] = {
    // VeraB doesn't ship the U+2191/U+2193 arrow glyphs, so use ASCII labels
    // until we vendor a font with them.
    { L"Up/Dn",  L"Move"        },
    { L"Enter",  L"Select"      },
    { L"Esc",    L"Back"        },
    { L"+/-",    L"Adjust"      },
    { L"F10",    L"Save & Exit" },
  };
  UINTN  Y    = mState.ScreenH - FooterH;
  UINTN  X    = 2 * GFX_PADDING_PX;
  UINTN  i;

  GfxFillRect (0, Y, mState.ScreenW, 1, GFX_PANEL_ALT);
  for (i = 0; i < sizeof (Hints) / sizeof (Hints[0]); i++) {
    DrawKeyBox (&X, Y, FooterH, Hints[i].Key, Hints[i].Label);
  }
}

STATIC
BOOLEAN
IsSubtitleOpcode (
  IN UINT8  OpCode
  )
{
  return (OpCode == EFI_IFR_SUBTITLE_OP);
}

STATIC
BOOLEAN
IsRefOpcode (
  IN UINT8  OpCode
  )
{
  return (OpCode == EFI_IFR_REF_OP);
}

//
// Render the right-column value for a question opcode. Phase 3a covers the
// most common types (boolean, 8/16/32/64-bit numerics, oneof — by looking up
// the option whose value matches CurrentValue). Strings/passwords/dates/times
// render a placeholder until Phase 3b/3c.
//

STATIC
BOOLEAN
HiiValuesEqual (
  IN UINT8                Type,
  IN EFI_IFR_TYPE_VALUE   *A,
  IN EFI_IFR_TYPE_VALUE   *B
  )
{
  switch (Type) {
    case EFI_IFR_TYPE_NUM_SIZE_8:  return A->u8  == B->u8;
    case EFI_IFR_TYPE_NUM_SIZE_16: return A->u16 == B->u16;
    case EFI_IFR_TYPE_NUM_SIZE_32: return A->u32 == B->u32;
    case EFI_IFR_TYPE_NUM_SIZE_64: return A->u64 == B->u64;
    case EFI_IFR_TYPE_BOOLEAN:     return A->b   == B->b;
    default:                       return FALSE;
  }
}

STATIC
VOID
FormatValueText (
  IN  FORM_DISPLAY_ENGINE_STATEMENT  *Stmt,
  IN  EFI_HII_HANDLE                 Hii,
  OUT CHAR16                         *Buf,
  IN  UINTN                          BufSize
  )
{
  Buf[0] = L'\0';

  if ((Stmt == NULL) || (Stmt->OpCode == NULL)) {
    return;
  }

  switch (Stmt->OpCode->OpCode) {
    case EFI_IFR_CHECKBOX_OP:
      StrCpyS (Buf, BufSize / sizeof (CHAR16),
               Stmt->CurrentValue.Value.b ? L"[X]" : L"[ ]");
      return;

    case EFI_IFR_NUMERIC_OP: {
      UINT64  Val = 0;
      switch (Stmt->CurrentValue.Type) {
        case EFI_IFR_TYPE_NUM_SIZE_8:  Val = Stmt->CurrentValue.Value.u8;  break;
        case EFI_IFR_TYPE_NUM_SIZE_16: Val = Stmt->CurrentValue.Value.u16; break;
        case EFI_IFR_TYPE_NUM_SIZE_32: Val = Stmt->CurrentValue.Value.u32; break;
        case EFI_IFR_TYPE_NUM_SIZE_64: Val = Stmt->CurrentValue.Value.u64; break;
      }
      UnicodeSPrint (Buf, BufSize, L"[%lu]", Val);
      return;
    }

    case EFI_IFR_ONE_OF_OP: {
      LIST_ENTRY               *Link;
      DISPLAY_QUESTION_OPTION  *Opt;
      EFI_IFR_ONE_OF_OPTION    *OptOp;
      CHAR16                   *Str;

      for (Link = GetFirstNode (&Stmt->OptionListHead);
           !IsNull (&Stmt->OptionListHead, Link);
           Link = GetNextNode (&Stmt->OptionListHead, Link))
      {
        Opt   = DISPLAY_QUESTION_OPTION_FROM_LINK (Link);
        OptOp = Opt->OptionOpCode;
        if (OptOp == NULL) {
          continue;
        }
        if (HiiValuesEqual (Stmt->CurrentValue.Type, &OptOp->Value, &Stmt->CurrentValue.Value)) {
          Str = HiiGetString (Hii, OptOp->Option, NULL);
          UnicodeSPrint (Buf, BufSize, L"<%s>", Str != NULL ? Str : L"?");
          if (Str != NULL) {
            FreePool (Str);
          }
          return;
        }
      }
      StrCpyS (Buf, BufSize / sizeof (CHAR16), L"<unknown>");
      return;
    }

    case EFI_IFR_STRING_OP:
      if ((Stmt->CurrentValue.Buffer != NULL) && (Stmt->CurrentValue.BufferLen > 0)) {
        UnicodeSPrint (
          Buf, BufSize, L"\"%s\"",
          (CHAR16 *)Stmt->CurrentValue.Buffer
          );
      } else {
        StrCpyS (Buf, BufSize / sizeof (CHAR16), L"\"\"");
      }
      return;

    case EFI_IFR_PASSWORD_OP:
      // Always mask — never show the underlying string.
      if ((Stmt->CurrentValue.Buffer != NULL) && (Stmt->CurrentValue.BufferLen > sizeof (CHAR16))) {
        StrCpyS (Buf, BufSize / sizeof (CHAR16), L"<set>");
      } else {
        StrCpyS (Buf, BufSize / sizeof (CHAR16), L"<not set>");
      }
      return;

    case EFI_IFR_DATE_OP:
      UnicodeSPrint (
        Buf, BufSize,
        L"%04u-%02u-%02u",
        (UINT32)Stmt->CurrentValue.Value.date.Year,
        (UINT32)Stmt->CurrentValue.Value.date.Month,
        (UINT32)Stmt->CurrentValue.Value.date.Day
        );
      return;

    case EFI_IFR_TIME_OP:
      UnicodeSPrint (
        Buf, BufSize,
        L"%02u:%02u:%02u",
        (UINT32)Stmt->CurrentValue.Value.time.Hour,
        (UINT32)Stmt->CurrentValue.Value.time.Minute,
        (UINT32)Stmt->CurrentValue.Value.time.Second
        );
      return;

    default:
      return;
  }
}

// Layout cached at end of every full RenderForm so partial-redraw paths
// (Up/Down navigation) don't have to re-derive the same numbers per
// keystroke. Geometry only changes when the resolution changes, which
// happens between FormDisplay calls — never inside the input loop.
STATIC UINTN  mListX, mListY, mListW, mListH, mLineH;

// Scrolling viewport — items below mFirstVisible are off-screen above the
// list, items past (mFirstVisible + VisibleRowCount) are off-screen below.
// Auto-adjusted to keep the highlighted row in view.
STATIC UINTN  mFirstVisible = 0;

STATIC
UINTN
VisibleRowCount (
  VOID
  )
{
  if (mLineH == 0) {
    return 0;
  }
  return (mListH > GFX_PADDING_PX) ? (mListH - GFX_PADDING_PX) / mLineH : 0;
}

STATIC
VOID
AdjustViewport (
  IN INTN   Highlight,
  IN UINTN  Count
  )
{
  UINTN  VRows;
  UINTN  MaxFirst;

  VRows = VisibleRowCount ();
  if (VRows == 0) {
    return;
  }
  MaxFirst = (Count > VRows) ? Count - VRows : 0;

  if (mFirstVisible > MaxFirst) {
    mFirstVisible = MaxFirst;
  }
  if (Highlight < 0) {
    return;
  }
  if ((UINTN)Highlight < mFirstVisible) {
    mFirstVisible = (UINTN)Highlight;
  } else if ((UINTN)Highlight >= mFirstVisible + VRows) {
    mFirstVisible = (UINTN)Highlight - VRows + 1;
  }
}

// Pick a status color for a value string by sniffing well-known tokens.
// Doesn't catch every case but handles the bread-and-butter Enabled/Disabled
// rows that dominate Dasharo's setup.
STATIC
VOID
SetValueStatusColor (
  IN CONST CHAR16  *Value,
  IN BOOLEAN       IsHighlighted
  )
{
  if ((StrStr (Value, L"[X]")     != NULL) ||
      (StrStr (Value, L"Enabled") != NULL))
  {
    GfxSetTextColor (TC_TEAL);
  } else if ((StrStr (Value, L"[ ]")      != NULL) ||
             (StrStr (Value, L"Disabled") != NULL))
  {
    GfxSetTextColor (TC_AMBER);
  } else if (IsHighlighted) {
    GfxSetTextColor (TC_WHITE);
  } else {
    GfxSetTextColor (TC_DIM);
  }
}

STATIC
VOID
DrawItemRow (
  IN ITEM            *Items,
  IN UINTN           Index,
  IN BOOLEAN         IsHighlighted,
  IN EFI_HII_HANDLE  Hii,
  IN UINTN           RowY
  )
{
  UINT8   Op;
  CHAR16  *Title;
  CHAR16  *Help;
  UINTN   TitleY;
  UINTN   HelpY;
  UINTN   ValueY;

  Op    = Items[Index].Stmt->OpCode->OpCode;
  Title = HiiGetString (Hii, Items[Index].Prompt, NULL);
  Help  = (Items[Index].Help != 0) ? HiiGetString (Hii, Items[Index].Help, NULL) : NULL;

  // Compute layout points within the row. Title sits a few pixels below
  // the highlight bg's top edge; help has clear space below it before the
  // next row's highlight starts; value/chevron is on the row midline so
  // it visually sits in the middle of the highlight regardless of how
  // many lines of body text the row has.
  TitleY = RowY + GFX_HIGHLIGHT_INSET_PX + 4;
  HelpY  = TitleY + mState.FontPx + 5;
  ValueY = RowY + (mLineH - mState.FontPx) / 2;

  // Reserve a thin strip on the right for the scroll indicator so it
  // doesn't get painted over when we clear / highlight a row.
  UINTN  RowW = mListW - GFX_SCROLLBAR_RESERVE_PX;

  GfxFillRect (mListX, RowY, RowW, mLineH, GFX_BG);

  // Always start each row in a known text color. Prior paths may exit
  // with TC_DIM or TC_AMBER set if the row before had a status pill;
  // without this reset, the title would inherit that color.
  GfxSetTextColor (TC_WHITE);

  if (IsSubtitleOpcode (Op)) {
    // Subtitle: section-header style, vertically centered in the row.
    if ((Title != NULL) && (Title[0] != L'\0')) {
      GfxSetTextColor (TC_DIM);
      GfxDrawTextW (
        mListX + GFX_PADDING_PX,
        RowY + (mLineH - mState.FontPx) / 2,
        Title
        );
      GfxSetTextColor (TC_WHITE);
    }
  } else {
    CHAR16  ValueBuf[128];

    if (IsHighlighted) {
      // Inset the highlight bg from the top and bottom of the row to give
      // a visible gap between rows. Also leave the scrollbar strip clear.
      // Unselectable rows (text labels, grayed-out questions) get a dimmer
      // highlight to communicate "you're here but Enter won't do anything".
      GfxFillRect (
        mListX,
        RowY + GFX_HIGHLIGHT_INSET_PX,
        RowW,
        mLineH - 2 * GFX_HIGHLIGHT_INSET_PX,
        Items[Index].Selectable ? GFX_HIGHLIGHT : GFX_HIGHLIGHT_DIM
        );
    }

    // Title (top line).
    if (Title != NULL) {
      GfxDrawTextW (mListX + 2 * GFX_PADDING_PX, TitleY, Title);
    }

    // Help (second line, dim, clipped if too wide).
    if ((Help != NULL) && (Help[0] != L'\0')) {
      if (IsHighlighted) {
        GfxSetTextColor (TC_WHITE);
      } else {
        GfxSetTextColor (TC_DIM);
      }
      GfxDrawTextWClipped (
        mListX + 2 * GFX_PADDING_PX,
        HelpY,
        mListW - 4 * GFX_PADDING_PX,
        Help
        );
      GfxSetTextColor (TC_WHITE);
    }

    // Right column: chevron for REF, color-coded value for editable
    // questions. Sits on the row's vertical midline so single-glyph
    // values like [X] don't visually anchor to the title line.
    if (IsRefOpcode (Op)) {
      GfxSetTextColor (TC_TEAL);
      GfxDrawTextW (
        mListX + mListW - 3 * GFX_PADDING_PX,
        ValueY,
        L"\x203A"
        );
      GfxSetTextColor (TC_WHITE);
    } else {
      FormatValueText (Items[Index].Stmt, Hii, ValueBuf, sizeof (ValueBuf));
      if (ValueBuf[0] != L'\0') {
        SetValueStatusColor (ValueBuf, IsHighlighted);
        GfxDrawTextWRightAligned (
          mListX + mListW - 2 * GFX_PADDING_PX,
          ValueY,
          ValueBuf
          );
        GfxSetTextColor (TC_WHITE);
      }
    }
  }

  if (Title != NULL) FreePool (Title);
  if (Help  != NULL) FreePool (Help);
}

// Returns TRUE and writes RowY when the given item index is currently
// inside the viewport; FALSE otherwise. Encapsulates both "above the
// scroll position" and "off the bottom" exclusions.
STATIC
BOOLEAN
GetRowY (
  IN  UINTN  Index,
  OUT UINTN  *RowY
  )
{
  UINTN  Y;

  if (Index < mFirstVisible) {
    return FALSE;
  }
  Y = mListY + GFX_PADDING_PX + (Index - mFirstVisible) * mLineH;
  if (Y + mLineH > mListY + mListH) {
    return FALSE;
  }
  *RowY = Y;
  return TRUE;
}

STATIC
VOID
DrawScrollIndicator (
  IN UINTN  ItemCount
  )
{
  UINTN  VRows = VisibleRowCount ();
  UINTN  BarX, BarY, BarH;
  UINTN  ThumbY, ThumbH;

  if ((VRows == 0) || (ItemCount == 0) || (VRows >= ItemCount)) {
    return;
  }

  BarX = mListX + mListW - 4;
  BarY = mListY + GFX_PADDING_PX;
  BarH = VRows * mLineH;

  GfxFillRect (BarX, BarY, 2, BarH, GFX_PANEL_ALT);

  ThumbH = BarH * VRows / ItemCount;
  if (ThumbH < 12) {
    ThumbH = 12;
  }
  ThumbY = BarY + (BarH - ThumbH) * mFirstVisible / (ItemCount - VRows);
  GfxFillRect (BarX, ThumbY, 2, ThumbH, GFX_ACCENT);
}

STATIC
VOID
RenderItems (
  IN ITEM           *Items,
  IN UINTN          ItemCount,
  IN INTN           HighlightIndex,
  IN EFI_HII_HANDLE Hii,
  IN UINTN          ListX,
  IN UINTN          ListY,
  IN UINTN          ListW,
  IN UINTN          ListH
  )
{
  UINTN  i;
  UINTN  y;

  // Cache the geometry so partial redraws can address rows without
  // recomputing. Row height has to fit:
  //   - top inset + title + gap + help + bottom inset
  // which at FontPx=12 wants ~43 px (3.6× font); the highlight bg insets
  // 3 px on top and bottom so adjacent rows have a visible gap.
  mListX = ListX;
  mListY = ListY;
  mListW = ListW;
  mListH = ListH;
  mLineH = mState.FontPx * 36 / 10;

  AdjustViewport (HighlightIndex, ItemCount);

  for (i = mFirstVisible; i < ItemCount; i++) {
    if (!GetRowY (i, &y)) {
      break;
    }
    DrawItemRow (Items, i, (INTN)i == HighlightIndex, Hii, y);
  }

  DrawScrollIndicator (ItemCount);
}

// Partial redraw on highlight change — re-paints only the two affected rows.
// This is the hot path for Up/Down navigation; doing a full RenderForm
// here was the main source of perceived lag.
STATIC
VOID
RedrawHighlightChange (
  IN FORM_DISPLAY_ENGINE_FORM  *Form,
  IN ITEM                      *Items,
  IN UINTN                     ItemCount,
  IN INTN                      OldIndex,
  IN INTN                      NewIndex
  )
{
  UINTN  RowY;

  if (mState.Gop == NULL) {
    return;
  }

  if ((OldIndex >= 0) && ((UINTN)OldIndex < ItemCount)) {
    if (GetRowY ((UINTN)OldIndex, &RowY)) {
      DrawItemRow (Items, (UINTN)OldIndex, FALSE, Form->HiiHandle, RowY);
    }
  }
  if ((NewIndex >= 0) && ((UINTN)NewIndex < ItemCount)) {
    if (GetRowY ((UINTN)NewIndex, &RowY)) {
      DrawItemRow (Items, (UINTN)NewIndex, TRUE, Form->HiiHandle, RowY);
    }
  }
}

// Re-renders the entire list area — used when the viewport scrolls (the
// rows shift positions, so all visible items need to be redrawn from
// scratch). Caller has already called AdjustViewport to set mFirstVisible.
STATIC
VOID
RedrawListArea (
  IN ITEM           *Items,
  IN UINTN          ItemCount,
  IN INTN           Highlight,
  IN EFI_HII_HANDLE Hii
  )
{
  UINTN  i;
  UINTN  RowY;

  if (mState.Gop == NULL) {
    return;
  }

  GfxFillRect (mListX, mListY, mListW, mListH, GFX_BG);
  for (i = mFirstVisible; i < ItemCount; i++) {
    if (!GetRowY (i, &RowY)) {
      break;
    }
    DrawItemRow (Items, i, (INTN)i == Highlight, Hii, RowY);
  }
  DrawScrollIndicator (ItemCount);
}

STATIC
VOID
MirrorHighlightChange (
  IN ITEM            *Items,
  IN UINTN           NewIndex,
  IN EFI_HII_HANDLE  Hii
  )
{
  CHAR16   Buf[256];
  CHAR16  *Str;

  if (mState.SerialCount == 0) {
    return;
  }
  Str = HiiGetString (Hii, Items[NewIndex].Prompt, NULL);
  UnicodeSPrint (Buf, sizeof (Buf), L"  > %s", Str != NULL ? Str : L"<?>");
  SerialOutLine (Buf);
  if (Str != NULL) {
    FreePool (Str);
  }
}

STATIC
VOID
MirrorToSerial (
  IN CONST CHAR16    *FormTitle,
  IN ITEM            *Items,
  IN UINTN           ItemCount,
  IN INTN            HighlightIndex,
  IN EFI_HII_HANDLE  Hii
  )
{
  CHAR16   Buf[320];
  CHAR16   *Str;
  UINTN    i;

  if (mState.SerialCount == 0) {
    return;
  }

  SerialOutLine (L"");
  SerialOutLine (L"---------------------------------------------------------------");
  UnicodeSPrint (Buf, sizeof (Buf), L"[%s]", FormTitle);
  SerialOutLine (Buf);
  for (i = 0; i < ItemCount; i++) {
    Str = HiiGetString (Hii, Items[i].Prompt, NULL);
    UnicodeSPrint (
      Buf, sizeof (Buf),
      L"  %s%s%s",
      (INTN)i == HighlightIndex ? L"> " : L"  ",
      Items[i].Selectable ? L"" : L"  ",  // visually distinguish labels
      Str != NULL ? Str : L"<?>"
      );
    SerialOutLine (Buf);
    if (Str != NULL) {
      FreePool (Str);
    }
  }
}

// System-info card used to give the Main form a dashboard-style header.
// Renders as a panel-coloured rectangle at the top of the content area
// containing key/value rows pulled from the cached SMBIOS data.
STATIC
VOID
DrawMainInfoCard (
  IN OUT UINTN  *ContentTopY,
  IN     UINTN  ContentX,
  IN     UINTN  ContentW
  )
{
  STATIC CONST struct {
    CONST CHAR16  *Label;
  } Rows[] = {
    { L"Platform"  },
    { L"Firmware"  },
    { L"Processor" },
    { L"Memory"    },
  };
  UINTN   CardX = ContentX;
  UINTN   CardY = *ContentTopY + GFX_PADDING_PX;
  UINTN   CardW = ContentW;
  UINTN   RowH  = mState.FontPx * 16 / 10;
  UINTN   HdrH  = RowH;
  UINTN   CardH = HdrH + RowH * 4 + 2 * GFX_PADDING_PX;
  UINTN   ValueX = CardX + CardW * 28 / 100;  // ~28% in for the value column
  CHAR16  RamBuf[16];
  CONST CHAR16  *Values[4];
  UINTN   i;
  UINTN   y;

  // Card body and header band.
  GfxFillRect (CardX, CardY, CardW, CardH, GFX_PANEL);
  GfxFillRect (CardX, CardY, CardW, HdrH,  GFX_PANEL_ALT);

  GfxSetTextColor (TC_DIM);
  GfxDrawTextW (
    CardX + 2 * GFX_PADDING_PX,
    CardY + (HdrH - mState.FontPx) / 2,
    L"SYSTEM"
    );

  // Build the value list.
  Values[0] = (mSysInfo.SystemProduct[0] != L'\0') ? mSysInfo.SystemProduct : L"-";
  Values[1] = (mSysInfo.BiosVersion[0]   != L'\0') ? mSysInfo.BiosVersion   : L"-";
  Values[2] = (mSysInfo.CpuBrand[0]      != L'\0') ? mSysInfo.CpuBrand      : L"-";
  if (mSysInfo.TotalRamMB > 0) {
    UnicodeSPrint (RamBuf, sizeof (RamBuf), L"%u GiB",
                   (UINT32)((mSysInfo.TotalRamMB + 1023) / 1024));
  } else {
    StrCpyS (RamBuf, ARRAY_SIZE (RamBuf), L"-");
  }
  Values[3] = RamBuf;

  for (i = 0; i < ARRAY_SIZE (Rows); i++) {
    y = CardY + HdrH + GFX_PADDING_PX + i * RowH + (RowH - mState.FontPx) / 2;
    GfxSetTextColor (TC_DIM);
    GfxDrawTextW (CardX + 2 * GFX_PADDING_PX, y, Rows[i].Label);
    GfxSetTextColor (TC_WHITE);
    GfxDrawTextWClipped (
      ValueX, y,
      CardX + CardW - ValueX - 2 * GFX_PADDING_PX,
      Values[i]
      );
  }
  GfxSetTextColor (TC_WHITE);

  *ContentTopY = CardY + CardH + GFX_PADDING_PX;
}

STATIC
VOID
RenderForm (
  IN FORM_DISPLAY_ENGINE_FORM  *Form,
  IN ITEM                      *Items,
  IN UINTN                     ItemCount,
  IN INTN                      HighlightIndex
  )
{
  CHAR16  *Title;
  CHAR16  *DisplayTitle;
  UINTN   HeaderH = (UINTN)mState.ScreenH * GFX_HEADER_PERCENT / 100;
  UINTN   TabBarH = (UINTN)mState.ScreenH * GFX_TABBAR_PERCENT / 100;
  UINTN   FooterH = (UINTN)mState.ScreenH * GFX_FOOTER_PERCENT / 100;
  UINTN   PageHdrH;
  UINTN   ContentY, ContentH;

  PageHdrH = mState.FontPx * GFX_PAGEHEADER_LINES + 2 * GFX_PADDING_PX;
  ContentY = HeaderH + TabBarH;
  ContentH = mState.ScreenH - ContentY - FooterH;

  Title        = HiiGetString (Form->HiiHandle, Form->FormTitle, NULL);
  DisplayTitle = (Title != NULL) ? Title : L"<no title>";

  if (mState.Gop != NULL) {
    UINTN  ItemsTopY = ContentY + PageHdrH;
    UINTN  ItemsX    = 2 * GFX_PADDING_PX;
    UINTN  ItemsW    = mState.ScreenW - 4 * GFX_PADDING_PX;
    UINTN  ItemsH;

    GfxClearScreen (GFX_BG);
    DrawHeader     (HeaderH);
    DrawTabBar     (HeaderH, TabBarH, Form);
    DrawPageHeader (ContentY, PageHdrH, Form);

    // On the Main form, prepend a system-info card at the top of the
    // content area; the items list takes whatever's left below.
    if (IsMainForm (Form)) {
      DrawMainInfoCard (&ItemsTopY, ItemsX, ItemsW);
    }

    ItemsH = (ContentY + ContentH > ItemsTopY) ? ContentY + ContentH - ItemsTopY : 0;
    RenderItems (Items, ItemCount, HighlightIndex, Form->HiiHandle,
                 ItemsX, ItemsTopY, ItemsW, ItemsH);
    DrawFooter (FooterH);
  }
  MirrorToSerial (DisplayTitle, Items, ItemCount, HighlightIndex, Form->HiiHandle);

  if (Title != NULL) {
    FreePool (Title);
  }
}

//
// Edit handlers — Phase 3.
//
// Checkbox toggles inline (no popup); one-of opens a centered picker popup.
// Numeric / string / date / time still pass the statement back to the
// browser — Phase 3c will handle them with input-box popups.
//

#define MAX_ONEOF_OPTS  32

typedef struct {
  EFI_IFR_ONE_OF_OPTION    *Op;
  EFI_STRING_ID             Prompt;
} ONEOF_OPT;

STATIC
UINTN
CollectOneOfOptions (
  IN  FORM_DISPLAY_ENGINE_STATEMENT  *Stmt,
  OUT ONEOF_OPT                      *Opts,
  IN  UINTN                          MaxOpts
  )
{
  LIST_ENTRY               *Link;
  DISPLAY_QUESTION_OPTION  *Opt;
  UINTN                    Count = 0;

  for (Link = GetFirstNode (&Stmt->OptionListHead);
       !IsNull (&Stmt->OptionListHead, Link) && (Count < MaxOpts);
       Link = GetNextNode (&Stmt->OptionListHead, Link))
  {
    Opt = DISPLAY_QUESTION_OPTION_FROM_LINK (Link);
    if (Opt->OptionOpCode == NULL) {
      continue;
    }
    Opts[Count].Op     = Opt->OptionOpCode;
    Opts[Count].Prompt = Opt->OptionOpCode->Option;
    Count++;
  }
  return Count;
}

STATIC
INTN
FindCurrentOneOfIndex (
  IN FORM_DISPLAY_ENGINE_STATEMENT  *Stmt,
  IN ONEOF_OPT                      *Opts,
  IN UINTN                          Count
  )
{
  UINTN  i;

  for (i = 0; i < Count; i++) {
    if (HiiValuesEqual (Stmt->CurrentValue.Type, &Opts[i].Op->Value, &Stmt->CurrentValue.Value)) {
      return (INTN)i;
    }
  }
  return -1;
}

STATIC
VOID
DrawOneOfPopup (
  IN CONST CHAR16     *Title,
  IN ONEOF_OPT        *Opts,
  IN UINTN            Count,
  IN INTN             Highlight,
  IN EFI_HII_HANDLE   Hii
  )
{
  UINTN   LineH;
  UINTN   PopupW, PopupH;
  UINTN   PopupX, PopupY;
  UINTN   y;
  UINTN   i;
  CHAR16  *Str;

  LineH = mState.FontPx * 16 / 10;

  // Width: capped at 60% of screen, height: title + count + footer rows.
  PopupW = (UINTN)mState.ScreenW * 60 / 100;
  PopupH = LineH * (Count + 4) + 2 * GFX_PADDING_PX;
  if (PopupH > (UINTN)mState.ScreenH * 80 / 100) {
    PopupH = (UINTN)mState.ScreenH * 80 / 100;
  }
  PopupX = (mState.ScreenW - PopupW) / 2;
  PopupY = (mState.ScreenH - PopupH) / 2;

  // 2 px accent border + filled body.
  GfxFillRect (PopupX - 2, PopupY - 2, PopupW + 4, PopupH + 4, GFX_ACCENT);
  GfxFillRect (PopupX,     PopupY,     PopupW,     PopupH,     GFX_PANEL);

  // Title row.
  GfxFillRect (PopupX, PopupY, PopupW, LineH, GFX_PANEL_ALT);
  GfxSetTextColor (TC_TEAL);
  GfxDrawTextWClipped (
    PopupX + 2 * GFX_PADDING_PX,
    PopupY + (LineH - mState.FontPx) / 2,
    PopupW - 4 * GFX_PADDING_PX,
    Title
    );
  GfxSetTextColor (TC_WHITE);

  // Options.
  y = PopupY + LineH + GFX_PADDING_PX;
  for (i = 0; i < Count; i++) {
    if (y + LineH > PopupY + PopupH - LineH) {
      break;  // simple clip; scrolling is Phase 5 polish.
    }
    if ((INTN)i == Highlight) {
      GfxFillRect (PopupX + GFX_PADDING_PX, y, PopupW - 2 * GFX_PADDING_PX, LineH, GFX_HIGHLIGHT);
    }
    Str = HiiGetString (Hii, Opts[i].Prompt, NULL);
    if (Str != NULL) {
      GfxDrawTextWClipped (
        PopupX + 3 * GFX_PADDING_PX,
        y + (LineH - mState.FontPx) / 2,
        PopupW - 6 * GFX_PADDING_PX,
        Str
        );
      FreePool (Str);
    }
    y += LineH;
  }

  // Footer hint.
  GfxDrawTextW (
    PopupX + 2 * GFX_PADDING_PX,
    PopupY + PopupH - LineH + (LineH - mState.FontPx) / 2,
    L"Enter Select    Esc Cancel"
    );
}

STATIC
VOID
MirrorOneOfPopup (
  IN CONST CHAR16    *Title,
  IN ONEOF_OPT       *Opts,
  IN UINTN           Count,
  IN INTN            Highlight,
  IN EFI_HII_HANDLE  Hii
  )
{
  CHAR16   Buf[256];
  UINTN    i;
  CHAR16  *Str;

  if (mState.SerialCount == 0) {
    return;
  }
  SerialOutLine (L"");
  UnicodeSPrint (Buf, sizeof (Buf), L"  Select: %s", Title);
  SerialOutLine (Buf);
  for (i = 0; i < Count; i++) {
    Str = HiiGetString (Hii, Opts[i].Prompt, NULL);
    UnicodeSPrint (
      Buf, sizeof (Buf),
      L"    %s%s",
      (INTN)i == Highlight ? L"> " : L"  ",
      Str != NULL ? Str : L"<?>"
      );
    SerialOutLine (Buf);
    if (Str != NULL) {
      FreePool (Str);
    }
  }
}

// Returns TRUE if the user committed a choice (Result filled), FALSE if Esc.
STATIC
BOOLEAN
RunOneOfPopup (
  IN  FORM_DISPLAY_ENGINE_FORM       *Form,
  IN  FORM_DISPLAY_ENGINE_STATEMENT  *Stmt,
  OUT USER_INPUT                     *Result
  )
{
  ONEOF_OPT      Opts[MAX_ONEOF_OPTS];
  UINTN          Count;
  INTN           Highlight;
  EFI_INPUT_KEY  Key;
  UINTN          Idx;
  CHAR16         *Title;

  Count = CollectOneOfOptions (Stmt, Opts, MAX_ONEOF_OPTS);
  if (Count == 0) {
    return FALSE;
  }
  Highlight = FindCurrentOneOfIndex (Stmt, Opts, Count);
  if (Highlight < 0) {
    Highlight = 0;
  }

  Title = HiiGetString (
            Form->HiiHandle,
            ((EFI_IFR_STATEMENT_HEADER *)(Stmt->OpCode + 1))->Prompt,
            NULL
            );

  for ( ; ;) {
    if (mState.Gop != NULL) {
      DrawOneOfPopup (
        Title != NULL ? Title : L"Select",
        Opts, Count, Highlight, Form->HiiHandle
        );
    }
    MirrorOneOfPopup (
      Title != NULL ? Title : L"Select",
      Opts, Count, Highlight, Form->HiiHandle
      );

    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Idx);
    if (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key) != EFI_SUCCESS) {
      continue;
    }

    if (Key.ScanCode == SCAN_ESC) {
      if (Title != NULL) {
        FreePool (Title);
      }
      return FALSE;
    }
    if (Key.ScanCode == SCAN_UP) {
      if (Highlight > 0) {
        Highlight--;
      } else {
        Highlight = (INTN)Count - 1;
      }
      continue;
    }
    if (Key.ScanCode == SCAN_DOWN) {
      Highlight++;
      if ((UINTN)Highlight >= Count) {
        Highlight = 0;
      }
      continue;
    }
    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
      Result->SelectedStatement   = Stmt;
      Result->InputValue.Type     = Stmt->CurrentValue.Type;
      Result->InputValue.Value    = Opts[Highlight].Op->Value;
      Result->InputValue.Buffer   = NULL;
      Result->InputValue.BufferLen = 0;
      if (Title != NULL) {
        FreePool (Title);
      }
      return TRUE;
    }
  }
}

//
// Numeric input popup — Phase 3c.
//
// The numeric opcode encodes Min / Max / Step in different sizes (1/2/4/8
// bytes) selected by the EFI_IFR_NUMERIC_SIZE flag bits. Up / + bumps by
// step; Down / - decrements by step; Enter commits, Esc cancels. All math
// is done in UINT64 with explicit overflow / underflow clamps.
//

STATIC
VOID
NumericConfig (
  IN  FORM_DISPLAY_ENGINE_STATEMENT  *Stmt,
  OUT UINT8                          *Size,
  OUT UINT64                         *MinV,
  OUT UINT64                         *MaxV,
  OUT UINT64                         *Step
  )
{
  EFI_IFR_NUMERIC  *N = (EFI_IFR_NUMERIC *)Stmt->OpCode;

  switch (N->Flags & EFI_IFR_NUMERIC_SIZE) {
    case EFI_IFR_NUMERIC_SIZE_1:
      *Size = 1; *MinV = N->data.u8.MinValue;  *MaxV = N->data.u8.MaxValue;  *Step = N->data.u8.Step;  break;
    case EFI_IFR_NUMERIC_SIZE_2:
      *Size = 2; *MinV = N->data.u16.MinValue; *MaxV = N->data.u16.MaxValue; *Step = N->data.u16.Step; break;
    case EFI_IFR_NUMERIC_SIZE_4:
      *Size = 4; *MinV = N->data.u32.MinValue; *MaxV = N->data.u32.MaxValue; *Step = N->data.u32.Step; break;
    case EFI_IFR_NUMERIC_SIZE_8:
      *Size = 8; *MinV = N->data.u64.MinValue; *MaxV = N->data.u64.MaxValue; *Step = N->data.u64.Step; break;
    default:
      *Size = 1; *MinV = 0; *MaxV = 0xFF; *Step = 1; break;
  }
  if (*Step == 0) {
    *Step = 1;
  }
}

STATIC
UINT64
NumericCurrent (
  IN FORM_DISPLAY_ENGINE_STATEMENT  *Stmt
  )
{
  switch (Stmt->CurrentValue.Type) {
    case EFI_IFR_TYPE_NUM_SIZE_8:  return Stmt->CurrentValue.Value.u8;
    case EFI_IFR_TYPE_NUM_SIZE_16: return Stmt->CurrentValue.Value.u16;
    case EFI_IFR_TYPE_NUM_SIZE_32: return Stmt->CurrentValue.Value.u32;
    case EFI_IFR_TYPE_NUM_SIZE_64: return Stmt->CurrentValue.Value.u64;
    default:                       return 0;
  }
}

STATIC
VOID
NumericSetResult (
  IN  UINT8       Size,
  IN  UINT64      Val,
  IN  UINT8       Type,
  OUT USER_INPUT  *Result
  )
{
  Result->InputValue.Type      = Type;
  Result->InputValue.Buffer    = NULL;
  Result->InputValue.BufferLen = 0;
  switch (Size) {
    case 1: Result->InputValue.Value.u8  = (UINT8) Val; break;
    case 2: Result->InputValue.Value.u16 = (UINT16)Val; break;
    case 4: Result->InputValue.Value.u32 = (UINT32)Val; break;
    case 8: Result->InputValue.Value.u64 =         Val; break;
  }
}

STATIC
VOID
DrawNumericPopup (
  IN CONST CHAR16  *Title,
  IN UINT64        Value,
  IN UINT64        MinV,
  IN UINT64        MaxV
  )
{
  UINTN   LineH = mState.FontPx * 16 / 10;
  UINTN   PopupW = (UINTN)mState.ScreenW * 50 / 100;
  UINTN   PopupH = LineH * 5 + 2 * GFX_PADDING_PX;
  UINTN   PopupX = (mState.ScreenW - PopupW) / 2;
  UINTN   PopupY = (mState.ScreenH - PopupH) / 2;
  CHAR16  Buf[128];

  GfxFillRect (PopupX - 2, PopupY - 2, PopupW + 4, PopupH + 4, GFX_ACCENT);
  GfxFillRect (PopupX,     PopupY,     PopupW,     PopupH,     GFX_PANEL);

  // Title row.
  GfxFillRect (PopupX, PopupY, PopupW, LineH, GFX_PANEL_ALT);
  GfxSetTextColor (TC_TEAL);
  GfxDrawTextWClipped (
    PopupX + 2 * GFX_PADDING_PX,
    PopupY + (LineH - mState.FontPx) / 2,
    PopupW - 4 * GFX_PADDING_PX,
    Title
    );
  GfxSetTextColor (TC_WHITE);

  // Big centered value.
  UnicodeSPrint (Buf, sizeof (Buf), L"%lu", Value);
  GfxDrawTextWCentered (PopupX + PopupW / 2, PopupY + LineH + GFX_PADDING_PX, Buf);

  // Range hint.
  UnicodeSPrint (Buf, sizeof (Buf), L"Range  %lu \x2026 %lu", MinV, MaxV);
  GfxDrawTextWCentered (PopupX + PopupW / 2, PopupY + LineH * 2 + GFX_PADDING_PX, Buf);

  // Footer.
  GfxDrawTextW (
    PopupX + 2 * GFX_PADDING_PX,
    PopupY + PopupH - LineH + (LineH - mState.FontPx) / 2,
    L"\x2191\x2193 +/- Adjust    Enter Save    Esc Cancel"
    );
}

STATIC
BOOLEAN
RunNumericPopup (
  IN  FORM_DISPLAY_ENGINE_FORM       *Form,
  IN  FORM_DISPLAY_ENGINE_STATEMENT  *Stmt,
  OUT USER_INPUT                     *Result
  )
{
  UINT8          Size;
  UINT64         MinV, MaxV, Step;
  UINT64         Cur;
  UINT64         New;
  EFI_INPUT_KEY  Key;
  UINTN          Idx;
  CHAR16         *Title;
  CHAR16         Mirror[160];

  NumericConfig (Stmt, &Size, &MinV, &MaxV, &Step);
  Cur = NumericCurrent (Stmt);
  if (Cur < MinV) Cur = MinV;
  if (Cur > MaxV) Cur = MaxV;

  Title = HiiGetString (
            Form->HiiHandle,
            ((EFI_IFR_STATEMENT_HEADER *)(Stmt->OpCode + 1))->Prompt,
            NULL
            );

  for ( ; ;) {
    if (mState.Gop != NULL) {
      DrawNumericPopup (Title != NULL ? Title : L"Edit", Cur, MinV, MaxV);
    }
    if (mState.SerialCount > 0) {
      UnicodeSPrint (
        Mirror, sizeof (Mirror),
        L"  Edit %s: %lu  [%lu .. %lu]  step %lu",
        Title != NULL ? Title : L"value", Cur, MinV, MaxV, Step
        );
      SerialOutLine (Mirror);
    }

    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Idx);
    if (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key) != EFI_SUCCESS) {
      continue;
    }

    if (Key.ScanCode == SCAN_ESC) {
      if (Title != NULL) FreePool (Title);
      return FALSE;
    }
    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
      Result->SelectedStatement = Stmt;
      NumericSetResult (Size, Cur, Stmt->CurrentValue.Type, Result);
      if (Title != NULL) FreePool (Title);
      return TRUE;
    }
    if ((Key.ScanCode == SCAN_UP) || (Key.UnicodeChar == L'+')) {
      New = Cur + Step;
      if ((New < Cur) || (New > MaxV)) {
        New = MaxV;
      }
      Cur = New;
      continue;
    }
    if ((Key.ScanCode == SCAN_DOWN) || (Key.UnicodeChar == L'-')) {
      Cur = (Cur >= MinV + Step) ? Cur - Step : MinV;
      continue;
    }
    // Direct digit type-in. Each digit shifts the current value left and
    // appends — gets clamped to MaxV automatically. Backspace divides by 10.
    if ((Key.UnicodeChar >= L'0') && (Key.UnicodeChar <= L'9')) {
      UINT64  Digit = (UINT64)(Key.UnicodeChar - L'0');
      New = Cur * 10 + Digit;
      if ((New / 10) != Cur) {  // overflow
        New = MaxV;
      } else if (New > MaxV) {
        New = MaxV;
      } else if (New < MinV) {
        New = MinV;
      }
      Cur = New;
      continue;
    }
    if (Key.UnicodeChar == CHAR_BACKSPACE) {
      Cur = Cur / 10;
      if (Cur < MinV) {
        Cur = MinV;
      }
      continue;
    }
  }
}

//
// String / password / date / time edit popups — Phase 3c remainder.
//
// Each popup follows the same chrome (centered, accent border, title row,
// footer hint band) so the framing helper is shared. The body and key
// handling differ per type.
//

STATIC
VOID
DrawPopupFrame (
  IN  CONST CHAR16  *Title,
  IN  UINTN         PopupX,
  IN  UINTN         PopupY,
  IN  UINTN         PopupW,
  IN  UINTN         PopupH
  )
{
  UINTN  TitleH = mState.FontPx * 16 / 10;

  GfxFillRect (PopupX - 2, PopupY - 2, PopupW + 4, PopupH + 4, GFX_ACCENT);
  GfxFillRect (PopupX,     PopupY,     PopupW,     PopupH,     GFX_PANEL);
  GfxFillRect (PopupX,     PopupY,     PopupW,     TitleH,     GFX_PANEL_ALT);

  GfxSetTextColor (TC_TEAL);
  GfxDrawTextWClipped (
    PopupX + 2 * GFX_PADDING_PX,
    PopupY + (TitleH - mState.FontPx) / 2,
    PopupW - 4 * GFX_PADDING_PX,
    Title
    );
  GfxSetTextColor (TC_WHITE);
}

STATIC
VOID
DrawPopupFooter (
  IN UINTN         PopupX,
  IN UINTN         PopupY,
  IN UINTN         PopupW,
  IN UINTN         PopupH,
  IN CONST CHAR16  *Hint
  )
{
  UINTN  LineH = mState.FontPx * 16 / 10;

  GfxSetTextColor (TC_DIM);
  GfxDrawTextW (
    PopupX + 2 * GFX_PADDING_PX,
    PopupY + PopupH - LineH + (LineH - mState.FontPx) / 2,
    Hint
    );
  GfxSetTextColor (TC_WHITE);
}

//
// String / password popup
//

STATIC
VOID
DrawStringPopup (
  IN CONST CHAR16  *Title,
  IN CONST CHAR16  *Buf,
  IN UINTN         Len,
  IN BOOLEAN       IsPassword
  )
{
  UINTN   PopupW  = (UINTN)mState.ScreenW * 60 / 100;
  UINTN   LineH   = mState.FontPx * 16 / 10;
  UINTN   PopupH  = LineH * 4 + 4 * GFX_PADDING_PX;
  UINTN   PopupX  = (mState.ScreenW - PopupW) / 2;
  UINTN   PopupY  = (mState.ScreenH - PopupH) / 2;
  UINTN   BoxX, BoxY, BoxW, BoxH;
  UINTN   TextX, TextY;
  CHAR16  Display[256];
  UINTN   i;
  UINTN   TextW;

  DrawPopupFrame (Title, PopupX, PopupY, PopupW, PopupH);

  // Render box.
  BoxX = PopupX + 2 * GFX_PADDING_PX;
  BoxY = PopupY + LineH + 2 * GFX_PADDING_PX;
  BoxW = PopupW - 4 * GFX_PADDING_PX;
  BoxH = mState.FontPx + 2 * GFX_PADDING_PX;
  GfxDrawRectOutline (BoxX, BoxY, BoxW, BoxH, GFX_TEXT_DIM);

  // Compose displayed string (mask for passwords).
  if (IsPassword) {
    UINTN  Cap = sizeof (Display) / sizeof (CHAR16) - 1;
    if (Len > Cap) {
      Len = Cap;
    }
    for (i = 0; i < Len; i++) {
      Display[i] = L'*';
    }
    Display[Len] = L'\0';
  } else {
    StrnCpyS (Display, sizeof (Display) / sizeof (CHAR16), Buf, Len);
    Display[Len] = L'\0';
  }

  TextX = BoxX + GFX_PADDING_PX;
  TextY = BoxY + (BoxH - mState.FontPx) / 2;
  GfxDrawTextWClipped (TextX, TextY, BoxW - 3 * GFX_PADDING_PX, Display);

  // Cursor — vertical accent bar just past the text.
  TextW = GfxMeasureTextW (Display);
  if (TextX + TextW + 2 < BoxX + BoxW - GFX_PADDING_PX) {
    GfxFillRect (TextX + TextW + 1, TextY, 2, mState.FontPx, GFX_ACCENT);
  }

  DrawPopupFooter (
    PopupX, PopupY, PopupW, PopupH,
    L"Type to edit    Backspace    Enter Save    Esc Cancel"
    );
}

STATIC
BOOLEAN
RunStringPopup (
  IN  FORM_DISPLAY_ENGINE_FORM       *Form,
  IN  FORM_DISPLAY_ENGINE_STATEMENT  *Stmt,
  IN  BOOLEAN                        IsPassword,
  OUT USER_INPUT                     *Result
  )
{
  CHAR16          Buf[256];
  UINTN           Len;
  UINTN           MaxLen;
  UINTN           MinLen;
  EFI_INPUT_KEY   Key;
  UINTN           Idx;
  CHAR16          *Title;

  // EFI_IFR_STRING and EFI_IFR_PASSWORD share the {Header, Question, ...}
  // prefix but EFI_IFR_PASSWORD's MinSize/MaxSize are UINT16 (vs UINT8 for
  // strings). Pulling them via the right struct is essential — otherwise
  // password's MaxSize reads as the low byte of MinSize, which is usually
  // zero and silently rejects every keystroke.
  if (IsPassword) {
    EFI_IFR_PASSWORD  *P = (EFI_IFR_PASSWORD *)Stmt->OpCode;
    MinLen = P->MinSize;
    MaxLen = P->MaxSize;
  } else {
    EFI_IFR_STRING  *S = (EFI_IFR_STRING *)Stmt->OpCode;
    MinLen = S->MinSize;
    MaxLen = S->MaxSize;
  }
  if (MaxLen >= ARRAY_SIZE (Buf)) {
    MaxLen = ARRAY_SIZE (Buf) - 1;
  }
  if (MaxLen == 0) {
    MaxLen = ARRAY_SIZE (Buf) - 1;  // generous default if VFR didn't set
  }

  // Seed buffer from current value — but only for plain strings. Password
  // fields don't carry the prior password (the browser hands us a zero-
  // padded buffer of MaxSize); pre-filling it would display a wall of
  // asterisks the user can't sensibly edit, and the convention is that
  // entering a password starts from an empty field.
  Len = 0;
  if (!IsPassword &&
      (Stmt->CurrentValue.Buffer != NULL) &&
      (Stmt->CurrentValue.BufferLen >= sizeof (CHAR16)))
  {
    CHAR16  *Src    = (CHAR16 *)Stmt->CurrentValue.Buffer;
    UINTN   CapChrs = Stmt->CurrentValue.BufferLen / sizeof (CHAR16);
    UINTN   Limit   = (CapChrs < MaxLen) ? CapChrs : MaxLen;
    // Find the actual content length (bounded by Limit and a leading NUL).
    while ((Len < Limit) && (Src[Len] != L'\0')) {
      Len++;
    }
    CopyMem (Buf, Src, Len * sizeof (CHAR16));
  }
  Buf[Len] = L'\0';

  Title = HiiGetString (
            Form->HiiHandle,
            ((EFI_IFR_STATEMENT_HEADER *)(Stmt->OpCode + 1))->Prompt,
            NULL
            );

  for ( ; ;) {
    if (mState.Gop != NULL) {
      DrawStringPopup (
        Title != NULL ? Title : (IsPassword ? L"Password" : L"Edit"),
        Buf, Len, IsPassword
        );
    }

    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Idx);
    if (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key) != EFI_SUCCESS) {
      continue;
    }

    if (Key.ScanCode == SCAN_ESC) {
      if (Title != NULL) FreePool (Title);
      return FALSE;
    }
    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
      if (Len < MinLen) {
        continue;  // honor MinSize — silently no-op until satisfied
      }
      // Allocate a CHAR16 buffer the browser owns.
      UINTN   ByteLen = (Len + 1) * sizeof (CHAR16);
      CHAR16  *Out    = AllocateZeroPool (ByteLen);
      if (Out != NULL) {
        CopyMem (Out, Buf, Len * sizeof (CHAR16));
        Result->SelectedStatement   = Stmt;
        Result->InputValue.Type     = EFI_IFR_TYPE_STRING;
        Result->InputValue.Buffer   = (UINT8 *)Out;
        Result->InputValue.BufferLen = (UINT16)ByteLen;
      }
      if (Title != NULL) FreePool (Title);
      return TRUE;
    }
    if (Key.UnicodeChar == CHAR_BACKSPACE) {
      if (Len > 0) {
        Buf[--Len] = L'\0';
      }
      continue;
    }
    // Printable ASCII (and basic Latin-1) — append.
    if ((Key.UnicodeChar >= 0x20) && (Key.UnicodeChar < 0x7F)) {
      if (Len < MaxLen) {
        Buf[Len++]   = Key.UnicodeChar;
        Buf[Len]     = L'\0';
      }
      continue;
    }
  }
}

//
// Date / Time popups
//

STATIC
VOID
ClampDate (
  IN OUT EFI_HII_DATE  *D
  )
{
  STATIC CONST UINT8  DaysInMonth[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
  };
  UINT8  MaxDay;

  if (D->Year  < 1900) D->Year  = 1900;
  if (D->Year  > 2099) D->Year  = 2099;
  if (D->Month < 1)    D->Month = 1;
  if (D->Month > 12)   D->Month = 12;

  MaxDay = DaysInMonth[D->Month - 1];
  if ((D->Month == 2) &&
      (((D->Year % 4 == 0) && (D->Year % 100 != 0)) || (D->Year % 400 == 0)))
  {
    MaxDay = 29;
  }
  if (D->Day < 1)      D->Day = 1;
  if (D->Day > MaxDay) D->Day = MaxDay;
}

STATIC
VOID
ClampTime (
  IN OUT EFI_HII_TIME  *T
  )
{
  if (T->Hour   > 23) T->Hour   = 23;
  if (T->Minute > 59) T->Minute = 59;
  if (T->Second > 59) T->Second = 59;
}

STATIC
VOID
BumpDateField (
  IN OUT EFI_HII_DATE  *D,
  IN     UINTN         Field,
  IN     INTN          Delta
  )
{
  switch (Field) {
    case 0: D->Year  = (UINT16)(D->Year  + Delta); break;
    case 1: D->Month = (UINT8) (D->Month + Delta); break;
    case 2: D->Day   = (UINT8) (D->Day   + Delta); break;
  }
  ClampDate (D);
}

STATIC
VOID
BumpTimeField (
  IN OUT EFI_HII_TIME  *T,
  IN     UINTN         Field,
  IN     INTN          Delta
  )
{
  switch (Field) {
    case 0: T->Hour   = (UINT8)(T->Hour   + Delta); break;
    case 1: T->Minute = (UINT8)(T->Minute + Delta); break;
    case 2: T->Second = (UINT8)(T->Second + Delta); break;
  }
  ClampTime (T);
}

STATIC
VOID
DrawFieldedPopup (
  IN CONST CHAR16  *Title,
  IN CONST CHAR16  *DisplayLine,
  IN UINTN         FieldStart,   // char offset where active field starts
  IN UINTN         FieldLen      // chars in the active field
  )
{
  UINTN   PopupW = (UINTN)mState.ScreenW * 50 / 100;
  UINTN   LineH  = mState.FontPx * 16 / 10;
  UINTN   PopupH = LineH * 4 + 4 * GFX_PADDING_PX;
  UINTN   PopupX = (mState.ScreenW - PopupW) / 2;
  UINTN   PopupY = (mState.ScreenH - PopupH) / 2;
  UINTN   ContentY = PopupY + LineH + 2 * GFX_PADDING_PX;
  UINTN   CenterX  = PopupX + PopupW / 2;
  CHAR16  Buf[64];
  UINTN   PreLen;
  UINTN   PrefixW, FieldW;

  DrawPopupFrame (Title, PopupX, PopupY, PopupW, PopupH);

  // Measure widths so we can underline just the active field.
  StrnCpyS (Buf, ARRAY_SIZE (Buf), DisplayLine, FieldStart);
  Buf[FieldStart] = L'\0';
  PrefixW = GfxMeasureTextW (Buf);

  StrnCpyS (Buf, ARRAY_SIZE (Buf), &DisplayLine[FieldStart], FieldLen);
  Buf[FieldLen] = L'\0';
  FieldW = GfxMeasureTextW (Buf);

  // Centered display line.
  GfxDrawTextWCentered (CenterX, ContentY + GFX_PADDING_PX, DisplayLine);

  // Underline the active field with a teal bar.
  {
    UINTN  FullW    = GfxMeasureTextW (DisplayLine);
    UINTN  TextLeft = CenterX - FullW / 2;
    UINTN  ULY      = ContentY + GFX_PADDING_PX + mState.FontPx + 2;
    GfxFillRect (TextLeft + PrefixW, ULY, FieldW, 2, GFX_ACCENT);
  }

  DrawPopupFooter (
    PopupX, PopupY, PopupW, PopupH,
    L"\x2190\x2192 Field    +/- Adjust    Enter Save    Esc Cancel"
    );

  PreLen = 0;  // suppress unused warning if optimizer chokes; harmless
  (VOID)PreLen;
}

STATIC
BOOLEAN
RunDatePopup (
  IN  FORM_DISPLAY_ENGINE_FORM       *Form,
  IN  FORM_DISPLAY_ENGINE_STATEMENT  *Stmt,
  OUT USER_INPUT                     *Result
  )
{
  EFI_HII_DATE   D;
  UINTN          Field;
  EFI_INPUT_KEY  Key;
  UINTN          Idx;
  CHAR16         *Title;
  CHAR16         Line[16];
  CHAR16         FieldBuf[8];

  D = Stmt->CurrentValue.Value.date;
  if (D.Year == 0) {
    D.Year  = 2026;
    D.Month = 1;
    D.Day   = 1;
  }
  ClampDate (&D);
  Field = 0;

  Title = HiiGetString (
            Form->HiiHandle,
            ((EFI_IFR_STATEMENT_HEADER *)(Stmt->OpCode + 1))->Prompt,
            NULL
            );

  for ( ; ;) {
    UINTN  FieldStart;
    UINTN  FieldLen;

    UnicodeSPrint (Line, sizeof (Line), L"%04u-%02u-%02u",
                   (UINT32)D.Year, (UINT32)D.Month, (UINT32)D.Day);
    if (Field == 0) { FieldStart = 0; FieldLen = 4; }
    else if (Field == 1) { FieldStart = 5; FieldLen = 2; }
    else { FieldStart = 8; FieldLen = 2; }

    if (mState.Gop != NULL) {
      DrawFieldedPopup (Title != NULL ? Title : L"Date", Line, FieldStart, FieldLen);
    }

    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Idx);
    if (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key) != EFI_SUCCESS) {
      continue;
    }

    if (Key.ScanCode == SCAN_ESC) {
      if (Title != NULL) FreePool (Title);
      return FALSE;
    }
    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
      Result->SelectedStatement = Stmt;
      Result->InputValue.Type   = EFI_IFR_TYPE_DATE;
      Result->InputValue.Value.date = D;
      Result->InputValue.Buffer = NULL;
      Result->InputValue.BufferLen = 0;
      if (Title != NULL) FreePool (Title);
      return TRUE;
    }
    if ((Key.ScanCode == SCAN_LEFT) || (Key.UnicodeChar == CHAR_TAB)) {
      Field = (Field + 2) % 3;
      continue;
    }
    if (Key.ScanCode == SCAN_RIGHT) {
      Field = (Field + 1) % 3;
      continue;
    }
    if ((Key.ScanCode == SCAN_UP) || (Key.UnicodeChar == L'+')) {
      BumpDateField (&D, Field, +1);
      continue;
    }
    if ((Key.ScanCode == SCAN_DOWN) || (Key.UnicodeChar == L'-')) {
      BumpDateField (&D, Field, -1);
      continue;
    }
    if ((Key.UnicodeChar >= L'0') && (Key.UnicodeChar <= L'9')) {
      UINT32  Digit = (UINT32)(Key.UnicodeChar - L'0');
      switch (Field) {
        case 0: D.Year  = (UINT16)(((UINT32)D.Year  % 1000) * 10 + Digit); break;
        case 1: D.Month = (UINT8) (((UINT32)D.Month % 10)   * 10 + Digit); break;
        case 2: D.Day   = (UINT8) (((UINT32)D.Day   % 10)   * 10 + Digit); break;
      }
      ClampDate (&D);
      continue;
    }
    (VOID)FieldBuf;
  }
}

STATIC
BOOLEAN
RunTimePopup (
  IN  FORM_DISPLAY_ENGINE_FORM       *Form,
  IN  FORM_DISPLAY_ENGINE_STATEMENT  *Stmt,
  OUT USER_INPUT                     *Result
  )
{
  EFI_HII_TIME   T;
  UINTN          Field;
  EFI_INPUT_KEY  Key;
  UINTN          Idx;
  CHAR16         *Title;
  CHAR16         Line[16];

  T = Stmt->CurrentValue.Value.time;
  ClampTime (&T);
  Field = 0;

  Title = HiiGetString (
            Form->HiiHandle,
            ((EFI_IFR_STATEMENT_HEADER *)(Stmt->OpCode + 1))->Prompt,
            NULL
            );

  for ( ; ;) {
    UINTN  FieldStart;
    UINTN  FieldLen = 2;

    UnicodeSPrint (Line, sizeof (Line), L"%02u:%02u:%02u",
                   (UINT32)T.Hour, (UINT32)T.Minute, (UINT32)T.Second);
    FieldStart = (Field == 0) ? 0 : (Field == 1 ? 3 : 6);

    if (mState.Gop != NULL) {
      DrawFieldedPopup (Title != NULL ? Title : L"Time", Line, FieldStart, FieldLen);
    }

    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Idx);
    if (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key) != EFI_SUCCESS) {
      continue;
    }

    if (Key.ScanCode == SCAN_ESC) {
      if (Title != NULL) FreePool (Title);
      return FALSE;
    }
    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
      Result->SelectedStatement = Stmt;
      Result->InputValue.Type   = EFI_IFR_TYPE_TIME;
      Result->InputValue.Value.time = T;
      Result->InputValue.Buffer = NULL;
      Result->InputValue.BufferLen = 0;
      if (Title != NULL) FreePool (Title);
      return TRUE;
    }
    if ((Key.ScanCode == SCAN_LEFT) || (Key.UnicodeChar == CHAR_TAB)) {
      Field = (Field + 2) % 3;
      continue;
    }
    if (Key.ScanCode == SCAN_RIGHT) {
      Field = (Field + 1) % 3;
      continue;
    }
    if ((Key.ScanCode == SCAN_UP) || (Key.UnicodeChar == L'+')) {
      BumpTimeField (&T, Field, +1);
      continue;
    }
    if ((Key.ScanCode == SCAN_DOWN) || (Key.UnicodeChar == L'-')) {
      BumpTimeField (&T, Field, -1);
      continue;
    }
    if ((Key.UnicodeChar >= L'0') && (Key.UnicodeChar <= L'9')) {
      UINT8  Digit = (UINT8)(Key.UnicodeChar - L'0');
      switch (Field) {
        case 0: T.Hour   = (UINT8)((T.Hour   % 10) * 10 + Digit); break;
        case 1: T.Minute = (UINT8)((T.Minute % 10) * 10 + Digit); break;
        case 2: T.Second = (UINT8)((T.Second % 10) * 10 + Digit); break;
      }
      ClampTime (&T);
      continue;
    }
  }
}

//
// Confirmation popup — used by ConfirmDataChange(). Three-button (Yes /
// No / Cancel) modal centered on the screen.
//

typedef enum {
  CONFIRM_YES,
  CONFIRM_NO,
  CONFIRM_CANCEL
} CONFIRM_RESULT;

STATIC
VOID
DrawConfirmPopup (
  IN CONST CHAR16  *Title,
  IN CONST CHAR16  *Message,
  IN INTN          Choice
  )
{
  CONST CHAR16  *Labels[3] = { L"  Yes  ", L"  No  ", L"  Cancel  " };
  UINTN         PopupW = (UINTN)mState.ScreenW * 55 / 100;
  UINTN         LineH  = mState.FontPx * 16 / 10;
  UINTN         PopupH = LineH * 5 + 4 * GFX_PADDING_PX;
  UINTN         PopupX = (mState.ScreenW - PopupW) / 2;
  UINTN         PopupY = (mState.ScreenH - PopupH) / 2;
  UINTN         BtnY, BtnH;
  UINTN         BtnW[3];
  UINTN         TotalW = 0;
  UINTN         GapW   = 3 * GFX_PADDING_PX;
  UINTN         BtnX;
  UINTN         i;

  DrawPopupFrame (Title, PopupX, PopupY, PopupW, PopupH);

  // Message line, centered.
  GfxSetTextColor (TC_WHITE);
  GfxDrawTextWCentered (
    PopupX + PopupW / 2,
    PopupY + LineH + 2 * GFX_PADDING_PX,
    Message
    );

  // Buttons row.
  BtnY = PopupY + PopupH - LineH * 2 - GFX_PADDING_PX;
  BtnH = LineH;
  for (i = 0; i < 3; i++) {
    BtnW[i] = GfxMeasureTextW (Labels[i]) + 2 * GFX_PADDING_PX;
    TotalW += BtnW[i];
  }
  TotalW += 2 * GapW;

  BtnX = PopupX + (PopupW - TotalW) / 2;
  for (i = 0; i < 3; i++) {
    if ((INTN)i == Choice) {
      GfxFillRect (BtnX, BtnY, BtnW[i], BtnH, GFX_HIGHLIGHT);
    } else {
      GfxDrawRectOutline (BtnX, BtnY, BtnW[i], BtnH, GFX_TEXT_DIM);
    }
    GfxSetTextColor (TC_WHITE);
    GfxDrawTextWCentered (
      BtnX + BtnW[i] / 2,
      BtnY + (BtnH - mState.FontPx) / 2,
      Labels[i]
      );
    BtnX += BtnW[i] + GapW;
  }

  DrawPopupFooter (
    PopupX, PopupY, PopupW, PopupH,
    L"\x2190\x2192 Move    Enter Confirm    Y/N quick    Esc Cancel"
    );
}

STATIC
CONFIRM_RESULT
RunConfirmPopup (
  IN CONST CHAR16  *Title,
  IN CONST CHAR16  *Message
  )
{
  EFI_INPUT_KEY  Key;
  UINTN          Idx;
  INTN           Choice = 0;  // start on "Yes"

  for ( ; ;) {
    if (mState.Gop != NULL) {
      DrawConfirmPopup (Title, Message, Choice);
    }
    if (mState.SerialCount > 0) {
      CHAR16  Buf[160];
      UnicodeSPrint (
        Buf, sizeof (Buf),
        L"  %s  [%c Yes]  [%c No]  [%c Cancel]",
        Message,
        Choice == 0 ? L'>' : L' ',
        Choice == 1 ? L'>' : L' ',
        Choice == 2 ? L'>' : L' '
        );
      SerialOutLine (Buf);
    }

    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Idx);
    if (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key) != EFI_SUCCESS) {
      continue;
    }

    if (Key.ScanCode == SCAN_ESC) {
      return CONFIRM_CANCEL;
    }
    if ((Key.ScanCode == SCAN_LEFT) || (Key.ScanCode == SCAN_UP)) {
      Choice = (Choice + 2) % 3;
      continue;
    }
    if ((Key.ScanCode == SCAN_RIGHT) || (Key.ScanCode == SCAN_DOWN) ||
        (Key.UnicodeChar == CHAR_TAB))
    {
      Choice = (Choice + 1) % 3;
      continue;
    }
    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
      switch (Choice) {
        case 0: return CONFIRM_YES;
        case 1: return CONFIRM_NO;
        default: return CONFIRM_CANCEL;
      }
    }
    if ((Key.UnicodeChar == L'y') || (Key.UnicodeChar == L'Y')) {
      return CONFIRM_YES;
    }
    if ((Key.UnicodeChar == L'n') || (Key.UnicodeChar == L'N')) {
      return CONFIRM_NO;
    }
  }
}

//
// Input loop — runs until user picks an item or exits.
//

STATIC
VOID
RunInputLoop (
  IN     FORM_DISPLAY_ENGINE_FORM  *Form,
  IN     ITEM                      *Items,
  IN     UINTN                     ItemCount,
  IN OUT INTN                      *Highlight,
  OUT    USER_INPUT                *Result
  )
{
  EFI_INPUT_KEY  Key;
  UINTN          Idx;
  INTN           NewHighlight;

  for ( ; ;) {
    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Idx);
    if (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key) != EFI_SUCCESS) {
      continue;
    }

    // Hot keys take precedence over the normal nav / dispatch path. The
    // browser registers them via FormBrowserEx2->RegisterHotKey (typically
    // F9 = Defaults, F10 = Save & Exit, etc.) and populates Form->HotKeyListHead.
    {
      LIST_ENTRY       *HkLink;
      BROWSER_HOT_KEY  *Hk;
      BOOLEAN          Matched = FALSE;
      for (HkLink = GetFirstNode (&Form->HotKeyListHead);
           !IsNull (&Form->HotKeyListHead, HkLink);
           HkLink = GetNextNode (&Form->HotKeyListHead, HkLink))
      {
        Hk = BROWSER_HOT_KEY_FROM_LINK (HkLink);
        if ((Hk->KeyData != NULL) &&
            (Hk->KeyData->ScanCode    == Key.ScanCode) &&
            (Hk->KeyData->UnicodeChar == Key.UnicodeChar))
        {
          Result->Action    = Hk->Action;
          Result->DefaultId = Hk->DefaultId;
          Matched           = TRUE;
          break;
        }
      }
      if (Matched) {
        return;
      }
    }

    switch (Key.ScanCode) {
      case SCAN_ESC:
        Result->Action = BROWSER_ACTION_FORM_EXIT;
        return;
      case SCAN_LEFT:
      case SCAN_RIGHT: {
        // Tab navigation. From Main, R goes to the first tab and L wraps
        // to the last. From a tab-level form, +/-1 with wrap. If the
        // target tab's REF is in the current form's statement list we
        // fire it directly; otherwise we stash the target and exit one
        // form-stack level — the next FormDisplay will keep walking up
        // until it finds a form that contains the target REF.
        INTN  Active;
        INTN  Target;
        FORM_DISPLAY_ENGINE_STATEMENT  *RefStmt;

        if (TabsCount () == 0) {
          continue;
        }
        Active = TabsFindActive (Form);
        if (Active < 0) {
          Target = (Key.ScanCode == SCAN_RIGHT) ? 0 : (INTN)(TabsCount () - 1);
        } else {
          INTN  Step = (Key.ScanCode == SCAN_RIGHT) ? +1 : -1;
          Target = (Active + Step + (INTN)TabsCount ()) % (INTN)TabsCount ();
          if (Target == Active) {
            continue;
          }
        }

        RefStmt = FindRefForTab (Form, (UINTN)Target);
        if (RefStmt != NULL) {
          Result->SelectedStatement = RefStmt;
          return;
        }
        // Not in this form — propagate exit and chase via mPendingTabIndex.
        mPendingTabIndex = Target;
        Result->Action   = BROWSER_ACTION_FORM_EXIT;
        return;
      }

      case SCAN_UP:
      case SCAN_DOWN: {
        INTN   Old      = *Highlight;
        UINTN  PriorTop = mFirstVisible;
        // Walk to the next row with prompt content. Unselectable labels
        // (text, subtitles with text) are reachable so they scroll into
        // view; empty padding rows are skipped. Activation gating is at
        // the Enter handler.
        INTN  Step = (Key.ScanCode == SCAN_UP) ? -1 : +1;
        NewHighlight = NextNavigable (Items, ItemCount, *Highlight + Step, Step);
        if ((NewHighlight >= 0) && (NewHighlight != Old)) {
          *Highlight = NewHighlight;
          AdjustViewport (NewHighlight, ItemCount);
          if (mFirstVisible != PriorTop) {
            // Viewport scrolled — every visible row's content shifted, so
            // we have to repaint the whole list area (still cheap: only
            // the list rectangle, not chrome / header / tabs / footer).
            RedrawListArea (Items, ItemCount, NewHighlight, Form->HiiHandle);
          } else {
            // No scroll — just the two affected rows.
            RedrawHighlightChange (Form, Items, ItemCount, Old, NewHighlight);
          }
          MirrorHighlightChange (Items, (UINTN)NewHighlight, Form->HiiHandle);
        }
        continue;
      }
      default:
        break;
    }

    if ((Key.UnicodeChar == CHAR_CARRIAGE_RETURN) && (*Highlight >= 0)) {
      // Enter on a non-selectable row (subtitle, plain text, grayed-out
      // question) is a no-op — we let the user navigate to those rows so
      // they can scroll into view, but they're not actionable.
      if (!Items[*Highlight].Selectable) {
        continue;
      }

      FORM_DISPLAY_ENGINE_STATEMENT  *Stmt = Items[*Highlight].Stmt;
      UINT8                          Op   = Stmt->OpCode->OpCode;

      switch (Op) {
        case EFI_IFR_CHECKBOX_OP:
          // Inline toggle — no popup, just commit the flipped value.
          Result->SelectedStatement    = Stmt;
          Result->InputValue.Type      = EFI_IFR_TYPE_BOOLEAN;
          Result->InputValue.Value.b   = (BOOLEAN)!Stmt->CurrentValue.Value.b;
          Result->InputValue.Buffer    = NULL;
          Result->InputValue.BufferLen = 0;
          return;

        case EFI_IFR_ONE_OF_OP:
          if (RunOneOfPopup (Form, Stmt, Result)) {
            return;
          }
          // Cancelled — popup overwrote the form area; redraw and resume.
          RenderForm (Form, Items, ItemCount, *Highlight);
          continue;

        case EFI_IFR_NUMERIC_OP:
          if (RunNumericPopup (Form, Stmt, Result)) {
            return;
          }
          RenderForm (Form, Items, ItemCount, *Highlight);
          continue;

        case EFI_IFR_STRING_OP:
          if (RunStringPopup (Form, Stmt, FALSE, Result)) {
            return;
          }
          RenderForm (Form, Items, ItemCount, *Highlight);
          continue;

        case EFI_IFR_PASSWORD_OP:
          if (RunStringPopup (Form, Stmt, TRUE, Result)) {
            return;
          }
          RenderForm (Form, Items, ItemCount, *Highlight);
          continue;

        case EFI_IFR_DATE_OP:
          if (RunDatePopup (Form, Stmt, Result)) {
            return;
          }
          RenderForm (Form, Items, ItemCount, *Highlight);
          continue;

        case EFI_IFR_TIME_OP:
          if (RunTimePopup (Form, Stmt, Result)) {
            return;
          }
          RenderForm (Form, Items, ItemCount, *Highlight);
          continue;

        default:
          // REFs / actions / unimplemented edit types: hand the statement
          // back to the browser unchanged. The browser navigates for REFs;
          // for editable questions Phase 3c adds proper edit popups.
          Result->SelectedStatement = Stmt;
          return;
      }
    }
  }
}

STATIC
EFI_STATUS
EFIAPI
FormDisplay (
  IN  FORM_DISPLAY_ENGINE_FORM  *FormData,
  OUT USER_INPUT                *UserInputData
  )
{
  ITEM   Items[MAX_ITEMS];
  UINTN  ItemCount;
  INTN   Highlight;
  UINTN  i;

  // Output discovery has to happen here, not at driver-entry time: when our
  // entry point runs we may dispatch *before* GraphicsOutputDxe (and before
  // the rest of the console stack is wired up), so a one-shot init in the
  // entry point misses everything. By FormDisplay time the console stack
  // is fully up.
  GfxOutputInit ();
  // SMBIOS is published to gST->ConfigurationTable during DXE; cached on
  // first call.
  SystemInfoCollect ();

  DEBUG ((DEBUG_ERROR,
          "GraphicsDisplayEngine: FormDisplay called (Gop=%a, SsfnReady=%a, Serial=%u)\n",
          mState.Gop != NULL ? "yes" : "NULL",
          mState.SsfnReady ? "yes" : "no",
          (UINT32)mState.SerialCount));

  if ((FormData == NULL) || (UserInputData == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (UserInputData, sizeof (*UserInputData));

  // Snapshot REFs as tabs the first time we see a tab-source form.
  TabsCaptureFromForm (FormData);

  // Pending L/R tab navigation: chase the target tab by exiting one form
  // level at a time until we land on a form that has its REF.
  if (mPendingTabIndex >= 0) {
    FORM_DISPLAY_ENGINE_STATEMENT  *RefStmt;
    RefStmt = FindRefForTab (FormData, (UINTN)mPendingTabIndex);
    if (RefStmt != NULL) {
      UserInputData->SelectedStatement = RefStmt;
      mPendingTabIndex = -1;
      return EFI_SUCCESS;
    }
    if (!IsMainForm (FormData)) {
      // Keep walking up — browser exits this form and will call us again
      // on its parent.
      UserInputData->Action = BROWSER_ACTION_FORM_EXIT;
      return EFI_SUCCESS;
    }
    // We're on Main and still didn't find it — give up cleanly.
    mPendingTabIndex = -1;
  }

  ItemCount = CollectItems (FormData, Items, MAX_ITEMS);

  // Initial highlight: prefer the browser's stored highlight, else first
  // selectable item.
  Highlight = -1;
  if (FormData->HighLightedStatement != NULL) {
    for (i = 0; i < ItemCount; i++) {
      if (Items[i].Stmt == FormData->HighLightedStatement) {
        Highlight = (INTN)i;
        break;
      }
    }
  }
  if (Highlight < 0) {
    Highlight = NextSelectable (Items, ItemCount, 0, +1);
  }

  RenderForm (FormData, Items, ItemCount, Highlight);
  RunInputLoop (FormData, Items, ItemCount, &Highlight, UserInputData);

  // If the user exited (Esc) without picking a statement, tell the browser
  // to leave the form. Otherwise SelectedStatement is set and the browser
  // dispatches it.
  if ((UserInputData->SelectedStatement == NULL) &&
      (UserInputData->Action == 0))
  {
    UserInputData->Action = BROWSER_ACTION_FORM_EXIT;
  }
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
ExitDisplay (
  VOID
  )
{
  if (mState.Gop != NULL) {
    GfxClearScreen (GFX_COLOR (0, 0, 0));
  }
  if (gST->ConOut != NULL) {
    gST->ConOut->ClearScreen (gST->ConOut);
  }
}

STATIC
UINTN
EFIAPI
ConfirmDataChange (
  VOID
  )
{
  // Browser calls this when the user is leaving the form / setup with
  // unsaved changes. Show the standard 3-button confirmation:
  //   Yes    → SUBMIT (save and continue)
  //   No     → DISCARD (throw away changes and continue)
  //   Cancel → NONE (stay in the menu)
  switch (RunConfirmPopup (L"Unsaved Changes", L"Save changes before continuing?")) {
    case CONFIRM_YES:    return BROWSER_ACTION_SUBMIT;
    case CONFIRM_NO:     return BROWSER_ACTION_DISCARD;
    default:             return BROWSER_ACTION_NONE;
  }
}

STATIC EDKII_FORM_DISPLAY_ENGINE_PROTOCOL  mFormDisplayEngine = {
  FormDisplay,
  ExitDisplay,
  ConfirmDataChange
};

EFI_STATUS
EFIAPI
GraphicsDisplayEngineEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;

  DEBUG ((DEBUG_ERROR, "GraphicsDisplayEngine: entry point reached\n"));

  // Note: deliberately do NOT enumerate GOP / serial outputs here — the
  // console stack isn't fully up yet at driver-entry time. Discovery is
  // deferred to the first FormDisplay call.

  Handle = NULL;
  Status = gBS->InstallProtocolInterface (
                  &Handle,
                  &gEdkiiFormDisplayEngineProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mFormDisplayEngine
                  );
  DEBUG ((DEBUG_ERROR, "GraphicsDisplayEngine: protocol install: %r\n", Status));
  return Status;
}
