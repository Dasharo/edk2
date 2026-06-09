/** @file
  Driver for displaying capsule update progress consistently.

  Position and size of progress bar produced by DisplayUpdateProgressLibGraphics
  depends on the size of boot logo. As the logo can be customized by users, a
  dummy logo with determinable dimensions, depending on the size of current GOP
  mode, is created to make progress bar appear always in the same place.

Copyright (c) 2024, 3mdeb Sp. z o.o. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent


**/

#include <Uefi.h>

#include <Protocol/BootLogo2.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/HiiDatabase.h>
#include <Protocol/HiiImageEx.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/SafeIntLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "Scaling.h"

/**
  scalable-font2 memory management assumes existence of realloc() as defined by
  C standard. The closest edk2 has to offer seems to be ReallocatePool(), but
  it requires the caller to pass the old size as an argument. Instead of adding
  bookkeeping code, just turn off dynamic memory allocation completely by
  defining SSFN_MAXLINES. This has some serious limitations, but should be
  enough to print few simple lines. It also makes context size significantly
  bigger, so move it to .bss to reduce stack usage.

  https://gitlab.com/bztsrc/scalable-font2/blob/master/docs/API.md#configuring-memory-management
**/

#define SSFN_MAXLINES 4096
#define SSFN_IMPLEMENTATION
#include <SSFN/ssfn.h>
#include <SSFN/VeraB.h>

ssfn_t     mCtx;
ssfn_buf_t mFBuf;

/**
  Dimensions used by DisplayUpdateProgressLibGraphics:

  +-----------------------------------------------------------------+
  |                                                                 |
  |                                                                 |
  |             +-------------------------------------+             |
  |             |                                     |             |
  |             |                                     |             |
  |             |                                     |             |
  |             |                LOGO                 |             |
  |             |                                     |             |
  |             |                                     |             |
  |             |                                     |             |
  |             |                                     |             |
  |             +-------------------------------------+             |
  |                                   ^                             |
  |                                   | padding - 20% * logo height |
  |                                   v                             |
  |             +-------------------------------------+             |
  |             |   Progress bar - 10% * logo height  |             |
  |             +-------------------------------------+             |
  |                                                                 |
  |                                                                 |
  +-----------------------------------------------------------------+

  Progress bar has the same width as logo, rounded up to represent 100 equally
  wide ticks. Note that all progress bar dimensions are relative to the logo,
  and not screen dimensions. In addition, DisplayUpdateProgressLibGraphics
  ignores all black and red (and everything in between) pixels of the logo when
  doing the calculations.

  This driver creates all-white logo with width equal to 75% of screen width,
  and height - 60% of screen height. The logo is aligned to top of screen and
  centered horizontally.

  This results in progress bar that:
  - has width of 75% of screen width (potentially rounded up by up to 99px)
  - has height of 6% (10% of 60% logo) of screen height
  - is centered horizontally
  - is located 22% (100% - 60% - (20% + 10%) * 60%) of screen height from the
    bottom.

**/

#define WIDTH_PERCENT     75
#define HEIGHT_PERCENT    (FixedPcdGetBool (PcdShowCapsuleLogo) ? 68 : 60)

#define TOP_MSG_POS_PERCENT     (FixedPcdGetBool (PcdShowCapsuleLogo) ? 6 : 37)
#define BOTTOM_MSG_POS_PERCENT  (FixedPcdGetBool (PcdShowCapsuleLogo) ? 94 : 88)

// Distance of the real logo from the top.
#define LOGO_TOP_MARGIN_PERCENT     12
// Distance of the real logo from the progress bar.
#define LOGO_BOTTOM_MARGIN_PERCENT  3

