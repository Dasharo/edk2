/** @file
  MTRR setting library

  @par Note:
    Most of services in this library instance are suggested to be invoked by BSP only,
    except for MtrrSetAllMtrrs() which is used to sync BSP's MTRR setting to APs.

  Copyright (c) 2008 - 2019, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Register/Intel/Cpuid.h>
#include <Register/Intel/Msr.h>

#include <Library/MtrrLib.h>
#include <Library/BaseLib.h>
#include <Library/CpuLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

#define OR_SEED      0x0101010101010101ull
#define CLEAR_SEED   0xFFFFFFFFFFFFFFFFull
#define MAX_WEIGHT   MAX_UINT8
#define SCRATCH_BUFFER_SIZE           (4 * SIZE_4KB)
#define MTRR_LIB_ASSERT_ALIGNED(B, L) ASSERT ((B & ~(L - 1)) == B);

#define M(x,y) ((x) * VertexCount + (y))
#define O(x,y) ((y) * VertexCount + (x))

//
// Context to save and restore when MTRRs are programmed
//
typedef struct {
  UINTN    Cr4;
  BOOLEAN  InterruptState;
} MTRR_CONTEXT;

typedef struct {
  UINT64                 Address;
  UINT64                 Alignment;
  UINT64                 Length;
  MTRR_MEMORY_CACHE_TYPE Type : 7;

  //
  // Temprary use for calculating the best MTRR settings.
  //
  BOOLEAN                Visited : 1;
  UINT8                  Weight;
  UINT16                 Previous;
} MTRR_LIB_ADDRESS;

//
// This table defines the offset, base and length of the fixed MTRRs
//
CONST FIXED_MTRR  mMtrrLibFixedMtrrTable[] = {
  {
    MSR_IA32_MTRR_FIX64K_00000,
    0,
    SIZE_64KB
  },
  {
    MSR_IA32_MTRR_FIX16K_80000,
    0x80000,
    SIZE_16KB
  },
  {
    MSR_IA32_MTRR_FIX16K_A0000,
    0xA0000,
    SIZE_16KB
  },
  {
    MSR_IA32_MTRR_FIX4K_C0000,
    0xC0000,
    SIZE_4KB
  },
  {
    MSR_IA32_MTRR_FIX4K_C8000,
    0xC8000,
    SIZE_4KB
  },
  {
    MSR_IA32_MTRR_FIX4K_D0000,
    0xD0000,
    SIZE_4KB
  },
  {
    MSR_IA32_MTRR_FIX4K_D8000,
    0xD8000,
    SIZE_4KB
  },
  {
    MSR_IA32_MTRR_FIX4K_E0000,
    0xE0000,
    SIZE_4KB
  },
  {
    MSR_IA32_MTRR_FIX4K_E8000,
    0xE8000,
    SIZE_4KB
  },
  {
    MSR_IA32_MTRR_FIX4K_F0000,
    0xF0000,
    SIZE_4KB
  },
  {
    MSR_IA32_MTRR_FIX4K_F8000,
    0xF8000,
    SIZE_4KB
  }
};

//
// Lookup table used to print MTRRs
//
GLOBAL_REMOVE_IF_UNREFERENCED CONST CHAR8 *mMtrrMemoryCacheTypeShortName[] = {
  "UC",  // CacheUncacheable
  "WC",  // CacheWriteCombining
  "R*",  // Invalid
  "R*",  // Invalid
  "WT",  // CacheWriteThrough
  "WP",  // CacheWriteProtected
  "WB",  // CacheWriteBack
  "R*"   // Invalid
};


/**
  Worker function prints all MTRRs for debugging.

  If MtrrSetting is not NULL, print MTRR settings from input MTRR
  settings buffer.
  If MtrrSetting is NULL, print MTRR settings from MTRRs.

  @param  MtrrSetting    A buffer holding all MTRRs content.
**/
VOID
MtrrDebugPrintAllMtrrsWorker (
  IN MTRR_SETTINGS    *MtrrSetting
  );

/**
  Worker function returns the variable MTRR count for the CPU.

  @return Variable MTRR count

**/
UINT32
GetVariableMtrrCountWorker (
  VOID
  )
{
  MSR_IA32_MTRRCAP_REGISTER MtrrCap;

  MtrrCap.Uint64 = AsmReadMsr64 (MSR_IA32_MTRRCAP);
  ASSERT (MtrrCap.Bits.VCNT <= ARRAY_SIZE (((MTRR_VARIABLE_SETTINGS *) 0)->Mtrr));
  return MtrrCap.Bits.VCNT;
}

/**
  Returns the variable MTRR count for the CPU.

  @return Variable MTRR count

**/
UINT32
EFIAPI
GetVariableMtrrCount (
  VOID
  )
{
  if (!IsMtrrSupported ()) {
    return 0;
  }
  return GetVariableMtrrCountWorker ();
}

/**
  Worker function returns the firmware usable variable MTRR count for the CPU.

  @return Firmware usable variable MTRR count

**/
UINT32
GetFirmwareVariableMtrrCountWorker (
  VOID
  )
{
  UINT32  VariableMtrrCount;
  UINT32  ReservedMtrrNumber;

  VariableMtrrCount = GetVariableMtrrCountWorker ();
  ReservedMtrrNumber = PcdGet32 (PcdCpuNumberOfReservedVariableMtrrs);
  if (VariableMtrrCount < ReservedMtrrNumber) {
    return 0;
  }

  return VariableMtrrCount - ReservedMtrrNumber;
}

/**
  Returns the firmware usable variable MTRR count for the CPU.

  @return Firmware usable variable MTRR count

**/
UINT32
EFIAPI
GetFirmwareVariableMtrrCount (
  VOID
  )
{
  if (!IsMtrrSupported ()) {
    return 0;
  }
  return GetFirmwareVariableMtrrCountWorker ();
}

/**
  Worker function returns the default MTRR cache type for the system.

  If MtrrSetting is not NULL, returns the default MTRR cache type from input
  MTRR settings buffer.
  If MtrrSetting is NULL, returns the default MTRR cache type from MSR.

  @param[in]  MtrrSetting    A buffer holding all MTRRs content.

  @return  The default MTRR cache type.

**/
MTRR_MEMORY_CACHE_TYPE
MtrrGetDefaultMemoryTypeWorker (
  IN MTRR_SETTINGS      *MtrrSetting
  )
{
  MSR_IA32_MTRR_DEF_TYPE_REGISTER DefType;

  if (MtrrSetting == NULL) {
    DefType.Uint64 = AsmReadMsr64 (MSR_IA32_MTRR_DEF_TYPE);
  } else {
    DefType.Uint64 = MtrrSetting->MtrrDefType;
  }

  return (MTRR_MEMORY_CACHE_TYPE) DefType.Bits.Type;
}


