#!/usr/bin/env python3
"""Generate tray icons for claudeusage (green/yellow/red variants).

Creates .ico files with 16x16 and 32x32 images using only the struct module.
No Pillow or ImageMagick required.
"""

import struct
import os

# Colors (R, G, B)
COLORS = {
    "green":  ((34, 139, 34),   (255, 255, 255)),  # bg, fg
    "yellow": ((218, 165, 32),  (0, 0, 0)),
    "red":    ((200, 40, 40),   (255, 255, 255)),
}

# Simple 8x8 "C" glyph (1 = foreground, 0 = background)
GLYPH_8 = [
    0b00111110,
    0b01111111,
    0b01100000,
    0b01100000,
    0b01100000,
    0b01100000,
    0b01111111,
    0b00111110,
]

def scale_glyph(glyph, src_size, dst_size):
    """Scale a 1-bit glyph to a larger size using nearest-neighbor."""
    result = []
    for y in range(dst_size):
        row = 0
        sy = y * src_size // dst_size
        for x in range(dst_size):
            sx = x * src_size // dst_size
            if glyph[sy] & (1 << (src_size - 1 - sx)):
                row |= (1 << (dst_size - 1 - x))
        result.append(row)
    return result

def make_bmp_data(size, bg_color, fg_color):
    """Create raw BGRA pixel data for one icon image."""
    glyph = scale_glyph(GLYPH_8, 8, size)
    # Add 2px padding by clearing edges
    pixels = bytearray()
    for y in range(size):
        for x in range(size):
            # Flip vertically (BMP is bottom-up)
            fy = size - 1 - y
            is_fg = bool(glyph[fy] & (1 << (size - 1 - x)))
            r, g, b = fg_color if is_fg else bg_color
            pixels.extend([b, g, r, 255])  # BGRA
    return bytes(pixels)

def make_icon_entry(size, bg_color, fg_color):
    """Create a single icon directory entry + image data."""
    pixels = make_bmp_data(size, bg_color, fg_color)

    # BITMAPINFOHEADER (40 bytes)
    bih = struct.pack('<IiiHHIIiiII',
        40,           # biSize
        size,         # biWidth
        size * 2,     # biHeight (doubled for XOR+AND mask)
        1,            # biPlanes
        32,           # biBitCount (BGRA)
        0,            # biCompression (BI_RGB)
        len(pixels),  # biSizeImage
        0, 0,         # biXPelsPerMeter, biYPelsPerMeter
        0, 0,         # biClrUsed, biClrImportant
    )

    # AND mask (1bpp, all zeros = fully opaque since we use 32-bit BGRA with alpha)
    and_mask_row_bytes = ((size + 31) // 32) * 4
    and_mask = b'\x00' * (and_mask_row_bytes * size)

    return bih + pixels + and_mask

def make_ico(entries_data, sizes):
    """Assemble a complete .ico file from multiple image entries."""
    num = len(entries_data)
    # ICO header: 6 bytes
    header = struct.pack('<HHH', 0, 1, num)

    # Directory entries: 16 bytes each
    offset = 6 + num * 16
    directory = b''
    for i, data in enumerate(entries_data):
        s = sizes[i] if sizes[i] < 256 else 0
        entry = struct.pack('<BBBBHHII',
            s,            # bWidth (0 = 256)
            s,            # bHeight
            0,            # bColorCount
            0,            # bReserved
            1,            # wPlanes
            32,           # wBitCount
            len(data),    # dwBytesInRes
            offset,       # dwImageOffset
        )
        directory += entry
        offset += len(data)

    return header + directory + b''.join(entries_data)

def main():
    os.makedirs('res', exist_ok=True)

    for name, (bg, fg) in COLORS.items():
        e16 = make_icon_entry(16, bg, fg)
        e32 = make_icon_entry(32, bg, fg)
        ico_data = make_ico([e16, e32], [16, 32])

        path = f'res/app_{name}.ico'
        with open(path, 'wb') as f:
            f.write(ico_data)
        print(f'Created {path} ({len(ico_data)} bytes)')

    # Default icon is green
    e16 = make_icon_entry(16, COLORS["green"][0], COLORS["green"][1])
    e32 = make_icon_entry(32, COLORS["green"][0], COLORS["green"][1])
    ico_data = make_ico([e16, e32], [16, 32])
    with open('res/app.ico', 'wb') as f:
        f.write(ico_data)
    print(f'Created res/app.ico ({len(ico_data)} bytes)')

if __name__ == '__main__':
    main()