EFI_STATUS
SetDummyLogo (
  IN  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *ModeInfo
)
{
  EFI_STATUS                        Status;
  EDKII_BOOT_LOGO2_PROTOCOL         *BootLogo2;
  UINTN                             Height, Width, OffsetX;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL     *BltBuffer;
  UINTN                             BufferSize;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL     White = {0xFF, 0xFF, 0xFF, 0};

  //
  // Try to open BootLogo2 protocol. DisplayUpdateProgressLibGraphics doesn't
  // use the original BootLogo.
  //
  Status = gBS->LocateProtocol (&gEdkiiBootLogo2ProtocolGuid, NULL, (VOID **)&BootLogo2);
  if (EFI_ERROR (Status)) {
    return EFI_UNSUPPORTED;
  }

  //
  // Calculate dummy logo dimensions from GOP mode size.
  //
  Height = ModeInfo->VerticalResolution * HEIGHT_PERCENT / 100;
  Width = ModeInfo->HorizontalResolution * WIDTH_PERCENT / 100;
  OffsetX = ((ModeInfo->HorizontalResolution * (100 - WIDTH_PERCENT)) / 2) / 100;

  //
  // Ensure the Height * Width * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL) does
  // not overflow UINTN
  //
  Status = SafeUintnMult (
             Width,
             Height,
             &BufferSize
             );
  if (EFI_ERROR (Status)) {
    return EFI_UNSUPPORTED;
  }

  Status = SafeUintnMult (
             BufferSize,
             sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL),
             &BufferSize
             );
  if (EFI_ERROR (Status)) {
    return EFI_UNSUPPORTED;
  }

  //
  // Allocate buffer for dummy logo and make it white.
  //
  BltBuffer = AllocatePool (BufferSize);
  if (BltBuffer == NULL) {
    return EFI_UNSUPPORTED;
  }

  for (UINTN i = 0; i < BufferSize / sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL); i++) {
    BltBuffer[i] = White;
  }

  //
  // Set the new logo in BootLogo2 protocol. Note that this doesn't display it.
  //
  Status = BootLogo2->SetBootLogo (BootLogo2, BltBuffer, OffsetX, 0, Width, Height);

  FreePool (BltBuffer);

  return Status;
}

/**
  Fetches logo built into the DXE, then proportionally scales it to fit into
  appropriate rectangular area of the screen, centers and finally draws it.

  @param[in]     GraphicsOutput  GOP to use for drawing the logo.
  @param[in,out] HiiHandle       HII package of this DXE.

  @retval EFI_SUCCESS  The image was displayed on the screen.
  @retval Otherwise    Something went wrong.
**/
STATIC
EFI_STATUS
DrawRealLogo (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL  *GraphicsOutput,
  IN  EFI_HII_HANDLE                *HiiHandle
)
{
  EFI_STATUS                            Status;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *ModeInfo;
  UINTN                                 OffsetY;
  UINTN                                 OffsetX;
  EFI_HII_IMAGE_EX_PROTOCOL             *HiiImageEx;
  EFI_IMAGE_INPUT                       Image;
  EFI_IMAGE_INPUT                       Scaled;
  float                                 MaxWidth;
  float                                 MaxHeight;
  float                                 ScaleFactor;

  Status = gBS->LocateProtocol (&gEfiHiiImageExProtocolGuid, NULL, (VOID **)&HiiImageEx);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a(): failed to find HiiImageEx protocol: %r\n", __func__, Status));
    return Status;
  }

  Status = HiiImageEx->GetImageEx (HiiImageEx, HiiHandle, IMAGE_TOKEN (IMG_LOGO), &Image);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a(): failed to lookc up logo image: %r\n", __func__, Status));
    return Status;
  }

  ModeInfo = GraphicsOutput->Mode->Info;

  // Calculate dimensions of the area available for drawing.
  MaxWidth  = ModeInfo->HorizontalResolution * WIDTH_PERCENT / 100;
  MaxHeight = ModeInfo->VerticalResolution * (HEIGHT_PERCENT - LOGO_BOTTOM_MARGIN_PERCENT) / 100;

  // Find the smaller factor of the two dimensions.
  ScaleFactor = MIN (MaxWidth / Image.Width, MaxHeight / Image.Height);

  //
  // Make sure the scaling won't change proportions of the original image by
  // using the same (smaller) factor for both dimensions.
  //
  Scaled.Width  = Image.Width * ScaleFactor;
  Scaled.Height = Image.Height * ScaleFactor;

  // Scaled.Bitmap is set by ScaleImage() on success.
  Scaled.Flags = Image.Flags;

  Status = ScaleImage (&Image, &Scaled);
  if (EFI_ERROR (Status)) {
    Scaled.Bitmap = NULL;
  } else {
    Image = Scaled;
  }

  OffsetX = (ModeInfo->HorizontalResolution - Scaled.Width) / 2;
  OffsetY = ModeInfo->VerticalResolution * LOGO_TOP_MARGIN_PERCENT / 100 +
            (MaxHeight - Scaled.Height) / 2;

  Status = GraphicsOutput->Blt (GraphicsOutput, Image.Bitmap, EfiBltBufferToVideo,
                                0, 0,
                                OffsetX, OffsetY,
                                Image.Width, Image.Height,
                                Image.Width * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));

  if (Scaled.Bitmap != NULL) {
    FreePool (Scaled.Bitmap);
  }

  return Status;
}