/**
  Returns the default MTRR cache type for the system.

  @return  The default MTRR cache type.

**/
MTRR_MEMORY_CACHE_TYPE
EFIAPI
MtrrGetDefaultMemoryType (
  VOID
  )
{
  if (!IsMtrrSupported ()) {
    return CacheUncacheable;
  }
  return MtrrGetDefaultMemoryTypeWorker (NULL);
}

/**
  Worker function gets the content in fixed MTRRs

  @param[out]  FixedSettings  A buffer to hold fixed MTRRs content.

  @retval The pointer of FixedSettings

**/
MTRR_FIXED_SETTINGS*
MtrrGetFixedMtrrWorker (
  OUT MTRR_FIXED_SETTINGS         *FixedSettings
  )
{
  UINT32  Index;

  for (Index = 0; Index < MTRR_NUMBER_OF_FIXED_MTRR; Index++) {
      FixedSettings->Mtrr[Index] =
        AsmReadMsr64 (mMtrrLibFixedMtrrTable[Index].Msr);
  }

  return FixedSettings;
}


/**
  This function gets the content in fixed MTRRs

  @param[out]  FixedSettings  A buffer to hold fixed MTRRs content.

  @retval The pointer of FixedSettings

**/
MTRR_FIXED_SETTINGS*
EFIAPI
MtrrGetFixedMtrr (
  OUT MTRR_FIXED_SETTINGS         *FixedSettings
  )
{
  if (!IsMtrrSupported ()) {
    return FixedSettings;
  }

  return MtrrGetFixedMtrrWorker (FixedSettings);
}


/**
  Worker function will get the raw value in variable MTRRs

  If MtrrSetting is not NULL, gets the variable MTRRs raw value from input
  MTRR settings buffer.
  If MtrrSetting is NULL, gets the variable MTRRs raw value from MTRRs.

  @param[in]  MtrrSetting        A buffer holding all MTRRs content.
  @param[in]  VariableMtrrCount  Number of variable MTRRs.
  @param[out] VariableSettings   A buffer to hold variable MTRRs content.

  @return The VariableSettings input pointer

**/
MTRR_VARIABLE_SETTINGS*
MtrrGetVariableMtrrWorker (
  IN  MTRR_SETTINGS           *MtrrSetting,
  IN  UINT32                  VariableMtrrCount,
  OUT MTRR_VARIABLE_SETTINGS  *VariableSettings
  )
{
  UINT32  Index;

  ASSERT (VariableMtrrCount <= ARRAY_SIZE (VariableSettings->Mtrr));

  for (Index = 0; Index < VariableMtrrCount; Index++) {
    if (MtrrSetting == NULL) {
      VariableSettings->Mtrr[Index].Base =
        AsmReadMsr64 (MSR_IA32_MTRR_PHYSBASE0 + (Index << 1));
      VariableSettings->Mtrr[Index].Mask =
        AsmReadMsr64 (MSR_IA32_MTRR_PHYSMASK0 + (Index << 1));
    } else {
      VariableSettings->Mtrr[Index].Base = MtrrSetting->Variables.Mtrr[Index].Base;
      VariableSettings->Mtrr[Index].Mask = MtrrSetting->Variables.Mtrr[Index].Mask;
    }
  }

  return  VariableSettings;
}

/**
  This function will get the raw value in variable MTRRs

  @param[out]  VariableSettings   A buffer to hold variable MTRRs content.

  @return The VariableSettings input pointer

**/
MTRR_VARIABLE_SETTINGS*
EFIAPI
MtrrGetVariableMtrr (
  OUT MTRR_VARIABLE_SETTINGS         *VariableSettings
  )
{
  if (!IsMtrrSupported ()) {
    return VariableSettings;
  }

  return MtrrGetVariableMtrrWorker (
           NULL,
           GetVariableMtrrCountWorker (),
           VariableSettings
           );
}

/**
  Worker function gets the attribute of variable MTRRs.

  This function shadows the content of variable MTRRs into an
  internal array: VariableMtrr.

  @param[in]   VariableSettings      The variable MTRR values to shadow
  @param[in]   VariableMtrrCount     The number of variable MTRRs
  @param[in]   MtrrValidBitsMask     The mask for the valid bit of the MTRR
  @param[in]   MtrrValidAddressMask  The valid address mask for MTRR
  @param[out]  VariableMtrr          The array to shadow variable MTRRs content

  @return      Number of MTRRs which has been used.

**/
UINT32
MtrrGetMemoryAttributeInVariableMtrrWorker (
  IN  MTRR_VARIABLE_SETTINGS  *VariableSettings,
  IN  UINTN                   VariableMtrrCount,
  IN  UINT64                  MtrrValidBitsMask,
  IN  UINT64                  MtrrValidAddressMask,
  OUT VARIABLE_MTRR           *VariableMtrr
  )
{
  UINTN   Index;
  UINT32  UsedMtrr;

  ZeroMem (VariableMtrr, sizeof (VARIABLE_MTRR) * ARRAY_SIZE (VariableSettings->Mtrr));
  for (Index = 0, UsedMtrr = 0; Index < VariableMtrrCount; Index++) {
    if (((MSR_IA32_MTRR_PHYSMASK_REGISTER *) &VariableSettings->Mtrr[Index].Mask)->Bits.V != 0) {
      VariableMtrr[Index].Msr         = (UINT32)Index;
      VariableMtrr[Index].BaseAddress = (VariableSettings->Mtrr[Index].Base & MtrrValidAddressMask);
      VariableMtrr[Index].Length      =
        ((~(VariableSettings->Mtrr[Index].Mask & MtrrValidAddressMask)) & MtrrValidBitsMask) + 1;
      VariableMtrr[Index].Type        = (VariableSettings->Mtrr[Index].Base & 0x0ff);
      VariableMtrr[Index].Valid       = TRUE;
      VariableMtrr[Index].Used        = TRUE;
      UsedMtrr++;
    }
  }
  return UsedMtrr;
}

