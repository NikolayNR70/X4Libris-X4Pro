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

## Next diagnostic gate

Use identical font and refresh settings, then capture matched B/W and gray
planes plus UC8179 commands for a multi-page text sample. Compare the donor
against X4Libris after each page turn, then verify any candidate on the user's
physical X4 Pro. Do not infer a fix from QEMU timing or copy only the driver:
the X4Libris reader has additional rendering policies and caches.
