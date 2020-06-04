/** @file  CorebootSMMStoreDxe.c

  Copyright (c) 2020, 9elements Agency GmbH<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/
#include <Uefi.h>

#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiRuntimeLib.h>
#include <Library/SMMStoreLib.h>

/*
 * calls into SMM with the given cmd and subcmd in eax, and arg in ebx
 *
 * static inline because the resulting assembly is often smaller than
 * the call sequence due to constant folding.
 */
static inline UINT32 call_smm(UINT8 cmd, UINT8 subcmd, UINT32 arg) {
	UINT32 res = ~0;
	__asm__ __volatile__ (
		"outb %b0, $0xb2"
		: "=a" (res)
		: "a" ((subcmd << 8) | cmd), "b" (arg)
		: "memory");
	return res;
}

#define SMMSTORE_RET_SUCCESS 0
#define SMMSTORE_RET_FAILURE 1
#define SMMSTORE_RET_UNSUPPORTED 2

/* Version 2 only */
#define SMMSTORE_CMD_INIT 4
#define SMMSTORE_CMD_RAW_READ 5
#define SMMSTORE_CMD_RAW_WRITE 6
#define SMMSTORE_CMD_RAW_CLEAR 7

/*
 * This allows the payload to store raw data in the flash regions.
 * This can be used by a FaultTolerantWrite implementation, that uses at least
 * two regions in an A/B update scheme.
 */

#pragma pack(1)
/*
 * Reads a chunk of raw data with size @bufsize from the block specified by
 * @block_id starting at @bufoffset.
 * The read data is placed in @buf.
 *
 * @block_id must be less than num_blocks
 * @bufoffset + @bufsize must be less than block_size
 */
struct smmstore_params_raw_write {
	UINT32 bufsize;
	UINT32 bufoffset;
	UINT32 block_id;
};

/*
 * Writes a chunk of raw data with size @bufsize to the block specified by
 * @block_id starting at @bufoffset.
 *
 * @block_id must be less than num_blocks
 * @bufoffset + @bufsize must be less than block_size
 */
struct smmstore_params_raw_read {
	UINT32 bufsize;
	UINT32 bufoffset;
	UINT32 block_id;
};

/*
 * Erases the specified block.
 *
 * @block_id must be less than num_blocks
 */
struct smmstore_params_raw_clear {
	UINT32 block_id;
};

typedef struct smmstore_comm_buffer {
  union {
    struct smmstore_params_raw_write raw_write;
    struct smmstore_params_raw_read raw_read;
    struct smmstore_params_raw_clear raw_clear;
  };
} SMMSTORE_COMBUF;
#pragma pack(0)

/*
 * A memory buffer to place arguments in.
 */
STATIC SMMSTORE_COMBUF *mArgComBuf;
STATIC UINT32 mArgComBufPhys;

/*
 * Metadata provided by the first stage bootloader.
 */
STATIC SMMSTORE_INFO *mSmmStoreInfo;

/**
  Read from SMMStore

  @param[in] Lba      The starting logical block index to read from.
  @param[in] Offset   Offset into the block at which to begin reading.
  @param[in] NumBytes On input, indicates the requested read size. On
                      output, indicates the actual number of bytes read
  @param[in] Buffer   Pointer to the buffer to read into.

**/
EFI_STATUS
SMMStoreRead (
  IN        EFI_LBA                              Lba,
  IN        UINTN                                Offset,
  IN        UINTN                                *NumBytes,
  IN        UINT8                                *Buffer
  )
{
  UINT32 Result;

  if (!mSmmStoreInfo) {
    return EFI_NO_MEDIA;
  }
  if (Lba >= mSmmStoreInfo->NumBlocks) {
    return EFI_INVALID_PARAMETER;
  }
  if ((*NumBytes + Offset) > mSmmStoreInfo->BlockSize ||
      (*NumBytes + Offset) > mSmmStoreInfo->ComBufferSize) {
    return EFI_INVALID_PARAMETER;
  }

  mArgComBuf->raw_read.bufsize = *NumBytes;
  mArgComBuf->raw_read.bufoffset = Offset;
  mArgComBuf->raw_read.block_id = Lba;

  Result = call_smm(mSmmStoreInfo->ApmCmd, SMMSTORE_CMD_RAW_READ, mArgComBufPhys);
  if (Result == SMMSTORE_RET_FAILURE) {
    return EFI_DEVICE_ERROR;
  } else if (Result == SMMSTORE_RET_UNSUPPORTED) {
    return EFI_UNSUPPORTED;
  } else if (Result != SMMSTORE_RET_SUCCESS) {
    return EFI_NO_RESPONSE;
  }

  CopyMem (Buffer, (VOID *)(mSmmStoreInfo->ComBuffer + Offset), *NumBytes);

  return EFI_SUCCESS;
}