/**
  Convert variable MTRRs to a RAW MTRR_MEMORY_RANGE array.
  One MTRR_MEMORY_RANGE element is created for each MTRR setting.
  The routine doesn't remove the overlap or combine the near-by region.

  @param[in]   VariableSettings      The variable MTRR values to shadow
  @param[in]   VariableMtrrCount     The number of variable MTRRs
  @param[in]   MtrrValidBitsMask     The mask for the valid bit of the MTRR
  @param[in]   MtrrValidAddressMask  The valid address mask for MTRR
  @param[out]  VariableMtrr          The array to shadow variable MTRRs content

  @return      Number of MTRRs which has been used.

**/
UINT32
MtrrLibGetRawVariableRanges (
  IN  MTRR_VARIABLE_SETTINGS  *VariableSettings,
  IN  UINTN                   VariableMtrrCount,
  IN  UINT64                  MtrrValidBitsMask,
  IN  UINT64                  MtrrValidAddressMask,
  OUT MTRR_MEMORY_RANGE       *VariableMtrr
  )
{
  UINTN   Index;
  UINT32  UsedMtrr;

  ZeroMem (VariableMtrr, sizeof (MTRR_MEMORY_RANGE) * ARRAY_SIZE (VariableSettings->Mtrr));
  for (Index = 0, UsedMtrr = 0; Index < VariableMtrrCount; Index++) {
    if (((MSR_IA32_MTRR_PHYSMASK_REGISTER *) &VariableSettings->Mtrr[Index].Mask)->Bits.V != 0) {
      VariableMtrr[Index].BaseAddress = (VariableSettings->Mtrr[Index].Base & MtrrValidAddressMask);
      VariableMtrr[Index].Length      =
        ((~(VariableSettings->Mtrr[Index].Mask & MtrrValidAddressMask)) & MtrrValidBitsMask) + 1;
      VariableMtrr[Index].Type        = (MTRR_MEMORY_CACHE_TYPE)(VariableSettings->Mtrr[Index].Base & 0x0ff);
      UsedMtrr++;
    }
  }
  return UsedMtrr;
}

/**
  Gets the attribute of variable MTRRs.

  This function shadows the content of variable MTRRs into an
  internal array: VariableMtrr.

  @param[in]   MtrrValidBitsMask     The mask for the valid bit of the MTRR
  @param[in]   MtrrValidAddressMask  The valid address mask for MTRR
  @param[out]  VariableMtrr          The array to shadow variable MTRRs content

  @return                       The return value of this parameter indicates the
                                number of MTRRs which has been used.

**/
UINT32
EFIAPI
MtrrGetMemoryAttributeInVariableMtrr (
  IN  UINT64                    MtrrValidBitsMask,
  IN  UINT64                    MtrrValidAddressMask,
  OUT VARIABLE_MTRR             *VariableMtrr
  )
{
  MTRR_VARIABLE_SETTINGS  VariableSettings;

  if (!IsMtrrSupported ()) {
    return 0;
  }

  MtrrGetVariableMtrrWorker (
    NULL,
    GetVariableMtrrCountWorker (),
    &VariableSettings
    );

  return MtrrGetMemoryAttributeInVariableMtrrWorker (
           &VariableSettings,
           GetFirmwareVariableMtrrCountWorker (),
           MtrrValidBitsMask,
           MtrrValidAddressMask,
           VariableMtrr
           );
}

/**
  Return the biggest alignment (lowest set bit) of address.
  The function is equivalent to: 1 << LowBitSet64 (Address).

  @param Address    The address to return the alignment.
  @param Alignment0 The alignment to return when Address is 0.

  @return The least alignment of the Address.
**/
UINT64
MtrrLibBiggestAlignment (
  UINT64    Address,
  UINT64    Alignment0
)
{
  if (Address == 0) {
    return Alignment0;
  }

  return Address & ((~Address) + 1);
}

/**
  Return whether the left MTRR type precedes the right MTRR type.

  The MTRR type precedence rules are:
    1. UC precedes any other type
    2. WT precedes WB
  For further details, please refer the IA32 Software Developer's Manual,
  Volume 3, Section "MTRR Precedences".

  @param Left  The left MTRR type.
  @param Right The right MTRR type.

  @retval TRUE  Left precedes Right.
  @retval FALSE Left doesn't precede Right.
**/
BOOLEAN
MtrrLibTypeLeftPrecedeRight (
  IN MTRR_MEMORY_CACHE_TYPE  Left,
  IN MTRR_MEMORY_CACHE_TYPE  Right
)
{
  return (BOOLEAN) (Left == CacheUncacheable || (Left == CacheWriteThrough && Right == CacheWriteBack));
}

/**
  Initializes the valid bits mask and valid address mask for MTRRs.

  This function initializes the valid bits mask and valid address mask for MTRRs.

  @param[out]  MtrrValidBitsMask     The mask for the valid bit of the MTRR
  @param[out]  MtrrValidAddressMask  The valid address mask for the MTRR

**/
VOID
MtrrLibInitializeMtrrMask (
  OUT UINT64 *MtrrValidBitsMask,
  OUT UINT64 *MtrrValidAddressMask
  )
{
  UINT32                          MaxExtendedFunction;
  CPUID_VIR_PHY_ADDRESS_SIZE_EAX  VirPhyAddressSize;


  AsmCpuid (CPUID_EXTENDED_FUNCTION, &MaxExtendedFunction, NULL, NULL, NULL);

  if (MaxExtendedFunction >= CPUID_VIR_PHY_ADDRESS_SIZE) {
    AsmCpuid (CPUID_VIR_PHY_ADDRESS_SIZE, &VirPhyAddressSize.Uint32, NULL, NULL, NULL);
  } else {
    VirPhyAddressSize.Bits.PhysicalAddressBits = 36;
  }

  *MtrrValidBitsMask = LShiftU64 (1, VirPhyAddressSize.Bits.PhysicalAddressBits) - 1;
  *MtrrValidAddressMask = *MtrrValidBitsMask & 0xfffffffffffff000ULL;
}


