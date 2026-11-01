# X4 Pro UC8179 sequential text AA experiment

## Physical trial update (2026-11-01)

The user reports that this second test image also rolled back. The SD card
with the new boot logs is not currently mounted, so the failure mode of this
trial cannot yet be attributed to the boot guard, a crash, or another cause.
The OTA file has been renamed with `.obsolete-do-not-flash` in the primary
record. Do not recommend reflashing this image. Diagnose the new logs before
making another firmware candidate.

Base: published X4Libris v1.0 source `fdc38cf`. This branch has not changed
the v1.0 release, tag, or `main`.

## Evidence

The preceding async-only transition candidate `b35e395` barely changed the
visible reading result and rolled back in the physical trial. The new
`system.log` records `Trial failed: startup did not remain healthy for the
30-second check` at 31.456 seconds. It does not show a panic or crash at that
point. A proven fix for the overly strict boot-trial loop-stall rule has been
ported from the earlier isolated CrossPoint WIP commit `0aba59d`; its host
regression test covers a long book-restore iteration and real late stalls.

The supplied CrossPoint 2.0.0rc-fb2 BIN reads well on the user's X4 Pro. Its
exact modified source is unavailable. In the public CrossPoint 2.0.0rc source,
the UC8179 reading path completes the B/W page base before rendering and
uploading LSB and MSB text AA masks; then it applies the gray waveform and
restores the B/W baseline. X4Libris v1.0 instead overlaps LSB rendering with
an async B/W waveform. The two sequences differ even though their UC8179
driver and default glyph masks are substantially the same.

This branch selects the sequential sequence only when running on an X4 Pro
whose detected display controller is UC8179. It uses the existing blocking
`displayBuffer()` path and existing two-plane `renderGrayscalePlanesSequential()`
path. Text layout, fonts, EPUB/FB2 scaling, gray image caching, settings,
other detected controllers, and other boards retain their existing code paths.
The sequence may take longer per page because the AA work no longer overlaps
the B/W waveform. Any contrast improvement remains a physical test question.

## Checks and physical validation gate

The UTF-8 X4 Pro build passed: 5,496,549 / 6,553,600 flash bytes (83.9%),
110,160 / 327,680 RAM bytes (33.6%). The 5,501,552-byte OTA app image is
656 bytes larger than the released v1.0 app. Its SHA-256 is
`4055D5A53BA19CCECC1F58185D24FD49DA6B9826CA44EC1576D2FD1BC5DBEDED`.
Esptool confirms ESP32-S3, 16 MB flash, valid image checksum and validation
hash. The FreeInk Pro host panel suites passed in dual and single buffer modes;
the boot-trial recovery host test passed. No emulator or device test has been
run on this image yet. The emulator cannot establish optical panel contrast.

Before release, compare identical EPUB and FB2 pages on the physical device
against the supplied CrossPoint BIN: normal and rapid turns, text shapes,
ghosting, +1s/+10s density, images, and sleep/wake. Confirm the boot log says
the trial was accepted after 30 seconds. Do not replace v1.0 based on a
successful compile or emulator screenshot alone.
