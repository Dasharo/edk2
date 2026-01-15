//
// A utility library for drawing simple popups.
//
// Implementation of drawing a GUI popup that takes DPI into account to a
// degree.
//
// Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
// SPDX-License-Identifier: GPL-2-or-later
//

#include <Library/PopUpLib.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

/* #include <PiDxe.h> */
#include <Protocol/GraphicsOutput.h>
#include <Protocol/HiiFont.h>

// Configuration
#define GLYPH_WIDTH         8
#define GLYPH_HEIGHT        19
#define PADDING_X           (2 * GLYPH_WIDTH) // 2 chars padding (unscaled)
#define PADDING_Y           (1 * GLYPH_HEIGHT) // 1 line padding (unscaled)
#define BORDER_WIDTH        4

// Colors (Blue Background, White Text, White Border)
#define POPUP_TEXT_COLOR    {0xFF, 0xFF, 0xFF, 0xFF}
#define POPUP_BORDER_COLOR  {0xFF, 0xFF, 0xFF, 0xFF}

STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL ErrorBgColor   = { 4, 117, 204, 0xFF };
STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL SuccessBgColor = { 3, 176, 100, 0xFF };

/**
  Helper: Upscales a source Blt buffer by ScaleFactor into a destination buffer.
  Uses Nearest-Neighbor scaling.
**/
STATIC
VOID
UpscaleBuffer (
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Source,
  IN UINTN                          SourceWidth,
  IN UINTN                          SourceHeight,
  IN UINTN                          ScaleFactor,
  OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Dest
  )
{
  UINTN x, y, i, j;
  UINTN DestWidth = SourceWidth * ScaleFactor;

  for (y = 0; y < SourceHeight; y++) {
    for (x = 0; x < SourceWidth; x++) {
      EFI_GRAPHICS_OUTPUT_BLT_PIXEL Pixel = Source[y * SourceWidth + x];

      // Top-left corner of the destination block
      UINTN DestStartX = x * ScaleFactor;
      UINTN DestStartY = y * ScaleFactor;

      // Fill the ScaleFactor * ScaleFactor block in the destination
      for (i = 0; i < ScaleFactor; i++) {       // Row
        for (j = 0; j < ScaleFactor; j++) {     // Column
          Dest[(DestStartY + i) * DestWidth + (DestStartX + j)] = Pixel;
        }
      }
    }
  }
}

/**
  Clears the screen to black using GOP.
**/
STATIC
VOID
EFIAPI
ClearScreen (
  VOID
  )
{
  EFI_STATUS                    Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Black = {0x00, 0x00, 0x00, 0x00};

  // Locate GOP
  Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (EFI_ERROR(Status)) {
    return;
  }

  // Fill the entire screen with black
  Gop->Blt (
    Gop,
    &Black,
    EfiBltVideoFill,
    0, 0, // Source X, Y (Not used for Fill)
    0, 0, // Dest X, Y
    Gop->Mode->Info->HorizontalResolution,
    Gop->Mode->Info->VerticalResolution,
    0     // Delta (Not used for Fill)
  );
}

STATIC
UINTN
GetDisplayScaleFactor (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  )
{
  EFI_STATUS                           Status;
  UINT32                               ModeIndex;
  UINT32                               MaxHRes = 0;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
  UINTN                                SizeOfInfo;

  // Iterate through all modes to find the largest (presumed native) resolution
  for (ModeIndex = 0; ModeIndex < Gop->Mode->MaxMode; ModeIndex++) {
    Status = Gop->QueryMode (Gop, ModeIndex, &SizeOfInfo, &Info);

    if (!EFI_ERROR (Status)) {
      if (Info->HorizontalResolution > MaxHRes) {
        MaxHRes = Info->HorizontalResolution;
      }
      FreePool (Info);
    }
  }

  if (MaxHRes > 1920)
    return 2;

  return 1;
}

