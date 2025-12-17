#  Color codes defined at MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsole.c
#  Foregrounds:
#  0x00 (EFI_BLACK)
#  0x01 (EFI_BLUE)
#  0x02 (EFI_GREEN)
#  0x03 (EFI_CYAN)
#  0x04 (EFI_RED)
#  0x05 (EFI_MAGENTA)
#  0x06 (EFI_BROWN)
#  0x07 (EFI_LIGHTGRAY)
#  0x08 (EFI_DARKGRAY)
#  0x09 (EFI_LIGHTBLUE)
#  0x0A (EFI_LIGHTGREEN)
#  0x0B (EFI_LIGHTCYAN)
#  0x0C (EFI_LIGHTRED)
#  0x0D (EFI_LIGHTMAGENTA)
#  0x0E (EFI_YELLOW)
#  0x0F (EFI_WHITE)
#
#  Backgrounds:
#  0x00 (EFI_BACKGROUND_BLACK)
#  0x10 (EFI_BACKGROUND_BLUE)
#  0x20 (EFI_BACKGROUND_GREEN)
#  0x30 (EFI_BACKGROUND_CYAN)
#  0x40 (EFI_BACKGROUND_RED)
#  0x50 (EFI_BACKGROUND_MAGENTA)
#  0x60 (EFI_BACKGROUND_BROWN)
#  0x70 (EFI_BACKGROUND_LIGHTGRAY)
#
# Because the background color in EDK2 does not support the bright bit, only
# colors 0-7 are available for backgrounds. To use colors like WHITE, some of
# the colors 0-7 need to be remapped to RGB value of white.

gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserFieldBackgroundHighlightColor|0x60 # Grey bg
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserFieldTextHighlightColor|0x0F       # White text
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserTitleBackgroundColor|0x60          # Grey bg
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserTitleTextColor|0x0F                # White text
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserPopupBackgroundColor|0x60          # Grey bg
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserPopupTextColor|0x0F                # White text
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserPopupInverseTextColor|0x06         # Grey text
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserPopupInverseBackgroundColor|0x60   # Grey bg
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserHelpTextColor|0x07                 # Light grey text
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserHelpBackgroundColor|0x30           # White bg
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserInfoTextColor|0x06                 # Grey text
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserInfoBackgroundColor|0x30           # White bg
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserFieldTextColor|0x06                # Grey text
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserFieldBackgroundColor|0x30          # White bg
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserSubtitleTextColor|0x02             # Green text
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserBannerBackgroundColor|0x60         # Grey bg
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserBannerTextColor|0x0F               # White text
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserKeyHelpTextColor|0x02              # Green text
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserKeyHelpBackgroundColor|0x60        # Grey bg
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserPickListTextColor|0x06             # Grey text
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserPickListBackgroundColor|0x30       # White bg
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserArrowTextColor|0x0F                # White text
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserArrowBackgroundColor|0x60          # Grey bg
gEfiMdeModulePkgTokenSpaceGuid.PcdBrowserFieldGrayedTestColor|0x07          # Light grey text

gEfiMdeModulePkgTokenSpaceGuid.PcdGraphicsConsoleColorPaletteBlack      | { 0x00, 0x00, 0x00, 0x00 }    # 0 Plain black
gEfiMdeModulePkgTokenSpaceGuid.PcdGraphicsConsoleColorPaletteBlue  | { 0x00, 0x00, 0x00, 0x98 }    # 1 Dark green
gEfiMdeModulePkgTokenSpaceGuid.PcdGraphicsConsoleColorPaletteGreen | { 0x00, 0x38, 0xD4, 0x30 }    # 2 Dasharo green
gEfiMdeModulePkgTokenSpaceGuid.PcdGraphicsConsoleColorPaletteCyan  | { 0x00, 0xFF, 0xFF, 0xFF }    # 3 cyan remapped to White for backgrounds
gEfiMdeModulePkgTokenSpaceGuid.PcdGraphicsConsoleColorPaletteRed   | { 0x00, 0x98, 0x00, 0x00 }    # 4 Red
gEfiMdeModulePkgTokenSpaceGuid.PcdGraphicsConsoleColorPaletteLightMagenta    | { 0x00, 0x98, 0x00, 0x98 }    # 5 Magenta
gEfiMdeModulePkgTokenSpaceGuid.PcdGraphicsConsoleColorPaletteBrown      | { 0x00, 0x29, 0x36, 0x3b }    # 6 Dasharo grey
gEfiMdeModulePkgTokenSpaceGuid.PcdGraphicsConsoleColorPaletteLightGray  | { 0x00, 0x80, 0x80, 0x80 }    # 7 Light grey

gEfiMdeModulePkgTokenSpaceGuid.PcdGraphicsConsoleColorPaletteDarkGray   | { 0x00, 0x30, 0x30, 0x30 }    # 8 Dark grey
gEfiMdeModulePkgTokenSpaceGuid.PcdGraphicsConsoleColorPaletteLightBlue      | { 0x00, 0x00, 0x00, 0xFF }    # 9 Blue
gEfiMdeModulePkgTokenSpaceGuid.PcdGraphicsConsoleColorPaletteLightGreen       | { 0x00, 0x00, 0xFF, 0x00 }    # A Green
gEfiMdeModulePkgTokenSpaceGuid.PcdGraphicsConsoleColorPaletteLightCyan       | { 0x00, 0xFF, 0xFF, 0x00 }    # B Cyan
gEfiMdeModulePkgTokenSpaceGuid.PcdGraphicsConsoleColorPaletteLightRed        | { 0x00, 0xFF, 0x00, 0x00 }    # C Red
gEfiMdeModulePkgTokenSpaceGuid.PcdGraphicsConsoleColorPaletteMagenta    | { 0x00, 0xFF, 0x00, 0xFF }    # D Fuchsia
gEfiMdeModulePkgTokenSpaceGuid.PcdGraphicsConsoleColorPaletteYellow     | { 0x00, 0xFF, 0xFF, 0x00 }    # E Yellow
gEfiMdeModulePkgTokenSpaceGuid.PcdGraphicsConsoleColorPaletteWhite      | { 0x00, 0xFF, 0xFF, 0xFF }    # F White
