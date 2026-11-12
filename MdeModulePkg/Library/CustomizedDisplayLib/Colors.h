/** @file
MACRO definitions for color used in Setup Browser.

Copyright (c) 2004 - 2011, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/
//
// Unicode collation protocol in

#ifndef _COLORS_H_
#define _COLORS_H_

//
// Screen Color Settings
//
// The PcdBrowser*Color PCDs pick palette indices that only look right with the
// custom palette (e.g. CYAN/BROWN remapped to white/grey). When the custom
// palette is off, fall back to the stock EDK2 attributes.
//
#define CUSTOM_PALETTE_COLOR(Pcd, Stock)  ((UINT8)(PcdGetBool (PcdConsoleUseCustomPalette) ? PcdGet8 (Pcd) : (Stock)))

#define PICKLIST_HIGHLIGHT_TEXT        CUSTOM_PALETTE_COLOR (PcdBrowserPickListTextColor,             EFI_WHITE)
#define PICKLIST_HIGHLIGHT_BACKGROUND  CUSTOM_PALETTE_COLOR (PcdBrowserPickListBackgroundColor,       EFI_BACKGROUND_CYAN)
#define TITLE_TEXT                     CUSTOM_PALETTE_COLOR (PcdBrowserTitleTextColor,                EFI_WHITE)
#define TITLE_BACKGROUND               CUSTOM_PALETTE_COLOR (PcdBrowserTitleBackgroundColor,          EFI_BACKGROUND_BLUE)
#define KEYHELP_TEXT                   CUSTOM_PALETTE_COLOR (PcdBrowserKeyHelpTextColor,              EFI_LIGHTGRAY)
#define KEYHELP_BACKGROUND             CUSTOM_PALETTE_COLOR (PcdBrowserKeyHelpBackgroundColor,        EFI_BACKGROUND_BLACK)
#define SUBTITLE_TEXT                  CUSTOM_PALETTE_COLOR (PcdBrowserSubtitleTextColor,             EFI_BLUE)
#define BANNER_TEXT                    CUSTOM_PALETTE_COLOR (PcdBrowserBannerTextColor,               EFI_BLUE)
#define BANNER_BACKGROUND              CUSTOM_PALETTE_COLOR (PcdBrowserBannerBackgroundColor,         EFI_BACKGROUND_LIGHTGRAY)
#define FIELD_TEXT                     CUSTOM_PALETTE_COLOR (PcdBrowserFieldTextColor,                EFI_BLACK)
#define FIELD_TEXT_GRAYED              CUSTOM_PALETTE_COLOR (PcdBrowserFieldGrayedTestColor,          EFI_DARKGRAY)
#define FIELD_BACKGROUND               CUSTOM_PALETTE_COLOR (PcdBrowserFieldBackgroundColor,          EFI_BACKGROUND_LIGHTGRAY)
#define FIELD_TEXT_HIGHLIGHT           CUSTOM_PALETTE_COLOR (PcdBrowserFieldTextHighlightColor,       EFI_LIGHTGRAY)
#define FIELD_BACKGROUND_HIGHLIGHT     CUSTOM_PALETTE_COLOR (PcdBrowserFieldBackgroundHighlightColor, EFI_BACKGROUND_BLACK)
#define POPUP_TEXT                     CUSTOM_PALETTE_COLOR (PcdBrowserPopupTextColor,                EFI_LIGHTGRAY)
#define POPUP_BACKGROUND               CUSTOM_PALETTE_COLOR (PcdBrowserPopupBackgroundColor,          EFI_BACKGROUND_BLUE)
#define POPUP_INVERSE_TEXT             CUSTOM_PALETTE_COLOR (PcdBrowserPopupInverseTextColor,         EFI_LIGHTGRAY)
#define POPUP_INVERSE_BACKGROUND       CUSTOM_PALETTE_COLOR (PcdBrowserPopupInverseBackgroundColor,   EFI_BACKGROUND_BLACK)
#define HELP_TEXT                      CUSTOM_PALETTE_COLOR (PcdBrowserHelpTextColor,                 EFI_BLUE)
#define HELP_BACKGROUND                CUSTOM_PALETTE_COLOR (PcdBrowserHelpBackgroundColor,           EFI_BACKGROUND_LIGHTGRAY)
#define ERROR_TEXT                     CUSTOM_PALETTE_COLOR (PcdBrowserErrorTextColor,                EFI_RED | EFI_BRIGHT)
#define INFO_TEXT                      CUSTOM_PALETTE_COLOR (PcdBrowserInfoTextColor,                 EFI_YELLOW | EFI_BRIGHT)
#define INFO_BACKGROUND                CUSTOM_PALETTE_COLOR (PcdBrowserInfoBackgroundColor,           EFI_BACKGROUND_LIGHTGRAY)
#define ARROW_TEXT                     CUSTOM_PALETTE_COLOR (PcdBrowserArrowTextColor,                EFI_RED | EFI_BRIGHT)
#define ARROW_BACKGROUND               CUSTOM_PALETTE_COLOR (PcdBrowserArrowBackgroundColor,          EFI_BACKGROUND_LIGHTGRAY)

#endif
