# X4 Pro rendering candidate from released v1.0

Date: 2026-11-01. Branch `fix/x4pro-crosspoint-vector-render` starts at released
X4Libris v1.0 (`fdc38cf`). Published `main` and the v1.0 release stay unchanged.

## Upstream baseline update (2026-11-02)

CrossPoint released stable [2.0.0](https://github.com/crosspoint-reader/crosspoint-reader/releases/tag/2.0.0)
on 2026-11-01 (tag `42d58da`), superseding the 2.0.0rc source used for the
initial audit. The stable FreeInk submodule is `5e94fe7`. A source diff from
rc to stable shows no change to `Uc8179Driver.cpp`; the `Uc8179Driver.h`
change only adds the controller name. Stable `TtfEpdFont.cpp` still requests
FreeType auto-hinting plus stem darkening, so this candidate's vector-font
alignment remains relevant. Stable `GfxRenderer.cpp` also fixes per-glyph
advance rounding and line-height measurement for SD fonts, which can change
layout but does not by itself explain a delayed physical contrast shift.
The upstream release does not constitute a verified X4Libris quality fix.

## Evidence before the edit

- The user's custom CrossPoint X4 Pro `2.0.0rc-fb2` BIN and published X4Libris
  v1.0 BIN contain byte-identical UC8179 gray and preconditioning LUTs.
- A locally instrumented eink-qemu captured both panel RAM plane hashes and
  visible transition-class counts before each refresh. On a single-chapter
  EPUB, four ordinary page turns in each firmware used a partial 25-frame
  preconditioning refresh followed by a six-frame gray refresh. In both,
  the next page's OLD plane matched the preceding page's final B/W base on
  three consecutive transitions. No loss of that baseline was observed.
- Default-font screenshot comparisons were misleading: the donor defaults to
  Noto Sans, while X4Libris v1.0 defaults to Bookerly. A separate X4Libris
  run selected Noto Sans 14 pt using its settings JSON. The two still have
  different line layout, and their generated Noto Sans bitmap tables differ.
  The source TTF file is identical; X4Libris's generated table uses forced
  auto-hinting, while public CrossPoint's table does not. This test cannot
  reproduce the physical fading the user sees on their own fonts.
- The shared FreeInk `FtFont.cpp` implementation is byte-identical. The
  `TtfEpdFont::initFace` call site differs: CrossPoint 2.0.0rc selects full
  FreeType auto-hinting and stem darkening for runtime TTF/OTF faces; released
  X4Libris selects `HintingMode::Default`. CrossPoint enables the optional
  auto-hinter in `platformio.ini`; X4Libris did not. This is a concrete
  difference that applies to user-supplied vector fonts.
- The donor's unscaled 2-bit glyph masks are BW `0x0e`, gray MSB `0x06`,
  gray LSB `0x04`. X4Libris's default darkness is 0; on UC8179 its overlay
  selector folding produces the same three masks. Changing the darkness
  default is therefore not a source-backed way to recover donor rendering.
- X4Libris's `fastAntiAliasing` setting is implemented only for X3 LUT banks;
  the display API states it has no effect on X4. It cannot account for the
  X4 Pro contrast gap. Both images' captured ordinary turns used a 625 ms
  preconditioning waveform followed by a 150 ms gray activation in QEMU.
  Reader scheduling differs (CrossPoint stages strips; X4Libris overlaps
  its plane render with an asynchronous base refresh), so physical charge
  behavior still requires a device check.

## Candidate change

Enable the optional auto-hinter on the X4 Pro build and select CrossPoint's
auto-hinting plus stem darkening for runtime vector fonts. If a build cannot
honor that mode, restore the prior native mode. The existing CFF and
PostScript-name modules remain enabled for OTF compatibility. No reader UI,
book scaling, grayscale image, controller waveform, or refresh-frequency logic
is changed in this candidate.

The patch addresses stroke shape and consistency for vector fonts. It does
not establish a fix for delayed physical fading, ghosting, or a visible second
refresh; those require physical X4 Pro testing. Pre-generated `.cpfont` and
built-in bitmap fonts do not take the runtime TTF/OTF path.

## Reproduction and acceptance

- Panel probe source patch: `test/rendering/x4pro-panel-plane-probe.patch`
  (applies to local eink-qemu 0.9.2 source). Parser:
  `test/rendering/analyze_uc8179_planes.py`.
- Local captured logs: `_primary-record/artifacts/2026-11-01/`
  `donor-longbook-plane-probe/browse-qemu-stderr.log` and
  `v1-longbook-noto-interactive/stderr.log`; SD and flash snapshots stay
  outside Git because they may include settings.
- Host FreeInk outline-format test must rasterize an actual CFF OTF and TTF
  with auto-hinting and stem darkening. The X4 Pro OTA build must fit its app
  partition and boot in the UC8179 emulator.
- Host test passed with OpenDyslexic CFF OTF and Noto Sans TTF. The X4 Pro
  `x4pro_gh_release` build succeeded; application image is 5,548,560 bytes
  (SHA-256 `F0A4D4734D7E77088CC9242E8F8F80AC2E7A0D6B9706CC85E41E5ABB2BE5CF86`),
  using 84.6% of the 6,553,600-byte app partition. Build log is in the
  primary workstation's 2026-11-01 artifacts folder.
- A physical X4 Pro check must compare the same user's TTF/OTF fonts and books
  to the donor after normal and rapid page turns, immediately and after ten
  seconds, with smoothing both on and off. Check ghosting, fading, stroke shape,
  turn time, EPUB/FB2 scaling, and OTF selection. Do not replace v1.0 until
  those results meet the user's objective.

The emulator reached `setup_complete`, detected UC8179 and refreshed the
panel, but the automated navigation remained on the boot image. A later key
probe showed a blank display. No reader-page or custom-font comparison was
established for this candidate in QEMU; this is a test limitation, not a
passed visual check. The probe logs and flash snapshots are in the local
`x4pro-vector-candidate-emulator` artifact folder.
Repeating the same short key probe against an unmodified v1.0 flash snapshot
showed the same early boot/blank progression, so this probe does not isolate
a candidate boot regression. After a longer emulator wait, the candidate
displayed its normal home screen (`key-late.png` in the local artifact folder).
The scripted book navigation still has not yielded a valid custom-font reader
comparison.
With longer waits, the candidate entered the library, displayed the test book
folder and then opened and paged the EPUB (`key-library-late.png`,
`key-books.ppm`, `indexed-open.png`). This establishes a basic EPUB reader
smoke check in QEMU. The test used the default built-in font, so it does not
exercise the changed runtime vector-font path or verify physical contrast.

A second isolated SD image put the original Noto Sans TTF in `/fonts/` and set
`sdFontFamilyName` to `Noto Sans` while keeping the built-in fallback as
Bookerly. The candidate opened the same EPUB with sans-serif glyphs in QEMU
(`_primary-record/artifacts/2026-11-02/x4pro-vector-font-emulator/key-open.png`),
so the raw SD font selection and runtime vector path were exercised. This is
still a digital raster check; it does not establish equal optical behavior to
CrossPoint or remove the user's physical fading report.

An attempted matched-font comparison used the same SD image with both
`/.crosspoint/settings.json` and `/.x4libris/settings.json`, requesting Ubuntu
TTF at 14 pt in each reader. The candidate visibly rendered Ubuntu, but the
user-supplied custom CrossPoint `2.0.0rc-fb2` BIN still rendered its built-in
Noto Sans. OpenDyslexic OTF produced the same donor fallback. The donor's
exact build source and font configuration are unavailable, so these emulator
pages are **not** a controlled same-font contrast comparison. Keep the donor
screenshots and the candidate screenshot as navigation evidence only. A
physical comparison must select and verify the identical installed font in
both firmwares before judging rendering quality.

## Official CrossPoint 2.0.0 source control

Built the official stable CrossPoint 2.0.0 X4 Pro target from tag `42d58da`
with its pinned submodules. The local build passed and fits the 0x640000 app
partition (5,377,152 bytes, SHA-256
`BD68FDFF6D936A6F9146B5B9D52167A59194F57EEB9DB139F296FA8C8F168188`).
Its build log and reference-only image are under the primary workstation's
`_primary-record/artifacts/2026-11-02/`. This image has not been flashed.

The first emulator attempt still showed the built-in font. Source inspection
identified a test-fixture mistake: for a loose `/fonts/Ubuntu-Regular.ttf`,
CrossPoint registers the family as `Ubuntu-Regular`, while the settings asked
for `Ubuntu`. A second SD snapshot adds byte-identical `/fonts/Ubuntu.ttf` so
the requested name resolves. With that card, the official build uses about
350 KiB more PSRAM at reader startup and displays visibly different Ubuntu
glyphs. The same TTF bytes and 14 pt request are therefore available to both
CrossPoint and the X4Libris candidate. A second X4Libris run allowed the
book's background build and grayscale pass to finish, then turned a page.
Its final screens are in the primary record's
`artifacts/2026-11-02/candidate-matched-font-emulator/`. The previous
`candidate-ubuntu-font-emulator/key-open.png` showed only an intermediate
B/W state. Both settled frames use the same four emulator gray levels
(16, 48, 81, 239). In the body region, black pixels comprise about 79.7%
of X4Libris's non-background pixels and 79.5% of CrossPoint's. This coarse
metric is not glyph-by-glyph equivalence because the layouts differ. Both
also issue 625 ms preconditioning and 150 ms gray activation on ordinary
turns in the emulator. These checks cannot prove physical fade, ghosting,
or panel charge behavior, including the user's reported change after about
ten seconds. Do not attribute that report to Noto Sans; it also occurs with
their own fonts.
