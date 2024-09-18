/* SPDX-License-Identifier: BSD-2-Clause-Patent */

#ifndef __EFI_VARS_LIB__
#define __EFI_VARS_LIB__

#include <Uefi/UefiBaseType.h>
#include <Uefi/UefiMultiPhase.h>
#include <Pi/PiFirmwareVolume.h>
#include <Guid/VariableFormat.h>

typedef struct {
  UINT8  *Start;
  UINTN  Length;
} MemRange;

typedef struct EfiVar {
  UINT32         Attrs;
  EFI_GUID       Guid;
  CHAR16         *Name;
  UINTN          NameSize; // in bytes
  UINT8          *Data;
  UINTN          DataSize; // in bytes
  struct EfiVar  *Next;
} EfiVar;

typedef struct {
  MemRange  Fv;
  MemRange  Store;
  EfiVar    *Vars;
} EfiVars;

BOOLEAN
EFIAPI
EfiVarsInit (
  IN OUT MemRange  Fv,
  OUT    EfiVars   *Storage
  );

BOOLEAN
EFIAPI
EfiVarsWrite (
  IN EfiVars  *Storage
  );

EfiVar *
EFIAPI
EfiVarsCreateVar (
  IN EfiVars  *Storage
  );

VOID
EFIAPI
EfiVarsFree (
  IN EfiVars  *Storage
  );

#endif