/**
  Draw a pop up windows based on the dimension, number of lines and
  strings specified. (GOP Scaled Version)

  @param BgColor         Color of the background.
  @param RequestedWidth  The width of the pop-up in standard character cells.
                         If 0, autosize to fit longest string.
  @param NumberOfLines   The number of lines.
  @param ...             A series of text strings that displayed in the pop-up.

**/
VOID
EFIAPI
DrawGraphicPopUp (
  IN  PopupKind  Kind,
  IN  UINTN      RequestedWidth,
  IN  UINTN      NumberOfLines,
  ...
  )
{
  UINTN       ScaleFactor;
  EFI_STATUS  Status;
  VA_LIST     Args, ArgsCopy;
  CHAR16      *String;
  UINTN       Index;

  // Protocols
  EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop;
  EFI_HII_FONT_PROTOCOL          *HiiFont;

  // Dimensions (Pixels)
  UINTN                          ScreenWidth, ScreenHeight;
  UINTN                          UnscaledContentW, UnscaledContentH;
  UINTN                          BoxW, BoxH;
  UINTN                          BoxX, BoxY;

  // Buffers
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *BackBuffer = NULL;      // Saves what is behind popup
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *RenderBuffer1x = NULL;  // Temporary buffer for font rendering
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *RenderBufferScaled = NULL;  // Upscaled buffer for display
  EFI_IMAGE_OUTPUT               *BltBufferObj = NULL;    // HII wrapper

  // 1. Locate Protocols
  Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (EFI_ERROR(Status)) return;

  Status = gBS->LocateProtocol(&gEfiHiiFontProtocolGuid, NULL, (VOID **)&HiiFont);
  if (EFI_ERROR(Status)) return;

  ScreenWidth  = Gop->Mode->Info->HorizontalResolution;
  ScreenHeight = Gop->Mode->Info->VerticalResolution;

  // 2. Calculate Dimensions
  // If RequestedWidth is 0, we must measure the strings.
  // If provided, RequestedWidth is usually in "Columns" (chars).

  UINTN MaxStrLen = 0;

  VA_START(Args, NumberOfLines);
  VA_COPY(ArgsCopy, Args);
  for (Index = 0; Index < NumberOfLines; Index++) {
    String = VA_ARG(Args, CHAR16 *);
    if (String != NULL) {
      UINTN Len = StrLen(String);
      if (Len > MaxStrLen) MaxStrLen = Len;
    }
  }
  VA_END(Args);

  if (RequestedWidth == 0) {
    RequestedWidth = MaxStrLen;
  }

  ClearScreen ();

  // Enforce a minimum width if needed, or cap to screen width logic
  if (RequestedWidth < MaxStrLen) RequestedWidth = MaxStrLen;

  // Calculate pixel dimensions based on 8x19 standard glyphs
  UnscaledContentW = RequestedWidth * GLYPH_WIDTH;
  UnscaledContentH = NumberOfLines * GLYPH_HEIGHT;

  // Final Box Size (Scaled Content + Scaled Padding)
  // We apply scale factor to everything to ensure crisp look
  ScaleFactor = GetDisplayScaleFactor (Gop);
  BoxW = (UnscaledContentW * ScaleFactor) + (PADDING_X * ScaleFactor * 2);
  BoxH = (UnscaledContentH * ScaleFactor) + (PADDING_Y * ScaleFactor * 2);

  // Center on screen
  BoxX = (ScreenWidth > BoxW) ? (ScreenWidth - BoxW) / 2 : 0;
  BoxY = (ScreenHeight > BoxH) ? (ScreenHeight - BoxH) / 2 : 0;

  // 3. Save Background
  BackBuffer = AllocateZeroPool(BoxW * BoxH * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
  if (BackBuffer != NULL) {
    Gop->Blt(Gop, BackBuffer, EfiBltVideoToBltBuffer, BoxX, BoxY, 0, 0, BoxW, BoxH, 0);
  }

  // 4. Draw Box & Border
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL BorderColor = POPUP_BORDER_COLOR;

  EFI_GRAPHICS_OUTPUT_BLT_PIXEL BgColor = (Kind == SuccessPopUp ? SuccessBgColor : ErrorBgColor);

  // Solid Fill
  Gop->Blt(Gop, &BgColor, EfiBltVideoFill, 0, 0, BoxX, BoxY, BoxW, BoxH, 0);

  // Borders
  Gop->Blt(Gop, &BorderColor, EfiBltVideoFill, 0, 0, BoxX, BoxY, BoxW, BORDER_WIDTH, 0); // Top
  Gop->Blt(Gop, &BorderColor, EfiBltVideoFill, 0, 0, BoxX, BoxY + BoxH - BORDER_WIDTH, BoxW, BORDER_WIDTH, 0); // Bottom
  Gop->Blt(Gop, &BorderColor, EfiBltVideoFill, 0, 0, BoxX, BoxY, BORDER_WIDTH, BoxH, 0); // Left
  Gop->Blt(Gop, &BorderColor, EfiBltVideoFill, 0, 0, BoxX + BoxW - BORDER_WIDTH, BoxY, BORDER_WIDTH, BoxH, 0); // Right

  // 5. Prepare for Text Rendering
  // We allocate enough space for one line at a time
  UINTN LinePixels1x = UnscaledContentW * GLYPH_HEIGHT;
  RenderBuffer1x = AllocateZeroPool(LinePixels1x * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
  RenderBufferScaled = AllocateZeroPool(LinePixels1x * ScaleFactor * ScaleFactor * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));

  BltBufferObj = AllocateZeroPool(sizeof(EFI_IMAGE_OUTPUT));
  BltBufferObj->Width = (UINT16)UnscaledContentW;
  BltBufferObj->Height = (UINT16)GLYPH_HEIGHT;
  BltBufferObj->Image.Bitmap = RenderBuffer1x;

  EFI_FONT_DISPLAY_INFO FontInfo;
  ZeroMem(&FontInfo, sizeof(EFI_FONT_DISPLAY_INFO));
  FontInfo.ForegroundColor = (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)POPUP_TEXT_COLOR;
  FontInfo.BackgroundColor = BgColor;

  // 6. Render Text Loop
  UINTN CurrentY = BoxY + (PADDING_Y * ScaleFactor);
  UINTN TextStartX = BoxX + (PADDING_X * ScaleFactor);

  for (Index = 0; Index < NumberOfLines; Index++) {
    String = VA_ARG(ArgsCopy, CHAR16 *);

    if (String != NULL) {
      // A. Clear 1x buffer with BG color (clean slate)
      SetMem32(RenderBuffer1x, LinePixels1x * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL), *(UINT32 *)&BgColor);

      // B. Calculate Centering Offset WITHIN the buffer
      //    We do the math in 1x pixels
      UINTN StrPxW = StrLen(String) * GLYPH_WIDTH;
      UINTN InternalOffsetX = 0;

      if (StrPxW < UnscaledContentW) {
        InternalOffsetX = (UnscaledContentW - StrPxW) / 2;
      }

      // C. Render Text into the buffer at the calculated offset
      HiiFont->StringToImage (
          HiiFont,
          EFI_HII_OUT_FLAG_CLIP,              // Standard flag
          String,
          &FontInfo,
          &BltBufferObj,
          InternalOffsetX,                    // X Offset inside the buffer
          0,                                  // Y Offset (Top of line)
          NULL, NULL, NULL
      );

      // D. Scale up
      UpscaleBuffer(RenderBuffer1x, UnscaledContentW, GLYPH_HEIGHT, ScaleFactor, RenderBufferScaled);

      // E. Blt to Screen at FIXED Left Margin
      //    We now blit the entire "strip" which fits perfectly inside the box padding
      Gop->Blt(Gop, RenderBufferScaled, EfiBltBufferToVideo,
               0, 0,
               TextStartX, CurrentY,          // Fixed X Position
               UnscaledContentW * ScaleFactor, GLYPH_HEIGHT * ScaleFactor,
               UnscaledContentW * ScaleFactor * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
    }

    CurrentY += (GLYPH_HEIGHT * ScaleFactor);
  }
  VA_END(ArgsCopy);

  if (RenderBuffer1x) FreePool(RenderBuffer1x);
  if (RenderBufferScaled) FreePool(RenderBufferScaled);
  if (BltBufferObj)   FreePool(BltBufferObj);
}