// Note: this is horizontal baseline (aka ascender), not line height.
#define FONT_SIZE_PERCENT  5
#define FONT_SIZE(Mode) (                                                   \
    (Mode->VerticalResolution * FONT_SIZE_PERCENT / 100) < SSFN_SIZE_MAX ?  \
    (Mode->VerticalResolution * FONT_SIZE_PERCENT / 100) : SSFN_SIZE_MAX    \
  )

EFI_STATUS
InitSsfn (
  EFI_PHYSICAL_ADDRESS                  FrameBufferBase,
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info
)
{
  int Status;

  //
  // Context must be zeroed, done by having it in .bss.
  //

  //
  // Load the font.
  //
  Status = ssfn_load(&mCtx, VeraB);
  if (Status != SSFN_OK) {
    DEBUG ((DEBUG_ERROR, "ssfn_load failed with %a!\n", ssfn_error(Status)));
    return EFI_UNSUPPORTED;
  }

  //
  // Select the face.
  //
  Status = ssfn_select(&mCtx, SSFN_FAMILY_ANY, NULL, SSFN_STYLE_BOLD, FONT_SIZE(Info));
  if (Status != SSFN_OK) {
    DEBUG ((DEBUG_ERROR, "ssfn_select failed with %a!\n", ssfn_error(Status)));
    return EFI_UNSUPPORTED;
  }

  //
  // Initialize fields of frame buffer context that won't change.
  //
  mFBuf.ptr = (uint8_t *)FrameBufferBase;
  mFBuf.w   = Info->HorizontalResolution;
  mFBuf.h   = Info->VerticalResolution;
  mFBuf.p   = Info->PixelsPerScanLine * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL);
  mFBuf.fg  = 0xFFFFFFFF;   // This uses alpha channel for anti-aliasing
  mFBuf.bg  = 0;

  return EFI_SUCCESS;
}

EFI_STATUS
RenderTextCenteredAt (
  const char *str,
  UINTN x,
  UINTN y
)
{
  int w, h, left, top;
  int Status;

  //
  // Get the size of rendered text.
  //
  Status = ssfn_bbox(&mCtx, str, &w, &h, &left, &top);
  if (Status != SSFN_OK) {
    DEBUG ((DEBUG_ERROR, "ssfn_bbox failed with %a!\n", ssfn_error(Status)));
    return EFI_UNSUPPORTED;
  }

  //
  // Calculate and bound leftmost baseline coordinates of text.
  //
  w /= 2;
  h /= 2;
  mFBuf.x = x > w ? x - w : 0;
  mFBuf.y = y > h ? y - h : 0;

  //
  // Add baseline to get the final coordinates.
  //
  mFBuf.x += left;
  mFBuf.y += top;

  //
  // Render the text. The function takes care to not write outside of buffer.
  // It returns number of bytes consumed to render one glyph (UTF-8). Special
  // characters like \n are not handled.
  //
  while((Status = ssfn_render(&mCtx, &mFBuf, str)) > 0)
    str += Status;

  if (Status < 0) {
    DEBUG ((DEBUG_ERROR, "ssfn_render failed with %a!\n", ssfn_error(Status)));
    return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}

/**
  Publish resources of this DXE to HII database, so it's possible to query
  Logo.bmp bundled with the DXE using EFI_HII_IMAGE_EX_PROTOCOL.

  @param[in]  ImageHandle  Handle for the DXE.
  @param[out] HiiHandle    HII package handle.

  @retval EFI_SUCCESS  Obtained HII package handle successfully.
  @retval Otherwise    Something went wrong.
**/
STATIC
EFI_STATUS
LoadHiiPackage (
  IN  EFI_HANDLE      ImageHandle,
  OUT EFI_HII_HANDLE  *HiiHandle
  )
{
  EFI_STATUS                   Status;
  EFI_HII_PACKAGE_LIST_HEADER  *PackageList;
  EFI_HII_DATABASE_PROTOCOL    *HiiDatabase;

  Status = gBS->LocateProtocol (
                  &gEfiHiiDatabaseProtocolGuid,
                  NULL,
                  (VOID **)&HiiDatabase
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "HII Database protocol look up failure: %r\n", Status));
    return Status;
  }

  //
  // Retrieve HII package list from ImageHandle.
  //
  Status = gBS->OpenProtocol (
                  ImageHandle,
                  &gEfiHiiPackageListProtocolGuid,
                  (VOID **)&PackageList,
                  ImageHandle,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "HII Image Package with logo not found in PE/COFF resource section: %r\n", Status));
    return Status;
  }

  //
  // Publish HII package list to HII Database.
  //
  return HiiDatabase->NewPackageList (
                        HiiDatabase,
                        PackageList,
                        NULL,
                        HiiHandle
                        );
}