/**
  Determines the real attribute of a memory range.

  This function is to arbitrate the real attribute of the memory when
  there are 2 MTRRs covers the same memory range. For further details,
  please refer the IA32 Software Developer's Manual, Volume 3,
  Section "MTRR Precedences".

  @param[in]  MtrrType1    The first kind of Memory type
  @param[in]  MtrrType2    The second kind of memory type

**/
MTRR_MEMORY_CACHE_TYPE
MtrrLibPrecedence (
  IN MTRR_MEMORY_CACHE_TYPE    MtrrType1,
  IN MTRR_MEMORY_CACHE_TYPE    MtrrType2
  )
{
  if (MtrrType1 == MtrrType2) {
    return MtrrType1;
  }

  ASSERT (
    MtrrLibTypeLeftPrecedeRight (MtrrType1, MtrrType2) ||
    MtrrLibTypeLeftPrecedeRight (MtrrType2, MtrrType1)
  );

  if (MtrrLibTypeLeftPrecedeRight (MtrrType1, MtrrType2)) {
    return MtrrType1;
  } else {
    return MtrrType2;
  }
}

/**
  Worker function will get the memory cache type of the specific address.

  If MtrrSetting is not NULL, gets the memory cache type from input
  MTRR settings buffer.
  If MtrrSetting is NULL, gets the memory cache type from MTRRs.

  @param[in]  MtrrSetting        A buffer holding all MTRRs content.
  @param[in]  Address            The specific address

  @return Memory cache type of the specific address

**/
MTRR_MEMORY_CACHE_TYPE
MtrrGetMemoryAttributeByAddressWorker (
  IN MTRR_SETTINGS      *MtrrSetting,
  IN PHYSICAL_ADDRESS   Address
  )
{
  MSR_IA32_MTRR_DEF_TYPE_REGISTER DefType;
  UINT64                          FixedMtrr;
  UINTN                           Index;
  UINTN                           SubIndex;
  MTRR_MEMORY_CACHE_TYPE          MtrrType;
  MTRR_MEMORY_RANGE               VariableMtrr[ARRAY_SIZE (MtrrSetting->Variables.Mtrr)];
  UINT64                          MtrrValidBitsMask;
  UINT64                          MtrrValidAddressMask;
  UINT32                          VariableMtrrCount;
  MTRR_VARIABLE_SETTINGS          VariableSettings;

  //
  // Check if MTRR is enabled, if not, return UC as attribute
  //
  if (MtrrSetting == NULL) {
    DefType.Uint64 = AsmReadMsr64 (MSR_IA32_MTRR_DEF_TYPE);
  } else {
    DefType.Uint64 = MtrrSetting->MtrrDefType;
  }

  if (DefType.Bits.E == 0) {
    return CacheUncacheable;
  }

  //
  // If address is less than 1M, then try to go through the fixed MTRR
  //
  if (Address < BASE_1MB) {
    if (DefType.Bits.FE != 0) {
      //
      // Go through the fixed MTRR
      //
      for (Index = 0; Index < MTRR_NUMBER_OF_FIXED_MTRR; Index++) {
        if (Address >= mMtrrLibFixedMtrrTable[Index].BaseAddress &&
            Address < mMtrrLibFixedMtrrTable[Index].BaseAddress +
            (mMtrrLibFixedMtrrTable[Index].Length * 8)) {
          SubIndex =
            ((UINTN) Address - mMtrrLibFixedMtrrTable[Index].BaseAddress) /
            mMtrrLibFixedMtrrTable[Index].Length;
          if (MtrrSetting == NULL) {
            FixedMtrr = AsmReadMsr64 (mMtrrLibFixedMtrrTable[Index].Msr);
          } else {
            FixedMtrr = MtrrSetting->Fixed.Mtrr[Index];
          }
          return (MTRR_MEMORY_CACHE_TYPE) (RShiftU64 (FixedMtrr, SubIndex * 8) & 0xFF);
        }
      }
    }
  }

  VariableMtrrCount = GetVariableMtrrCountWorker ();
  ASSERT (VariableMtrrCount <= ARRAY_SIZE (MtrrSetting->Variables.Mtrr));
  MtrrGetVariableMtrrWorker (MtrrSetting, VariableMtrrCount, &VariableSettings);

  MtrrLibInitializeMtrrMask (&MtrrValidBitsMask, &MtrrValidAddressMask);
  MtrrLibGetRawVariableRanges (
    &VariableSettings,
    VariableMtrrCount,
    MtrrValidBitsMask,
    MtrrValidAddressMask,
    VariableMtrr
  );

  //
  // Go through the variable MTRR
  //
  MtrrType = CacheInvalid;
  for (Index = 0; Index < VariableMtrrCount; Index++) {
    if (VariableMtrr[Index].Length != 0) {
      if (Address >= VariableMtrr[Index].BaseAddress &&
          Address < VariableMtrr[Index].BaseAddress + VariableMtrr[Index].Length) {
        if (MtrrType == CacheInvalid) {
          MtrrType = (MTRR_MEMORY_CACHE_TYPE) VariableMtrr[Index].Type;
        } else {
          MtrrType = MtrrLibPrecedence (MtrrType, (MTRR_MEMORY_CACHE_TYPE) VariableMtrr[Index].Type);
        }
      }
    }
  }

  //
  // If there is no MTRR which covers the Address, use the default MTRR type.
  //
  if (MtrrType == CacheInvalid) {
    MtrrType = (MTRR_MEMORY_CACHE_TYPE) DefType.Bits.Type;
  }

  return MtrrType;
}


/**
  This function will get the memory cache type of the specific address.

  This function is mainly for debug purpose.

  @param[in]  Address   The specific address

  @return Memory cache type of the specific address

**/
MTRR_MEMORY_CACHE_TYPE
EFIAPI
MtrrGetMemoryAttribute (
  IN PHYSICAL_ADDRESS   Address
  )
{
  if (!IsMtrrSupported ()) {
    return CacheUncacheable;
  }

  return MtrrGetMemoryAttributeByAddressWorker (NULL, Address);
}

