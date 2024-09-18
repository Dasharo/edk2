/* SPDX-License-Identifier: BSD-2-Clause-Patent */

#include <Library/EfiVarsLib.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/SmmStoreLib.h>

// Firmware volume is what's stored in SMMSTORE region of CBFS.  It wraps
// variable store.
//
// Variable store is part of firmware volume.  This unit doesn't deal with its
// header only with data that follows.

STATIC
UINT16
CalcChecksum (
  CONST UINT16 *Hdr,
  UINTN Size
  )
{
  UINT16  Checksum;
  UINTN   Idx;

  ASSERT (Size % 2 == 0 && "Header can't have odd length.");

  Checksum = 0;
  for (Idx = 0; Idx < Size / 2; ++Idx)
    Checksum += Hdr[Idx];

  return Checksum;
}

STATIC
BOOLEAN
InitFv (
  IN OUT MemRange  Fv
  )
{
  EFI_STATUS                  Status;
  UINTN                       BlockSize;
  UINTN                       VolHdrLen;
  EFI_FIRMWARE_VOLUME_HEADER  *VolHdr;
  VARIABLE_STORE_HEADER       *VarStoreHdr;

  Status = SmmStoreLibGetBlockSize (&BlockSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): SmmStoreLibGetBlockSize() failed with: %r\n",
      __FUNCTION__,
      Status
      ));
    return FALSE;
  }

  if (Fv.Length % BlockSize != 0) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): firmware Volume size is not a multiple of 64KiB\n",
      __FUNCTION__
      ));
    return FALSE;
  }

  SetMem (Fv.Start, Fv.Length, 0xff);

  VolHdrLen = sizeof (*VolHdr) + sizeof (EFI_FV_BLOCK_MAP_ENTRY);

  VolHdr = (EFI_FIRMWARE_VOLUME_HEADER *) Fv.Start;
  SetMem (VolHdr, sizeof (*VolHdr), 0x00);

  VolHdr->FileSystemGuid = gEfiSystemNvDataFvGuid;
  VolHdr->FvLength       = Fv.Length;
  VolHdr->Signature      = EFI_FVH_SIGNATURE;
  VolHdr->Attributes     = EFI_FVB2_READ_ENABLED_CAP
                         | EFI_FVB2_READ_STATUS
                         | EFI_FVB2_WRITE_ENABLED_CAP
                         | EFI_FVB2_WRITE_STATUS
                         | EFI_FVB2_STICKY_WRITE
                         | EFI_FVB2_MEMORY_MAPPED
                         | EFI_FVB2_ERASE_POLARITY;
  VolHdr->HeaderLength   = VolHdrLen;
  VolHdr->Revision       = EFI_FVH_REVISION;

  VolHdr->BlockMap[0].NumBlocks = Fv.Length / BlockSize;
  VolHdr->BlockMap[0].Length    = BlockSize;
  VolHdr->BlockMap[1].NumBlocks = 0;
  VolHdr->BlockMap[1].Length    = 0;

  VolHdr->Checksum = ~CalcChecksum ((CONST UINT16 *) VolHdr, VolHdrLen) + 1;

  VarStoreHdr = (VARIABLE_STORE_HEADER *) (Fv.Start + VolHdrLen);
  SetMem (VarStoreHdr, sizeof (*VarStoreHdr), 0x00);

  // Authentication-related fields will be filled with 0xff.
  VarStoreHdr->Signature = gEfiAuthenticatedVariableGuid;
  // Actual size of the storage is block size, the rest is
  // Fault Tolerant Write (FTW) space and the FTW spare space.
  VarStoreHdr->Size      = BlockSize - VolHdrLen;
  VarStoreHdr->Format    = VARIABLE_STORE_FORMATTED;
  VarStoreHdr->State     = VARIABLE_STORE_HEALTHY;

  return TRUE;
}

