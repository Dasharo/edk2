/** @file  SmmStoreVarPreserve.c

  Keeps a fixed set of variables across a reformat of the variable store
  done in BOOT_WITH_DEFAULT_SETTINGS boot mode (for example after a CMOS
  clear): the setup password and the whole Secure Boot configuration.

  The entries are copied verbatim, so authenticated variables keep their
  Attributes, MonotonicCount, TimeStamp and PubKeyIndex and need no
  re-enrolment.

  Copyright (c) 2026, 3mdeb Sp. z o.o.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiDxe.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>

#include <Guid/VariableFormat.h>

#include "SmmStoreFvbRuntime.h"

typedef struct {
  CONST CHAR16    *Name;
  CONST EFI_GUID  *Guid;
} PRESERVED_VARIABLE;

STATIC CONST PRESERVED_VARIABLE  mPreservedVariables[] = {
  { L"Password",         &gUserAuthenticationGuid         },
  { L"PK",               &gEfiGlobalVariableGuid          },
  { L"KEK",              &gEfiGlobalVariableGuid          },
  { L"db",               &gEfiImageSecurityDatabaseGuid   },
  { L"dbx",              &gEfiImageSecurityDatabaseGuid   },
  { L"dbt",              &gEfiImageSecurityDatabaseGuid   },
  { L"dbr",              &gEfiImageSecurityDatabaseGuid   },
  { L"certdb",           &gEfiCertDbGuid                  },
  { L"VendorKeysNv",     &gEfiVendorKeysNvGuid            },
  { L"SecureBootEnable", &gEfiSecureBootEnableDisableGuid },
  { L"CustomMode",       &gEfiCustomModeEnableGuid        },
};

/**
  Return the preserve-list entry matching a variable, or NULL.
**/
STATIC
CONST PRESERVED_VARIABLE *
FindPreservedVariable (
  IN CONST AUTHENTICATED_VARIABLE_HEADER  *Variable
  )
{
  CONST CHAR16  *Name;
  UINTN         Index;

  Name = (CONST CHAR16 *)(Variable + 1);
  for (Index = 0; Index < ARRAY_SIZE (mPreservedVariables); Index++) {
    if ((Variable->NameSize == StrSize (mPreservedVariables[Index].Name)) &&
        CompareGuid (&Variable->VendorGuid, mPreservedVariables[Index].Guid) &&
        (CompareMem (Name, mPreservedVariables[Index].Name, Variable->NameSize) == 0))
    {
      return &mPreservedVariables[Index];
    }
  }

  return NULL;
}

/**
  Return TRUE if Buffer already holds an entry with the same name and GUID
  as Variable.
**/
STATIC
BOOLEAN
IsAlreadySaved (
  IN CONST UINT8                          *Buffer,
  IN UINTN                                Size,
  IN CONST AUTHENTICATED_VARIABLE_HEADER  *Variable
  )
{
  CONST AUTHENTICATED_VARIABLE_HEADER  *Saved;
  UINTN                                Offset;

  for (Offset = 0; Offset < Size; ) {
    Saved = (CONST AUTHENTICATED_VARIABLE_HEADER *)(Buffer + Offset);
    if ((Saved->NameSize == Variable->NameSize) &&
        CompareGuid (&Saved->VendorGuid, &Variable->VendorGuid) &&
        (CompareMem (Saved + 1, Variable + 1, Variable->NameSize) == 0))
    {
      return TRUE;
    }

    Offset += HEADER_ALIGN (
                sizeof (*Saved) +
                Saved->NameSize + GET_PAD_SIZE (Saved->NameSize) +
                Saved->DataSize + GET_PAD_SIZE (Saved->DataSize)
                );
  }

  return FALSE;
}

