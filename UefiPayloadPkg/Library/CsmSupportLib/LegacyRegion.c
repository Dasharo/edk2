/** @file
  Legacy Region Support

  Copyright (c) 2006 - 2016, Intel Corporation. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "LegacyRegion.h"

//
// Intel PAM map.
//
// PAM Range       Offset Bits  Operation
// ===============  ====  ====  ===============================================================
// 0xC0000-0xC3FFF  0x81  1:0   00 = DRAM Disabled, 01= Read Only, 10 = Write Only, 11 = Normal
// 0xC4000-0xC7FFF  0x81  5:4   00 = DRAM Disabled, 01= Read Only, 10 = Write Only, 11 = Normal
// 0xC8000-0xCBFFF  0x82  1:0   00 = DRAM Disabled, 01= Read Only, 10 = Write Only, 11 = Normal
// 0xCC000-0xCFFFF  0x82  5:4   00 = DRAM Disabled, 01= Read Only, 10 = Write Only, 11 = Normal
// 0xD0000-0xD3FFF  0x83  1:0   00 = DRAM Disabled, 01= Read Only, 10 = Write Only, 11 = Normal
// 0xD4000-0xD7FFF  0x83  5:4   00 = DRAM Disabled, 01= Read Only, 10 = Write Only, 11 = Normal
// 0xD8000-0xDBFFF  0x84  1:0   00 = DRAM Disabled, 01= Read Only, 10 = Write Only, 11 = Normal
// 0xDC000-0xDFFFF  0x84  5:4   00 = DRAM Disabled, 01= Read Only, 10 = Write Only, 11 = Normal
// 0xE0000-0xE3FFF  0x85  1:0   00 = DRAM Disabled, 01= Read Only, 10 = Write Only, 11 = Normal
// 0xE4000-0xE7FFF  0x85  5:4   00 = DRAM Disabled, 01= Read Only, 10 = Write Only, 11 = Normal
// 0xE8000-0xEBFFF  0x86  1:0   00 = DRAM Disabled, 01= Read Only, 10 = Write Only, 11 = Normal
// 0xEC000-0xEFFFF  0x86  5:4   00 = DRAM Disabled, 01= Read Only, 10 = Write Only, 11 = Normal
// 0xF0000-0xFFFFF  0x80  5:4   00 = DRAM Disabled, 01= Read Only, 10 = Write Only, 11 = Normal
//
STATIC LEGACY_MEMORY_SECTION_INFO   mSectionArrayIntel[] = {
  {0xC0000, SIZE_16KB, FALSE, FALSE},
  {0xC4000, SIZE_16KB, FALSE, FALSE},
  {0xC8000, SIZE_16KB, FALSE, FALSE},
  {0xCC000, SIZE_16KB, FALSE, FALSE},
  {0xD0000, SIZE_16KB, FALSE, FALSE},
  {0xD4000, SIZE_16KB, FALSE, FALSE},
  {0xD8000, SIZE_16KB, FALSE, FALSE},
  {0xDC000, SIZE_16KB, FALSE, FALSE},
  {0xE0000, SIZE_16KB, FALSE, FALSE},
  {0xE4000, SIZE_16KB, FALSE, FALSE},
  {0xE8000, SIZE_16KB, FALSE, FALSE},
  {0xEC000, SIZE_16KB, FALSE, FALSE},
  {0xF0000, SIZE_64KB, FALSE, FALSE}
};

STATIC INTEL_SEGMENT_REGISTER_VALUE  mRegisterValuesIntel[] = {
  {DRAMC_REGISTER (MCH_PAM1), 0x01, 0x02},
  {DRAMC_REGISTER (MCH_PAM1), 0x10, 0x20},
  {DRAMC_REGISTER (MCH_PAM2), 0x01, 0x02},
  {DRAMC_REGISTER (MCH_PAM2), 0x10, 0x20},
  {DRAMC_REGISTER (MCH_PAM3), 0x01, 0x02},
  {DRAMC_REGISTER (MCH_PAM3), 0x10, 0x20},
  {DRAMC_REGISTER (MCH_PAM4), 0x01, 0x02},
  {DRAMC_REGISTER (MCH_PAM4), 0x10, 0x20},
  {DRAMC_REGISTER (MCH_PAM5), 0x01, 0x02},
  {DRAMC_REGISTER (MCH_PAM5), 0x10, 0x20},
  {DRAMC_REGISTER (MCH_PAM6), 0x01, 0x02},
  {DRAMC_REGISTER (MCH_PAM6), 0x10, 0x20},
  {DRAMC_REGISTER (MCH_PAM0), 0x10, 0x20}
};

//
// AMD sets legacy segments memory access with fixed MTRRs
//
STATIC LEGACY_MEMORY_SECTION_INFO mSectionArrayAMD[] = {
  {0xC0000, SIZE_4KB, FALSE, FALSE},
  {0xC1000, SIZE_4KB, FALSE, FALSE},
  {0xC2000, SIZE_4KB, FALSE, FALSE},
  {0xC3000, SIZE_4KB, FALSE, FALSE},
  {0xC4000, SIZE_4KB, FALSE, FALSE},
  {0xC5000, SIZE_4KB, FALSE, FALSE},
  {0xC6000, SIZE_4KB, FALSE, FALSE},
  {0xC7000, SIZE_4KB, FALSE, FALSE},
  {0xC8000, SIZE_4KB, FALSE, FALSE},
  {0xC9000, SIZE_4KB, FALSE, FALSE},
  {0xCA000, SIZE_4KB, FALSE, FALSE},
  {0xCB000, SIZE_4KB, FALSE, FALSE},
  {0xCC000, SIZE_4KB, FALSE, FALSE},
  {0xCD000, SIZE_4KB, FALSE, FALSE},
  {0xCE000, SIZE_4KB, FALSE, FALSE},
  {0xCF000, SIZE_4KB, FALSE, FALSE},
  {0xD0000, SIZE_4KB, FALSE, FALSE},
  {0xD1000, SIZE_4KB, FALSE, FALSE},
  {0xD2000, SIZE_4KB, FALSE, FALSE},
  {0xD3000, SIZE_4KB, FALSE, FALSE},
  {0xD4000, SIZE_4KB, FALSE, FALSE},
  {0xD5000, SIZE_4KB, FALSE, FALSE},
  {0xD6000, SIZE_4KB, FALSE, FALSE},
  {0xD7000, SIZE_4KB, FALSE, FALSE},
  {0xD8000, SIZE_4KB, FALSE, FALSE},
  {0xD9000, SIZE_4KB, FALSE, FALSE},
  {0xDA000, SIZE_4KB, FALSE, FALSE},
  {0xDB000, SIZE_4KB, FALSE, FALSE},
  {0xDC000, SIZE_4KB, FALSE, FALSE},
  {0xDD000, SIZE_4KB, FALSE, FALSE},
  {0xDE000, SIZE_4KB, FALSE, FALSE},
  {0xDF000, SIZE_4KB, FALSE, FALSE},
  {0xE0000, SIZE_4KB, FALSE, FALSE},
  {0xE1000, SIZE_4KB, FALSE, FALSE},
  {0xE2000, SIZE_4KB, FALSE, FALSE},
  {0xE3000, SIZE_4KB, FALSE, FALSE},
  {0xE4000, SIZE_4KB, FALSE, FALSE},
  {0xE5000, SIZE_4KB, FALSE, FALSE},
  {0xE6000, SIZE_4KB, FALSE, FALSE},
  {0xE7000, SIZE_4KB, FALSE, FALSE},
  {0xE8000, SIZE_4KB, FALSE, FALSE},
  {0xE9000, SIZE_4KB, FALSE, FALSE},
  {0xEA000, SIZE_4KB, FALSE, FALSE},
  {0xEB000, SIZE_4KB, FALSE, FALSE},
  {0xEC000, SIZE_4KB, FALSE, FALSE},
  {0xED000, SIZE_4KB, FALSE, FALSE},
  {0xEE000, SIZE_4KB, FALSE, FALSE},
  {0xEF000, SIZE_4KB, FALSE, FALSE},
  {0xF0000, SIZE_4KB, FALSE, FALSE},
  {0xF1000, SIZE_4KB, FALSE, FALSE},
  {0xF2000, SIZE_4KB, FALSE, FALSE},
  {0xF3000, SIZE_4KB, FALSE, FALSE},
  {0xF4000, SIZE_4KB, FALSE, FALSE},
  {0xF5000, SIZE_4KB, FALSE, FALSE},
  {0xF6000, SIZE_4KB, FALSE, FALSE},
  {0xF7000, SIZE_4KB, FALSE, FALSE},
  {0xF8000, SIZE_4KB, FALSE, FALSE},
  {0xF9000, SIZE_4KB, FALSE, FALSE},
  {0xFA000, SIZE_4KB, FALSE, FALSE},
  {0xFB000, SIZE_4KB, FALSE, FALSE},
  {0xFC000, SIZE_4KB, FALSE, FALSE},
  {0xFD000, SIZE_4KB, FALSE, FALSE},
  {0xFE000, SIZE_4KB, FALSE, FALSE},
  {0xFF000, SIZE_4KB, FALSE, FALSE},
};

STATIC AMD_SEGMENT_REGISTER_VALUE  mRegisterValuesAMD[] = {
  {MSR_AMD_MTRR_FIX4K_C0000,  0x0000000000000008, 0x0000000000000004},
  {MSR_AMD_MTRR_FIX4K_C0000,  0x0000000000000800, 0x0000000000000400},
  {MSR_AMD_MTRR_FIX4K_C0000,  0x0000000000080000, 0x0000000000040000},
  {MSR_AMD_MTRR_FIX4K_C0000,  0x0000000008000000, 0x0000000004000000},
  {MSR_AMD_MTRR_FIX4K_C0000,  0x0000000800000000, 0x0000000400000000},
  {MSR_AMD_MTRR_FIX4K_C0000,  0x0000080000000000, 0x0000040000000000},
  {MSR_AMD_MTRR_FIX4K_C0000,  0x0008000000000000, 0x0004000000000000},
  {MSR_AMD_MTRR_FIX4K_C0000,  0x0800000000000000, 0x0400000000000000},
  {MSR_AMD_MTRR_FIX4K_C8000,  0x0000000000000008, 0x0000000000000004},
  {MSR_AMD_MTRR_FIX4K_C8000,  0x0000000000000800, 0x0000000000000400},
  {MSR_AMD_MTRR_FIX4K_C8000,  0x0000000000080000, 0x0000000000040000},
  {MSR_AMD_MTRR_FIX4K_C8000,  0x0000000008000000, 0x0000000004000000},
  {MSR_AMD_MTRR_FIX4K_C8000,  0x0000000800000000, 0x0000000400000000},
  {MSR_AMD_MTRR_FIX4K_C8000,  0x0000080000000000, 0x0000040000000000},
  {MSR_AMD_MTRR_FIX4K_C8000,  0x0008000000000000, 0x0004000000000000},
  {MSR_AMD_MTRR_FIX4K_C8000,  0x0800000000000000, 0x0400000000000000},
  {MSR_AMD_MTRR_FIX4K_D0000,  0x0000000000000008, 0x0000000000000004},
  {MSR_AMD_MTRR_FIX4K_D0000,  0x0000000000000800, 0x0000000000000400},
  {MSR_AMD_MTRR_FIX4K_D0000,  0x0000000000080000, 0x0000000000040000},
  {MSR_AMD_MTRR_FIX4K_D0000,  0x0000000008000000, 0x0000000004000000},
  {MSR_AMD_MTRR_FIX4K_D0000,  0x0000000800000000, 0x0000000400000000},
  {MSR_AMD_MTRR_FIX4K_D0000,  0x0000080000000000, 0x0000040000000000},
  {MSR_AMD_MTRR_FIX4K_D0000,  0x0008000000000000, 0x0004000000000000},
  {MSR_AMD_MTRR_FIX4K_D0000,  0x0800000000000000, 0x0400000000000000},
  {MSR_AMD_MTRR_FIX4K_D8000,  0x0000000000000008, 0x0000000000000004},
  {MSR_AMD_MTRR_FIX4K_D8000,  0x0000000000000800, 0x0000000000000400},
  {MSR_AMD_MTRR_FIX4K_D8000,  0x0000000000080000, 0x0000000000040000},
  {MSR_AMD_MTRR_FIX4K_D8000,  0x0000000008000000, 0x0000000004000000},
  {MSR_AMD_MTRR_FIX4K_D8000,  0x0000000800000000, 0x0000000400000000},
  {MSR_AMD_MTRR_FIX4K_D8000,  0x0000080000000000, 0x0000040000000000},
  {MSR_AMD_MTRR_FIX4K_D8000,  0x0008000000000000, 0x0004000000000000},
  {MSR_AMD_MTRR_FIX4K_D8000,  0x0800000000000000, 0x0400000000000000},
  {MSR_AMD_MTRR_FIX4K_E0000,  0x0000000000000008, 0x0000000000000004},
  {MSR_AMD_MTRR_FIX4K_E0000,  0x0000000000000800, 0x0000000000000400},
  {MSR_AMD_MTRR_FIX4K_E0000,  0x0000000000080000, 0x0000000000040000},
  {MSR_AMD_MTRR_FIX4K_E0000,  0x0000000008000000, 0x0000000004000000},
  {MSR_AMD_MTRR_FIX4K_E0000,  0x0000000800000000, 0x0000000400000000},
  {MSR_AMD_MTRR_FIX4K_E0000,  0x0000080000000000, 0x0000040000000000},
  {MSR_AMD_MTRR_FIX4K_E0000,  0x0008000000000000, 0x0004000000000000},
  {MSR_AMD_MTRR_FIX4K_E0000,  0x0800000000000000, 0x0400000000000000},
  {MSR_AMD_MTRR_FIX4K_E8000,  0x0000000000000008, 0x0000000000000004},
  {MSR_AMD_MTRR_FIX4K_E8000,  0x0000000000000800, 0x0000000000000400},
  {MSR_AMD_MTRR_FIX4K_E8000,  0x0000000000080000, 0x0000000000040000},
  {MSR_AMD_MTRR_FIX4K_E8000,  0x0000000008000000, 0x0000000004000000},
  {MSR_AMD_MTRR_FIX4K_E8000,  0x0000000800000000, 0x0000000400000000},
  {MSR_AMD_MTRR_FIX4K_E8000,  0x0000080000000000, 0x0000040000000000},
  {MSR_AMD_MTRR_FIX4K_E8000,  0x0008000000000000, 0x0004000000000000},
  {MSR_AMD_MTRR_FIX4K_E8000,  0x0800000000000000, 0x0400000000000000},
  {MSR_AMD_MTRR_FIX4K_F0000,  0x0000000000000008, 0x0000000000000004},
  {MSR_AMD_MTRR_FIX4K_F0000,  0x0000000000000800, 0x0000000000000400},
  {MSR_AMD_MTRR_FIX4K_F0000,  0x0000000000080000, 0x0000000000040000},
  {MSR_AMD_MTRR_FIX4K_F0000,  0x0000000008000000, 0x0000000004000000},
  {MSR_AMD_MTRR_FIX4K_F0000,  0x0000000800000000, 0x0000000400000000},
  {MSR_AMD_MTRR_FIX4K_F0000,  0x0000080000000000, 0x0000040000000000},
  {MSR_AMD_MTRR_FIX4K_F0000,  0x0008000000000000, 0x0004000000000000},
  {MSR_AMD_MTRR_FIX4K_F0000,  0x0800000000000000, 0x0400000000000000},
  {MSR_AMD_MTRR_FIX4K_F8000,  0x0000000000000008, 0x0000000000000004},
  {MSR_AMD_MTRR_FIX4K_F8000,  0x0000000000000800, 0x0000000000000400},
  {MSR_AMD_MTRR_FIX4K_F8000,  0x0000000000080000, 0x0000000000040000},
  {MSR_AMD_MTRR_FIX4K_F8000,  0x0000000008000000, 0x0000000004000000},
  {MSR_AMD_MTRR_FIX4K_F8000,  0x0000000800000000, 0x0000000400000000},
  {MSR_AMD_MTRR_FIX4K_F8000,  0x0000080000000000, 0x0000040000000000},
  {MSR_AMD_MTRR_FIX4K_F8000,  0x0008000000000000, 0x0004000000000000},
  {MSR_AMD_MTRR_FIX4K_F8000,  0x0800000000000000, 0x0400000000000000},
};

#define AMD_SYS_CFG_MSR 0xC0010010
#define AMD_SYS_CFG_MtrrFixDramModEn (1 << 19)
#define AMD_SYS_CFG_MtrrFixDramEn    (1 << 18)
//
// Handle used to install the Legacy Region Protocol
//
STATIC EFI_HANDLE  mHandle = NULL;

//
// Variables holding the processor vendor
//
STATIC BOOLEAN ProcIsIntel = FALSE;
STATIC BOOLEAN ProcIsAMD = FALSE;

//
// Instance of the Legacy Region Protocol to install into the handle database
//
STATIC EFI_LEGACY_REGION2_PROTOCOL  mLegacyRegion2 = {
  LegacyRegion2Decode,
  LegacyRegion2Lock,
  LegacyRegion2BootLock,
  LegacyRegion2Unlock,
  LegacyRegionGetInfo
};

STATIC
EFI_STATUS
LegacyRegionManipulationIntel (
  IN  UINT32                  Start,
  IN  UINT32                  Length,
  IN  BOOLEAN                 *ReadEnable,
  IN  BOOLEAN                 *WriteEnable,
  OUT UINT32                  *Granularity
  )
{
  UINT32                        EndAddress;
  UINTN                         Index;
  UINTN                         StartIndex;

  //
  // Validate input parameters.
  //
  if (Length == 0 || Granularity == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  EndAddress = Start + Length - 1;
  if ((Start < PAM_BASE_ADDRESS) || EndAddress > PAM_LIMIT_ADDRESS) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Loop to find the start PAM.
  //
  StartIndex = 0;
  for (Index = 0; Index < ARRAY_SIZE (mSectionArrayIntel); Index++) {
    if ((Start >= mSectionArrayIntel[Index].Start) && (Start < (mSectionArrayIntel[Index].Start + mSectionArrayIntel[Index].Length))) {
      StartIndex = Index;
      break;
    }
  }
  ASSERT (Index < ARRAY_SIZE (mSectionArrayIntel));

  //
  // Program PAM until end PAM is encountered
  //
  for (Index = StartIndex; Index < ARRAY_SIZE (mSectionArrayIntel); Index++) {
    if (ReadEnable != NULL) {
      if (*ReadEnable) {
        PciOr8 (
          mRegisterValuesIntel[Index].RegAddress,
          mRegisterValuesIntel[Index].ReadEnableData
          );
      } else {
        PciAnd8 (
          mRegisterValuesIntel[Index].RegAddress,
          (UINT8) (~mRegisterValuesIntel[Index].ReadEnableData)
          );
      }
    }
    if (WriteEnable != NULL) {
      if (*WriteEnable) {
        PciOr8 (
          mRegisterValuesIntel[Index].RegAddress,
          mRegisterValuesIntel[Index].WriteEnableData
          );
      } else {
        PciAnd8 (
          mRegisterValuesIntel[Index].RegAddress,
          (UINT8) (~mRegisterValuesIntel[Index].WriteEnableData)
          );
      }
    }

    //
    // If the end PAM is encountered, record its length as granularity and jump out.
    //
    if ((EndAddress >= mSectionArrayIntel[Index].Start) && (EndAddress < (mSectionArrayIntel[Index].Start + mSectionArrayIntel[Index].Length))) {
      *Granularity = mSectionArrayIntel[Index].Length;
      break;
    }
  }
  ASSERT (Index < ARRAY_SIZE (mSectionArrayIntel));

  return EFI_SUCCESS;
}


STATIC
EFI_STATUS
LegacyRegionManipulationAmd (
  IN  UINT32                  Start,
  IN  UINT32                  Length,
  IN  BOOLEAN                 *ReadEnable,
  IN  BOOLEAN                 *WriteEnable,
  OUT UINT32                  *Granularity
  )
{
  UINT32                        EndAddress;
  UINTN                         Index;
  UINTN                         StartIndex;

  //
  // Validate input parameters.
  //
  if (Length == 0 || Granularity == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  EndAddress = Start + Length - 1;
  if ((Start < PAM_BASE_ADDRESS) || EndAddress > PAM_LIMIT_ADDRESS) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Loop to find the start segment.
  //
  StartIndex = 0;
  for (Index = 0; Index < ARRAY_SIZE (mSectionArrayAMD); Index++) {
    if ((Start >= mSectionArrayAMD[Index].Start) && (Start < (mSectionArrayAMD[Index].Start + mSectionArrayAMD[Index].Length))) {
      StartIndex = Index;
      break;
    }
  }
  ASSERT (Index < ARRAY_SIZE (mSectionArrayAMD));

  // Enable access to Read/Write enables
  AsmMsrOr64(AMD_SYS_CFG_MSR, (UINT64)AMD_SYS_CFG_MtrrFixDramModEn);
  //
  // Program MTRR until end PAM is encountered
  //
  for (Index = StartIndex; Index < ARRAY_SIZE (mSectionArrayAMD); Index++) {
    if (ReadEnable != NULL) {
      if (*ReadEnable) {
        AsmMsrOr64 (
          mRegisterValuesAMD[Index].RegAddress,
          mRegisterValuesAMD[Index].ReadEnableData
          );
      } else {
        AsmMsrAnd64 (
          mRegisterValuesAMD[Index].RegAddress,
          (UINT64) (~mRegisterValuesAMD[Index].ReadEnableData)
          );
      }
    }
    if (WriteEnable != NULL) {
      if (*WriteEnable) {
        AsmMsrOr64 (
          mRegisterValuesAMD[Index].RegAddress,
          mRegisterValuesAMD[Index].WriteEnableData
          );
      } else {
        AsmMsrAnd64 (
          mRegisterValuesAMD[Index].RegAddress,
          (UINT64) (~mRegisterValuesAMD[Index].WriteEnableData)
          );
      }
    }
    //
    // If the end PAM is encountered, record its length as granularity and jump out.
    //
    if ((EndAddress >= mSectionArrayAMD[Index].Start) && (EndAddress < (mSectionArrayAMD[Index].Start + mSectionArrayAMD[Index].Length))) {
      *Granularity = mSectionArrayAMD[Index].Length;
      break;
    }
  }
  // Lock the Read/Write enable bits
  AsmMsrAnd64(AMD_SYS_CFG_MSR, (UINT64)~AMD_SYS_CFG_MtrrFixDramModEn);
  ASSERT (Index < ARRAY_SIZE (mSectionArrayAMD));

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
LegacyRegionManipulationInternal (
  IN  UINT32                  Start,
  IN  UINT32                  Length,
  IN  BOOLEAN                 *ReadEnable,
  IN  BOOLEAN                 *WriteEnable,
  OUT UINT32                  *Granularity
  )
{
  if (ProcIsIntel) {
    return LegacyRegionManipulationIntel(Start, Length, ReadEnable, WriteEnable, Granularity);
  }

  if (ProcIsAMD) {
    return LegacyRegionManipulationAmd(Start, Length, ReadEnable, WriteEnable, Granularity);
  }

  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
LegacyRegionGetInfoAmd (
  OUT UINT32                        *DescriptorCount,
  OUT LEGACY_MEMORY_SECTION_INFO    **Descriptor
  )
{
  UINTN     Index;
  UINT64    PamValue;

  //
  // Check input parameters
  //
  if (DescriptorCount == NULL || Descriptor == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Enable access to Read/Write enables
  AsmMsrOr64(AMD_SYS_CFG_MSR, (UINT64)AMD_SYS_CFG_MtrrFixDramModEn);
  //
  // Fill in current status of legacy region.
  //
  *DescriptorCount = sizeof(mSectionArrayAMD) / sizeof (mSectionArrayAMD[0]);
  for (Index = 0; Index < *DescriptorCount; Index++) {
    PamValue = AsmReadMsr64 (mRegisterValuesAMD[Index].RegAddress);
    mSectionArrayAMD[Index].ReadEnabled = FALSE;
    if ((PamValue & mRegisterValuesAMD[Index].ReadEnableData) != 0) {
      mSectionArrayAMD[Index].ReadEnabled = TRUE;
    }
    mSectionArrayAMD[Index].WriteEnabled = FALSE;
    if ((PamValue & mRegisterValuesAMD[Index].WriteEnableData) != 0) {
      mSectionArrayAMD[Index].WriteEnabled = TRUE;
    }
  }

  // Lock access to Read/Write enables
  AsmMsrAnd64(AMD_SYS_CFG_MSR, (UINT64)~AMD_SYS_CFG_MtrrFixDramModEn);

  *Descriptor = mSectionArrayAMD;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
LegacyRegionGetInfoIntel (
  OUT UINT32                        *DescriptorCount,
  OUT LEGACY_MEMORY_SECTION_INFO    **Descriptor
  )
{
  UINTN    Index;
  UINT8    PamValue;

  //
  // Check input parameters
  //
  if (DescriptorCount == NULL || Descriptor == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Fill in current status of legacy region.
  //
  *DescriptorCount = sizeof(mSectionArrayIntel) / sizeof (mSectionArrayIntel[0]);
  for (Index = 0; Index < *DescriptorCount; Index++) {
    PamValue = PciRead8 (mRegisterValuesIntel[Index].RegAddress);
    mSectionArrayIntel[Index].ReadEnabled = FALSE;
    if ((PamValue & mRegisterValuesIntel[Index].ReadEnableData) != 0) {
      mSectionArrayIntel[Index].ReadEnabled = TRUE;
    }
    mSectionArrayIntel[Index].WriteEnabled = FALSE;
    if ((PamValue & mRegisterValuesIntel[Index].WriteEnableData) != 0) {
      mSectionArrayIntel[Index].WriteEnabled = TRUE;
    }
  }

  *Descriptor = mSectionArrayIntel;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
LegacyRegionGetInfoInternal (
  OUT UINT32                        *DescriptorCount,
  OUT LEGACY_MEMORY_SECTION_INFO    **Descriptor
  )
{
  if (ProcIsIntel) {
    return LegacyRegionGetInfoIntel(DescriptorCount, Descriptor);
  }

  if (ProcIsAMD) {
    return LegacyRegionGetInfoAmd(DescriptorCount, Descriptor);
  }

  return EFI_UNSUPPORTED;
}

/**
  Modify the hardware to allow (decode) or disallow (not decode) memory reads in a region.

  If the On parameter evaluates to TRUE, this function enables memory reads in the address range
  Start to (Start + Length - 1).
  If the On parameter evaluates to FALSE, this function disables memory reads in the address range
  Start to (Start + Length - 1).

  @param  This[in]              Indicates the EFI_LEGACY_REGION_PROTOCOL instance.
  @param  Start[in]             The beginning of the physical address of the region whose attributes
                                should be modified.
  @param  Length[in]            The number of bytes of memory whose attributes should be modified.
                                The actual number of bytes modified may be greater than the number
                                specified.
  @param  Granularity[out]      The number of bytes in the last region affected. This may be less
                                than the total number of bytes affected if the starting address
                                was not aligned to a region's starting address or if the length
                                was greater than the number of bytes in the first region.
  @param  On[in]                Decode / Non-Decode flag.

  @retval EFI_SUCCESS           The region's attributes were successfully modified.
  @retval EFI_INVALID_PARAMETER If Start or Length describe an address not in the Legacy Region.

**/
EFI_STATUS
EFIAPI
LegacyRegion2Decode (
  IN  EFI_LEGACY_REGION2_PROTOCOL  *This,
  IN  UINT32                       Start,
  IN  UINT32                       Length,
  OUT UINT32                       *Granularity,
  IN  BOOLEAN                      *On
  )
{
  return LegacyRegionManipulationInternal (Start, Length, On, NULL, Granularity);
}


