//
// Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.
// SPDX-License-Identifier: GPL-2-or-later
//
// Implementation of image scaling.  EFI_IMAGE_INPUT is used just as a
// convenience to not pass lots of parameters.
//

#ifndef _SCALING_H_
#define _SCALING_H_

#include <Protocol/HiiImage.h>

/**
  Upscales an image to the desired dimensions.  Downscaling also works, but
  doesn't produce an accurate result.

  @param[in]     Original  Bitmap to scale and its dimensions.
  @param[in,out] Scaled    Input: dimension fields.
                           Output: the bitmap field.

  @retval EFI_SUCCESS           Scaled->Bitmap was initialized and filled with
                                the scaled image.
  @retval EFI_OUT_OF_RESOURCES  Not enough memory for upscaling.
**/
EFI_STATUS
EFIAPI
ScaleImage (
  IN     CONST EFI_IMAGE_INPUT  *Original,
  IN OUT       EFI_IMAGE_INPUT  *Scaled
  );

#endif // _SCALING_H_
