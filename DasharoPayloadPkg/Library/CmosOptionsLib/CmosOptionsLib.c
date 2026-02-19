/** @file
  CmosOptionsLib – coreboot CMOS option table parser and CMOS/RTC I/O.

  Locates the CB_TAG_CMOS_OPTION_TABLE record in the coreboot table passed
  by the bootloader, provides iterators for its sub-records, and reads/writes
  individual option values via the standard PC RTC I/O ports.

  Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/BlParseLib.h>
#include <Library/CmosOptionsLib.h>
#include <Coreboot.h>

//
// Standard PC RTC/CMOS I/O port pairs.
// Low bank  (addresses   0-127): index 0x70, data 0x71.
// High bank (addresses 128-255): index 0x72, data 0x73.
//
#define CMOS_INDEX_LOW   0x70
#define CMOS_DATA_LOW    0x71
#define CMOS_INDEX_HIGH  0x72
#define CMOS_DATA_HIGH   0x73

/**
  Locate the coreboot CMOS option table.
**/
struct cb_cmos_option_table *
EFIAPI
CmosGetOptionTable (
  VOID
  )
{
  return (struct cb_cmos_option_table *)FindCbTag (CB_TAG_CMOS_OPTION_TABLE);
}

/**
  Return the first sub-record with the given tag inside the CMOS option table.
**/
struct cb_record *
EFIAPI
CmosFirstRecord (
  IN struct cb_cmos_option_table  *CmosTable,
  IN UINT32                        Tag
  )
{
  UINT8             *Ptr;
  UINT8             *End;
  struct cb_record  *Rec;

  if (CmosTable == NULL) {
    return NULL;
  }

  Ptr = (UINT8 *)CmosTable + CmosTable->header_length;
  End = (UINT8 *)CmosTable + CmosTable->size;

  while (Ptr + sizeof (struct cb_record) <= End) {
    Rec = (struct cb_record *)Ptr;
    if (Rec->size == 0) {
      break;  // Corrupt table – prevent infinite loop.
    }
    if (Rec->tag == Tag) {
      return Rec;
    }
    Ptr += Rec->size;
  }

  return NULL;
}

/**
  Return the next sub-record with the given tag after Rec.
**/
struct cb_record *
EFIAPI
CmosNextRecord (
  IN struct cb_cmos_option_table  *CmosTable,
  IN UINT32                        Tag,
  IN struct cb_record             *Rec
  )
{
  UINT8             *Ptr;
  UINT8             *End;
  struct cb_record  *Current;

  if ((CmosTable == NULL) || (Rec == NULL)) {
    return NULL;
  }

  End = (UINT8 *)CmosTable + CmosTable->size;
  Ptr = (UINT8 *)Rec + Rec->size;

  while (Ptr + sizeof (struct cb_record) <= End) {
    Current = (struct cb_record *)Ptr;
    if (Current->size == 0) {
      break;
    }
    if (Current->tag == Tag) {
      return Current;
    }
    Ptr += Current->size;
  }

  return NULL;
}

/**
  Read one byte from CMOS/RTC RAM.
**/
UINT8
EFIAPI
CmosReadByte (
  IN UINT16  Address
  )
{
  if (Address < 128) {
    IoWrite8 (CMOS_INDEX_LOW, (UINT8)Address);
    return IoRead8 (CMOS_DATA_LOW);
  } else {
    IoWrite8 (CMOS_INDEX_HIGH, (UINT8)Address);
    return IoRead8 (CMOS_DATA_HIGH);
  }
}

/**
  Write one byte to CMOS/RTC RAM.
**/
VOID
EFIAPI
CmosWriteByte (
  IN UINT16  Address,
  IN UINT8   Data
  )
{
  if (Address < 128) {
    IoWrite8 (CMOS_INDEX_LOW, (UINT8)Address);
    IoWrite8 (CMOS_DATA_LOW, Data);
  } else {
    IoWrite8 (CMOS_INDEX_HIGH, (UINT8)Address);
    IoWrite8 (CMOS_DATA_HIGH, Data);
  }
}

