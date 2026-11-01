# X4 Pro UC8179 reading transition candidate

## Physical test result (2026-11-01)

The user reports that the visible page rendering changed very little and that
the device rolled back after installing this candidate. The new device log is
not yet available, so the cause of that rollback is unconfirmed. In particular,
this branch inherited the v1.0 `BootTrialHealth` implementation that can reject
a responsive boot after a long book-restore iteration; the old behavior must
not be assumed to explain this particular trial without its log. The candidate
OTA image has been renamed with `.obsolete-do-not-flash` in the primary record.
This branch records a failed experiment and must not be offered as an update.

Base: published X4Libris v1.0 source at `fdc38cf`. The v1.0 release asset,
tag, and `main` remain unchanged. This branch does not use the discarded
CrossPoint renderer rebase or the abandoned monochrome text-dither experiment.

## Evidence and diagnosis

The user reports stable reading with a local
`CrossPoint-ESP32-2.0.0rc-fb2` ESP32-S3 image (5,483,696 bytes,
SHA-256 `87BC492F9B624D696F8427E69D203FBEC6125DEA357D0AA527D6A82CF28E0552`).
Its image header validates as an ESP32-S3 / 16 MB application. Embedded strings
include the UC8179 gray precondition and gray-base paths, but the exact custom
build source is unavailable. The public CrossPoint 2.0.0rc source is a useful
behavioral reference, not proof of every byte in that supplied image.

X4Libris v1.0 already contains nearly the same UC8179 panel driver. The key
reader difference is how it starts the B/W base of a gray-AA page. CrossPoint
uses `displayGrayscaleBase()`, which calls the driver's
`transitionGrayscaleBase()` on a Fast page after AA. X4Libris's inline-AA path
calls `triggerDisplayAsync()`, which enters `Uc8179Driver::displayStart()`
directly. Previously that skipped `_redriveAfterGray` and used ordinary DU
partial instead of the stock previous-to-current gray-base transition. This
is a source-level reason for charge carryover and inconsistent contrast; the
physical effect still requires a controlled device test.

The driver now applies the same gray-base transition from `displayStart()`
when its post-AA state is valid. It completes the transition before accepting
the next gray planes and returns `false` to the asynchronous caller because no
waveform remains pending. Full/Half refreshes, other panels, font rasterization,
EPUB/FB2 scaling, grayscale image rendering, and settings are unchanged. The
host driver test exercises the formerly bypassed async entry and asserts a
single custom-LUT refresh, synchronized B/W state, and no pending refresh.

The X4 Pro `x4pro_gh_release` build succeeded with 5,495,941 / 6,553,600
flash bytes (83.9%). The OTA application image is 5,500,944 bytes, 48 bytes
larger than the published v1.0 image and 17,248 bytes larger than the supplied
CrossPoint image. SHA-256:
`8085461CC00989E26CEADC54BA03EA1D507F67BEF370FDDBAA853F460307BCF3`.
It is stored in the primary record under
`artifacts/2026-11-01/steady-render-v1/X4Libris-X4Pro-v1-UC8179-transition-OTA-CANDIDATE.bin`.
Both dual-buffer and single-buffer FreeInk Pro host suites passed. This image
is an OTA candidate, not a factory image or a released update.

## Validation gate

Host driver test and X4 Pro build results are recorded in the primary session
journal. Before release, compare the candidate with the user's known-good
CrossPoint image on the same physical X4 Pro and the same EPUB/FB2 pages. Check
at least 30 normal page turns, fast turns, small and scaled text, image pages,
sleep/wake, visible ghosting and contrast after 1 and 10 seconds. This is a
targeted candidate; it is not yet a verified visual fix.
