/** @file
  Library interface for parsing the coreboot CMOS option table and
  accessing RTC/CMOS RAM.

  The coreboot CMOS option table (CB_TAG_CMOS_OPTION_TABLE) describes the
  layout of CMOS/RTC RAM on a given board.  This library lets callers:

    - Locate the table via the coreboot table pointer passed by the bootloader.
    - Iterate over cb_cmos_entries and cb_cmos_enums sub-records.
    - Read and write individual option values using the hardware I/O ports.

  Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef _CMOS_OPTIONS_LIB_H_
#define _CMOS_OPTIONS_LIB_H_

#include <Uefi.h>
#include <Coreboot.h>

/**
  Locate the coreboot CMOS option table.

  Searches the coreboot table (pointed to by the bootloader parameter) for a
  record tagged CB_TAG_CMOS_OPTION_TABLE.

  @return  Pointer to struct cb_cmos_option_table, or NULL if not found.
**/
struct cb_cmos_option_table *
EFIAPI
CmosGetOptionTable (
  VOID
  );

/**
  Return the first sub-record with the given tag inside the CMOS option table.

  Sub-records are stored immediately after the cb_cmos_option_table header
  (at offset header_length) and traversed by the size field, exactly like
  top-level coreboot table records.

  @param[in]  CmosTable  Pointer to the CMOS option table.
  @param[in]  Tag        Tag value to search for (CB_TAG_OPTION etc.).

  @return  Pointer to the first matching sub-record, or NULL.
**/
struct cb_record *
EFIAPI
CmosFirstRecord (
  IN struct cb_cmos_option_table  *CmosTable,
  IN UINT32                        Tag
  );

/**
  Return the next sub-record with the given tag after Rec.

  @param[in]  CmosTable  Pointer to the CMOS option table.
  @param[in]  Tag        Tag value to search for.
  @param[in]  Rec        The current record; the search starts after it.

  @return  Pointer to the next matching sub-record, or NULL.
**/
struct cb_record *
EFIAPI
CmosNextRecord (
  IN struct cb_cmos_option_table  *CmosTable,
  IN UINT32                        Tag,
  IN struct cb_record             *Rec
  );

/**
  Read one byte from CMOS/RTC RAM.

  Addresses 0-127 are accessed via ports 0x70/0x71.
  Addresses 128-255 are accessed via ports 0x72/0x73.

  @param[in]  Address  CMOS address (0-255).

  @return  The byte value at that address.
**/
UINT8
EFIAPI
CmosReadByte (
  IN UINT16  Address
  );

/**
  Write one byte to CMOS/RTC RAM.

  @param[in]  Address  CMOS address (0-255).
  @param[in]  Data     Value to write.
**/
VOID
EFIAPI
CmosWriteByte (
  IN UINT16  Address,
  IN UINT8   Data
  );

/**
  Read the current value of a CMOS option into a UINT32.

  Handles arbitrary bit positions and lengths up to 32 bits by doing
  byte-granularity reads and assembling the result from LSB to MSB.

  Reserved ('r') and string ('s') entries are not supported and return
  EFI_UNSUPPORTED.  Options longer than 32 bits also return EFI_UNSUPPORTED.

  @param[in]   Entry   cb_cmos_entries record describing the option.
  @param[out]  Value   Receives the current value.

  @retval  EFI_SUCCESS            Read successfully.
  @retval  EFI_INVALID_PARAMETER  Entry or Value is NULL.
  @retval  EFI_UNSUPPORTED        Reserved/string type, or length > 32.
**/
EFI_STATUS
EFIAPI
CmosReadEntry (
  IN  struct cb_cmos_entries  *Entry,
  OUT UINT32                  *Value
  );

/**
  Write a value to a CMOS option.

  Only the bits belonging to the option are modified; surrounding bits in
  each affected byte are preserved with a read-modify-write sequence.

  @param[in]  Entry   cb_cmos_entries record describing the option.
  @param[in]  Value   New value (only the low Entry->length bits are used).

  @retval  EFI_SUCCESS            Written successfully.
  @retval  EFI_INVALID_PARAMETER  Entry is NULL.
  @retval  EFI_UNSUPPORTED        Reserved/string type, or length > 32.
**/
EFI_STATUS
EFIAPI
CmosWriteEntry (
  IN struct cb_cmos_entries  *Entry,
  IN UINT32                   Value
  );

