# X4 Pro UC8179: shorten the visible B/W to gray interval

## User requirement and prior result

The user sees a B/W page, then a visibly delayed gray text-AA update. They
want a stable appearance selected by the AA toggle. A previous universal
ordered monochrome AA candidate was rejected in the emulator for its dotted
appearance (`_primary-record/sessions/2026-11-01/x4libris-universal-text-contrast-20261101.md`).
Do not present the same visual method as a new solution.

The current UC8179 driver exposes separate B/W and gray overlay activations.
`supportsGrayFrame()` is false. Its absolute Direct mode uses the OEM full-gray
packet/long waveform, resets the panel configuration and requires a B/W resync;
it is not a fast, drop-in single-activation page-turn mode.

## Isolated candidate

Capture both AA masks during the normal B/W text render, before the B/W panel
activation. After that activation, upload the already prepared masks and run
the short gray waveform. This retains actual light/dark-gray edge levels and
removes the two extra whole-page text renders from the visible interval. The
path also stages cached image-gray masks before the B/W activation, so image
tones are preserved. The same mask capture is used for
pre-rendered EPUB/FB2 pages, and for TXT/Markdown.

If mask allocation fails, or a page is already being skipped by navigation,
the candidate shows the B/W page without a delayed AA touch-up on UC8179.
It logs the allocation fallback. Other panel
controllers keep their original refresh path.

This is still TWO physical activations. CPU precomputation can shorten the
interval but cannot prove it invisible. A true one-activation gray page would
require a validated panel waveform and matching previous-state handling.

## Verification gate

Run host FreeInk tests and build the X4 Pro OTA. Inspect EPUB/FB2 text at both
toggle settings and multiple font sizes in an emulator. On physical UC8179,
measure the time from B/W completion to gray activation and compare text at
immediate, +1s and +10s, 20+ page turns, normal/fast turns and refresh interval.
Check ghosting, brightness, page turn latency, image pages, TXT/Markdown,
orientation, and sleep/wake. Reject the candidate if there is still a visible
second-stage change or new ghosting. Do not replace published v1.0 on build or
emulator success alone.

## Emulator evidence (2026-11-01)

UTF-8 PlatformIO build passed for `x4pro_gh_release`; FreeInk's dual/single
buffer Pro host tests passed. In an isolated X4 Pro/UC8179 QEMU run, an EPUB
opened and advanced from `ABCD` to `EFGH` with the captured real-gray AA path.
The serial trace shows the first B/W activation completed at 12,649 ms, the
two gray plane uploads by 12,759 ms, gray activation by 12,970 ms and cleanup
by 13,075 ms. The next page's fast gray-base transition completed at 17,526 ms,
the planes by 17,636 ms, gray activation by 17,846 ms and cleanup by 17,951 ms.
The interval from completed base to the start of the gray waveform was about
170 ms in QEMU. This does not establish invisibility or contrast stability on
physical glass. QEMU screenshots and serial log are in
`_primary-record/artifacts/2026-11-01/steady-render-v1/captured-gray-emulator/`.

Final source-only adjustment releases held pre-render masks on invalidation and
skips the gray activation for pages already superseded by navigation. A final
UTF-8 `x4pro_gh_release` rebuild passed (`build-captured-gray-aa-final2-utf8.log`),
RAM 110,160 / 327,680 (33.6%), app flash 5,498,133 / 6,553,600 (83.9%).
The validated ESP32-S3 / 16 MB OTA application image is
`_primary-record/artifacts/2026-11-01/steady-render-v1/X4Libris-X4Pro-v1-UC8179-captured-gray-AA-bounded-boot-OTA-TEST.bin`,
5,503,136 bytes, SHA-256
`D64F792C28B8075CF30816D5A84C28C851FA75CAB69C81B60FE0D90490FBD1F7`.
This final image has not been physically flashed; the QEMU screenshots predate
the last mask-cleanup/navigation adjustment, which does not change an ordinary
page's AA masks. Published v1.0/main is unchanged.
