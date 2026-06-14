/** @file
  Provides functions for communicating with Dasharo EC.

  Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.

  SPDX-License-Identifier: GPL-2.0-only
**/

#ifndef EC_FLASHING_H__
#define EC_FLASHING_H__

typedef enum {
  PROGRESS_SET_TOTAL,
  PROGRESS_SET_CURRENT,
  PROGRESS_INC_CURRENT,
} PROGRESS_OP;

typedef VOID (*FW_PROGRESS_CONTROL) (
  PROGRESS_OP  Op,
  UINTN        Value,
  VOID         *Context
  );

UINT8
EFIAPI
EcReadVersion (
  CHAR8  *Buf,
  UINTN  BufSize
  );

BOOLEAN
EFIAPI
EcImageIsValid (
  CONST VOID  *Image,
  UINTN       ImageSz
  );

EFI_STATUS
EFIAPI
EcFlashImage (
  CONST VOID           *Image,
  UINTN                ImageSz,
  FW_PROGRESS_CONTROL  ProgressCtl,
  VOID                 *ProgressContext
  );

EFI_STATUS
EFIAPI
EcReset (
  VOID
  );

#endif // EC_FLASHING_H__
