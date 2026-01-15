//
// A utility library for drawing simple popups.
//
// Functions for constructing a popup dynamically and then displaying it in both
// text and GUI formats.
//
// Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
// SPDX-License-Identifier: GPL-2-or-later
//

#include <Library/PopUpLib.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CustomizedDisplayLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>

VOID
EFIAPI
DrainInput (
  VOID
)
{
  EFI_INPUT_KEY  Key;

  //
  // Drain any queued keys.
  //
  while (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
    //
    // Throw away the key.
    //
  }
}

VOID
EFIAPI
PopUpInit (
  OUT PopUpData  *PopUp,
  IN UINTN       Width
  )
{
  ZeroMem (PopUp, sizeof(*PopUp));
  PopUp->Width = Width;
}

VOID
EFIAPI
AddTitle (
  IN PopUpData     *PopUp,
  IN CONST CHAR16  *Line
  )
{
  CHAR16  *Separator;
  UINTN   Index;

  AddLineF (PopUp, L"%s", Line);

  Separator = AllocatePool (StrSize (Line));
  if (Separator == NULL) {
    return;
  }

  for (Index = 0; Line[Index] != L'\0'; ++Index) {
    Separator[Index] = BOXDRAW_DOUBLE_HORIZONTAL;
  }
  Separator[Index] = L'\0';

  AddLine (PopUp, Separator);
  AddLine (PopUp, L"");

  FreePool (Separator);
}

STATIC
VOID
AddALine (
  IN PopUpData     *PopUp,
  IN CONST CHAR16  *Line,
  IN BOOLEAN       FullWidth
  )
{
  UINTN   MaxByteLength;
  UINTN   LineByteLength;
  UINTN   ByteLength;
  UINTN   Index;
  CHAR16  *Buffer;

  // Cut the bottom lines off if we're out of space.
  if (PopUp->Length == ARRAY_SIZE (PopUp->Lines)) {
    FreePool (PopUp->Lines[PopUp->Length - 1]);
    FreePool (PopUp->Lines[PopUp->Length - 2]);
    PopUp->Length -= 2;

    AddALine (PopUp, L"...", /*FullWidth=*/FALSE);
  }

  // `PopUp->Width - 1` here and below because actual width of the contents is
  // one less than what's passed to CreateMultiStringPopUp().
  MaxByteLength = (PopUp->Width - 1 + 1) * 2;
  if (FullWidth) {
    ByteLength = MaxByteLength;
  } else {
    LineByteLength = StrSize (Line);
    ByteLength     = MIN (MaxByteLength, LineByteLength);
  }

  Buffer = AllocatePool (ByteLength);
  if (Buffer == NULL) {
    return;
  }

  StrnCpyS (Buffer, ByteLength / 2, Line, ByteLength / 2 - 1);

  if (FullWidth) {
    for (Index = StrLen (Buffer); Index < PopUp->Width - 1; ++Index) {
      Buffer[Index] = L' ';
    }
    Buffer[Index] = L'\0';
  }

  PopUp->Lines[PopUp->Length++] = Buffer;

  if (StrLen (Buffer) < StrLen (Line)) {
    // And continuation of the line, which has the effect of wrapping long
    // lines.
    Line += StrLen (Buffer);

    // Because leading spaces are treated specially by CreateMultiStringPopUp(),
    // skip them when wrapping.
    while (*Line == L' ') {
      ++Line;
    }
    if (*Line != L'\0') {
      AddALine (PopUp, Line, FullWidth);
    }
  }
}

STATIC
VOID
EFIAPI
AddFLine (
  IN PopUpData     *PopUp,
  IN CONST CHAR16  *Format,
  IN VA_LIST       Marker,
  IN BOOLEAN       FullWidth
  )
{
  VA_LIST  ExtraMarker;
  UINTN    ByteLength;
  CHAR16   *Buffer;

  VA_COPY (ExtraMarker, Marker);
  ByteLength = (SPrintLength (Format, ExtraMarker) + 1) * 2;
  VA_END (ExtraMarker);

  Buffer = AllocatePool (ByteLength);
  if (Buffer == NULL) {
    return;
  }

  UnicodeVSPrint (Buffer, ByteLength, Format, Marker);

  AddALine (PopUp, Buffer, FullWidth);
  FreePool (Buffer);
}

VOID
EFIAPI
AddLineF (
  IN PopUpData     *PopUp,
  IN CONST CHAR16  *Format,
  ...
  )
{
  VA_LIST  Marker;

  VA_START (Marker, Format);
  AddFLine (PopUp, Format, Marker, /*FullWidth=*/FALSE);
  VA_END (Marker);
}

VOID
EFIAPI
AddFullLineF (
  IN PopUpData     *PopUp,
  IN CONST CHAR16  *Format,
  ...
  )
{
  VA_LIST  Marker;

  VA_START (Marker, Format);
  AddFLine (PopUp, Format, Marker, /*FullWidth=*/TRUE);
  VA_END (Marker);
}

VOID
EFIAPI
AddLine (
  IN PopUpData     *PopUp,
  IN CONST CHAR16  *Line
  )
{
  AddALine (PopUp, Line, /*FullWidth=*/FALSE);
}

VOID
EFIAPI
AddFullLine (
  IN PopUpData     *PopUp,
  IN CONST CHAR16  *Line
  )
{
  AddALine (PopUp, Line, /*FullWidth=*/TRUE);
}

VOID
EFIAPI
PopUpDraw (
  IN CONST PopUpData  *PopUp,
  IN PopupKind        GuiKind
  )
{
  //
  // A "dynamic" popup.  Always passing all lines, but limiting number of lines
  // that get displayed, the implementation doesn't look at the rest of lines.
  //
  CreateMultiStringPopUp (
      PopUp->Width,
      PopUp->Length,
      PopUp->Lines[0], PopUp->Lines[1], PopUp->Lines[2], PopUp->Lines[3],
      PopUp->Lines[4], PopUp->Lines[5], PopUp->Lines[6], PopUp->Lines[7],
      PopUp->Lines[8], PopUp->Lines[9], PopUp->Lines[10], PopUp->Lines[11],
      PopUp->Lines[12], PopUp->Lines[13], PopUp->Lines[14], PopUp->Lines[15],
      PopUp->Lines[16], PopUp->Lines[17], PopUp->Lines[18], PopUp->Lines[19],
      PopUp->Lines[20], PopUp->Lines[21], PopUp->Lines[22], PopUp->Lines[23]
      );

  //
  // Using the same approach for GUI.  This is a no-op if graphics isn't
  // available, so must be done after drawing text version.  Otherwise, it
  // clears the screen and draws a nicer version.  This way in the absence of
  // GOP the text is displayed (at least on serial) and with the GOP serial gets
  // text version while display has a GUI one.
  //
  DrawGraphicPopUp (
      GuiKind,
      PopUp->Width,
      PopUp->Length,
      PopUp->Lines[0], PopUp->Lines[1], PopUp->Lines[2], PopUp->Lines[3],
      PopUp->Lines[4], PopUp->Lines[5], PopUp->Lines[6], PopUp->Lines[7],
      PopUp->Lines[8], PopUp->Lines[9], PopUp->Lines[10], PopUp->Lines[11],
      PopUp->Lines[12], PopUp->Lines[13], PopUp->Lines[14], PopUp->Lines[15],
      PopUp->Lines[16], PopUp->Lines[17], PopUp->Lines[18], PopUp->Lines[19],
      PopUp->Lines[20], PopUp->Lines[21], PopUp->Lines[22], PopUp->Lines[23]
      );
}
