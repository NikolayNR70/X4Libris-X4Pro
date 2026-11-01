# AlReader WebDAV sync candidate

This candidate uses AlReader XE Ink 1.33's **WebDAV** provider. It does not
contact AlReader.com and does not reuse KOReader's credentials or sync logic.

Configure an HTTPS WebDAV base URL, username and password in **Settings →
Network** on X4Libris. Use the same WebDAV location and credentials in AlReader.
The server must support HTTP Basic authentication over a valid HTTPS certificate.
The URL must refer to an existing writable root directory; do not append
`AlReaderX-01Sync` yourself. X4Libris creates that child directory on the first
upload. The password is stored with the project's device-bound obfuscation and
is not returned by the web settings API.

## Koofr internet service

Koofr can host the WebDAV folder on the internet without a PC running. Its
documented endpoint is `https://app.koofr.net/dav/Koofr` on port 443. Create a
Koofr account and generate an application-specific password in its web app;
Koofr does not accept the normal account password for WebDAV. In both Android
AlReader and X4Libris, use that endpoint, the Koofr account email as username,
and an application password. X4Libris connects directly over Wi-Fi and HTTPS.
No Koofr credential belongs in the repository or a support log. The firmware
trust store includes DigiCert Global Root G2, which currently anchors Koofr's
TLS certificate; certificate and hostname verification remain enabled.

In Android AlReader, use its normal book CRC for the sync filename. The APK
also offers a simplified filename option for non-AlReader.com providers; when
enabled, it uses the first 96 Java characters of the book filename (with most
punctuation replaced by `_`) plus `.sync`. This candidate currently uses the
CRC mode, so the simplified filename option must be off for matching records.
Books must be the same edition/content on both devices for the key and text
position to match.

For each book, the client reads or writes
`<base>/AlReaderX-01Sync/<AlReader book key>`. The file is UTF-8 text with
`!<sync device ID>!<normalized text position>`, matching the APK's position
record. A missing file means no shared position yet. After upload, X4Libris
reads the record back and checks the ID and position. On a conflict, the reader
asks whether to send its position or use the remote one.

The HTTP transport sends an explicit `Content-Length`, including zero for
an empty `MKCOL` request. A PUT first tries the existing collection and only
attempts `MKCOL` after a 409 conflict, then retries the PUT. AlReader.com's
separate service protocol remains frozen and is not contacted by this client.

Only this isolated development branch contains the candidate. The released
v1.0 firmware and KOReader sync implementation are unchanged. A build and
protocol inspection do not establish a successful round trip with the user's
WebDAV server and Android AlReader; that requires device testing before merge.
