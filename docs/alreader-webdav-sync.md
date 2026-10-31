# AlReader WebDAV sync candidate

This candidate uses AlReader XE Ink 1.33's **WebDAV** provider. It does not
contact AlReader.com and does not reuse KOReader's credentials or sync logic.

Configure an HTTPS WebDAV base URL, username and password in **Settings →
Network** on X4Libris. Use the same WebDAV location and credentials in AlReader.
The URL must refer to an existing writable root directory; do not append
`AlReaderX-01Sync` yourself. X4Libris creates that child directory on the first
upload. The password is stored with the project's device-bound obfuscation and
is not returned by the web settings API.

For each book, the client reads or writes
`<base>/AlReaderX-01Sync/<AlReader book key>`. The file is UTF-8 text with
`!<sync device ID>!<normalized text position>`, matching the APK's position
record. A missing file means no shared position yet. After upload, X4Libris
reads the record back and checks the ID and position. On a conflict, the reader
asks whether to send its position or use the remote one.

Only this isolated development branch contains the candidate. The released
v1.0 firmware and KOReader sync implementation are unchanged. A build and
protocol inspection do not establish a successful round trip with the user's
WebDAV server and Android AlReader; that requires device testing before merge.