BOOLEAN
EFIAPI
EfiVarsInit (
  IN OUT MemRange  Fv,
  OUT EfiVars      *Storage
  )
{
  CONST EFI_FIRMWARE_VOLUME_HEADER  *VolHdr;
  CONST VARIABLE_STORE_HEADER       *VarStoreHdr;
  UINT8                             *VolData;
  UINTN                             VolDataSize;

  if (!InitFv (Fv)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(): failed to create variable store",
      __FUNCTION__
      ));
    return FALSE;
  }

  VolHdr      = (CONST EFI_FIRMWARE_VOLUME_HEADER *) Fv.Start;
  VolData     = Fv.Start + VolHdr->HeaderLength;
  VolDataSize = Fv.Length - VolHdr->HeaderLength;
  VarStoreHdr = (CONST VARIABLE_STORE_HEADER *) VolData;

  Storage->Fv           = Fv;
  Storage->Store.Start  = VolData + sizeof (*VarStoreHdr);
  Storage->Store.Length = VolDataSize - sizeof (*VarStoreHdr);
  Storage->Vars         = NULL;

  return TRUE;
}

STATIC
VOID
StoreVar (
  IN  CONST EfiVar  *Var,
  OUT UINT8         *Data
  )
{
  AUTHENTICATED_VARIABLE_HEADER hdr;
  SetMem (&hdr, sizeof (hdr), 0xff);

  hdr.StartId    = VARIABLE_DATA;
  hdr.State      = VAR_ADDED;
  hdr.Reserved   = 0;
  hdr.Attributes = Var->Attrs;
  hdr.VendorGuid = Var->Guid;
  hdr.NameSize   = Var->NameSize;
  hdr.DataSize   = Var->DataSize;

  CopyMem (Data, &hdr, sizeof (hdr));
  Data += sizeof (hdr);

  CopyMem (Data, Var->Name, Var->NameSize);
  CopyMem (Data + Var->NameSize, Var->Data, Var->DataSize);
}

BOOLEAN
EFIAPI
EfiVarsWrite (
  IN EfiVars  *Storage
  )
{
  MemRange  VarStore;
  UINT8     *OutData;
  EfiVar    *Var;
  UINTN     VarSize;

  VarStore = Storage->Store;
  OutData  = VarStore.Start;

  for (Var = Storage->Vars; Var != NULL; Var = Var->Next) {
    VarSize =
      sizeof (AUTHENTICATED_VARIABLE_HEADER) + Var->NameSize + Var->DataSize;
    if (OutData + VarSize > VarStore.Start + VarStore.Length) {
      DEBUG ((
        DEBUG_ERROR,
        "%a(): not enough space to serialize Variable Store (have 0x%x).\n",
        __FUNCTION__,
        VarStore.Length
        ));
      return FALSE;
    }

    StoreVar (Var, OutData);
    OutData += HEADER_ALIGN (VarSize);
  }

  // The rest is "uninitialized".
  SetMem (OutData, VarStore.Length - (OutData - VarStore.Start), 0xff);

  return TRUE;
}

EfiVar *
EFIAPI
EfiVarsCreateVar (
  IN EfiVars  *Storage
  )
{
  EfiVar *NewVar;
  EfiVar *Var;

  NewVar = AllocateZeroPool (sizeof (*NewVar));
  if (NewVar == NULL)
    return NULL;

  Var = Storage->Vars;
  if (Var == NULL) {
    Storage->Vars = NewVar;
  } else {
    while (Var->Next != NULL)
      Var = Var->Next;
    Var->Next = NewVar;
  }

  return NewVar;
}

VOID
EFIAPI
EfiVarsFree (
  IN EfiVars  *Storage
  )
{
  EfiVar *Next;
  EfiVar *Var;

  for (Var = Storage->Vars; Var != NULL; Var = Next) {
    Next = Var->Next;

    FreePool (Var->Name);
    FreePool (Var->Data);
    FreePool (Var);
  }
}
