/** @file  AmdSpiLib.h

  Copyright (c) 2020, 9elements Agency GmbH<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef __AMD_SPI_LIB_H__
#define __AMD_SPI_LIB_H__

#include <Base.h>
#include <Uefi/UefiBaseType.h>

/**
  Read from AMD SPI

  @param[in] Lba      The starting logical block index to read from.
  @param[in] Offset   Offset into the block at which to begin reading.
  @param[in] NumBytes On input, indicates the requested read size. On
                      output, indicates the actual number of bytes read
  @param[in] Buffer   Pointer to the buffer to read into.

**/
EFI_STATUS
AmdSpiRead (
  IN        EFI_LBA                              Lba,
  IN        UINTN                                Offset,
  IN        UINTN                                *NumBytes,
  IN        UINT8                                *Buffer
  );


/**
  Write to AMD SPI

  @param[in] Lba      The starting logical block index to write to.
  @param[in] Offset   Offset into the block at which to begin writing.
  @param[in] NumBytes On input, indicates the requested write size. On
                      output, indicates the actual number of bytes written
  @param[in] Buffer   Pointer to the data to write.

**/
EFI_STATUS
AmdSpiWrite (
  IN        EFI_LBA                              Lba,
  IN        UINTN                                Offset,
  IN        UINTN                                *NumBytes,
  IN        UINT8                                *Buffer
  );


/**
  Erase a block using the AMD SPI

  @param Lba    The logical block index to erase.

**/
EFI_STATUS
AmdSpiEraseBlock (
  IN         EFI_LBA                              Lba
  );


/**
  Notify the AMD SPI Library about a VirtualNotify

**/

VOID
EFIAPI
AmdSpiVirtualNotifyEvent (
  IN EFI_EVENT        Event,
  IN VOID             *Context
  );

/**
  Initializes AMD SPI support
  @retval EFI_WRITE_PROTECTED   The AMD SPI is not present.
  @retval EFI_SUCCESS           The AMD SPI is supported.

**/
EFI_STATUS
AmdSpiInitialize (
    VOID
  );

#endif /* __AMD_SPI_LIB_H__ */
