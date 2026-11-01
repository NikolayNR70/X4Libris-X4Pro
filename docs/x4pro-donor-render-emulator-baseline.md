# X4 Pro rendering baseline: CrossPoint donor and X4Libris v1.0

Date: 2026-11-01. This is a read-only diagnostic checkpoint. The released v1.0
and the firmware source on this branch are unchanged.

## Objective

Match the perceived text rendering and smoothing quality of the user's
`crosspointX4pro_2_0_0rc-fb2.bin` while retaining the X4Libris interface and
features. The user explicitly confirmed preservation of both.

## Inputs and isolation

- User-supplied CrossPoint X4 Pro OTA app: 5,483,696 bytes, SHA-256
  `87BC492F9B624D696F8427E69D203FBEC6125DEA357D0AA527D6A82CF28E0552`.
- Published X4Libris v1.0 OTA app: 5,500,896 bytes, SHA-256
  `4FABE071E563D1CC0CC55150366BC863C58D72BBDD61A6BA116AC25F62129D6B`.
- Both were inserted into separate 16 MiB QEMU flash copies, in both OTA app
  partitions. QEMU used `x4pro,panel=uc8179`, the same read-only SD snapshot,
  and `snapshot=on` for flash and SD. No physical device was flashed.
- Local screenshots, serial logs and the harness are under
  `_primary-record/artifacts/2026-11-01/{crosspoint-donor-emulator,x4libris-v1-emulator-comparison}`.
  Flash/SD snapshots are local artifacts and must not be committed because they
  may include device settings.

## Observed

- Both apps booted, probed the emulated UC8179 (`LUT_VER=01`), opened the
  `ABCD`/`EFGH` sample EPUB and advanced to its second page.
- Both serial traces show a B/W activation, LSB and MSB gray uploads, a gray
  activation, and cleanup. The donor's second page also performed a 625 ms
  gray preconditioning waveform and a 1,500 ms full B/W waveform. X4Libris's
  second page used a 249 ms B/W waveform before the gray pass. These are
  emulator timings, not measured physical durations.
- The final screenshots use different default fonts, so glyph shape/density
  cannot yet be compared fairly. The emulator output does not establish
  ghosting or long-term fading on physical glass.
- Public CrossPoint 2.0.0rc reader source also has separate B/W and gray
  activations on UC8179; its combined one-waveform base is limited to a
  different panel path. The donor is a custom `-fb2` binary, and its exact
  source has not been identified.

## Longer single-chapter probe

The two-page EPUB above crossed a chapter boundary and made the donor build a
new section, resetting its full-refresh counter. That did not represent normal
turns. A clean 256 MiB FAT32 SD image with a 220-paragraph, single-chapter EPUB
was then used for both images. Each opened the book and advanced four pages.
QEMU panel trace (`-d trace:uc8179_refresh`) and final screenshots are saved
under `_primary-record/artifacts/2026-11-01/{donor-longbook,v1-longbook-read}`.

On ordinary turns after the initial page, **both** traces show the same
waveform classes: UC8179 `psr=0x3f`, partial 25-frame / 625 ms gray
preconditioning, followed by `psr=0x3f`, nonpartial 6-frame / 150 ms gray
activation. The first page and navigation to it differ in refresh count;
those differences do not explain progressive fading within continued reading.
The user reports the visual gap also with their own fonts, so changing the
default font is not the proposed fix.

The public CrossPoint UC8179 driver's page and gray waveforms match the
X4Libris driver's logic; the latter mostly substitutes equivalent EpdBus
transaction calls. Both default 2-bit glyph masks select raw levels `{1,2,3}`
for BW, `{1,2}` for gray MSB and `{2}` for gray LSB on UC8179. This is a source
comparison, not proof that full-page masks and panel RAM bytes are equal:
X4Libris has a different reader and buffer lifecycle.

## Exact donor BIN waveform check

The user's observation also holds with the same personally selected fonts.
To check whether the custom donor BIN carries a different UC8179 waveform than
its public source, the two verified OTA binaries were scanned for the distinctive
12-byte starts of the UC8179 gray tables. The 215-byte preconditioning table at
donor offset `0x3a510` is byte-identical to v1.0 at `0x3e984`. The 215-byte
short gray table at donor `0x3a611` is byte-identical to v1.0 at `0x3ea85`.
The separate 42-byte dark-gray table at donor `0x3a5e7` matches v1.0 at
`0x3ea5b`. This is a binary comparison of the actual supplied donor and
released v1.0, not an assumption about the donor's source.

Consequently, a different embedded gray LUT is not supported as the cause of
the contrast gap. These bytes do not show which table is selected on each page
or what pixel masks are sent. The remaining comparison must inspect the plane
contents and previous-frame state, with the same font and rendering settings.

## Next diagnostic gate

Capture actual base and gray plane contents and controller RAM baseline across
matched page turns, especially after several pages. Check physical settings
and panel response on the user's X4 Pro before another OTA candidate. QEMU's
optical waveform is an approximation and cannot prove ghosting or fading.
Do not infer a fix from matching waveform classes or copy only the driver.
