/** @file
  SMBIOS-derived system information rendered in the graphical setup header
  (vendor / product, firmware version, CPU brand, total RAM) plus a helper
  to format the current real-time clock.

  The lookup walks gST->ConfigurationTable for the SMBIOS 3.0 entry point
  (falling back to the 2.x entry point) and iterates structures linearly,
  pulling strings by their 1-based index from the trailing string list.
  Cached on first call — SMBIOS doesn't change at runtime.

  Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "GraphicsDisplayEngine.h"

#include <Guid/SmBios.h>
#include <IndustryStandard/SmBios.h>

SYSTEM_INFO  mSysInfo;

STATIC
BOOLEAN
FindSmbiosTable (
  OUT VOID    **TableBase,
  OUT UINT32  *TableLen
  )
{
  UINTN  i;

  // Prefer the 64-bit entry point; otherwise fall back to legacy.
  for (i = 0; i < gST->NumberOfTableEntries; i++) {
    if (CompareGuid (&gST->ConfigurationTable[i].VendorGuid, &gEfiSmbios3TableGuid)) {
      SMBIOS_TABLE_3_0_ENTRY_POINT  *Ep = gST->ConfigurationTable[i].VendorTable;
      *TableBase = (VOID *)(UINTN)Ep->TableAddress;
      *TableLen  = Ep->TableMaximumSize;
      return TRUE;
    }
  }
  for (i = 0; i < gST->NumberOfTableEntries; i++) {
    if (CompareGuid (&gST->ConfigurationTable[i].VendorGuid, &gEfiSmbiosTableGuid)) {
      SMBIOS_TABLE_ENTRY_POINT  *Ep = gST->ConfigurationTable[i].VendorTable;
      *TableBase = (VOID *)(UINTN)Ep->TableAddress;
      *TableLen  = Ep->TableLength;
      return TRUE;
    }
  }
  return FALSE;
}

// Fetches the Index-th string (1-based) from the trailing string list of an
// SMBIOS structure. Returns NULL if the index is 0 or out of range.
STATIC
CONST CHAR8 *
SmbiosString (
  IN SMBIOS_STRUCTURE  *Struct,
  IN UINT8             Index
  )
{
  CONST CHAR8  *p;
  UINT8        n;

  if ((Index == 0) || (Struct->Length < sizeof (SMBIOS_STRUCTURE))) {
    return NULL;
  }
  p = (CONST CHAR8 *)Struct + Struct->Length;
  for (n = 1; n < Index; n++) {
    while (*p != 0) {
      p++;
    }
    p++;
    if (*p == 0) {
      return NULL;  // walked off the end of the string list
    }
  }
  return *p == 0 ? NULL : p;
}

STATIC
SMBIOS_STRUCTURE *
NextStructure (
  IN SMBIOS_STRUCTURE  *Cur
  )
{
  CONST UINT8  *p = (CONST UINT8 *)Cur + Cur->Length;
  // String list ends with a double-NUL.
  while ((p[0] != 0) || (p[1] != 0)) {
    p++;
  }
  return (SMBIOS_STRUCTURE *)(p + 2);
}

STATIC
VOID
AsciiToWide (
  IN  CONST CHAR8  *Src,
  OUT CHAR16       *Dst,
  IN  UINTN        DstChars
  )
{
  UINTN  i;

  if ((Src == NULL) || (DstChars == 0)) {
    if (DstChars > 0) {
      Dst[0] = L'\0';
    }
    return;
  }
  for (i = 0; (i < DstChars - 1) && (Src[i] != 0); i++) {
    Dst[i] = (CHAR16)(UINT8)Src[i];
  }
  Dst[i] = L'\0';
}

VOID
SystemInfoCollect (
  VOID
  )
{
  VOID              *TableBase;
  UINT32            TableLen;
  SMBIOS_STRUCTURE  *s;
  UINT8             *End;

  if (mSysInfo.Collected) {
    return;
  }
  mSysInfo.Collected = TRUE;

  StrCpyS (mSysInfo.SystemVendor,  ARRAY_SIZE (mSysInfo.SystemVendor),  L"?");
  StrCpyS (mSysInfo.SystemProduct, ARRAY_SIZE (mSysInfo.SystemProduct), L"?");
  StrCpyS (mSysInfo.BiosVersion,   ARRAY_SIZE (mSysInfo.BiosVersion),   L"?");
  StrCpyS (mSysInfo.CpuBrand,      ARRAY_SIZE (mSysInfo.CpuBrand),      L"?");
  mSysInfo.TotalRamMB = 0;

  if (!FindSmbiosTable (&TableBase, &TableLen)) {
    DEBUG ((DEBUG_ERROR, "SystemInfo: no SMBIOS table found in ConfigurationTable\n"));
    return;
  }
  DEBUG ((
    DEBUG_ERROR,
    "SystemInfo: SMBIOS at 0x%p, len=0x%x\n",
    TableBase, (UINTN)TableLen
    ));

  s   = (SMBIOS_STRUCTURE *)TableBase;
  End = (UINT8 *)TableBase + TableLen;

  while (((UINT8 *)s + sizeof (SMBIOS_STRUCTURE)) <= End) {
    if (s->Length < sizeof (SMBIOS_STRUCTURE)) {
      break;  // malformed
    }

    switch (s->Type) {
      case 0: {  // BIOS Information
        SMBIOS_TABLE_TYPE0  *t = (SMBIOS_TABLE_TYPE0 *)s;
        AsciiToWide (
          SmbiosString (s, t->BiosVersion),
          mSysInfo.BiosVersion, ARRAY_SIZE (mSysInfo.BiosVersion)
          );
        break;
      }
      case 1: {  // System Information
        SMBIOS_TABLE_TYPE1  *t = (SMBIOS_TABLE_TYPE1 *)s;
        AsciiToWide (
          SmbiosString (s, t->Manufacturer),
          mSysInfo.SystemVendor, ARRAY_SIZE (mSysInfo.SystemVendor)
          );
        AsciiToWide (
          SmbiosString (s, t->ProductName),
          mSysInfo.SystemProduct, ARRAY_SIZE (mSysInfo.SystemProduct)
          );
        break;
      }
      case 4: {  // Processor Information
        SMBIOS_TABLE_TYPE4  *t = (SMBIOS_TABLE_TYPE4 *)s;
        AsciiToWide (
          SmbiosString (s, t->ProcessorVersion),
          mSysInfo.CpuBrand, ARRAY_SIZE (mSysInfo.CpuBrand)
          );
        break;
      }
      case 17: {  // Memory Device — sum sizes across DIMMs.
        SMBIOS_TABLE_TYPE17  *t = (SMBIOS_TABLE_TYPE17 *)s;
        UINT64               SizeMB = 0;
        if ((t->Size != 0xFFFF) && (t->Size != 0)) {
          if (t->Size == 0x7FFF) {
            SizeMB = t->ExtendedSize & 0x7FFFFFFF;
          } else if ((t->Size & 0x8000) != 0) {
            SizeMB = (t->Size & 0x7FFF) / 1024;  // KB → MB (rounds down)
          } else {
            SizeMB = t->Size;                     // already MB
          }
        }
        mSysInfo.TotalRamMB += SizeMB;
        break;
      }
      default:
        break;
    }

    if (s->Type == SMBIOS_TYPE_END_OF_TABLE) {
      break;
    }
    s = NextStructure (s);
    if ((UINT8 *)s >= End) {
      break;
    }
  }

  DEBUG ((
    DEBUG_ERROR,
    "SystemInfo: vendor=\"%s\" product=\"%s\" bios=\"%s\" cpu=\"%s\" ram=%lu MB\n",
    mSysInfo.SystemVendor,
    mSysInfo.SystemProduct,
    mSysInfo.BiosVersion,
    mSysInfo.CpuBrand,
    mSysInfo.TotalRamMB
    ));
}

VOID
SystemInfoFormatTime (
  OUT CHAR16  *Buf,
  IN  UINTN   BufSize
  )
{
  EFI_TIME  T;

  if (EFI_ERROR (gRT->GetTime (&T, NULL))) {
    StrCpyS (Buf, BufSize / sizeof (CHAR16), L"--:--:--");
    return;
  }
  UnicodeSPrint (
    Buf, BufSize,
    L"%04u-%02u-%02u  %02u:%02u:%02u",
    (UINT32)T.Year, (UINT32)T.Month,  (UINT32)T.Day,
    (UINT32)T.Hour, (UINT32)T.Minute, (UINT32)T.Second
    );
}

VOID
SystemInfoFormatSummary (
  OUT CHAR16  *Buf,
  IN  UINTN   BufSize
  )
{
  if (mSysInfo.TotalRamMB > 0) {
    UnicodeSPrint (
      Buf, BufSize,
      L"PLATFORM %s  \x00B7  FW %s  \x00B7  CPU %s  \x00B7  RAM %u GiB",
      mSysInfo.SystemProduct,
      mSysInfo.BiosVersion,
      mSysInfo.CpuBrand,
      (UINT32)((mSysInfo.TotalRamMB + 1023) / 1024)
      );
  } else {
    UnicodeSPrint (
      Buf, BufSize,
      L"PLATFORM %s  \x00B7  FW %s  \x00B7  CPU %s",
      mSysInfo.SystemProduct,
      mSysInfo.BiosVersion,
      mSysInfo.CpuBrand
      );
  }
}
