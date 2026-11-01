# X4 Pro rendering candidate from released v1.0

Date: 2026-11-01. Branch `fix/x4pro-crosspoint-vector-render` starts at released
X4Libris v1.0 (`fdc38cf`). Published `main` and the v1.0 release stay unchanged.

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
