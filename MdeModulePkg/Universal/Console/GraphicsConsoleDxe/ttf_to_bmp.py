from PIL import Image, ImageFont, ImageDraw
import sys
import re
import matplotlib.pyplot as plt
import numpy as np

SAMPLE_FONT_FILE="LaffStd.c.bak"
OUT_FONT_FILE="LaffStd.c"
FONT_PATH = "oswald.ttf"
FONT_SIZE = 56
GLYPH_WIDTH = 8
GLYPH_HEIGHT = 19
TARGET_SIZE = (GLYPH_WIDTH, GLYPH_HEIGHT)
BIG_CANVAS_SIZE = (GLYPH_WIDTH*6, GLYPH_HEIGHT*4)


def extract_unicode_chars_from_c(file_path):
    with open(file_path, "r") as f:
        src = f.read()

    # Match: { 0xABCD, 0x.., { ... } }
    matches = re.findall(r'\{\s*0x([0-9A-Fa-f]{4})\s*,', src)
    return sorted(set(chr(int(m, 16)) for m in matches))

CHARS = extract_unicode_chars_from_c(SAMPLE_FONT_FILE)



def render_char_to_bitmap(char, font):

    # Render character to large canvas
    big_img = Image.new("L", BIG_CANVAS_SIZE, color=0)
    draw = ImageDraw.Draw(big_img)
    draw.text((0,0), char, font=font, fill=255)

    # Get actual bounds
    bbox = big_img.getbbox()
    #print(bbox)
    if not bbox:
        return [0x00] * GLYPH_HEIGHT
    bbox_scaled_pos = (round(bbox[0]/BIG_CANVAS_SIZE[0] * GLYPH_WIDTH), round(bbox[1] / BIG_CANVAS_SIZE[1] * GLYPH_HEIGHT))

    big_img_cropped = big_img.crop(bbox)

    glyph_w = min((big_img_cropped.width / big_img.width) * GLYPH_WIDTH, GLYPH_WIDTH)
    glyph_h = (big_img_cropped.height / big_img.height) * GLYPH_HEIGHT

    glyph_img = big_img_cropped.resize((round(glyph_w), round(glyph_h)), resample=Image.Resampling.NEAREST)

    # Create target canvas (8x19) and paste glyph centered
    final_img = Image.new("L", TARGET_SIZE, color=0)
    final_img.paste(glyph_img, bbox_scaled_pos)


    # Convert to 1-bit bitmap
    bitmap = []
    final_img_bmp = np.zeros((19,8))
    for y in range(GLYPH_HEIGHT):
        byte = 0
        for x in range(GLYPH_WIDTH):
            pixel = final_img.getpixel((x, y))
            final_img_bmp[y,x] = 1 if pixel > 128 else 0
            bit = 1 if pixel > 128 else 0
            byte = (byte << 1) | bit
        bitmap.append(byte)

    # fig, axes = plt.subplots(ncols=5, nrows=1, figsize=TARGET_SIZE)
    # plt.title(f"Character: '{char}'")
    # axes[0].imshow(big_img, cmap="gray")
    # axes[1].imshow(big_img_cropped, cmap="gray")
    # axes[2].imshow(glyph_img, cmap="gray")
    # axes[3].imshow(final_img, cmap="gray", interpolation="nearest")
    # axes[4].imshow(final_img_bmp, cmap="gray")
    # plt.axis("off")
    # plt.show()
    return bitmap


def format_as_c_glyph(char, bitmap):
    unicode_hex = f"0x{ord(char):04X}"
    attr = "0x00"
    bitmap_str = ", ".join(f"0x{b:02X}" for b in bitmap)
    return f"  {{ {unicode_hex}, {attr}, {{ {bitmap_str} }} }}"

def main():
    with open(OUT_FONT_FILE, "w") as file:
        try:
            font = ImageFont.truetype(FONT_PATH, FONT_SIZE, layout_engine=ImageFont.Layout.BASIC)
        except IOError:
            print(f"Error: Could not load font from '{FONT_PATH}'")
            sys.exit(1)
        file.write('#include "GraphicsConsole.h"\n')
        file.write("EFI_NARROW_GLYPH gUsStdNarrowGlyphData[] = {\n")
        for char in CHARS:
            bitmap = render_char_to_bitmap(char, font)
            file.write(format_as_c_glyph(char, bitmap) + ",\n")
        file.write("};\n")
        file.write('UINT32  mNarrowFontSize =  sizeof (gUsStdNarrowGlyphData);\n')

if __name__ == "__main__":
    main()