/**
  Modify the hardware to disallow memory attribute changes in a region.

  This function makes the attributes of a region read only. Once a region is boot-locked with this
  function, the read and write attributes of that region cannot be changed until a power cycle has
  reset the boot-lock attribute. Calls to Decode(), Lock() and Unlock() will have no effect.

  @param  This[in]              Indicates the EFI_LEGACY_REGION_PROTOCOL instance.
  @param  Start[in]             The beginning of the physical address of the region whose
                                attributes should be modified.
  @param  Length[in]            The number of bytes of memory whose attributes should be modified.
                                The actual number of bytes modified may be greater than the number
                                specified.
  @param  Granularity[out]      The number of bytes in the last region affected. This may be less
                                than the total number of bytes affected if the starting address was
                                not aligned to a region's starting address or if the length was
                                greater than the number of bytes in the first region.

  @retval EFI_SUCCESS           The region's attributes were successfully modified.
  @retval EFI_INVALID_PARAMETER If Start or Length describe an address not in the Legacy Region.
  @retval EFI_UNSUPPORTED       The chipset does not support locking the configuration registers in
                                a way that will not affect memory regions outside the legacy memory
                                region.

**/
EFI_STATUS
EFIAPI
LegacyRegion2BootLock (
  IN  EFI_LEGACY_REGION2_PROTOCOL         *This,
  IN  UINT32                              Start,
  IN  UINT32                              Length,
  OUT UINT32                              *Granularity
  )
{
  if ((Start < 0xC0000) || ((Start + Length - 1) > 0xFFFFF)) {
    return EFI_INVALID_PARAMETER;
  }

  return EFI_UNSUPPORTED;
}


