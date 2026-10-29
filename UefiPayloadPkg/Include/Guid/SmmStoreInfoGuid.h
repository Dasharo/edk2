/** @file
  This file defines the hob structure for coreboot's SmmStore.

  Copyright (c) 2022, 9elements GmbH<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#pragma once

///
/// System Table Information GUID
///
extern EFI_GUID  gEfiSmmStoreInfoHobGuid;

typedef struct {
  UINT64    ComBuffer;
  UINT32    ComBufferSize;
  UINT32    NumBlocks;
  UINT32    BlockSize;
  UINT64    MmioAddress;
  UINT8     ApmCmd;
  //
  // Fields below were added later and are absent in HOBs produced by older
  // firmware, which is detected by the size of the HOB.  Tail padding of the
  // original structure can contain garbage, hence it's not used.
  //
  UINT8     Reserved[7];
  UINT32    FullFlashSmallBlockSize; // Block size of raw full flash commands
                                     // with small blocks, zero if unsupported
} SMMSTORE_INFO;