/**
  Locate the CB_TAG_OPTION_CHECKSUM sub-record and return the byte positions
  for the checksummed range and the checksum storage location.

  All three positions are stored as bit offsets in the coreboot table; this
  helper converts them to byte offsets and verifies byte alignment.

  @param[out]  StartByte     First byte covered by the checksum.
  @param[out]  EndByte       Last byte covered by the checksum (inclusive).
  @param[out]  LocationByte  Byte address where the 16-bit checksum is stored
                             (big-endian: high byte at LocationByte, low byte
                             at LocationByte + 1).

  @retval  EFI_SUCCESS           Information retrieved successfully.
  @retval  EFI_NOT_FOUND         No CMOS option table or no checksum record.
  @retval  EFI_UNSUPPORTED       A bit position is not byte-aligned.
**/
STATIC
EFI_STATUS
CmosGetChecksumInfo (
  OUT UINT16  *StartByte,
  OUT UINT16  *EndByte,
  OUT UINT16  *LocationByte
  )
{
  struct cb_cmos_option_table  *CmosTable;
  struct cb_record             *Rec;
  struct cb_cmos_checksum      *CsumRec;

  CmosTable = CmosGetOptionTable ();
  if (CmosTable == NULL) {
    return EFI_NOT_FOUND;
  }

  Rec = CmosFirstRecord (CmosTable, CB_TAG_OPTION_CHECKSUM);
  if (Rec == NULL) {
    return EFI_NOT_FOUND;
  }

  CsumRec = (struct cb_cmos_checksum *)Rec;

  //
  // In the coreboot table, range_start and location are the bit index of
  // the first bit of each field, so they must be multiples of 8.
  // range_end is the bit index of the LAST bit of the last covered byte,
  // i.e. (last_byte * 8 + 7), so its remainder mod 8 must be 7, not 0.
  // This matches the checks in nvramtool checksum_layout_to_bytes().
  //
  if ((CsumRec->range_start % 8) != 0) {
    return EFI_UNSUPPORTED;
  }

  if ((CsumRec->range_end % 8) != 7) {
    return EFI_UNSUPPORTED;
  }

  if ((CsumRec->location % 8) != 0) {
    return EFI_UNSUPPORTED;
  }

  *StartByte    = (UINT16)(CsumRec->range_start / 8);
  *EndByte      = (UINT16)(CsumRec->range_end   / 8);  // (byte*8+7)/8 == byte
  *LocationByte = (UINT16)(CsumRec->location    / 8);
  return EFI_SUCCESS;
}

/**
  Compute the CMOS checksum over the range described by the coreboot checksum
  record.

  The checksum is a simple 16-bit unsigned sum of all bytes in the range,
  truncated to 16 bits – matching the algorithm used by nvramtool.

  @param[out]  Checksum  Receives the computed checksum value.

  @retval  EFI_SUCCESS           Computed successfully.
  @retval  EFI_INVALID_PARAMETER Checksum is NULL.
  @retval  EFI_NOT_FOUND         No CMOS option table or no checksum record.
  @retval  EFI_UNSUPPORTED       Checksum range is not byte-aligned.
**/
EFI_STATUS
EFIAPI
CmosChecksumCompute (
  UINT16 *Checksum
  )
{
  EFI_STATUS  Status;
  UINT16      StartByte;
  UINT16      EndByte;
  UINT16      LocationByte;
  UINT16      i;
  UINT32      Sum;

  Status = CmosGetChecksumInfo (&StartByte, &EndByte, &LocationByte);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Sum = 0;
  for (i = StartByte; i <= EndByte; i++) {
    Sum += CmosReadByte (i);
  }

  *Checksum = Sum & 0xFFFF;

  return EFI_SUCCESS;
}