/**
  Modify the hardware to disallow memory writes in a region.

  This function changes the attributes of a memory range to not allow writes.

  @param  This[in]              Indicates the EFI_LEGACY_REGION_PROTOCOL instance.
  @param  Start[in]             The beginning of the physical address of the region whose
                                attributes should be modified.
  @param  Length[in]            The number of bytes of memory whose attributes should be modified.
                                The actual number of bytes modified may be greater than the number
                                specified.
  @param  Granularity[out]      The number of bytes in the last region affected. This may be less
                                than the total number of bytes affected if the starting address was
                                not aligned to a region's starting address or if the length was
                                greater than the number of bytes in the first region.

  @retval EFI_SUCCESS           The region's attributes were successfully modified.
  @retval EFI_INVALID_PARAMETER If Start or Length describe an address not in the Legacy Region.

**/
EFI_STATUS
EFIAPI
LegacyRegion2Lock (
  IN  EFI_LEGACY_REGION2_PROTOCOL *This,
  IN  UINT32                      Start,
  IN  UINT32                      Length,
  OUT UINT32                      *Granularity
  )
{
  BOOLEAN  WriteEnable;

  WriteEnable = FALSE;
  return LegacyRegionManipulationInternal (Start, Length, NULL, &WriteEnable, Granularity);
}