/**
  Read the variable store and copy out, verbatim, every live entry whose
  (VendorGuid, Name) is on the preserve list.

  An entry left in VAR_IN_DELETED_TRANSITION state by an interrupted update
  is taken only if there is no fully added copy of the same variable, which
  is the copy the variable driver would use. The State of every returned
  entry is normalised to VAR_ADDED.

  @param[in]  Instance  Pointer to SmmStore instance.
  @param[out] Entries   Saved entries, HEADER_ALIGNMENT-padded. The caller
                        frees it. NULL if nothing was saved.
  @param[out] Size      Size of Entries in bytes, 0 if nothing was saved.

  @retval EFI_SUCCESS    The matching entries, if any, were saved.
  @retval EFI_NOT_FOUND  There is no valid variable store to save from.
  @retval other          The store could not be read.
**/
EFI_STATUS
SavePreservedVariables (
  IN  SMMSTORE_INSTANCE  *Instance,
  OUT UINT8              **Entries,
  OUT UINTN              *Size
  )
{
  EFI_STATUS                     Status;
  UINT8                          *Store;
  UINT8                          *Saved;
  UINTN                          SavedSize;
  UINTN                          StoreSize;
  UINTN                          Offset;
  UINTN                          NumBytes;
  UINTN                          VarStart;
  UINTN                          VarEnd;
  UINTN                          VarSize;
  UINTN                          Pass;
  EFI_FIRMWARE_VOLUME_HEADER     *FvHeader;
  VARIABLE_STORE_HEADER          *StoreHeader;
  AUTHENTICATED_VARIABLE_HEADER  *Variable;
  CONST PRESERVED_VARIABLE       *Match;

  *Entries = NULL;
  *Size    = 0;

  Status = ValidateFvHeader ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "%a: No valid variable store, nothing to preserve\n", __func__));
    return EFI_NOT_FOUND;
  }

  StoreSize = PcdGet32 (PcdFlashNvStorageVariableSize);
  Store     = AllocatePool (StoreSize);
  Saved     = AllocatePool (StoreSize);
  if ((Store == NULL) || (Saved == NULL)) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  //
  // FvbRead cannot cross a block boundary.
  //
  for (Offset = 0; Offset < StoreSize; Offset += NumBytes) {
    NumBytes = MIN (StoreSize - Offset, Instance->BlockSize);
    Status   = FvbRead (&Instance->FvbProtocol, Offset / Instance->BlockSize, 0, &NumBytes, Store + Offset);
    if (!EFI_ERROR (Status) && (NumBytes == 0)) {
      Status = EFI_DEVICE_ERROR;
    }

    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Failed to read the variable store: %r\n", __func__, Status));
      goto Exit;
    }
  }

  FvHeader = (EFI_FIRMWARE_VOLUME_HEADER *)Store;
  if (FvHeader->HeaderLength + sizeof (VARIABLE_STORE_HEADER) > StoreSize) {
    Status = EFI_NOT_FOUND;
    goto Exit;
  }

  StoreHeader = (VARIABLE_STORE_HEADER *)(Store + FvHeader->HeaderLength);
  if (!CompareGuid (&StoreHeader->Signature, &gEfiAuthenticatedVariableGuid)) {
    DEBUG ((DEBUG_INFO, "%a: Not an authenticated variable store, nothing to preserve\n", __func__));
    Status = EFI_NOT_FOUND;
    goto Exit;
  }

  VarStart = HEADER_ALIGN (FvHeader->HeaderLength + sizeof (VARIABLE_STORE_HEADER));
  VarEnd   = MIN (FvHeader->HeaderLength + StoreHeader->Size, StoreSize);

  //
  // Pass 0 takes fully added entries, pass 1 the ones in deleted transition
  // that have no fully added counterpart.
  //
  SetMem (Saved, StoreSize, 0xFF);
  SavedSize = 0;
  for (Pass = 0; Pass < 2; Pass++) {
    for (Offset = VarStart; Offset + sizeof (*Variable) <= VarEnd; Offset += VarSize) {
      Variable = (AUTHENTICATED_VARIABLE_HEADER *)(Store + Offset);
      if (Variable->StartId != VARIABLE_DATA) {
        break;
      }

      if ((Variable->NameSize > VarEnd - Offset) ||
          (Variable->DataSize > VarEnd - Offset))
      {
        break;
      }

      VarSize = HEADER_ALIGN (
                  sizeof (*Variable) +
                  Variable->NameSize + GET_PAD_SIZE (Variable->NameSize) +
                  Variable->DataSize + GET_PAD_SIZE (Variable->DataSize)
                  );
      if (VarSize > VarEnd - Offset) {
        break;
      }

      if (Pass == 0) {
        if (Variable->State != VAR_ADDED) {
          continue;
        }
      } else {
        if (Variable->State != (VAR_IN_DELETED_TRANSITION & VAR_ADDED)) {
          continue;
        }
      }

      Match = FindPreservedVariable (Variable);
      if ((Match == NULL) || IsAlreadySaved (Saved, SavedSize, Variable)) {
        continue;
      }

      DEBUG ((DEBUG_INFO, "%a: Preserving %s (%g), %u bytes\n", __func__, Match->Name, Match->Guid, Variable->DataSize));
      CopyMem (Saved + SavedSize, Variable, sizeof (*Variable) + Variable->NameSize + GET_PAD_SIZE (Variable->NameSize) + Variable->DataSize);
      ((AUTHENTICATED_VARIABLE_HEADER *)(Saved + SavedSize))->State = VAR_ADDED;
      SavedSize += VarSize;
    }
  }

  Status = EFI_SUCCESS;
  if (SavedSize != 0) {
    *Entries = Saved;
    *Size    = SavedSize;
    Saved    = NULL;
  }

