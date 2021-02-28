/** @file
  Various register numbers and value bits based on the following publications:
  - Intel(R) datasheet 316966-002
  - Intel(R) datasheet 316972-004

  Copyright (C) 2015, Red Hat, Inc.
  Copyright (c) 2014, Gabriel L. Somlo <somlo@cmu.edu>

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef __INTEL_REGS_H__
#define __INTEL_REGS_H__

#include <Library/PciLib.h>
#include <Uefi/UefiBaseType.h>
#include <Uefi/UefiSpec.h>
#include <Protocol/PciRootBridgeIo.h>

//
// B/D/F/Type: 0/0/0/PCI
//
#define DRAMC_REGISTER(Offset) PCI_LIB_ADDRESS (0, 0, 0, (Offset))


#define MCH_PAM0                  0x80
#define MCH_PAM1                  0x81
#define MCH_PAM2                  0x82
#define MCH_PAM3                  0x83
#define MCH_PAM4                  0x84
#define MCH_PAM5                  0x85
#define MCH_PAM6                  0x86

#define MSR_AMD_MTRR_FIX4K_C0000          0x00000268
#define MSR_AMD_MTRR_FIX4K_C8000          0x00000269
#define MSR_AMD_MTRR_FIX4K_D0000          0x0000026A
#define MSR_AMD_MTRR_FIX4K_D8000          0x0000026B
#define MSR_AMD_MTRR_FIX4K_E0000          0x0000026C
#define MSR_AMD_MTRR_FIX4K_E8000          0x0000026D
#define MSR_AMD_MTRR_FIX4K_F0000          0x0000026E
#define MSR_AMD_MTRR_FIX4K_F8000          0x0000026F

#endif
