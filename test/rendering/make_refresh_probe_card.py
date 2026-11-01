"""Create a clean FAT32 SD card with one long, single-chapter EPUB for QEMU.

Requires Python 3.12 and pyfatfs. Example:
    py -3.12 test/rendering/make_refresh_probe_card.py --out-dir C:/temp/x4-render
"""

import argparse
import io
import shutil
import struct
import zipfile
from pathlib import Path

from pyfatfs.PyFat import PyFat
from pyfatfs.PyFatFS import PyFatFS

parser = argparse.ArgumentParser()
parser.add_argument("--out-dir", type=Path, required=True)
args = parser.parse_args()
out = args.out_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
volume = out / "refresh-probe-fat.img"
card = out / "refresh-probe-card.img"
volume_bytes = 255 * 1024 * 1024  # 1 MiB MBR offset + volume = 256 MiB QEMU SD card
offset = 2048 * 512

paragraph = (
    "The quiet river held its color while the page changed. "
    "Letters should stay equally dark from one turn to the next, "
    "and the background should remain clear without a second visible correction. "
)
body = "\n".join(f"<p>{i:03d}. {paragraph * 3}</p>" for i in range(1, 221))
xhtml = (
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Refresh Probe</title></head>'
    f'<body><h1>Refresh Probe</h1>{body}</body></html>'
)
container = (
    '<?xml version="1.0"?>'
    '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">'
    '<rootfiles><rootfile full-path="OEBPS/content.opf" '
    'media-type="application/oebps-package+xml"/></rootfiles></container>'
)
opf = (
    '<?xml version="1.0" encoding="UTF-8"?>'
    '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id">'
    '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
    '<dc:title>Refresh Probe</dc:title><dc:creator>Test</dc:creator>'
    '<dc:language>en</dc:language><dc:identifier id="id">refresh-probe-20261101</dc:identifier>'
    '</metadata><manifest><item id="chapter" href="chapter.xhtml" '
    'media-type="application/xhtml+xml"/></manifest>'
    '<spine><itemref idref="chapter"/></spine></package>'
)
epub = io.BytesIO()
with zipfile.ZipFile(epub, "w") as z:
    z.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
    z.writestr("META-INF/container.xml", container)
    z.writestr("OEBPS/content.opf", opf)
    z.writestr("OEBPS/chapter.xhtml", xhtml)

fat = PyFat()
with volume.open("wb") as f:
    f.truncate(volume_bytes)
fat.mkfs(str(volume), fat_type=PyFat.FAT_TYPE_FAT32, size=volume_bytes, label="RENDERTEST")
fat.close()
fs = PyFatFS(str(volume))
try:
    fs.makedir("Books")
    with fs.openbin("Books/Refresh-Probe.epub", "w") as f:
        f.write(epub.getvalue())
finally:
    fs.close()

mbr = bytearray(512)
mbr[446 + 4] = 0x0C
struct.pack_into("<I", mbr, 446 + 8, 2048)
struct.pack_into("<I", mbr, 446 + 12, volume_bytes // 512)
mbr[510:512] = b"\x55\xaa"
with card.open("wb") as dest:
    dest.write(mbr)
    dest.write(b"\0" * (offset - 512))
    with volume.open("rb") as src:
        shutil.copyfileobj(src, dest, length=1024 * 1024)
print(f"EPUB={len(epub.getvalue())} bytes; card={card.stat().st_size} bytes")