/**
  Modify the hardware to allow memory writes in a region.

  This function changes the attributes of a memory range to allow writes.

  @param  This[in]              Indicates the EFI_LEGACY_REGION_PROTOCOL instance.
  @param  Start[in]             The beginning of the physical address of the region whose
                                attributes should be modified.
  @param  Length[in]            The number of bytes of memory whose attributes should be modified.
                                The actual number of bytes modified may be greater than the number
                                specified.
  @param  Granularity[out]      The number of bytes in the last region affected. This may be less
                                than the total number of bytes affected if the starting address was
                                not aligned to a region's starting address or if the length was
                                greater than the number of bytes in the first region.

  @retval EFI_SUCCESS           The region's attributes were successfully modified.
  @retval EFI_INVALID_PARAMETER If Start or Length describe an address not in the Legacy Region.

**/
EFI_STATUS
EFIAPI
LegacyRegion2Unlock (
  IN  EFI_LEGACY_REGION2_PROTOCOL  *This,
  IN  UINT32                       Start,
  IN  UINT32                       Length,
  OUT UINT32                       *Granularity
  )
{
  BOOLEAN  WriteEnable;

  WriteEnable = TRUE;
  return LegacyRegionManipulationInternal (Start, Length, NULL, &WriteEnable, Granularity);
}

