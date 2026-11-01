# X4 Pro UC8179 sequential text AA experiment

## Physical trial update (2026-11-01)

The user reports that this second test image also rolled back. Fresh
`system.log` shows `Trial failed: UI not ready` at 31.455 seconds, followed by
the OTA slot switch. At 27.715 seconds the reader began a render after an
incremental FB2 section build. This is a strict-deadline boot-guard rejection,
not a logged panic. The old OTA file has been renamed with
`.obsolete-do-not-flash` in the primary record; do not recommend reflashing it.
The follow-up branch `fix/x4pro-boot-trial-bounded-ui` retries the health check
for at most another 30 seconds while the main loop remains responsive. It
still rolls back an unresponsive loop immediately and a perpetually unfinished
UI by 60 seconds. This changes trial timing, not the AA reader path.

The follow-up build passed for `x4pro_gh_release`: 5,496,821 / 6,553,600
flash bytes (83.9%) and 110,160 / 327,680 RAM bytes (33.6%). Its
5,501,824-byte OTA app is 272 bytes larger than the preceding test image and
928 bytes larger than released v1.0. SHA-256:
`1E6465E202E6F9E08E1AD771DD23153E23C4CF6F3DB4C5784D01BF8E4D3F9D59`.
Esptool validates the ESP32-S3 image header, checksum and validation hash.
The host trial-health test covers a rendering pass that finishes after 30
seconds, an unfinished UI at 60 seconds, a stalled main loop, and the existing
book-restore and wraparound cases. An ESP-IDF timer test in the locally
installed framework exercises restarting a one-shot timer from its callback.
The updated image has not been tested on the physical panel; no claim about
contrast or boot acceptance follows from the build.

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