/**
  The Entry Point for CapsuleSplash driver.

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.
  @param[in] SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS       This is the only returned value, even in case of
                            errors. Reasoning is that this driver gives only
                            informational output, and failure to do so shouldn't
                            abort the update process.
**/
EFI_STATUS
EFIAPI
CapsuleSplashEntry (
  IN EFI_HANDLE                        ImageHandle,
  IN EFI_SYSTEM_TABLE                  *SystemTable
  )
{
  EFI_STATUS                            Status;
  EFI_HII_HANDLE                        HiiHandle;
  EFI_GRAPHICS_OUTPUT_PROTOCOL          *GraphicsOutput;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *ModeInfo;
  CONST CHAR8                           *Message;

  //
  // Find GOP and ensure that pixel size is 32b.
  //
  Status = gBS->HandleProtocol (gST->ConsoleOutHandle,
                                &gEfiGraphicsOutputProtocolGuid,
                                (VOID **)&GraphicsOutput);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Couldn't find GOP\n"));
    return EFI_SUCCESS;
  }

  ModeInfo = GraphicsOutput->Mode->Info;
  if (ModeInfo->PixelFormat != PixelRedGreenBlueReserved8BitPerColor &&
      ModeInfo->PixelFormat != PixelBlueGreenRedReserved8BitPerColor) {
    DEBUG ((DEBUG_ERROR, "Wrong pixel format\n"));
    return EFI_SUCCESS;
  }

  if (FixedPcdGetBool (PcdShowCapsuleLogo)) {
    Status = LoadHiiPackage (ImageHandle, &HiiHandle);
    if (EFI_ERROR (Status)) {
      // Absence of the logo is not a reason for stopping.
      DEBUG ((DEBUG_WARN, "Couldn't load HII package\n"));
      HiiHandle = NULL;
    }
  } else {
    // Disabling this functionality.
    HiiHandle = NULL;
  }

  //
  // Set dummy logo for consistent progress bar
  //
  Status = SetDummyLogo (ModeInfo);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Couldn't set dummy logo\n"));
    return EFI_SUCCESS;
  }

  //
  // Initialize font renderer.
  //
  Status = InitSsfn (GraphicsOutput->Mode->FrameBufferBase, ModeInfo);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Couldn't init font renderer\n"));
    return EFI_SUCCESS;
  }

  //
  // Clear the background. We don't know what logo was shown there, if any.
  //
  ZeroMem ((VOID *)GraphicsOutput->Mode->FrameBufferBase,
           GraphicsOutput->Mode->FrameBufferSize);

  if (HiiHandle != NULL) {
    Status = DrawRealLogo (GraphicsOutput, HiiHandle);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN, "Error while displaying real logo: %r\n", Status));
    }
  }

  if (FixedPcdGetBool (PcdEcFirmware))
    Message = "EC firmware update is in progress...";
  else
    Message = "Firmware update is in progress...";

  //
  // Print some warnings. Ignore the result, we still want to try printing even
  // if one of the earlier lines fails.
  //
  Status = RenderTextCenteredAt(Message,
                                ModeInfo->HorizontalResolution / 2,
                                ModeInfo->VerticalResolution * TOP_MSG_POS_PERCENT / 100);

  Status = RenderTextCenteredAt("Don't turn off your device!",
                                ModeInfo->HorizontalResolution / 2,
                                ModeInfo->VerticalResolution * BOTTOM_MSG_POS_PERCENT / 100);

  return EFI_SUCCESS;
}