/**
  Write a 16-bit checksum to CMOS.

  The checksum is stored in big-endian byte order: high byte first.

  @param[in]  Checksum  Value to write.

  @retval  EFI_SUCCESS      Written successfully.
  @retval  EFI_NOT_FOUND    No CMOS option table or no checksum record.
  @retval  EFI_UNSUPPORTED  Checksum location is not byte-aligned.
**/
EFI_STATUS
EFIAPI
CmosChecksumWrite (
  IN UINT16  Checksum
  )
{
  EFI_STATUS  Status;
  UINT16      StartByte;
  UINT16      EndByte;
  UINT16      LocationByte;

  Status = CmosGetChecksumInfo (&StartByte, &EndByte, &LocationByte);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  CmosWriteByte (LocationByte,               (UINT8)(Checksum >> 8));
  CmosWriteByte ((UINT16)(LocationByte + 1), (UINT8)(Checksum & 0x00FF));
  return EFI_SUCCESS;
}

/**
  Recompute the CMOS checksum and write it back to CMOS.

  Convenience wrapper: calls CmosChecksumCompute() then CmosChecksumWrite().

  @retval  EFI_SUCCESS      Checksum updated successfully.
  @retval  EFI_NOT_FOUND    No CMOS option table or no checksum record.
  @retval  EFI_UNSUPPORTED  Checksum range or location is not byte-aligned.
**/
EFI_STATUS
EFIAPI
CmosChecksumUpdate (
  VOID
  )
{
  EFI_STATUS Status;
  UINT16     Checksum;

  Status = CmosChecksumCompute(&Checksum);
  if (EFI_ERROR(Status))
    return Status;

  return CmosChecksumWrite (Checksum);
}

/**
  Read the current value of a CMOS option into a UINT32.

  Bits are read LSB-first: entry->bit is the least-significant bit of the
  value, stored in byte (entry->bit / 8), bit (entry->bit % 8).
**/
EFI_STATUS
EFIAPI
CmosReadEntry (
  IN  struct cb_cmos_entries  *Entry,
  OUT UINT32                  *Value
  )
{
  UINT32  BitPos;
  UINT32  BitsLeft;
  UINT32  BitsRead;
  UINT32  BitInByte;
  UINT32  BitsFromByte;
  UINT8   ByteVal;
  UINT32  Result;

  if ((Entry == NULL) || (Value == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Entry->config == CMOS_ENTRY_TYPE_RESERVED) ||
      (Entry->config == CMOS_ENTRY_TYPE_STRING))
  {
    return EFI_UNSUPPORTED;
  }

  if (Entry->length > 32) {
    return EFI_UNSUPPORTED;
  }

  Result   = 0;
  BitPos   = Entry->bit;
  BitsLeft = Entry->length;
  BitsRead = 0;

  while (BitsLeft > 0) {
    BitInByte    = BitPos % 8;
    BitsFromByte = MIN (8 - BitInByte, BitsLeft);

    ByteVal  = CmosReadByte ((UINT16)(BitPos / 8));
    ByteVal >>= BitInByte;
    ByteVal  &= (UINT8)((1u << BitsFromByte) - 1u);

    Result   |= (UINT32)ByteVal << BitsRead;
    BitPos   += BitsFromByte;
    BitsLeft -= BitsFromByte;
    BitsRead += BitsFromByte;
  }

  *Value = Result;
  return EFI_SUCCESS;
}

