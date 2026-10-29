#!/usr/bin/env python3
"""Generate the X4 Pro UI's fixed-size Lucide masks for FreeInk's Icon format.

Requires CairoSVG and Pillow. Source SVGs and their license are kept alongside
this script so the monochrome firmware masks can be regenerated reproducibly.
"""

from io import BytesIO
from pathlib import Path

import cairosvg
from PIL import Image


ROOT = Path(__file__).resolve().parent
OUTPUT = ROOT.parent / "PerfectInkIcons.h"
SIZES = (24, 30, 32, 54)  # Native X4 Pro UI sizes; avoid nearest-neighbor distortion in primary slots.
THRESHOLD = 110
ICONS = (
    ("open_book", "book-open"),
    ("library", "library-big"),
    ("folder", "folder"),
    ("wifi", "wifi"),
    ("usb", "usb"),
    ("globe", "globe"),
    ("settings", "settings"),
    ("monitor", "monitor"),
    ("hand", "hand"),
    ("grid", "grid-2x2"),
    ("ellipsis", "ellipsis"),
    ("text", "file-text"),
    ("image", "image"),
    ("file", "file"),
    ("recent", "clock"),
    ("transfer", "arrow-down-up"),
    ("weather", "cloud-sun"),
    ("stats", "chart-no-axes-combined"),
    ("calendar", "calendar-days"),
    ("today", "calendar-check-2"),
    ("case", "case-sensitive"),
    ("spacing", "arrow-up-down"),
    ("alignment", "align-start-horizontal"),
)


def rasterize(name: str, size: int) -> tuple[list[int], int]:
    svg = (ROOT / f"{name}.svg").read_text(encoding="utf-8").replace("currentColor", "#000000")
    png = cairosvg.svg2png(bytestring=svg.encode("utf-8"), output_width=size, output_height=size)
    image = Image.open(BytesIO(png)).convert("RGBA")
    white = Image.new("RGBA", image.size, (255, 255, 255, 255))
    white.alpha_composite(image)
    gray = white.convert("L")
    bits: list[int] = []
    sum_y = 0
    count = 0
    for y in range(size):
        for byte_x in range(0, size, 8):
            byte = 0
            for bit in range(8):
                x = byte_x + bit
                transparent = x >= size or gray.getpixel((x, y)) >= THRESHOLD
                byte |= int(transparent) << (7 - bit)
                if not transparent:
                    count += 1
                    sum_y += y
            bits.append(byte)
    return bits, round(sum_y / count) if count else size // 2


def rotate_ccw(bits: list[int], size: int) -> list[int]:
    """Rotate an MSB-first Mask1 raster 90 degrees counter-clockwise."""
    stride = (size + 7) // 8
    rotated: list[int] = []
    for y in range(size):
        for byte_x in range(0, size, 8):
            byte = 0
            for bit in range(8):
                x = byte_x + bit
                transparent = True
                if x < size:
                    # Inverse mapping for a 90-degree counter-clockwise rotation.
                    src_x, src_y = size - 1 - y, x
                    src_byte = bits[src_y * stride + src_x // 8]
                    transparent = bool(src_byte & (1 << (7 - (src_x & 7))))
                byte |= int(transparent) << (7 - bit)
            rotated.append(byte)
    return rotated


def generate() -> None:
    rows = [
        "#pragma once",
        "",
        '#include "Icon.h"',
        "",
        "// Generated from the adjacent Lucide SVGs; see LICENSE.txt.",
        f"// {','.join(map(str, SIZES))}px 1-bpp masks, MSB-first, bit 1 = transparent; threshold {THRESHOLD}.",
        "",
    ]
    for alias, source in ICONS:
        rows.append(f"// {alias}: {source}.svg")
        for size in SIZES:
            bits, center = rasterize(source, size)
            rows.append(f"static const uint8_t perfectink_{alias}_{size}_bits[] = {{")
            for start in range(0, len(bits), 16):
                rows.append("    " + ", ".join(f"0x{byte:02X}" for byte in bits[start : start + 16]) + ",")
            rows.append("};")
            rows.append(
                f"static const freeink::Icon perfectink_{alias}_{size} = "
                f"{{{size}, {size}, {center}, perfectink_{alias}_{size}_bits}};"
            )
        rows.append("")
    rotated_alignment = rotate_ccw(rasterize("align-start-horizontal", 24)[0], 24)
    rows.append("// alignment_rotated_ccw: align-start-horizontal.svg rotated 90 degrees left for reader layout rows.")
    rows.append("static const uint8_t perfectink_alignment_24_rotated_ccw_bits[] = {")
    for start in range(0, len(rotated_alignment), 16):
        rows.append("    " + ", ".join(f"0x{byte:02X}" for byte in rotated_alignment[start : start + 16]) + ",")
    rows.append("};")
    rows.append("")
    OUTPUT.write_text("\n".join(rows), encoding="utf-8")
    print(f"Generated {OUTPUT}: {len(ICONS)} icons at {','.join(map(str, SIZES))}px")


if __name__ == "__main__":
    generate()


