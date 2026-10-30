#!/usr/bin/env python3
"""Regenerate the monochrome X4Libris firmware assets from the approved lockup."""

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
IMAGE_DIR = ROOT / "src" / "images"
SOURCE = IMAGE_DIR / "X4Libris-approved.png"
THRESHOLD = 128


def pack_1bpp(image: Image.Image, symbol: str) -> str:
    gray = image.convert("L")
    width, height = gray.size
    pixels = gray.load()
    data = []
    for y in range(height):
        for x0 in range(0, width, 8):
            value = 0
            for bit in range(8):
                x = x0 + bit
                is_white = x >= width or pixels[x, y] >= THRESHOLD
                value |= int(is_white) << (7 - bit)
            data.append(value)

    lines = [f"// Image dimensions: {width}x{height}", f"static const uint8_t {symbol}[] = {{"]
    for start in range(0, len(data), 16):
        row = ", ".join(f"0x{byte:02X}" for byte in data[start : start + 16])
        lines.append(f"    {row},")
    lines[-1] = lines[-1].rstrip(",")
    lines.append("};")
    return "\n".join(lines) + "\n"


def write_header(filename: str, symbol: str) -> None:
    (IMAGE_DIR / filename).write_text(
        "#pragma once\n#include <cstdint>\n\n"
        f"// Defined once in {Path(filename).with_suffix('.cpp').name}; shared by boot and sleep screens.\n"
        f"extern const uint8_t {symbol}[];\n"
        + ("\nclass GfxRenderer;\nvoid drawX4LibrisLogo(GfxRenderer& renderer, int x, int y);\n"
           "inline constexpr int LogoX4LibrisWidth = 440;\n"
           "inline constexpr int LogoX4LibrisHeight = 161;\n"
           if symbol == "LogoX4Libris" else ""),
        encoding="ascii",
    )


def write_definition(filename: str, body: str) -> None:
    logo_helper = ""
    if Path(filename).stem == "LogoX4Libris":
        logo_helper = '''
#include <GfxRenderer.h>

void drawX4LibrisLogo(GfxRenderer& renderer, int x, int y) {
  constexpr int width = 440;
  constexpr int height = 161;
  constexpr int stride = (width + 7) / 8;
  for (int row = 0; row < height; ++row) {
    for (int col = 0; col < width; ++col) {
      const uint8_t packed = LogoX4Libris[row * stride + col / 8];
      if ((packed & (0x80 >> (col % 8))) == 0) renderer.drawPixel(x + col, y + row, true);
    }
  }
}
'''
    (IMAGE_DIR / filename).write_text(
        f'#include "{Path(filename).with_suffix(".h").name}"\n\n'
        f'{body.replace("static const uint8_t", "extern const uint8_t", 1)}'
        f'{logo_helper}',
        encoding="ascii",
    )


def main() -> None:
    source = Image.open(SOURCE).convert("L")
    width, height = source.size
    if (width, height) != (1093, 399):
        raise SystemExit(f"Unexpected approved logo dimensions: {width}x{height}")

    source = source.point(lambda value: 255 if value >= THRESHOLD else 0).convert("L")
    icon = Image.new("L", (height, height), 255)
    book = source.crop((0, 0, 340, height))
    icon.paste(book, ((height - book.width) // 2, 0))
    icon = icon.resize((120, 120), Image.Resampling.LANCZOS)
    icon = icon.point(lambda value: 255 if value >= THRESHOLD else 0)
    icon.save(IMAGE_DIR / "Logo120.png", optimize=True)
    write_header("Logo120.h", "Logo120")
    write_definition("Logo120.cpp", pack_1bpp(icon, "Logo120"))

    lockup_height = round(height * 440 / width)
    lockup = source.resize((440, lockup_height), Image.Resampling.LANCZOS)
    lockup = lockup.point(lambda value: 255 if value >= THRESHOLD else 0)
    lockup.save(IMAGE_DIR / "LogoX4Libris.png", optimize=True)
    write_header("LogoX4Libris.h", "LogoX4Libris")
    write_definition("LogoX4Libris.cpp", pack_1bpp(lockup, "LogoX4Libris"))

    (IMAGE_DIR / "Logo120.svg").write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" width="120" height="120" viewBox="0 0 399 399">\n'
        '  <image href="X4Libris-approved.png" x="0" y="0" width="1093" height="399"/>\n'
        '</svg>\n',
        encoding="ascii",
    )
    (IMAGE_DIR / "logo.svg").write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" width="1093" height="399" viewBox="0 0 1093 399">\n'
        '  <image href="X4Libris-approved.png" x="0" y="0" width="1093" height="399"/>\n'
        '</svg>\n',
        encoding="ascii",
    )


if __name__ == "__main__":
    main()