/**
  Write a value to a CMOS option using read-modify-write per byte.
**/
EFI_STATUS
EFIAPI
CmosWriteEntry (
  IN struct cb_cmos_entries  *Entry,
  IN UINT32                   Value
  )
{
  UINT32      BitPos;
  UINT32      BitsLeft;
  UINT32      BitInByte;
  UINT32      BitsFromByte;
  UINT8       Mask;
  UINT8       ByteVal;
  EFI_STATUS  CsumStatus;

  if (Entry == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Entry->config == CMOS_ENTRY_TYPE_RESERVED) ||
      (Entry->config == CMOS_ENTRY_TYPE_STRING))
  {
    return EFI_UNSUPPORTED;
  }

  if (Entry->length > 32) {
    return EFI_UNSUPPORTED;
  }

  BitPos   = Entry->bit;
  BitsLeft = Entry->length;

  while (BitsLeft > 0) {
    BitInByte    = BitPos % 8;
    BitsFromByte = MIN (8 - BitInByte, BitsLeft);

    Mask    = (UINT8)(((1u << BitsFromByte) - 1u) << BitInByte);
    ByteVal = CmosReadByte ((UINT16)(BitPos / 8));
    ByteVal &= ~Mask;
    ByteVal |= (UINT8)((Value & ((1u << BitsFromByte) - 1u)) << BitInByte);
    CmosWriteByte ((UINT16)(BitPos / 8), ByteVal);

    Value    >>= BitsFromByte;
    BitPos   += BitsFromByte;
    BitsLeft -= BitsFromByte;
  }

  //
  // Refresh the CMOS checksum after modifying CMOS contents.  Ignore
  // EFI_NOT_FOUND – not all coreboot builds include a checksum record.
  //
  CsumStatus = CmosChecksumUpdate ();
  if (EFI_ERROR (CsumStatus) && (CsumStatus != EFI_NOT_FOUND)) {
    return CsumStatus;
  }

  return EFI_SUCCESS;
}

/**
  Find a CMOS option entry by its ASCII name.

  Scans all CB_TAG_OPTION sub-records in the CMOS option table for one whose
  name field matches Name (case-sensitive, up to CMOS_MAX_NAME_LENGTH bytes).
**/
struct cb_cmos_entries *
EFIAPI
CmosFindEntryByName (
  IN CONST CHAR8  *Name
  )
{
  struct cb_cmos_option_table  *CmosTable;
  struct cb_record             *Rec;
  struct cb_cmos_entries       *Entry;

  if (Name == NULL) {
    return NULL;
  }

  // Get the base CMOS option table
  CmosTable = CmosGetOptionTable ();
  if (CmosTable == NULL) {
    return NULL;
  }

  // Iterate through all records looking for CB_TAG_OPTION
  Rec = CmosFirstRecord (CmosTable, CB_TAG_OPTION);
  while (Rec != NULL) {
    Entry = (struct cb_cmos_entries *)Rec;

    // Compare the option name with the requested name.
    // Assumes Name is null-terminated and Entry->name is properly formatted.
    if (AsciiStrnCmp ((CONST CHAR8 *)Entry->name, Name, CMOS_MAX_NAME_LENGTH) == 0) {
      return Entry;
    }

    // Move to the next CB_TAG_OPTION record
    Rec = CmosNextRecord (CmosTable, CB_TAG_OPTION, Rec);
  }

  return NULL;
}

/**
  Read a CMOS option value by name.

  Convenience wrapper around CmosFindEntryByName() + CmosReadEntry().
**/
EFI_STATUS
EFIAPI
CmosReadOptionByName (
  IN  CONST CHAR8  *Name,
  OUT UINT32       *Value
  )
{
  struct cb_cmos_entries  *Entry;

  if ((Name == NULL) || (Value == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Entry = CmosFindEntryByName (Name);
  if (Entry == NULL) {
    return EFI_NOT_FOUND;
  }

  return CmosReadEntry (Entry, Value);
}

/**
  Write a CMOS option value by name.

  Convenience wrapper around CmosFindEntryByName() + CmosWriteEntry().
**/
EFI_STATUS
EFIAPI
CmosWriteOptionByName (
  IN CONST CHAR8  *Name,
  IN UINT32        Value
  )
{
  struct cb_cmos_entries  *Entry;

  if (Name == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Entry = CmosFindEntryByName (Name);
  if (Entry == NULL) {
    return EFI_NOT_FOUND;
  }

  return CmosWriteEntry (Entry, Value);
}