/**
  Update the Ranges array to change the specified range identified by
  BaseAddress and Length to Type.

  @param Ranges      Array holding memory type settings for all memory regions.
  @param Capacity    The maximum count of memory ranges the array can hold.
  @param Count       Return the new memory range count in the array.
  @param BaseAddress The base address of the memory range to change type.
  @param Length      The length of the memory range to change type.
  @param Type        The new type of the specified memory range.

  @retval RETURN_SUCCESS          The type of the specified memory range is
                                  changed successfully.
  @retval RETURN_ALREADY_STARTED  The type of the specified memory range equals
                                  to the desired type.
  @retval RETURN_OUT_OF_RESOURCES The new type set causes the count of memory
                                  range exceeds capacity.
**/
RETURN_STATUS
MtrrLibSetMemoryType (
  IN MTRR_MEMORY_RANGE             *Ranges,
  IN UINTN                         Capacity,
  IN OUT UINTN                     *Count,
  IN UINT64                        BaseAddress,
  IN UINT64                        Length,
  IN MTRR_MEMORY_CACHE_TYPE        Type
  )
{
  UINTN                            Index;
  UINT64                           Limit;
  UINT64                           LengthLeft;
  UINT64                           LengthRight;
  UINTN                            StartIndex;
  UINTN                            EndIndex;
  UINTN                            DeltaCount;

  LengthRight = 0;
  LengthLeft  = 0;
  Limit = BaseAddress + Length;
  StartIndex = *Count;
  EndIndex = *Count;
  for (Index = 0; Index < *Count; Index++) {
    if ((StartIndex == *Count) &&
        (Ranges[Index].BaseAddress <= BaseAddress) &&
        (BaseAddress < Ranges[Index].BaseAddress + Ranges[Index].Length)) {
      StartIndex = Index;
      LengthLeft = BaseAddress - Ranges[Index].BaseAddress;
    }

    if ((EndIndex == *Count) &&
        (Ranges[Index].BaseAddress < Limit) &&
        (Limit <= Ranges[Index].BaseAddress + Ranges[Index].Length)) {
      EndIndex = Index;
      LengthRight = Ranges[Index].BaseAddress + Ranges[Index].Length - Limit;
      break;
    }
  }

  ASSERT (StartIndex != *Count && EndIndex != *Count);
  if (StartIndex == EndIndex && Ranges[StartIndex].Type == Type) {
    return RETURN_ALREADY_STARTED;
  }

  //
  // The type change may cause merging with previous range or next range.
  // Update the StartIndex, EndIndex, BaseAddress, Length so that following
  // logic doesn't need to consider merging.
  //
  if (StartIndex != 0) {
    if (LengthLeft == 0 && Ranges[StartIndex - 1].Type == Type) {
      StartIndex--;
      Length += Ranges[StartIndex].Length;
      BaseAddress -= Ranges[StartIndex].Length;
    }
  }
  if (EndIndex != (*Count) - 1) {
    if (LengthRight == 0 && Ranges[EndIndex + 1].Type == Type) {
      EndIndex++;
      Length += Ranges[EndIndex].Length;
    }
  }

  //
  // |- 0 -|- 1 -|- 2 -|- 3 -| StartIndex EndIndex DeltaCount  Count (Count = 4)
  //   |++++++++++++++++++|    0          3         1=3-0-2    3
  //   |+++++++|               0          1        -1=1-0-2    5
  //   |+|                     0          0        -2=0-0-2    6
  // |+++|                     0          0        -1=0-0-2+1  5
  //
  //
  DeltaCount = EndIndex - StartIndex - 2;
  if (LengthLeft == 0) {
    DeltaCount++;
  }
  if (LengthRight == 0) {
    DeltaCount++;
  }
  if (*Count - DeltaCount > Capacity) {
    return RETURN_OUT_OF_RESOURCES;
  }

  //
  // Reserve (-DeltaCount) space
  //
  CopyMem (&Ranges[EndIndex + 1 - DeltaCount], &Ranges[EndIndex + 1], (*Count - EndIndex - 1) * sizeof (Ranges[0]));
  *Count -= DeltaCount;

  if (LengthLeft != 0) {
    Ranges[StartIndex].Length = LengthLeft;
    StartIndex++;
  }
  if (LengthRight != 0) {
    Ranges[EndIndex - DeltaCount].BaseAddress = BaseAddress + Length;
    Ranges[EndIndex - DeltaCount].Length = LengthRight;
    Ranges[EndIndex - DeltaCount].Type = Ranges[EndIndex].Type;
  }
  Ranges[StartIndex].BaseAddress = BaseAddress;
  Ranges[StartIndex].Length = Length;
  Ranges[StartIndex].Type = Type;
  return RETURN_SUCCESS;
}

/**
  Return the number of memory types in range [BaseAddress, BaseAddress + Length).

  @param Ranges      Array holding memory type settings for all memory regions.
  @param RangeCount  The count of memory ranges the array holds.
  @param BaseAddress Base address.
  @param Length      Length.
  @param Types       Return bit mask to indicate all memory types in the specified range.

  @retval  Number of memory types.
**/
UINT8
MtrrLibGetNumberOfTypes (
  IN CONST MTRR_MEMORY_RANGE     *Ranges,
  IN UINTN                       RangeCount,
  IN UINT64                      BaseAddress,
  IN UINT64                      Length,
  IN OUT UINT8                   *Types  OPTIONAL
  )
{
  UINTN                          Index;
  UINT8                          TypeCount;
  UINT8                          LocalTypes;

  TypeCount = 0;
  LocalTypes = 0;
  for (Index = 0; Index < RangeCount; Index++) {
    if ((Ranges[Index].BaseAddress <= BaseAddress) &&
        (BaseAddress < Ranges[Index].BaseAddress + Ranges[Index].Length)
        ) {
      if ((LocalTypes & (1 << Ranges[Index].Type)) == 0) {
        LocalTypes |= (UINT8)(1 << Ranges[Index].Type);
        TypeCount++;
      }

      if (BaseAddress + Length > Ranges[Index].BaseAddress + Ranges[Index].Length) {
        Length -= Ranges[Index].BaseAddress + Ranges[Index].Length - BaseAddress;
        BaseAddress = Ranges[Index].BaseAddress + Ranges[Index].Length;
      } else {
        break;
      }
    }
  }

  if (Types != NULL) {
    *Types = LocalTypes;
  }
  return TypeCount;
}

/**
  Return the memory type bit mask that's compatible to first type in the Ranges.

  @param Ranges     Memory range array holding the memory type
                    settings for all memory address.
  @param RangeCount Count of memory ranges.

  @return Compatible memory type bit mask.
**/
UINT8
MtrrLibGetCompatibleTypes (
  IN CONST MTRR_MEMORY_RANGE *Ranges,
  IN UINTN                   RangeCount
  )
{
  ASSERT (RangeCount != 0);

  switch (Ranges[0].Type) {
  case CacheWriteBack:
  case CacheWriteThrough:
    return (1 << CacheWriteBack) | (1 << CacheWriteThrough) | (1 << CacheUncacheable);
    break;

  case CacheWriteCombining:
  case CacheWriteProtected:
    return (1 << Ranges[0].Type) | (1 << CacheUncacheable);
    break;

  case CacheUncacheable:
    if (RangeCount == 1) {
      return (1 << CacheUncacheable);
    }
    return MtrrLibGetCompatibleTypes (&Ranges[1], RangeCount - 1);
    break;

  case CacheInvalid:
  default:
    ASSERT (FALSE);
    break;
  }
  return 0;
}


