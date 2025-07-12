import re
import sys

with open("LaffStd.c", "r") as f:
    source_code = f.read()

glyph_pattern = re.compile(
    r'\{\s*0x([0-9A-Fa-f]{4})\s*,\s*0x([0-9A-Fa-f]{2})\s*,\s*\{((?:\s*0x[0-9A-Fa-f]{2},?\s*){19})\}\s*\}',
    re.MULTILINE
)

glyphs = []
for match in glyph_pattern.finditer(source_code):
    unicode_hex = match.group(1)
    attr_hex = match.group(2)
    bitmap_hexes = match.group(3).replace('\n', '').split(',')
    bitmap = [int(b.strip(), 16) for b in bitmap_hexes if b.strip()]
    glyphs.append({
        "UnicodeChar": f"U+{int(unicode_hex, 16):04X}",
        "Char": chr(int(unicode_hex, 16)) if 32 <= int(unicode_hex, 16) < 127 else '',
        "Attr": int(attr_hex, 16),
        "Bitmap": bitmap
    })

def render_ascii(bitmap):
    for byte in bitmap:
        row = ''.join('#' if (byte >> (7 - bit)) & 1 else ' ' for bit in range(8))
        print(row)
    print()  # Spacer between glyphs

# Render first 3 glyphs
if len(sys.argv)>1:
    b = int(sys.argv[1])
else:
    b = 3

if len(sys.argv)>2:
    a = int(sys.argv[2])
else:
    a = b-1
for g in glyphs[a:b]:
    print(f"{g['UnicodeChar']} '{g['Char']}' Attr: {g['Attr']}")
    render_ascii(g["Bitmap"])
