#!/usr/bin/env python3
"""Create X4Libris icon bitmaps sampled from the approved interface sketch."""

from pathlib import Path
import math
from PIL import Image, ImageDraw, ImageOps

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/x4libris-ui-reference.png"
OUT_H = ROOT / "src/components/icons/sketchIcons.h"
OUT_PREVIEW = ROOT / "outputs/sketch-icons-preview.png"
LOGO = ROOT / "src/images/LogoX4Libris.png"
NAMES = ["corner_book", "open_book", "folder", "book_stack", "filled_book_stack", "logo_book",
         "grid", "wifi_usb", "gear", "small_gear", "monitor", "hand_pointer", "globe", "ellipsis"]

# Tight source rectangles around the matching symbols in the supplied 971x1619
# reference sketch. Home shortcuts deliberately use their filled book artwork.
CROPS = {
    "corner_book": (897, 20, 944, 66),
    "open_book": (579, 380, 660, 451),
    "folder": (55, 692, 92, 728),
    "book_stack": (579, 636, 665, 714),
    "filled_book_stack": (264, 687, 311, 731),
    "grid": (56, 752, 91, 790),
    "wifi_usb": (261, 748, 331, 791),
    "gear": (590, 132, 652, 198),
    "small_gear": (56, 812, 93, 857),
    "monitor": (798, 132, 872, 196),
    "hand_pointer": (808, 379, 864, 459),
    "globe": (798, 642, 869, 711),
    "ellipsis": (269, 816, 312, 842),
}
SIZES = (32, 34, 42, 54)
THRESHOLD = 155


def render(name, size, source, logo_source=None):
    if name == "logo_book":
        if logo_source is None:
            raise ValueError("The X4Libris logo is required for the logo-book icon")
        crop = logo_source.crop((0, 0, 132, 161)).convert("L")
    elif name == "wifi_usb":
        # Redraw this combined shortcut as two complete symbols at native size:
        # three clearly separated Wi-Fi waves and a full USB trident.
        scale = size / 42.0
        hi = 4
        canvas = Image.new("1", (size * hi, size * hi), 1)
        d = ImageDraw.Draw(canvas)
        def xy(v): return int(round(v * scale * hi))
        stroke = max(1, xy(1.7))
        wifi_stroke = max(1, xy(2.0))
        for radius in (11, 7.5, 4):
            points = []
            for angle in range(215, 326, 5):
                rad = angle * 3.141592653589793 / 180.0
                points.append((xy(12 + radius * math.cos(rad)), xy(20 + radius * math.sin(rad))))
            d.line(points, fill=0, width=wifi_stroke, joint="curve")
        d.ellipse((xy(10), xy(23), xy(14), xy(27)), fill=0)
        # Keep the USB trident within the Wi-Fi mark's optical bounds (about
        # 18x18 at the 42 px master size). Its old 18x33 silhouette was
        # noticeably taller than the neighboring Wi-Fi symbol.
        d.line((xy(31), xy(25), xy(31), xy(13)), fill=0, width=stroke)
        d.polygon([(xy(31), xy(9)), (xy(26), xy(14)), (xy(36), xy(14))], fill=0)
        # Right branch with circular terminal; left branch with square terminal.
        d.line((xy(31), xy(17), xy(36), xy(13)), fill=0, width=stroke)
        d.ellipse((xy(36), xy(12), xy(40), xy(16)), outline=0, width=stroke)
        d.line((xy(31), xy(21), xy(26), xy(17)), fill=0, width=stroke)
        d.rectangle((xy(22), xy(15), xy(26), xy(19)), outline=0, width=stroke)
        # Rounded base terminal.
        d.line((xy(31), xy(25), xy(28), xy(23)), fill=0, width=stroke)
        d.ellipse((xy(26), xy(22), xy(30), xy(26)), outline=0, width=stroke)
        return canvas.resize((size, size), Image.Resampling.LANCZOS).convert("L").point(
            lambda p: 0 if p < 128 else 255, mode="1")
    else:
        crop = source.crop(CROPS[name]).convert("L")
    # Crop out antialiasing/background noise while keeping the sketch's exact
    # outline and proportions. Add a small optical margin before fitting.
    mask = crop.point(lambda p: 0 if p < THRESHOLD else 255, mode="L")
    bbox = mask.point(lambda p: 255 if p == 0 else 0).getbbox()
    if bbox is None:
        raise ValueError(f"No ink detected in {name}")
    mask = mask.crop(bbox)
    target = max(1, size - 4)
    fitted = ImageOps.contain(mask, (target, target), Image.Resampling.LANCZOS)
    fitted = fitted.point(lambda p: 0 if p < 128 else 255, mode="1")
    canvas = Image.new("1", (size, size), 1)
    canvas.paste(fitted, ((size - fitted.width) // 2, (size - fitted.height) // 2))
    return canvas


def icon_bytes(im):
    w, h = im.size
    row_bytes = (w + 7) // 8
    out = []
    pix = im.load()
    for y in range(h):
        for bx in range(row_bytes):
            value = 0xFF
            for bit in range(8):
                x = bx * 8 + bit
                if x < w and pix[x, y] == 0:
                    value &= ~(0x80 >> bit)
            out.append(value)
    return out


def emit_header(source, logo_source):
    lines = ["#pragma once", "", '#include "Icon.h"', "",
             "// Monochrome icon masks extracted from the approved X4Libris UI sketch.", ""]
    for name in NAMES:
        for size in SIZES:
            ident = f"icon_sketch_{name}_{size}"
            data = icon_bytes(render(name, size, source, logo_source))
            lines.append(f"static const uint8_t {ident}_bits[] = {{")
            for start in range(0, len(data), 16):
                lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[start:start+16]) + ",")
            lines.append("};")
            lines.append(f"static const freeink::Icon {ident} = {{{size}, {size}, {size // 2}, {ident}_bits}};")
            lines.append("")
    OUT_H.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def emit_preview(source, logo_source):
    cell_w, cell_h = 144, 178
    sheet = Image.new("RGB", (cell_w * 5, cell_h * 2), "white")
    draw = ImageDraw.Draw(sheet)
    for i, name in enumerate(NAMES):
        native_size = 54 if name in {"gear", "monitor", "open_book", "hand_pointer", "book_stack", "globe", "logo_book"} else 42 if name == "wifi_usb" else 34
        icon = render(name, native_size, source, logo_source).convert("RGB").resize((native_size * 2, native_size * 2), Image.Resampling.NEAREST)
        x, y = (i % 5) * cell_w + 24, (i // 5) * cell_h + 2
        sheet.paste(icon, (x, y))
        draw.text(((i % 5) * cell_w + 8, (i // 5) * cell_h + 108),
                  name.replace("_", " "), fill="black")
    sheet.save(OUT_PREVIEW)


if __name__ == "__main__":
    reference = Image.open(SOURCE).convert("RGB")
    if reference.size != (971, 1619):
        raise ValueError(f"Unexpected reference dimensions: {reference.size}")
    logo_source = Image.open(LOGO).convert("RGB")
    emit_header(reference, logo_source)
    emit_preview(reference, logo_source)
    print(OUT_H)
    print(OUT_PREVIEW)