/**
  Apply the fixed MTRR settings to memory range array.

  @param Fixed             The fixed MTRR settings.
  @param Ranges            Return the memory range array holding memory type
                           settings for all memory address.
  @param RangeCapacity     The capacity of memory range array.
  @param RangeCount        Return the count of memory range.

  @retval RETURN_SUCCESS          The memory range array is returned successfully.
  @retval RETURN_OUT_OF_RESOURCES The count of memory ranges exceeds capacity.
**/
RETURN_STATUS
MtrrLibApplyFixedMtrrs (
  IN     MTRR_FIXED_SETTINGS  *Fixed,
  IN OUT MTRR_MEMORY_RANGE    *Ranges,
  IN     UINTN                RangeCapacity,
  IN OUT UINTN                *RangeCount
  )
{
  RETURN_STATUS               Status;
  UINTN                       MsrIndex;
  UINTN                       Index;
  MTRR_MEMORY_CACHE_TYPE      MemoryType;
  UINT64                      Base;

  Base = 0;
  for (MsrIndex = 0; MsrIndex < ARRAY_SIZE (mMtrrLibFixedMtrrTable); MsrIndex++) {
    ASSERT (Base == mMtrrLibFixedMtrrTable[MsrIndex].BaseAddress);
    for (Index = 0; Index < sizeof (UINT64); Index++) {
      MemoryType = (MTRR_MEMORY_CACHE_TYPE)((UINT8 *)(&Fixed->Mtrr[MsrIndex]))[Index];
      Status = MtrrLibSetMemoryType (
                 Ranges, RangeCapacity, RangeCount, Base, mMtrrLibFixedMtrrTable[MsrIndex].Length, MemoryType
                 );
      if (Status == RETURN_OUT_OF_RESOURCES) {
        return Status;
      }
      Base += mMtrrLibFixedMtrrTable[MsrIndex].Length;
    }
  }
  ASSERT (Base == BASE_1MB);
  return RETURN_SUCCESS;
}

/**
  Apply the variable MTRR settings to memory range array.

  @param VariableMtrr      The variable MTRR array.
  @param VariableMtrrCount The count of variable MTRRs.
  @param Ranges            Return the memory range array with new MTRR settings applied.
  @param RangeCapacity     The capacity of memory range array.
  @param RangeCount        Return the count of memory range.

  @retval RETURN_SUCCESS          The memory range array is returned successfully.
  @retval RETURN_OUT_OF_RESOURCES The count of memory ranges exceeds capacity.
**/
RETURN_STATUS
MtrrLibApplyVariableMtrrs (
  IN     CONST MTRR_MEMORY_RANGE *VariableMtrr,
  IN     UINT32                  VariableMtrrCount,
  IN OUT MTRR_MEMORY_RANGE       *Ranges,
  IN     UINTN                   RangeCapacity,
  IN OUT UINTN                   *RangeCount
  )
{
  RETURN_STATUS                  Status;
  UINTN                          Index;

  //
  // WT > WB
  // UC > *
  // UC > * (except WB, UC) > WB
  //

  //
  // 1. Set WB
  //
  for (Index = 0; Index < VariableMtrrCount; Index++) {
    if ((VariableMtrr[Index].Length != 0) && (VariableMtrr[Index].Type == CacheWriteBack)) {
      Status = MtrrLibSetMemoryType (
        Ranges, RangeCapacity, RangeCount,
        VariableMtrr[Index].BaseAddress, VariableMtrr[Index].Length, VariableMtrr[Index].Type
      );
      if (Status == RETURN_OUT_OF_RESOURCES) {
        return Status;
      }
    }
  }

  //
  // 2. Set other types than WB or UC
  //
  for (Index = 0; Index < VariableMtrrCount; Index++) {
    if ((VariableMtrr[Index].Length != 0) &&
        (VariableMtrr[Index].Type != CacheWriteBack) && (VariableMtrr[Index].Type != CacheUncacheable)) {
      Status = MtrrLibSetMemoryType (
                 Ranges, RangeCapacity, RangeCount,
                 VariableMtrr[Index].BaseAddress, VariableMtrr[Index].Length, VariableMtrr[Index].Type
                 );
      if (Status == RETURN_OUT_OF_RESOURCES) {
        return Status;
      }
    }
  }

  //
  // 3. Set UC
  //
  for (Index = 0; Index < VariableMtrrCount; Index++) {
    if (VariableMtrr[Index].Length != 0 && VariableMtrr[Index].Type == CacheUncacheable) {
      Status = MtrrLibSetMemoryType (
                 Ranges, RangeCapacity, RangeCount,
                 VariableMtrr[Index].BaseAddress, VariableMtrr[Index].Length, VariableMtrr[Index].Type
                 );
      if (Status == RETURN_OUT_OF_RESOURCES) {
        return Status;
      }
    }
  }
  return RETURN_SUCCESS;
}


/**
  This function attempts to set the attributes into MTRR setting buffer for multiple memory ranges.

  @param[in, out]  MtrrSetting  MTRR setting buffer to be set.
  @param[in]       Scratch      A temporary scratch buffer that is used to perform the calculation.
  @param[in, out]  ScratchSize  Pointer to the size in bytes of the scratch buffer.
                                It may be updated to the actual required size when the calculation
                                needs more scratch buffer.
  @param[in]       Ranges       Pointer to an array of MTRR_MEMORY_RANGE.
                                When range overlap happens, the last one takes higher priority.
                                When the function returns, either all the attributes are set successfully,
                                or none of them is set.
  @param[in]       RangeCount   Count of MTRR_MEMORY_RANGE.

  @retval RETURN_SUCCESS            The attributes were set for all the memory ranges.
  @retval RETURN_INVALID_PARAMETER  Length in any range is zero.
  @retval RETURN_UNSUPPORTED        The processor does not support one or more bytes of the
                                    memory resource range specified by BaseAddress and Length in any range.
  @retval RETURN_UNSUPPORTED        The bit mask of attributes is not support for the memory resource
                                    range specified by BaseAddress and Length in any range.
  @retval RETURN_OUT_OF_RESOURCES   There are not enough system resources to modify the attributes of
                                    the memory resource ranges.
  @retval RETURN_ACCESS_DENIED      The attributes for the memory resource range specified by
                                    BaseAddress and Length cannot be modified.
  @retval RETURN_BUFFER_TOO_SMALL   The scratch buffer is too small for MTRR calculation.
**/
RETURN_STATUS
EFIAPI
MtrrSetMemoryAttributesInMtrrSettings (
  IN OUT MTRR_SETTINGS           *MtrrSetting,
  IN     VOID                    *Scratch,
  IN OUT UINTN                   *ScratchSize,
  IN     CONST MTRR_MEMORY_RANGE *Ranges,
  IN     UINTN                   RangeCount
  )
{
  return RETURN_SUCCESS;
}

