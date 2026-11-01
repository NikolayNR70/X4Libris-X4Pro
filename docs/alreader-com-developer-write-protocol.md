# AlReader.com position upload — developer specification (2026-11-02)

The AlReader developer supplied the Java write path directly. This is an
implementation reference for a separate AlReader.com provider, **not** the
existing WebDAV provider. No live request or firmware change follows from
this note.

## Wire request

- HTTPS host `alreader.com`, path `/aldb/index2.php`.
- Method `POST`, empty body, explicit `Content-Length: 0`, bounded connect/read
  timeouts, no cached response. The present `SecureHttpClient::sendRequest`
  already frames empty POST requests this way.
- Query for writing: `o=put`, `u=<user identifier>`, `p=null`,
  `n=<UTF-8 URL-encoded book key>`, `a=AlReaderX`,
  `v=!<persistent device id>!<position in permille>`.
- HTTP 200 is required. The response body must also be inspected: if it
  contains `login incorrect`, call the registration request, wait one second,
  then retry the write once.
- Registration request: empty POST to the same endpoint with `o=setUser`,
  `u=<user identifier>`, `p=null`, `n=user`, `a=AlReaderX`.
  It too requires HTTP 200. It is a retry path, not an unconditional preflight.

The user identifier is the email or other unique string supplied by the user.
It is not a password. The application must not print the full URL or identifier
in diagnostics. The server request sends the identifier in the URL, so TLS
verification and log redaction matter.

## Book key and position

- Take AlReader's `bookInfo.fileName`: for an archive, the entry name **inside**
  the archive; for EPUB, the EPUB filename. Do not use the `.zip` container name.
- Remove the final extension by truncating at the final dot (if the dot is not
  the first character); inspect at most 96 Java UTF-16 code units of that stem.
  Retain dots and `AlUnicode.isLetterOrDigit` characters; replace every other
  character with `_`. Append `.percent`.
- `syncReadPos = floor(readPositionStart * 1000 / size)` with a 64-bit
  intermediate and a nonzero text size. Both operands refer to AlReader's
  parsed/normalized text, **not** page counts, file bytes or EPUB archive bytes.
- `syncID` is a persistent device-specific signed `long`: initialize once from
  Unix seconds if absent, then reuse the stored value. It is separate from the
  user identifier.

## Differences from the isolated WebDAV candidate

The current `AlReaderBookKey` produces a CRC/UUID key for the WebDAV file and
`AlReaderSyncActivity` writes an absolute UTF-16 text position. Neither value
can be reused unchanged in the AlReader.com request. The WebDAV and KOReader
paths should retain their independent formats and credentials. For the new
provider, X4Libris must compute total normalized text length across all spines
before converting its existing AlReader text position to permille. Missing
spine indexes cannot be replaced with a page percentage or source file size.

## Still needed from the developer before two-way sync

- The read request (`o=...`), exact response format, its missing-position
  response and the error strings/statuses.
- Whether a position sent with one `syncID` should be ignored when the same
  device subsequently reads, and how the server resolves newer/older writes.
- Confirmation of `bookInfo.size` boundaries for EPUB and FB2, including title,
  notes and non-reading sections, so our normalized text count uses the same
  denominator.
- Whether `u` needs Java-style URL encoding for identifiers containing spaces,
  `+`, `&` or non-ASCII characters. The supplied Java snippet encodes `n` but
  concatenates `u` and `v` directly.

Do not send test traffic to AlReader.com until the offline request bytes and
position arithmetic have been checked against this specification.