/**
  Find a CMOS option entry by its ASCII name.

  Scans all CB_TAG_OPTION sub-records in the CMOS option table for one whose
  name field matches Name (case-sensitive, up to CMOS_MAX_NAME_LENGTH bytes).

  @param[in]  Name  Null-terminated ASCII option name (e.g. "hyper_threading").

  @return  Pointer to the matching cb_cmos_entries record, or NULL if not found
           or if there is no CMOS option table.
**/
struct cb_cmos_entries *
EFIAPI
CmosFindEntryByName (
  IN CONST CHAR8  *Name
  );

/**
  Read a CMOS option value by name.

  Convenience wrapper around CmosFindEntryByName() + CmosReadEntry().

  @param[in]   Name   Null-terminated ASCII option name.
  @param[out]  Value  Receives the current value.

  @retval  EFI_SUCCESS            Found and read successfully.
  @retval  EFI_NOT_FOUND          No option with that name exists.
  @retval  EFI_INVALID_PARAMETER  Name or Value is NULL.
  @retval  EFI_UNSUPPORTED        Option is reserved/string or longer than 32 bits.
**/
EFI_STATUS
EFIAPI
CmosReadOptionByName (
  IN  CONST CHAR8  *Name,
  OUT UINT32       *Value
  );

/**
  Write a CMOS option value by name.

  Convenience wrapper around CmosFindEntryByName() + CmosWriteEntry().

  @param[in]  Name   Null-terminated ASCII option name.
  @param[in]  Value  New value (high bits beyond the option's length are ignored).

  @retval  EFI_SUCCESS            Found and written successfully.
  @retval  EFI_NOT_FOUND          No option with that name exists.
  @retval  EFI_INVALID_PARAMETER  Name is NULL.
  @retval  EFI_UNSUPPORTED        Option is reserved/string or longer than 32 bits.
**/
EFI_STATUS
EFIAPI
CmosWriteOptionByName (
  IN CONST CHAR8  *Name,
  IN UINT32        Value
  );

/**
  Compute the CMOS checksum over the range described by the coreboot checksum
  record.

  The checksum is a simple 16-bit unsigned sum of all bytes in the checksummed
  area, truncated to 16 bits, matching the algorithm used by nvramtool.

  @param[out]  Checksum  Receives the computed checksum value.

  @retval  EFI_SUCCESS           Computed successfully.
  @retval  EFI_INVALID_PARAMETER Checksum is NULL.
  @retval  EFI_NOT_FOUND         No CMOS option table or no checksum record.
  @retval  EFI_UNSUPPORTED       Checksum range is not byte-aligned.
           the range is not byte-aligned.
**/
EFI_STATUS
EFIAPI
CmosChecksumCompute (
  UINT16 *Checksum
  );

/**
  Write a 16-bit checksum to the CMOS checksum location.

  The value is stored in big-endian byte order: high byte first.

  @param[in]  Checksum  Value to write.

  @retval  EFI_SUCCESS      Written successfully.
  @retval  EFI_NOT_FOUND    No CMOS option table or no checksum record.
  @retval  EFI_UNSUPPORTED  Checksum location is not byte-aligned.
**/
EFI_STATUS
EFIAPI
CmosChecksumWrite (
  IN UINT16  Checksum
  );

/**
  Recompute the CMOS checksum and write it back to CMOS.

  Convenience wrapper: calls CmosChecksumCompute() then CmosChecksumWrite().
  This is called automatically by CmosWriteEntry() after every write.

  @retval  EFI_SUCCESS      Checksum updated successfully.
  @retval  EFI_NOT_FOUND    No CMOS option table or no checksum record.
  @retval  EFI_UNSUPPORTED  Checksum range or location is not byte-aligned.
**/
EFI_STATUS
EFIAPI
CmosChecksumUpdate (
  VOID
  );

#endif // _CMOS_OPTIONS_LIB_H_
