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

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/SafeIntLib.h>
#include <Library/UefiBootServicesTableLib.h>

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
#define HEIGHT_PERCENT    60

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
  The Entry Point for CapsuleSplash driver.

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.
  @param[in] SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS       The entry point is executed successfully.
  @retval other             Some error occurs when executing this entry point.

**/
EFI_STATUS
EFIAPI
CapsuleSplashEntry (
  IN EFI_HANDLE                        ImageHandle,
  IN EFI_SYSTEM_TABLE                  *SystemTable
  )
{
  EFI_STATUS                            Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL          *GraphicsOutput;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *ModeInfo;

  //
  // Find GOP and ensure that pixel size is 32b.
  //
  Status = gBS->HandleProtocol (gST->ConsoleOutHandle,
                                &gEfiGraphicsOutputProtocolGuid,
                                (VOID **)&GraphicsOutput);
  if (EFI_ERROR (Status)) {
    return EFI_UNSUPPORTED;
  }

  ModeInfo = GraphicsOutput->Mode->Info;
  if (ModeInfo->PixelFormat != PixelRedGreenBlueReserved8BitPerColor &&
      ModeInfo->PixelFormat != PixelBlueGreenRedReserved8BitPerColor) {
    return EFI_UNSUPPORTED;
  }

  Status = SetDummyLogo (ModeInfo);

  return Status;
}