/**
  Get region information for the attributes of the Legacy Region.

  This function is used to discover the granularity of the attributes for the memory in the legacy
  region. Each attribute may have a different granularity and the granularity may not be the same
  for all memory ranges in the legacy region.

  @param  This[in]              Indicates the EFI_LEGACY_REGION_PROTOCOL instance.
  @param  DescriptorCount[out]  The number of region descriptor entries returned in the Descriptor
                                buffer.
  @param  Descriptor[out]       A pointer to a pointer used to return a buffer where the legacy
                                region information is deposited. This buffer will contain a list of
                                DescriptorCount number of region descriptors.  This function will
                                provide the memory for the buffer.

  @retval EFI_SUCCESS           The region's attributes were successfully modified.
  @retval EFI_INVALID_PARAMETER If Start or Length describe an address not in the Legacy Region.

**/
EFI_STATUS
EFIAPI
LegacyRegionGetInfo (
  IN  EFI_LEGACY_REGION2_PROTOCOL   *This,
  OUT UINT32                        *DescriptorCount,
  OUT EFI_LEGACY_REGION_DESCRIPTOR  **Descriptor
  )
{
  LEGACY_MEMORY_SECTION_INFO   *SectionInfo;
  UINT32                       SectionCount;
  EFI_LEGACY_REGION_DESCRIPTOR *DescriptorArray;
  UINTN                        Index;
  UINTN                        DescriptorIndex;

  //
  // Get section numbers and information
  //
  LegacyRegionGetInfoInternal (&SectionCount, &SectionInfo);

  //
  // Each section has 3 descriptors, corresponding to readability, writeability, and lock status.
  //
  DescriptorArray = AllocatePool (sizeof (EFI_LEGACY_REGION_DESCRIPTOR) * SectionCount * 3);
  if (DescriptorArray == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  DescriptorIndex = 0;
  for (Index = 0; Index < SectionCount; Index++) {
    DescriptorArray[DescriptorIndex].Start       = SectionInfo[Index].Start;
    DescriptorArray[DescriptorIndex].Length      = SectionInfo[Index].Length;
    DescriptorArray[DescriptorIndex].Granularity = SectionInfo[Index].Length;
    if (SectionInfo[Index].ReadEnabled) {
      DescriptorArray[DescriptorIndex].Attribute   = LegacyRegionDecoded;
    } else {
      DescriptorArray[DescriptorIndex].Attribute   = LegacyRegionNotDecoded;
    }
    DescriptorIndex++;

    //
    // Create descriptor for writeability, according to lock status
    //
    DescriptorArray[DescriptorIndex].Start       = SectionInfo[Index].Start;
    DescriptorArray[DescriptorIndex].Length      = SectionInfo[Index].Length;
    DescriptorArray[DescriptorIndex].Granularity = SectionInfo[Index].Length;
    if (SectionInfo[Index].WriteEnabled) {
      DescriptorArray[DescriptorIndex].Attribute = LegacyRegionWriteEnabled;
    } else {
      DescriptorArray[DescriptorIndex].Attribute = LegacyRegionWriteDisabled;
    }
    DescriptorIndex++;

    //
    // Chipset does not support bootlock.
    //
    DescriptorArray[DescriptorIndex].Start       = SectionInfo[Index].Start;
    DescriptorArray[DescriptorIndex].Length      = SectionInfo[Index].Length;
    DescriptorArray[DescriptorIndex].Granularity = SectionInfo[Index].Length;
    DescriptorArray[DescriptorIndex].Attribute   = LegacyRegionNotLocked;
    DescriptorIndex++;
  }

  *DescriptorCount = (UINT32) DescriptorIndex;
  *Descriptor      = DescriptorArray;

  return EFI_SUCCESS;
}

/**
  Initialize Legacy Region support

  @retval EFI_SUCCESS   Successfully initialized

**/
EFI_STATUS
LegacyRegionInit (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT16      HostBridgeVenId;

  //
  // Query Host Bridge DID to determine platform type
  //
  HostBridgeVenId = PciRead16(PCI_LIB_ADDRESS(0, 0, 0, 0));
  switch (HostBridgeVenId) {
  /* Intel */
  case 0x8086:
    ProcIsIntel = TRUE;
    DEBUG ((EFI_D_ERROR, "%a: Intel Host Bridge Device detected\n", __FUNCTION__));
    break;
  /* AMD */
  case 0x1022:
    ProcIsAMD = TRUE;
    DEBUG ((EFI_D_ERROR, "%a: AMD Host Bridge Device detected\n", __FUNCTION__));
    // Enable decoding of the legacy segments
    AsmMsrOr64(AMD_SYS_CFG_MSR, (UINT64)AMD_SYS_CFG_MtrrFixDramEn);
    break;
  default:
    DEBUG ((EFI_D_ERROR, "%a: Unknown Host Bridge Device Vendor ID: 0x%04x\n",
            __FUNCTION__, HostBridgeVenId));
    ASSERT (FALSE);
    return RETURN_UNSUPPORTED;
  }

  //
  // Install the Legacy Region Protocol on a new handle
  //
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mHandle,
                  &gEfiLegacyRegion2ProtocolGuid, &mLegacyRegion2,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  return Status;
}

