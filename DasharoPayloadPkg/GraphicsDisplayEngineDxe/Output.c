/** @file
  Output sinks for the Dasharo graphical display engine.

  Owns:
   - GOP for graphical rendering (NULL if absent — text-only fallback path)
   - the SSFN scalable font context
   - serial-only EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL handles for the in-sync
     text mirror (selected by walking device paths for UART / terminal nodes
     so we bypass ConSplitter and don't double-render text on the GOP
     framebuffer).

  Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "GraphicsDisplayEngine.h"

#include <Library/DevicePathLib.h>

#include <Protocol/DevicePath.h>

//
// scalable-font2 expects realloc(); avoid it by disabling dynamic allocation.
// Same trick CapsuleSplashDxe uses — context lands in .bss so the large
// SSFN_MAXLINES storage doesn't blow the stack.
//
#define SSFN_MAXLINES 4096
#define SSFN_IMPLEMENTATION
#include <SSFN/ssfn.h>
#include <SSFN/Vera.h>  // Bitstream Vera Sans collection (regular + bold + italic)

GFX_DISPLAY_ENGINE_STATE  mState;
STATIC ssfn_t             mSsfnCtx;
STATIC ssfn_buf_t         mSsfnBuf;

STATIC
BOOLEAN
DevicePathHasSerialNode (
  IN EFI_DEVICE_PATH_PROTOCOL  *Path
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Node;

  for (Node = Path; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if (DevicePathType (Node) == MESSAGING_DEVICE_PATH) {
      // UART nodes are serial; vendor-messaging nodes are terminal wrappers
      // (ANSI/PCANSI/VT100/etc.) and also serial for our purposes.
      if (DevicePathSubType (Node) == MSG_UART_DP ||
          DevicePathSubType (Node) == MSG_VENDOR_DP)
      {
        return TRUE;
      }
    }
  }
  return FALSE;
}

STATIC
EFI_STATUS
CollectSerialOutputs (
  VOID
  )
{
  EFI_STATUS                       Status;
  EFI_HANDLE                       *Handles;
  UINTN                            HandleCount;
  UINTN                            Index;
  EFI_DEVICE_PATH_PROTOCOL         *Path;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *Out;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleTextOutProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mState.SerialOutputs = AllocateZeroPool (sizeof (*mState.SerialOutputs) * HandleCount);
  if (mState.SerialOutputs == NULL) {
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiDevicePathProtocolGuid, (VOID **)&Path);
    if (EFI_ERROR (Status)) {
      // ConSplitter's aggregate handle has no device path — skip it so we
      // don't fan a single write back through every console (including GOP).
      continue;
    }
    if (!DevicePathHasSerialNode (Path)) {
      continue;
    }
    Status = gBS->HandleProtocol (Handles[Index], &gEfiSimpleTextOutProtocolGuid, (VOID **)&Out);
    if (EFI_ERROR (Status)) {
      continue;
    }
    mState.SerialOutputs[mState.SerialCount++] = Out;
  }

  FreePool (Handles);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
LocateGop (
  VOID
  )
{
  EFI_STATUS  Status;

  Status = gBS->HandleProtocol (
                  gST->ConsoleOutHandle,
                  &gEfiGraphicsOutputProtocolGuid,
                  (VOID **)&mState.Gop
                  );
  if (EFI_ERROR (Status)) {
    Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&mState.Gop);
  }
  if (EFI_ERROR (Status)) {
    mState.Gop = NULL;
    DEBUG ((DEBUG_ERROR, "GraphicsDisplayEngine: no GOP found (%r)\n", Status));
    return Status;
  }

  DEBUG ((
    DEBUG_ERROR,
    "GraphicsDisplayEngine: GOP %ux%u, format=%d, fb=0x%lx, size=0x%lx\n",
    mState.Gop->Mode->Info->HorizontalResolution,
    mState.Gop->Mode->Info->VerticalResolution,
    (UINTN)mState.Gop->Mode->Info->PixelFormat,
    (UINT64)mState.Gop->Mode->FrameBufferBase,
    (UINT64)mState.Gop->Mode->FrameBufferSize
    ));

  return EFI_SUCCESS;
}

STATIC
BOOLEAN
GopFormatSupportsDirectFb (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  )
{
  // SSFN writes pixels directly to the framebuffer, so we need a known
  // 32 bpp packed RGB or BGR format and a non-zero linear framebuffer base.
  if (Gop->Mode->FrameBufferBase == 0) {
    return FALSE;
  }
  if ((Gop->Mode->Info->PixelFormat != PixelRedGreenBlueReserved8BitPerColor) &&
      (Gop->Mode->Info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor))
  {
    return FALSE;
  }
  return TRUE;
}

STATIC
UINTN
ComputeFontPx (
  IN UINT32  VerticalResolution
  )
{
  UINTN  Size = (UINTN)VerticalResolution * GFX_FONT_SIZE_PERCENT / 100;

  if (Size < 12) {
    Size = 12;
  }
  if (Size > SSFN_SIZE_MAX) {
    Size = SSFN_SIZE_MAX;
  }
  return Size;
}

STATIC
EFI_STATUS
InitFont (
  VOID
  )
{
  int  Status;

  Status = ssfn_load (&mSsfnCtx, Vera);
  if (Status != SSFN_OK) {
    DEBUG ((DEBUG_ERROR, "GraphicsDisplayEngine: ssfn_load failed: %a\n", ssfn_error (Status)));
    return EFI_UNSUPPORTED;
  }

  Status = ssfn_select (
             &mSsfnCtx,
             SSFN_FAMILY_SANS,
             NULL,
             SSFN_STYLE_REGULAR,
             ComputeFontPx (mState.Gop->Mode->Info->VerticalResolution)
             );
  if (Status != SSFN_OK) {
    DEBUG ((DEBUG_ERROR, "GraphicsDisplayEngine: ssfn_select failed: %a\n", ssfn_error (Status)));
    return EFI_UNSUPPORTED;
  }

  mSsfnBuf.ptr = (uint8_t *)(UINTN)mState.Gop->Mode->FrameBufferBase;
  // SSFN packs `fg` as 0xAARRGGBB and uses sign of width to flip R/B in the
  // output: positive w => writes BGR (low byte first), negative w => RGB.
  // Pick the sign from the framebuffer pixel format so the same AARRGGBB
  // packing in fg renders identically on both BGR8888 and RGB8888 GOPs.
  if (mState.Gop->Mode->Info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
    mSsfnBuf.w = -(int)mState.Gop->Mode->Info->HorizontalResolution;
  } else {
    mSsfnBuf.w =  (int)mState.Gop->Mode->Info->HorizontalResolution;
  }
  mSsfnBuf.h   = mState.Gop->Mode->Info->VerticalResolution;
  mSsfnBuf.p   = mState.Gop->Mode->Info->PixelsPerScanLine *
                 sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL);
  mSsfnBuf.fg  = 0xFFFFFFFF;  // alpha used for AA
  mSsfnBuf.bg  = 0;           // transparent — caller pre-fills the background

  return EFI_SUCCESS;
}

VOID
GfxSetTextColor (
  IN UINT8  R,
  IN UINT8  G,
  IN UINT8  B
  )
{
  // SSFN expects fg as 0xAARRGGBB. The pixel-format-dependent R/B swap is
  // already handled via the buffer width sign at init time.
  mSsfnBuf.fg = (UINT32)(0xFF000000U
                         | ((UINT32)R << 16)
                         | ((UINT32)G <<  8)
                         | ((UINT32)B <<  0));
}

EFI_STATUS
GfxOutputInit (
  VOID
  )
{
  EFI_STATUS  Status;

  // Re-collect serial outputs each call: handles can come and go between
  // calls (and they certainly didn't all exist when our driver entry ran).
  if (mState.SerialOutputs != NULL) {
    FreePool (mState.SerialOutputs);
    mState.SerialOutputs = NULL;
    mState.SerialCount   = 0;
  }
  CollectSerialOutputs ();
  DEBUG ((DEBUG_ERROR, "GraphicsDisplayEngine: %u serial outputs collected\n",
          (UINT32)mState.SerialCount));

  // Find GOP only if we don't have it yet — once acquired, keep it.
  // Refresh resolution-derived geometry every call though, so resolution
  // changes between forms get picked up.
  if (mState.Gop == NULL) {
    Status = LocateGop ();
    if (EFI_ERROR (Status)) {
      return EFI_SUCCESS;
    }
  }

  mState.ScreenW = mState.Gop->Mode->Info->HorizontalResolution;
  mState.ScreenH = mState.Gop->Mode->Info->VerticalResolution;
  mState.FontPx  = ComputeFontPx (mState.ScreenH);
  // Resolution-bucketed scale factor for fixed pixel constants. Two-step
  // is plenty in practice: 800×600 / 1080p stay at 1, 1440p+ goes to 2,
  // 4K+ to 3. FontPx is already linear-with-resolution so this only
  // affects paddings, insets, and the scrollbar reserve.
  if (mState.ScreenH > 1800) {
    mState.Scale = 3;
  } else if (mState.ScreenH > 1080) {
    mState.Scale = 2;
  } else {
    mState.Scale = 1;
  }

  // SSFN renders pixels straight to the linear framebuffer. If the GOP
  // doesn't expose one in a format we can poke directly, leave SsfnReady
  // FALSE — panels still render via Blt regardless.
  if (!mState.SsfnReady) {
    if (GopFormatSupportsDirectFb (mState.Gop)) {
      Status = InitFont ();
      if (!EFI_ERROR (Status)) {
        mState.SsfnReady = TRUE;
      }
    } else {
      DEBUG ((
        DEBUG_ERROR,
        "GraphicsDisplayEngine: GOP pixel format %d / fb=0x%lx unusable for direct text; panels only\n",
        (UINTN)mState.Gop->Mode->Info->PixelFormat,
        (UINT64)mState.Gop->Mode->FrameBufferBase
        ));
    }
  }

  return EFI_SUCCESS;
}

VOID
GfxFillRect (
  IN UINTN                          X,
  IN UINTN                          Y,
  IN UINTN                          W,
  IN UINTN                          H,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  )
{
  if (mState.Gop == NULL) {
    return;
  }
  if ((X >= mState.ScreenW) || (Y >= mState.ScreenH)) {
    return;
  }
  if (X + W > mState.ScreenW) {
    W = mState.ScreenW - X;
  }
  if (Y + H > mState.ScreenH) {
    H = mState.ScreenH - Y;
  }

  mState.Gop->Blt (mState.Gop, &Color, EfiBltVideoFill, 0, 0, X, Y, W, H, 0);
}

VOID
GfxClearScreen (
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  )
{
  if (mState.Gop == NULL) {
    return;
  }
  GfxFillRect (0, 0, mState.ScreenW, mState.ScreenH, Color);
}

VOID
GfxDrawText (
  IN UINTN        X,
  IN UINTN        Y,
  IN CONST CHAR8  *Utf8
  )
{
  int  Consumed;

  if (!mState.SsfnReady || (Utf8 == NULL)) {
    return;
  }

  // Caller passes the desired top-left of the text cell; SSFN expects the
  // glyph baseline so we offset by the font ascent (~= the configured size).
  mSsfnBuf.x = (int)X;
  mSsfnBuf.y = (int)(Y + mState.FontPx);

  while ((Consumed = ssfn_render (&mSsfnCtx, &mSsfnBuf, Utf8)) > 0) {
    Utf8 += Consumed;
  }
}

STATIC
VOID
TranscodeUtf16ToUtf8 (
  IN  CONST CHAR16  *Wide,
  IN  UINTN         WideLen,
  OUT CHAR8         *Buf,
  IN  UINTN         BufSize
  )
{
  UINTN  i;
  UINTN  j;

  for (i = 0, j = 0; (i < WideLen) && (j + 4 < BufSize); i++) {
    UINT32  Cp = Wide[i];
    if (Cp < 0x80) {
      Buf[j++] = (CHAR8)Cp;
    } else if (Cp < 0x800) {
      Buf[j++] = (CHAR8)(0xC0 | (Cp >> 6));
      Buf[j++] = (CHAR8)(0x80 | (Cp & 0x3F));
    } else {
      Buf[j++] = (CHAR8)(0xE0 | (Cp >> 12));
      Buf[j++] = (CHAR8)(0x80 | ((Cp >> 6) & 0x3F));
      Buf[j++] = (CHAR8)(0x80 | (Cp & 0x3F));
    }
  }
  Buf[j] = '\0';
}

VOID
GfxDrawTextW (
  IN UINTN         X,
  IN UINTN         Y,
  IN CONST CHAR16  *Wide
  )
{
  CHAR8  Buf[512];

  if (Wide == NULL) {
    return;
  }
  TranscodeUtf16ToUtf8 (Wide, StrLen (Wide), Buf, sizeof (Buf));
  GfxDrawText (X, Y, Buf);
}

VOID
GfxDrawTextWRightAligned (
  IN UINTN         RightX,
  IN UINTN         Y,
  IN CONST CHAR16  *Wide
  )
{
  CHAR8  Buf[512];
  int    W;

  if (Wide == NULL) {
    return;
  }
  TranscodeUtf16ToUtf8 (Wide, StrLen (Wide), Buf, sizeof (Buf));

  if (mState.SsfnReady) {
    int  H, Left, Top;
    if (ssfn_bbox (&mSsfnCtx, Buf, &W, &H, &Left, &Top) != SSFN_OK) {
      W = 0;
    }
  } else {
    W = (int)(StrLen (Wide) * (mState.FontPx ? mState.FontPx : 12) * 6 / 10);
  }

  if (RightX < (UINTN)W) {
    return;
  }
  GfxDrawText (RightX - W, Y, Buf);
}

UINTN
GfxMeasureTextW (
  IN CONST CHAR16  *Wide
  )
{
  CHAR8  Buf[512];
  int    W;

  if (Wide == NULL) {
    return 0;
  }
  TranscodeUtf16ToUtf8 (Wide, StrLen (Wide), Buf, sizeof (Buf));

  if (mState.SsfnReady) {
    int  H, Left, Top;
    if (ssfn_bbox (&mSsfnCtx, Buf, &W, &H, &Left, &Top) != SSFN_OK) {
      W = 0;
    }
  } else {
    W = (int)(StrLen (Wide) * (mState.FontPx ? mState.FontPx : 12) * 6 / 10);
  }
  return (UINTN)W;
}

VOID
GfxDrawRectOutline (
  IN UINTN                          X,
  IN UINTN                          Y,
  IN UINTN                          W,
  IN UINTN                          H,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Color
  )
{
  if ((W == 0) || (H == 0)) {
    return;
  }
  GfxFillRect (X,         Y,         W, 1, Color);
  GfxFillRect (X,         Y + H - 1, W, 1, Color);
  GfxFillRect (X,         Y,         1, H, Color);
  GfxFillRect (X + W - 1, Y,         1, H, Color);
}

VOID
GfxDrawTextWRightAlignedClipped (
  IN UINTN         RightX,
  IN UINTN         Y,
  IN UINTN         MaxWidthPx,
  IN CONST CHAR16  *Wide
  )
{
  UINTN  Len;
  UINTN  Lo, Hi, Mid;
  UINTN  W;

  if ((Wide == NULL) || (MaxWidthPx == 0)) {
    return;
  }

  // Fast path: it already fits.
  W = GfxMeasureTextW (Wide);
  if (W <= MaxWidthPx) {
    GfxDrawTextWRightAligned (RightX, Y, Wide);
    return;
  }

  // Binary search the longest prefix whose rendered form, plus an ellipsis,
  // fits within MaxWidthPx.
  Len = StrLen (Wide);
  Lo  = 0;
  Hi  = Len;
  while (Lo < Hi) {
    CHAR16  Trim[256];
    UINTN   Keep;

    Mid  = (Lo + Hi + 1) / 2;
    Keep = Mid;
    if (Keep + 4 >= sizeof (Trim) / sizeof (CHAR16)) {
      Keep = sizeof (Trim) / sizeof (CHAR16) - 4;
    }
    CopyMem (Trim, Wide, Keep * sizeof (CHAR16));
    Trim[Keep + 0] = L'.';
    Trim[Keep + 1] = L'.';
    Trim[Keep + 2] = L'.';
    Trim[Keep + 3] = L'\0';

    if (GfxMeasureTextW (Trim) <= MaxWidthPx) {
      Lo = Mid;
    } else {
      Hi = Mid - 1;
    }
  }

  {
    CHAR16  Trim[256];
    UINTN   Keep = Lo;
    if (Keep + 4 >= sizeof (Trim) / sizeof (CHAR16)) {
      Keep = sizeof (Trim) / sizeof (CHAR16) - 4;
    }
    CopyMem (Trim, Wide, Keep * sizeof (CHAR16));
    Trim[Keep + 0] = L'.';
    Trim[Keep + 1] = L'.';
    Trim[Keep + 2] = L'.';
    Trim[Keep + 3] = L'\0';
    GfxDrawTextWRightAligned (RightX, Y, Trim);
  }
}

VOID
GfxDrawTextWCentered (
  IN UINTN         CenterX,
  IN UINTN         Y,
  IN CONST CHAR16  *Wide
  )
{
  CHAR8  Buf[512];
  int    W;

  if (Wide == NULL) {
    return;
  }
  TranscodeUtf16ToUtf8 (Wide, StrLen (Wide), Buf, sizeof (Buf));

  if (mState.SsfnReady) {
    int  H, Left, Top;
    if (ssfn_bbox (&mSsfnCtx, Buf, &W, &H, &Left, &Top) != SSFN_OK) {
      W = 0;
    }
  } else {
    W = (int)(StrLen (Wide) * (mState.FontPx ? mState.FontPx : 12) * 6 / 10);
  }

  if (CenterX < (UINTN)(W / 2)) {
    return;
  }
  GfxDrawText (CenterX - W / 2, Y, Buf);
}

STATIC
int
MeasureWidth (
  IN CONST CHAR8  *Utf8
  )
{
  int  W, H, Left, Top;

  if (!mState.SsfnReady) {
    return 0;
  }
  if (ssfn_bbox (&mSsfnCtx, Utf8, &W, &H, &Left, &Top) != SSFN_OK) {
    return 0;
  }
  return W;
}

VOID
GfxDrawTextWClipped (
  IN UINTN         X,
  IN UINTN         Y,
  IN UINTN         MaxWidthPx,
  IN CONST CHAR16  *Wide
  )
{
  CHAR8  Buf[512];
  UINTN  Len;
  UINTN  Lo, Hi, Mid;

  if ((Wide == NULL) || (MaxWidthPx == 0)) {
    return;
  }

  Len = StrLen (Wide);
  TranscodeUtf16ToUtf8 (Wide, Len, Buf, sizeof (Buf));

  // SSFN unavailable: fall back to a rough char-count estimate so the call
  // still does something visible on Blt-only GOP modes.
  if (!mState.SsfnReady) {
    UINTN  Approx = MaxWidthPx / ((mState.FontPx ? mState.FontPx : 12) * 6 / 10);
    if (Len > Approx) {
      // Hand-truncate the wide string with an ellipsis.
      CHAR16  Trim[256];
      UINTN   Keep = (Approx > 3) ? (Approx - 3) : 0;
      if (Keep >= sizeof (Trim) / sizeof (CHAR16) - 4) {
        Keep = sizeof (Trim) / sizeof (CHAR16) - 4;
      }
      CopyMem (Trim, Wide, Keep * sizeof (CHAR16));
      Trim[Keep + 0] = L'.';
      Trim[Keep + 1] = L'.';
      Trim[Keep + 2] = L'.';
      Trim[Keep + 3] = L'\0';
      GfxDrawTextW (X, Y, Trim);
    } else {
      GfxDrawTextW (X, Y, Wide);
    }
    return;
  }

  if ((UINTN)MeasureWidth (Buf) <= MaxWidthPx) {
    GfxDrawText (X, Y, Buf);
    return;
  }

  // Binary search the longest UTF-16 prefix length whose UTF-8 form, plus
  // an ellipsis, fits within MaxWidthPx.
  Lo = 0;
  Hi = Len;
  while (Lo < Hi) {
    CHAR16  Trim[256];
    CHAR8   TrimUtf8[512];
    UINTN   Keep;

    Mid  = (Lo + Hi + 1) / 2;
    Keep = Mid;
    if (Keep + 4 >= sizeof (Trim) / sizeof (CHAR16)) {
      Keep = sizeof (Trim) / sizeof (CHAR16) - 4;
    }
    CopyMem (Trim, Wide, Keep * sizeof (CHAR16));
    Trim[Keep + 0] = L'.';
    Trim[Keep + 1] = L'.';
    Trim[Keep + 2] = L'.';
    Trim[Keep + 3] = L'\0';

    TranscodeUtf16ToUtf8 (Trim, Keep + 3, TrimUtf8, sizeof (TrimUtf8));
    if ((UINTN)MeasureWidth (TrimUtf8) <= MaxWidthPx) {
      Lo = Mid;
    } else {
      Hi = Mid - 1;
    }
  }

  // Render the chosen prefix + ellipsis.
  {
    CHAR16  Trim[256];
    UINTN   Keep = Lo;
    if (Keep + 4 >= sizeof (Trim) / sizeof (CHAR16)) {
      Keep = sizeof (Trim) / sizeof (CHAR16) - 4;
    }
    CopyMem (Trim, Wide, Keep * sizeof (CHAR16));
    Trim[Keep + 0] = L'.';
    Trim[Keep + 1] = L'.';
    Trim[Keep + 2] = L'.';
    Trim[Keep + 3] = L'\0';
    GfxDrawTextW (X, Y, Trim);
  }
}

VOID
SerialOut (
  IN CONST CHAR16  *Text
  )
{
  UINTN  i;

  if (Text == NULL) {
    return;
  }
  for (i = 0; i < mState.SerialCount; i++) {
    mState.SerialOutputs[i]->OutputString (mState.SerialOutputs[i], (CHAR16 *)Text);
  }
}

VOID
SerialOutLine (
  IN CONST CHAR16  *Text
  )
{
  SerialOut (Text);
  SerialOut (L"\r\n");
}
