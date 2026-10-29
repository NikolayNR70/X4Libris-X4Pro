# FB2 sibling EPUB conversion (legacy, explicit publishing path)

This document describes the separate FB2-to-EPUB publishing/conversion action.
The family-3 reader does not call this conversion path when opening an FB2:
it reads the FB2 source directly and keeps its generated reader cache in
`/.crosspoint/tmp/fb2-reader-v3-*`. Do not use this publishing action as the FB2
open workflow.

Opening `Books/book.Fb2` produces `Books/book.epub`. Existing destinations,
including files uploaded during conversion, are never replaced or reused.
A collision leaves the source visible and reports an error.

Conversion uses a separate `/.crosspoint/fb2-publish-v1-*` staging directory.
The XML parser validates FB2 before packaging. Each packaged file is checked
against its ZIP entry CRC while copying. After closing the archive, the entire
ZIP (headers, contents, directory and footer) is read back and compared with
the emitted byte count and CRC. The published path is checked again.

Only after these checks, and another check that the source is unchanged, the
original is renamed to `Books/.book.Fb2`. Its contents are retained. If the
hidden name already exists, or hiding fails, both visible books remain.
To restore the original name, move the EPUB aside, then remove the leading dot
from the FB2 name. Enable Show Hidden Files or use a computer to find it.
A failed validation after publication leaves the suspect EPUB and visible FB2
for inspection; move the suspect EPUB aside before retrying conversion.

Publication and hiding notify the normal library freshness generation, so the
library rebuilds with the sibling EPUB and the current hidden-file setting.
The reader transfers any existing Recent Books row to the new EPUB path.
Legacy v1/v2 cache source mappings remain readable and staging uses a separate
namespace. Release v1.1 and device flash are unchanged.

Host tests run through CMake/CTest as `Fb2ConverterPublication`; the wrapper
runs both converter and semantic formatting suites on isolated SD adapters.
The cases cover UTF-8/Windows-1251, metadata, images, footnotes, malformed input,
write and rename failures, corrupt staging/published archives, destination races,
backup collisions, mixed-case extensions, changing sources and legacy caches.