/**
  This function attempts to set the attributes into MTRR setting buffer for a memory range.

  @param[in, out]  MtrrSetting  MTRR setting buffer to be set.
  @param[in]       BaseAddress  The physical address that is the start address
                                of a memory range.
  @param[in]       Length       The size in bytes of the memory range.
  @param[in]       Attribute    The bit mask of attributes to set for the
                                memory range.

  @retval RETURN_SUCCESS            The attributes were set for the memory range.
  @retval RETURN_INVALID_PARAMETER  Length is zero.
  @retval RETURN_UNSUPPORTED        The processor does not support one or more bytes of the
                                    memory resource range specified by BaseAddress and Length.
  @retval RETURN_UNSUPPORTED        The bit mask of attributes is not support for the memory resource
                                    range specified by BaseAddress and Length.
  @retval RETURN_ACCESS_DENIED      The attributes for the memory resource range specified by
                                    BaseAddress and Length cannot be modified.
  @retval RETURN_OUT_OF_RESOURCES   There are not enough system resources to modify the attributes of
                                    the memory resource range.
                                    Multiple memory range attributes setting by calling this API multiple
                                    times may fail with status RETURN_OUT_OF_RESOURCES. It may not mean
                                    the number of CPU MTRRs are too small to set such memory attributes.
                                    Pass the multiple memory range attributes to one call of
                                    MtrrSetMemoryAttributesInMtrrSettings() may succeed.
  @retval RETURN_BUFFER_TOO_SMALL   The fixed internal scratch buffer is too small for MTRR calculation.
                                    Caller should use MtrrSetMemoryAttributesInMtrrSettings() to specify
                                    external scratch buffer.
**/
RETURN_STATUS
EFIAPI
MtrrSetMemoryAttributeInMtrrSettings (
  IN OUT MTRR_SETTINGS       *MtrrSetting,
  IN PHYSICAL_ADDRESS        BaseAddress,
  IN UINT64                  Length,
  IN MTRR_MEMORY_CACHE_TYPE  Attribute
  )
{
  return RETURN_SUCCESS;
}

/**
  This function attempts to set the attributes for a memory range.

  @param[in]  BaseAddress        The physical address that is the start
                                 address of a memory range.
  @param[in]  Length             The size in bytes of the memory range.
  @param[in]  Attributes         The bit mask of attributes to set for the
                                 memory range.

  @retval RETURN_SUCCESS            The attributes were set for the memory
                                    range.
  @retval RETURN_INVALID_PARAMETER  Length is zero.
  @retval RETURN_UNSUPPORTED        The processor does not support one or
                                    more bytes of the memory resource range
                                    specified by BaseAddress and Length.
  @retval RETURN_UNSUPPORTED        The bit mask of attributes is not support
                                    for the memory resource range specified
                                    by BaseAddress and Length.
  @retval RETURN_ACCESS_DENIED      The attributes for the memory resource
                                    range specified by BaseAddress and Length
                                    cannot be modified.
  @retval RETURN_OUT_OF_RESOURCES   There are not enough system resources to
                                    modify the attributes of the memory
                                    resource range.
                                    Multiple memory range attributes setting by calling this API multiple
                                    times may fail with status RETURN_OUT_OF_RESOURCES. It may not mean
                                    the number of CPU MTRRs are too small to set such memory attributes.
                                    Pass the multiple memory range attributes to one call of
                                    MtrrSetMemoryAttributesInMtrrSettings() may succeed.
  @retval RETURN_BUFFER_TOO_SMALL   The fixed internal scratch buffer is too small for MTRR calculation.
                                    Caller should use MtrrSetMemoryAttributesInMtrrSettings() to specify
                                    external scratch buffer.
**/
RETURN_STATUS
EFIAPI
MtrrSetMemoryAttribute (
  IN PHYSICAL_ADDRESS        BaseAddress,
  IN UINT64                  Length,
  IN MTRR_MEMORY_CACHE_TYPE  Attribute
  )
{
  return RETURN_SUCCESS;
}

/**
  This function sets variable MTRRs

  @param[in]  VariableSettings   A buffer to hold variable MTRRs content.

  @return The pointer of VariableSettings

**/
MTRR_VARIABLE_SETTINGS*
EFIAPI
MtrrSetVariableMtrr (
  IN MTRR_VARIABLE_SETTINGS         *VariableSettings
  )
{
  return  VariableSettings;
}

/**
  This function sets fixed MTRRs

  @param[in]  FixedSettings  A buffer to hold fixed MTRRs content.

  @retval The pointer of FixedSettings

**/
MTRR_FIXED_SETTINGS*
EFIAPI
MtrrSetFixedMtrr (
  IN MTRR_FIXED_SETTINGS          *FixedSettings
  )
{
  return FixedSettings;
}


/**
  This function gets the content in all MTRRs (variable and fixed)

  @param[out]  MtrrSetting  A buffer to hold all MTRRs content.

  @retval the pointer of MtrrSetting

**/
MTRR_SETTINGS *
EFIAPI
MtrrGetAllMtrrs (
  OUT MTRR_SETTINGS                *MtrrSetting
  )
{
  if (!IsMtrrSupported ()) {
    return MtrrSetting;
  }

  //
  // Get fixed MTRRs
  //
  MtrrGetFixedMtrrWorker (&MtrrSetting->Fixed);

  //
  // Get variable MTRRs
  //
  MtrrGetVariableMtrrWorker (
    NULL,
    GetVariableMtrrCountWorker (),
    &MtrrSetting->Variables
    );

  //
  // Get MTRR_DEF_TYPE value
  //
  MtrrSetting->MtrrDefType = AsmReadMsr64 (MSR_IA32_MTRR_DEF_TYPE);

  return MtrrSetting;
}


