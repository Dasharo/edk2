//
// A utility library for drawing simple popups.
//
// Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
// SPDX-License-Identifier: GPL-2-or-later
//
//

#ifndef _POP_UP_LIB_H_
#define _POP_UP_LIB_H_

typedef enum {
  ErrorPopUp,
  SuccessPopUp,
} PopupKind;

VOID
EFIAPI
DrainInput (
  VOID
  );

/**
  Draw a pop up windows based on the dimension, number of lines and
  strings specified. (GOP Scaled Version)

  @param Kind            Color of the background.
  @param RequestedWidth  The width of the pop-up in standard character cells.
                         If 0, autosize to fit longest string.
  @param NumberOfLines   The number of lines.
  @param ...             A series of text strings that displayed in the pop-up.

**/
VOID
EFIAPI
DrawGraphicPopUp (
  IN  PopupKind  Kind,
  IN  UINTN      RequestedWidth,
  IN  UINTN      NumberOfLines,
  ...
  );

#endif // _POP_UP_LIB_H_