/**
  Write to SMMStore

  @param[in] Lba      The starting logical block index to write to.
  @param[in] Offset   Offset into the block at which to begin writing.
  @param[in] NumBytes On input, indicates the requested write size. On
                      output, indicates the actual number of bytes written
  @param[in] Buffer   Pointer to the data to write.

**/
EFI_STATUS
SMMStoreWrite (
  IN        EFI_LBA                             Lba,
  IN        UINTN                               Offset,
  IN        UINTN                               *NumBytes,
  IN        UINT8                               *Buffer
  )
{
  UINTN             Result;

  if (!mSmmStoreInfo) {
    return EFI_NO_MEDIA;
  }
  if (Lba >= mSmmStoreInfo->NumBlocks) {
    return EFI_INVALID_PARAMETER;
  }
  if ((*NumBytes + Offset) > mSmmStoreInfo->BlockSize ||
      (*NumBytes + Offset) > mSmmStoreInfo->ComBufferSize) {
    return EFI_INVALID_PARAMETER;
  }

  mArgComBuf->raw_write.bufsize = *NumBytes;
  mArgComBuf->raw_write.bufoffset = Offset;
  mArgComBuf->raw_write.block_id = Lba;

  CopyMem ((VOID *)(mSmmStoreInfo->ComBuffer + Offset), Buffer, *NumBytes);

  Result = call_smm(mSmmStoreInfo->ApmCmd, SMMSTORE_CMD_RAW_WRITE, mArgComBufPhys);
  if (Result == SMMSTORE_RET_FAILURE) {
    return EFI_DEVICE_ERROR;
  } else if (Result == SMMSTORE_RET_UNSUPPORTED) {
    return EFI_UNSUPPORTED;
  } else if (Result != SMMSTORE_RET_SUCCESS) {
    return EFI_NO_RESPONSE;
  }

  return EFI_SUCCESS;
}


/**
  Erase a SMMStore block

  @param Lba    The logical block index to erase.

**/
EFI_STATUS
SMMStoreEraseBlock (
  IN   EFI_LBA                              Lba
  )
{
  UINTN             Result;

  if (!mSmmStoreInfo) {
    return EFI_NO_MEDIA;
  }
  if (Lba >= mSmmStoreInfo->NumBlocks) {
    return EFI_INVALID_PARAMETER;
  }

  mArgComBuf->raw_clear.block_id = Lba;

  Result = call_smm(mSmmStoreInfo->ApmCmd, SMMSTORE_CMD_RAW_CLEAR, mArgComBufPhys);
  if (Result == SMMSTORE_RET_FAILURE) {
    return EFI_DEVICE_ERROR;
  } else if (Result == SMMSTORE_RET_UNSUPPORTED) {
    return EFI_UNSUPPORTED;
  } else if (Result != SMMSTORE_RET_SUCCESS) {
    return EFI_NO_RESPONSE;
  }

  return EFI_SUCCESS;
}

VOID
EFIAPI
SMMStoreVirtualNotifyEvent (
  IN EFI_EVENT        Event,
  IN VOID             *Context
  )
{
  EfiConvertPointer (0x0, (VOID**)&mArgComBuf);
  if (mSmmStoreInfo) {
    EfiConvertPointer (0x0, (VOID**)&mSmmStoreInfo->ComBuffer);
    EfiConvertPointer (0x0, (VOID**)&mSmmStoreInfo);
  }

  return;
}

/**
  Initializes SMMStore support

  @param[in] Ptr                  A runtime buffer where arguments are stored
                                  for SMM communication
  @param[in] SmmStoreInfoHob      A runtime buffer with a copy of the
                                  SmmStore Info Hob

  @retval EFI_WRITE_PROTECTED   The SMMSTORE is not present.
  @retval EFI_SUCCESS           The SMMSTORE is supported.

**/
EFI_STATUS
SMMStoreInitialize (
    IN         VOID                      *Ptr,
    IN         SMMSTORE_INFO             *SmmStoreInfoHob
  )
{
  ASSERT (Ptr != NULL);
  ASSERT (SmmStoreInfoHob != NULL);

  mArgComBuf = Ptr;
  mArgComBufPhys = (UINT32)(UINTN)mArgComBuf;

  mSmmStoreInfo = SmmStoreInfoHob;

  return EFI_SUCCESS;
}
