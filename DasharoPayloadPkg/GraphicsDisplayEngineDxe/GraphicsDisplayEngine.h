/** @file
  Dasharo graphical display engine — shared internal definitions.

  Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef GRAPHICS_DISPLAY_ENGINE_H_
#define GRAPHICS_DISPLAY_ENGINE_H_

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HiiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include <Protocol/DisplayProtocol.h>
#include <Protocol/FormBrowserEx2.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextOut.h>

#include <Uefi/UefiInternalFormRepresentation.h>

//
// Layout constants. Sizes that aren't fixed pixels are percentages of the
// active GOP mode and are recomputed on every FormDisplay call so that
// resolution changes take effect immediately.
//
#define GFX_FONT_SIZE_PERCENT      2   // body font (% of vertical res)
#define GFX_HEADER_PERCENT         7   // brand band (logo + product line)
#define GFX_TABBAR_PERCENT         5   // numbered horizontal tab strip
#define GFX_PAGEHEADER_LINES       4   // breadcrumb + section + title + spacer
#define GFX_FOOTER_PERCENT         5   // hot-key band
// Base values, scaled at runtime by mState.Scale (resolution-dependent).
// All are referenced as `GFX_PADDING_PX` etc. at use sites; the macros
// below resolve to a runtime expression that incorporates mState.Scale,
// so simple uses like `2 * GFX_PADDING_PX` continue to compose correctly.
#define GFX_PADDING_PX_BASE             8u
#define GFX_HIGHLIGHT_INSET_PX_BASE     3u   // gap above/below row highlight
#define GFX_SCROLLBAR_RESERVE_PX_BASE   8u   // right strip kept clear of row
                                             // fills so the scroll indicator
                                             // stays visible across redraws

#define GFX_PADDING_PX            (GFX_PADDING_PX_BASE          * mState.Scale)
#define GFX_HIGHLIGHT_INSET_PX    (GFX_HIGHLIGHT_INSET_PX_BASE  * mState.Scale)
#define GFX_SCROLLBAR_RESERVE_PX  (GFX_SCROLLBAR_RESERVE_PX_BASE * mState.Scale)

//
// Panel colors (used by Blt fills). Stored BGR + reserved to match
// EFI_GRAPHICS_OUTPUT_BLT_PIXEL — Blt is pixel-format agnostic.
//
#define GFX_COLOR(R, G, B)  ((EFI_GRAPHICS_OUTPUT_BLT_PIXEL){ (B), (G), (R), 0 })
#define GFX_BG              GFX_COLOR (0x05, 0x09, 0x14)  // near-black, slight blue
#define GFX_PANEL           GFX_COLOR (0x0C, 0x14, 0x22)  // header/footer bands
#define GFX_PANEL_ALT       GFX_COLOR (0x16, 0x1F, 0x30)  // separator rules, popup title
#define GFX_HIGHLIGHT       GFX_COLOR (0x10, 0x60, 0x55)  // selected row bg (teal-tinted)
#define GFX_HIGHLIGHT_DIM   GFX_COLOR (0x33, 0x3D, 0x52)  // selected-but-not-actionable row
                                                          // — bright enough to be obviously
                                                          // a highlight even on rows with no text
#define GFX_ACCENT          GFX_COLOR (0x14, 0xB8, 0xA6)  // primary accent (active indicators)
#define GFX_TEXT            GFX_COLOR (0xFF, 0xFF, 0xFF)  // (legacy; text uses TC_*)
#define GFX_TEXT_DIM        GFX_COLOR (0x55, 0x60, 0x70)  // outlines, separators

//
// Text colors as RGB triplets — fed straight into GfxSetTextColor().
//
#define TC_WHITE    0xFF, 0xFF, 0xFF  // primary
#define TC_DIM      0x80, 0x88, 0x95  // descriptions, breadcrumbs
#define TC_DIMMER   0x55, 0x60, 0x70  // hardly-there labels
#define TC_TEAL     0x14, 0xB8, 0xA6  // active accents
#define TC_AMBER    0xF5, 0x9E, 0x0B  // warnings
#define TC_RED      0xEF, 0x44, 0x4F  // errors

typedef struct {
  EFI_GRAPHICS_OUTPUT_PROTOCOL       *Gop;          // NULL => no graphics at all
  BOOLEAN                            SsfnReady;     // direct framebuffer text usable
  UINT32                             ScreenW;
  UINT32                             ScreenH;
  UINTN                              FontPx;
  UINTN                              Scale;         // 1, 2, 3 — driven by ScreenH

  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL    **SerialOutputs;
  UINTN                              SerialCount;

  // The form we treat as "Main" — recorded the first time TabsCaptureFromForm
  // commits to a tab source, used to special-case rendering (system info
  // cards instead of plain REF list) and tab navigation (L/R wrapping).
  EFI_GUID                           MainFormSetGuid;
  UINT16                             MainFormId;
  BOOLEAN                            MainFormIdentified;
} GFX_DISPLAY_ENGINE_STATE;

extern GFX_DISPLAY_ENGINE_STATE  mState;

//
// Cached top-level navigation tabs. Captured from the REF statements on the
// first form that contains REFs (typically the front page). Tab destinations
// are referenced by FormSet GUID + Form ID, which stay valid across
// FormDisplay calls (statement pointers do not).
//
#define MAX_TABS  16

typedef struct {
  EFI_GUID         FormSetGuid;
  UINT16           FormId;
  EFI_HII_HANDLE   HiiHandle;       // kept for diagnostic / future use
  EFI_STRING_ID    Prompt;          // (likewise — string is cached below)
  CHAR16           Name[64];        // resolved at capture time (StringIds
                                    // can be invalidated when UiApp
                                    // rebuilds the front page)
} TAB_ENTRY;

//
// Output.c — protocol discovery, font init, drawing primitives, serial mirror.
//
EFI_STATUS  GfxOutputInit    (VOID);
VOID        GfxFillRect      (IN UINTN X, IN UINTN Y, IN UINTN W, IN UINTN H,
                              IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL Color);
VOID        GfxClearScreen   (IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL Color);
VOID        GfxDrawText      (IN UINTN X, IN UINTN Y, IN CONST CHAR8  *Utf8);
VOID        GfxDrawTextW     (IN UINTN X, IN UINTN Y, IN CONST CHAR16 *Wide);
// Sets the foreground color used by all subsequent text draws until next call.
VOID        GfxSetTextColor  (IN UINT8 R, IN UINT8 G, IN UINT8 B);
// Truncates with an ellipsis if the string would render wider than MaxWidthPx.
VOID        GfxDrawTextWClipped (IN UINTN X, IN UINTN Y, IN UINTN MaxWidthPx,
                                  IN CONST CHAR16 *Wide);
// Renders Wide so its right edge sits at RightX (text grows leftward).
VOID        GfxDrawTextWRightAligned (IN UINTN RightX, IN UINTN Y,
                                       IN CONST CHAR16 *Wide);
// Like GfxDrawTextWRightAligned but truncates with an ellipsis when the
// rendered width would exceed MaxWidthPx.
VOID        GfxDrawTextWRightAlignedClipped (IN UINTN RightX, IN UINTN Y,
                                              IN UINTN MaxWidthPx,
                                              IN CONST CHAR16 *Wide);
// Renders Wide centered horizontally on CenterX.
VOID        GfxDrawTextWCentered (IN UINTN CenterX, IN UINTN Y,
                                   IN CONST CHAR16 *Wide);
// Measures the rendered pixel width of Wide using the active font.
UINTN       GfxMeasureTextW       (IN CONST CHAR16 *Wide);
// Draws an outlined rectangle (1 px border, no fill).
VOID        GfxDrawRectOutline    (IN UINTN X, IN UINTN Y, IN UINTN W, IN UINTN H,
                                    IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL Color);
VOID        SerialOut        (IN CONST CHAR16 *Text);
VOID        SerialOutLine    (IN CONST CHAR16 *Text);

//
// SystemInfo.c — SMBIOS-derived hardware summary + RTC clock formatting.
//
typedef struct {
  CHAR16     SystemVendor[64];
  CHAR16     SystemProduct[64];
  CHAR16     BiosVersion[64];
  CHAR16     CpuBrand[80];
  UINT64     TotalRamMB;
  BOOLEAN    Collected;
} SYSTEM_INFO;

extern SYSTEM_INFO  mSysInfo;

VOID  SystemInfoCollect       (VOID);
VOID  SystemInfoFormatTime    (OUT CHAR16 *Buf, IN UINTN BufSize);
VOID  SystemInfoFormatSummary (OUT CHAR16 *Buf, IN UINTN BufSize);

//
// Tabs.c — top-level navigation tab cache.
//
VOID        TabsCaptureFromForm    (IN FORM_DISPLAY_ENGINE_FORM *Form);
UINTN       TabsCount              (VOID);
TAB_ENTRY  *TabsGet                (IN UINTN Index);
INTN        TabsFindActive         (IN FORM_DISPLAY_ENGINE_FORM *Form);
// Renders the horizontal tab bar across the strip [TopY .. TopY+BarH).
VOID        TabsRenderHorizontal   (IN UINTN TopY, IN UINTN BarH,
                                     IN INTN ActiveIndex);

#endif // GRAPHICS_DISPLAY_ENGINE_H_