Exit:
  if (Store != NULL) {
    FreePool (Store);
  }

  if (Saved != NULL) {
    FreePool (Saved);
  }

  return Status;
}

/**
  Append saved entries to a freshly formatted variable store.

  @param[in]  Instance  Pointer to SmmStore instance.
  @param[in]  Entries   Entries returned by SavePreservedVariables().
  @param[in]  Size      Size of Entries in bytes.

  @retval EFI_SUCCESS  The entries were written.
  @retval other        The write failed; the store may hold a partial entry.
**/
EFI_STATUS
RestorePreservedVariables (
  IN  SMMSTORE_INSTANCE  *Instance,
  IN  UINT8              *Entries,
  IN  UINTN              Size
  )
{
  EFI_STATUS  Status;
  UINTN       Start;
  UINTN       Offset;
  UINTN       Written;
  UINTN       NumBytes;

  //
  // Matches the headers written by InitializeFvAndVariableStoreHeaders().
  //
  Start = HEADER_ALIGN (
            sizeof (EFI_FIRMWARE_VOLUME_HEADER) + sizeof (EFI_FV_BLOCK_MAP_ENTRY) +
            sizeof (VARIABLE_STORE_HEADER)
            );
  if (Size > PcdGet32 (PcdFlashNvStorageVariableSize) - Start) {
    return EFI_BAD_BUFFER_SIZE;
  }

  //
  // FvbWrite cannot cross a block boundary.
  //
  for (Written = 0; Written < Size; Written += NumBytes) {
    Offset   = Start + Written;
    NumBytes = MIN (Size - Written, Instance->BlockSize - Offset % Instance->BlockSize);
    Status   = FvbWrite (
                 &Instance->FvbProtocol,
                 Offset / Instance->BlockSize,
                 Offset % Instance->BlockSize,
                 &NumBytes,
                 Entries + Written
                 );
    if (!EFI_ERROR (Status) && (NumBytes == 0)) {
      Status = EFI_DEVICE_ERROR;
    }

    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  DEBUG ((DEBUG_INFO, "%a: Restored %Lu bytes of preserved variables\n", __func__, (UINT64)Size));
  return EFI_SUCCESS;
}
