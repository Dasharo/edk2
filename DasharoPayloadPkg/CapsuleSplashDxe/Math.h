//
// Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.
// SPDX-License-Identifier: GPL-2-or-later
//
// Math-related functions needed for image scaling.
//

#ifndef _MATH_H_
#define _MATH_H_

#define M_PI 3.14159265358979323846

/**
  Computes sin() of the argument.

  @param[in]  X  Angle in radians.

  @retval Y  sin(X)
**/
double
EFIAPI
Sin (
  IN double  X
  );

#endif // _MATH_H_
