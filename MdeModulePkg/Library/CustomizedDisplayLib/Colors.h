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
#define PICKLIST_HIGHLIGHT_TEXT        PcdGet8 (PcdBrowserPickListTextColor)
#define PICKLIST_HIGHLIGHT_BACKGROUND  PcdGet8 (PcdBrowserPickListBackgroundColor)
#define TITLE_TEXT                     PcdGet8 (PcdBrowserTitleTextColor)
#define TITLE_BACKGROUND               PcdGet8 (PcdBrowserTitleBackgroundColor)
#define KEYHELP_TEXT                   PcdGet8 (PcdBrowserKeyHelpTextColor)
#define KEYHELP_BACKGROUND             PcdGet8 (PcdBrowserKeyHelpBackgroundColor)
#define SUBTITLE_TEXT                  PcdGet8 (PcdBrowserSubtitleTextColor)
#define BANNER_TEXT                    PcdGet8 (PcdBrowserBannerTextColor)
#define BANNER_BACKGROUND              PcdGet8 (PcdBrowserBannerBackgroundColor)
#define FIELD_TEXT                     PcdGet8 (PcdBrowserFieldTextColor)
#define FIELD_TEXT_GRAYED              PcdGet8 (PcdBrowserFieldGrayedTextColor)
#define FIELD_BACKGROUND               PcdGet8 (PcdBrowserFieldBackgroundColor)
#define FIELD_TEXT_HIGHLIGHT           PcdGet8 (PcdBrowserFieldTextHighlightColor)
#define FIELD_BACKGROUND_HIGHLIGHT     PcdGet8 (PcdBrowserFieldBackgroundHighlightColor)
#define POPUP_TEXT                     PcdGet8 (PcdBrowserPopupTextColor)
#define POPUP_BACKGROUND               PcdGet8 (PcdBrowserPopupBackgroundColor)
#define POPUP_INVERSE_TEXT             PcdGet8 (PcdBrowserPopupInverseTextColor)
#define POPUP_INVERSE_BACKGROUND       PcdGet8 (PcdBrowserPopupInverseBackgroundColor)
#define HELP_TEXT                      PcdGet8 (PcdBrowserHelpTextColor)
#define HELP_BACKGROUND                PcdGet8 (PcdBrowserHelpBackgroundColor)
#define ERROR_TEXT                     PcdGet8 (PcdBrowserErrorTextColor)
#define INFO_TEXT                      PcdGet8 (PcdBrowserInfoTextColor)
#define INFO_BACKGROUND                PcdGet8 (PcdBrowserInfoBackgroundColor)
#define ARROW_TEXT                     PcdGet8 (PcdBrowserArrowTextColor)
#define ARROW_BACKGROUND               PcdGet8 (PcdBrowserArrowBackgroundColor)

#endif
