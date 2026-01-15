//
// A utility library for drawing simple popups.
//
// Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
// SPDX-License-Identifier: GPL-2-or-later
//

#include <Library/PopUpLib.h>

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