/**
  This function sets all MTRRs (variable and fixed)

  @param[in]  MtrrSetting  A buffer holding all MTRRs content.

  @retval The pointer of MtrrSetting

**/
MTRR_SETTINGS *
EFIAPI
MtrrSetAllMtrrs (
  IN MTRR_SETTINGS                *MtrrSetting
  )
{
  return MtrrSetting;
}


/**
  Checks if MTRR is supported.

  @retval TRUE  MTRR is supported.
  @retval FALSE MTRR is not supported.

**/
BOOLEAN
EFIAPI
IsMtrrSupported (
  VOID
  )
{
  CPUID_VERSION_INFO_EDX    Edx;
  MSR_IA32_MTRRCAP_REGISTER MtrrCap;

  //
  // Check CPUID(1).EDX[12] for MTRR capability
  //
  AsmCpuid (CPUID_VERSION_INFO, NULL, NULL, NULL, &Edx.Uint32);
  if (Edx.Bits.MTRR == 0) {
    return FALSE;
  }

  //
  // Check number of variable MTRRs and fixed MTRRs existence.
  // If number of variable MTRRs is zero, or fixed MTRRs do not
  // exist, return false.
  //
  MtrrCap.Uint64 = AsmReadMsr64 (MSR_IA32_MTRRCAP);
  if ((MtrrCap.Bits.VCNT == 0) || (MtrrCap.Bits.FIX == 0)) {
    return FALSE;
  }
  return TRUE;
}


/**
  Worker function prints all MTRRs for debugging.

  If MtrrSetting is not NULL, print MTRR settings from input MTRR
  settings buffer.
  If MtrrSetting is NULL, print MTRR settings from MTRRs.

  @param  MtrrSetting    A buffer holding all MTRRs content.
**/
VOID
MtrrDebugPrintAllMtrrsWorker (
  IN MTRR_SETTINGS    *MtrrSetting
  )
{
  DEBUG_CODE (
    MTRR_SETTINGS     LocalMtrrs;
    MTRR_SETTINGS     *Mtrrs;
    UINTN             Index;
    UINTN             RangeCount;
    UINT64            MtrrValidBitsMask;
    UINT64            MtrrValidAddressMask;
    UINT32            VariableMtrrCount;
    BOOLEAN           ContainVariableMtrr;
    MTRR_MEMORY_RANGE Ranges[
      ARRAY_SIZE (mMtrrLibFixedMtrrTable) * sizeof (UINT64) + 2 * ARRAY_SIZE (Mtrrs->Variables.Mtrr) + 1
      ];
    MTRR_MEMORY_RANGE RawVariableRanges[ARRAY_SIZE (Mtrrs->Variables.Mtrr)];

    if (!IsMtrrSupported ()) {
      return;
    }

    VariableMtrrCount = GetVariableMtrrCountWorker ();

    if (MtrrSetting != NULL) {
      Mtrrs = MtrrSetting;
    } else {
      MtrrGetAllMtrrs (&LocalMtrrs);
      Mtrrs = &LocalMtrrs;
    }

    //
    // Dump RAW MTRR contents
    //
    DEBUG ((DEBUG_CACHE, "MTRR Settings:\n"));
    DEBUG ((DEBUG_CACHE, "=============\n"));
    DEBUG ((DEBUG_CACHE, "MTRR Default Type: %016lx\n", Mtrrs->MtrrDefType));
    for (Index = 0; Index < ARRAY_SIZE (mMtrrLibFixedMtrrTable); Index++) {
      DEBUG ((DEBUG_CACHE, "Fixed MTRR[%02d]   : %016lx\n", Index, Mtrrs->Fixed.Mtrr[Index]));
    }
    ContainVariableMtrr = FALSE;
    for (Index = 0; Index < VariableMtrrCount; Index++) {
      if ((Mtrrs->Variables.Mtrr[Index].Mask & BIT11) == 0) {
        //
        // If mask is not valid, then do not display range
        //
        continue;
      }
      ContainVariableMtrr = TRUE;
      DEBUG ((DEBUG_CACHE, "Variable MTRR[%02d]: Base=%016lx Mask=%016lx\n",
        Index,
        Mtrrs->Variables.Mtrr[Index].Base,
        Mtrrs->Variables.Mtrr[Index].Mask
        ));
    }
    if (!ContainVariableMtrr) {
      DEBUG ((DEBUG_CACHE, "Variable MTRR    : None.\n"));
    }
    DEBUG((DEBUG_CACHE, "\n"));

    //
    // Dump MTRR setting in ranges
    //
    DEBUG((DEBUG_CACHE, "Memory Ranges:\n"));
    DEBUG((DEBUG_CACHE, "====================================\n"));
    MtrrLibInitializeMtrrMask (&MtrrValidBitsMask, &MtrrValidAddressMask);
    Ranges[0].BaseAddress = 0;
    Ranges[0].Length      = MtrrValidBitsMask + 1;
    Ranges[0].Type        = MtrrGetDefaultMemoryTypeWorker (Mtrrs);
    RangeCount = 1;

    MtrrLibGetRawVariableRanges (
      &Mtrrs->Variables, VariableMtrrCount,
      MtrrValidBitsMask, MtrrValidAddressMask, RawVariableRanges
      );
    MtrrLibApplyVariableMtrrs (
      RawVariableRanges, VariableMtrrCount,
      Ranges, ARRAY_SIZE (Ranges), &RangeCount
      );

    MtrrLibApplyFixedMtrrs (&Mtrrs->Fixed, Ranges, ARRAY_SIZE (Ranges), &RangeCount);

    for (Index = 0; Index < RangeCount; Index++) {
      DEBUG ((DEBUG_CACHE, "%a:%016lx-%016lx\n",
        mMtrrMemoryCacheTypeShortName[Ranges[Index].Type],
        Ranges[Index].BaseAddress, Ranges[Index].BaseAddress + Ranges[Index].Length - 1
        ));
    }
  );
}

/**
  This function prints all MTRRs for debugging.
**/
VOID
EFIAPI
MtrrDebugPrintAllMtrrs (
  VOID
  )
{
  MtrrDebugPrintAllMtrrsWorker (NULL);
}
