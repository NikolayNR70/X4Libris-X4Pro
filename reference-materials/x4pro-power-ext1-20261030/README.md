# X4 Pro Power / EXT1 handoff — reference only

**Status: historical investigation; this is not part of the active X4Libris v1.0 source or release branch.** The material is retained on the separate GitHub branch `reference/x4pro-power-ext1-20261030` so it can be recovered after a clean OS reinstall.

## What was investigated

On X4 Pro, the configured deep-sleep wake source is ESP32-S3 EXT1 (active-low GPIO3); GPIO1 is the power latch. The archived `HalGPIO::getWakeupReason()` recognized GPIO wake but omitted EXT1 deep-sleep wake. The main startup path applies the Power-button wake handling only when the wake reason is classified as `PowerButton`. This is a code-path diagnosis; it does not establish that this is the only physical-device fault.

The candidate adds `ESP_SLEEP_WAKEUP_EXT1 && ESP_RST_DEEPSLEEP` to that classification. Exact source change is recorded in `EXT1-power-fix-diff.md`; the candidate file is under `candidate-diff/lib/hal/HalGPIO.cpp`.

## Source provenance and limits

- `baseline-source-v3.1.3/` is a preserved source snapshot from the PerfectInk/X4Pro 3.1.3 work. It is the nearest available source, not proven to be the exact source for the installed image.
- It differs from the temporary candidate working tree only by the added `RECOVERED-WORKING-SOURCE.md` note and the one `lib/hal/HalGPIO.cpp` change; generated caches and build outputs are intentionally excluded. A private local journal path in one document was redacted in this public snapshot.
- Reconstruct the candidate by applying `EXT1-power-fix-diff.md` to the baseline snapshot; do not copy this historical tree into the active v1.0 branch.

## Build evidence

- Target: PlatformIO `x4pro_gh_release`, `esp32-s3-devkitc1-n16r8`, `FREEINK_DEVICE_X4PRO=1`; PlatformIO runs in its Python 3.13 environment.
- Command: `py -3.13 -m platformio run -e x4pro_gh_release` — successful.
- RAM: 110,152 / 327,680 bytes (33.6%). App flash: 5,486,185 / 6,553,600 bytes (83.7%). esptool 5.3.0 identified ESP32-S3 and valid image checksum/hash.
- Candidate app image: 5,491,200 bytes; SHA-256 `1157856C029773BED9FA835591EDA7A0ABADA40B49393C20086C9BF58724EC01`.
- Installed image recorded by the user: 5,484,544 bytes; SHA-256 `A660EABAD31171CD5AFD43D84ED1BE7CB92AC147941E897854723B485DAFEB92`.
- The candidate is not a byte-exact rebuild of the installed image. It was not flashed or physically tested. Treat the proposed fix as unverified until the user tests it on X4 Pro.

## Artifacts kept outside this public source branch

The local `_primary-record/reference-materials/X4Pro-Power-EXT1-handoff-20261030/` archive contains rollback/candidate BINs, ELF/map files, logs, and the unredacted handoff. Public branch records hashes and source only; do not upload a rollback BIN or unverified firmware into the v1.0 release without a separate explicit release instruction.
