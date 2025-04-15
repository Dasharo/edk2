/** @file
  Driver for blocking capsule updates if no AC adapter is connected.

Copyright (c) 2025, 3mdeb Sp. z o.o. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent


**/

#include <Uefi.h>

#include <Protocol/BootLogo2.h>
#include <Protocol/GraphicsOutput.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/LaptopBatteryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/SafeIntLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

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

// Note: this is horizontal baseline (aka ascender), not line height.
#define FONT_SIZE_PERCENT  3
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
CapsuleChargerCheckEntry (
  IN EFI_HANDLE                        ImageHandle,
  IN EFI_SYSTEM_TABLE                  *SystemTable
  )
{
  EFI_STATUS                            Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL          *GraphicsOutput;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *ModeInfo;
  RETURN_STATUS                         RetStatus;
  BOOLEAN                               AcConnected;
  EFI_TPL                               OriginalTPL;

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

  //
  // Initialize font renderer.
  //
  Status = InitSsfn (GraphicsOutput->Mode->FrameBufferBase, ModeInfo);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Couldn't init font renderer\n"));
    return EFI_SUCCESS;
  }

  //
  // Ensure nothing else is accessing the EC while we are.
  //
  OriginalTPL = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  RetStatus = LaptopGetAcState(&AcConnected);
  gBS->RestoreTPL (OriginalTPL);

  //
  // Exit early if AC adapter check succeeds or is unsupported.
  //
  if (RetStatus == RETURN_UNSUPPORTED) {
    return EFI_SUCCESS;
  }

  if (AcConnected) {
    return EFI_SUCCESS;
  }

  //
  // Clear the background. We don't know what was drawn there, if anything.
  //
  ZeroMem ((VOID *)GraphicsOutput->Mode->FrameBufferBase,
           GraphicsOutput->Mode->FrameBufferSize);

  //
  // Print the error before resetting the platform.
  //
  Status = RenderTextCenteredAt("AC adapter is unplugged. Please plug in the AC and retry firmware update.",
                                ModeInfo->HorizontalResolution / 2,
                                ModeInfo->VerticalResolution / 2);

  Status = RenderTextCenteredAt("Cancelling update and rebooting in 10 seconds.",
                                ModeInfo->HorizontalResolution / 2,
                                ModeInfo->VerticalResolution * 3 / 4);

  gBS->Stall(10 * 1000 * 1000);

  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  return EFI_SUCCESS;
}
