//
// Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.
// SPDX-License-Identifier: GPL-2-or-later
//
// This unit implements 2D Lanczos scaling.  Downscaling isn't taken into
// account (should look at more than 25 pixels to work properly) and thus
// shouldn't really be used because results don't look nice.
//
// The implementation takes advantage of the separable nature of the
// transformation: can be applied to any number of dimensions by applying it to
// each dimension in turn (order doesn't matter).  Each 1D scaling is performed
// with precomputed weights.  Taken together this makes the implementation
// reasonably fast so that it should run under 50ms in most cases (this is
// rounded, one result is that 600x308 logo is doubled in size in just 13ms).
//

#include "Scaling.h"

#include <Protocol/GraphicsOutput.h>
#include <Protocol/HiiImage.h>

#include <Library/MemoryAllocationLib.h>

#include "Math.h"

// Shorten the type name.
typedef EFI_GRAPHICS_OUTPUT_BLT_PIXEL Pixel;

/**
  Lanczos-3 kernel used for convolution (5x5 for 2D case).  The definition can
  be seen at <https://en.wikipedia.org/wiki/Lanczos_resampling>.

  @param[in] D  Distance of an interpolated point from an original position
                along some dimension.

  @retval W  Weight that signifies contribution of the original point to the
             computed interpolated value.
**/
STATIC
float
Lanczos3 (
  IN float  D
  )
{
  if (D == 0) {
    return 1;
  }

  if (D > -3 && D < 3) {
    return (3 * Sin (D * M_PI) * Sin (D * M_PI / 3)) / (D * D * M_PI * M_PI);
  }

  return 0;
}

/**
  Converts result of interpolation into 0-255 range.

  @param[in] F  Result of interpolation.

  @retval U  8-bit unsigned value of a color channel.
**/
STATIC
UINT8
Clamp (
  IN float  F
  )
{
  if (F < 0) {
    return 0;
  }
  if (F > 255) {
    return 255;
  }
  return F;
}

/**
  Performs a 1D Lanczos scaling.

  @param[in]  In         Original pixels.
  @param[in]  InStride   Distance between two neighbours of a dimension.
  @param[in]  InLength   Original size of a dimension.
  @param[out] Out        Scaled pixels.
  @param[in]  OutStride  Distance between two neighbours of a dimension.
  @param[in]  OutLength  Scaled size of a dimension.
  @param[in]  Ws         (OutLength x 5) matrix of weights.
**/
STATIC
VOID
Scale1D (
  IN     CONST Pixel  *In,
  IN     UINTN        InStride,
  IN     UINTN        InLength,
     OUT Pixel        *Out,
  IN     UINTN        OutStride,
  IN     UINTN        OutLength,
  IN     CONST float  *Ws
  )
{
  UINTN  DstIndex;
  Pixel  P;
  float  Ratio;
  INTN   Offset;
  INTN   SrcIndex;
  float  ProjectedIndex;
  float  BlueSum;
  float  GreenSum;
  float  RedSum;
  float  W;
  float  TotalW;

  // Move to the central column for convenience.
  Ws += 2;

  Ratio = (InLength - 1.0f) / (OutLength - 1.0f);
  for (DstIndex = 0; DstIndex < OutLength; ++DstIndex) {
    ProjectedIndex = DstIndex * Ratio;

    BlueSum  = 0.0f;
    GreenSum = 0.0f;
    RedSum   = 0.0f;
    TotalW   = 0.0f;

    for (Offset = -2; Offset <= 2; ++Offset) {
      SrcIndex = (INTN)ProjectedIndex + Offset;
      if (SrcIndex >= 0 && SrcIndex < InLength) {
        P = In[SrcIndex * InStride];

        W = Ws[DstIndex * 5 + Offset];
        BlueSum  += P.Blue * W;
        GreenSum += P.Green * W;
        RedSum   += P.Red * W;
        TotalW   += W;
      }
    }

    P.Blue     = Clamp (BlueSum / TotalW);
    P.Green    = Clamp (GreenSum / TotalW);
    P.Red      = Clamp (RedSum / TotalW);
    P.Reserved = 0;

    Out[DstIndex * OutStride] = P;
  }
}

/**
  Pre-computes weights for a 1D Lanczos scaling.

  @param[in]     Original  Original size of a dimension.
  @param[in]     Scaled    Scaled size of a dimension.
  @param[in,out] Ws        (Scaled x 5) matrix of weights.
**/
STATIC
VOID
PrecomputeWeights (
  IN     UINTN  Original,
  IN     UINTN  Scaled,
  IN OUT float  *Ws
  )
{
  UINTN  DstIndex;
  INTN   SrcIndex;
  INTN   Offset;
  float  ProjectedIndex;
  float  Ratio;

  // Move to the central column for convenience.
  Ws += 2;

  Ratio = (Original - 1.0f) / (Scaled - 1.0f);
  for (DstIndex = 0; DstIndex < Scaled; ++DstIndex) {
    ProjectedIndex = DstIndex * Ratio;
    for (Offset = -2; Offset <= 2; ++Offset) {
      SrcIndex = (INTN)ProjectedIndex + Offset;
      Ws[DstIndex * 5 + Offset] = Lanczos3 (ProjectedIndex - SrcIndex);
    }
  }
}

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
  )
{
  EFI_IMAGE_INPUT  Tmp;
  float            *Ws;
  UINTN            Index;

  //
  // The weights will be precomputed twice (once for each 1D scaling because
  // each row/column uses the same weights).  Allocate enough space for both
  // cases.
  //
  Ws = AllocatePool (MAX (Scaled->Width, Scaled->Height) * 5 * sizeof (*Ws));
  if (Ws == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Tmp.Height = Original->Height;
  Tmp.Width  = Scaled->Width;
  Tmp.Bitmap = AllocatePool (Original->Height * Scaled->Width * sizeof (Pixel));
  if (Tmp.Bitmap == NULL) {
    FreePool (Ws);
    return EFI_OUT_OF_RESOURCES;
  }

  Scaled->Bitmap = AllocatePool (Scaled->Height * Scaled->Width * sizeof (Pixel));
  if (Scaled->Bitmap == NULL) {
    FreePool (Ws);
    FreePool (Tmp.Bitmap);
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Scale horizontally.  Iterating by rows and stretching them.
  //
  // (OriginalHeight x OriginalWidth) -> (OriginalHeight x ScaledWidth)
  //
  PrecomputeWeights (Original->Width, Tmp.Width, Ws);
  for (Index = 0; Index < Original->Height; ++Index) {
    Scale1D (
      Original->Bitmap + Index * Original->Width, // Source row.
      1,                                          // Source stride.
      Original->Width,                            // Source row length.
      Tmp.Bitmap + Index * Tmp.Width,             // Target row.
      1,                                          // Target stride.
      Tmp.Width,                                  // Target row length.
      Ws                                          // Precomputed weights.
      );
  }

  //
  // Scale vertically.  Iterating by columns and stretching them.
  //
  // (OriginalHeight x ScaledWidth) -> (ScaledHeight x ScaledWidth)
  //
  PrecomputeWeights (Tmp.Height, Scaled->Height, Ws);
  for (Index = 0; Index < Tmp.Width; ++Index) {
    Scale1D (
      Tmp.Bitmap + Index,     // Source column.
      Tmp.Width,              // Source stride.
      Tmp.Height,             // Source column length.
      Scaled->Bitmap + Index, // Target column.
      Tmp.Width,              // Target stride.
      Scaled->Height,         // Target column length.
      Ws                      // Precomputed weights.
      );
  }

  FreePool (Tmp.Bitmap);

  return EFI_SUCCESS;
}
