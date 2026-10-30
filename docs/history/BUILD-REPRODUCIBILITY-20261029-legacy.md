# X4Libris v1.0 build record

- Frozen label: X4Libris v1.0 (trial candidate), 2026-10-29.
- Source snapshot: a recovered X4Pro project snapshot. The BIN alone is not the original C/C++ source; technical provenance and required component notices are preserved separately.
- Trial app image: `artifacts/X4Libris-X4Pro-v1.0.bin`, 5,491,200 bytes, SHA-256 `1157856C029773BED9FA835591EDA7A0ABADA40B49393C20086C9BF58724EC01`.
- User-supplied PerfectInk BIN: 5,491,184 bytes, SHA-256 `4D51696A11AD4D6B440189496A9989AFA8A206B26A732019C0DFEBE2FBAD9C0F`; embedded ELF SHA-256 `77D4433D47EE1E162CCBEC43E37CA7E7D526337E9638AA60E8579EBC7122F29E`.
- Trial build embedded ELF SHA-256: `ED554F94076DE46D506AEE272349294F274C2121C35E0BDDC30E117947D35720`. Therefore the trial image is not an exact rebuild of the supplied BIN.
- Build succeeded with PlatformIO `x4pro_gh_release` (Python 3.13): RAM 110152/327680, application flash 5486185/6553600. esptool 5.3.0 verified ESP32-S3 format, checksum, and validation hash.
- Build environment caveats: source copy had no `.git` metadata; `terser` was unavailable, making the generated web assets approximately 17% larger than the CI release bundle. Result still fits the app partition.

## Web interface packaging candidate

The firmware image above predates the X4Libris web-bundle configuration. The source already contains the web server and the first-visit setup page, but the separate `web-<hash>.bin` bundle was not attached to the public v1.0 release. This is why that image cannot install the bundle from the X4Libris release automatically.

The local review branch configures both the web bundle and stable firmware updater to use the `X4Libris-v1.0` release in `NikolayNR70/X4Libris-X4Pro`. The OTA checker reads only the latest stable release; beta updates are not offered. For automatic firmware updates, the release needs the board asset name `firmware-x4pro.bin`; the package script emits that OTA-compatible name alongside the human-readable `X4Libris-X4Pro-v1.0.bin`. The branded tag's numeric version is used for update comparisons. Build and collect the paired candidate image and web bundle with `scripts/package_x4libris_release.ps1`. The output is placed under `artifacts/review/` and is ignored by Git. These review files do not replace the frozen image in `artifacts/X4Libris-X4Pro-v1.0.bin` and are not uploaded automatically.

## Local X4Libris repository candidate — 2026-10-29

- Built from the local review branch after branding and update endpoints were changed to X4Libris; public GitHub files were not changed.
- Firmware: `artifacts/review/X4Libris-v1.0-final/X4Libris-X4Pro-v1.0.bin`; 5,489,440 bytes; SHA-256 `3322D7BA5B0EB2287467EC0FAC41C0D8B8340729AA4B161EBC48C896C01A5F2B`.
- OTA-compatible duplicate: `firmware-x4pro.bin`, same 5,489,440 bytes and SHA-256.
- Web bundle: `web-164ece56515f.bin`; 449,072 bytes; SHA-256 `164ECE56515F9DF163222BC56C46C3D7D46843AD6EE2210549A9DB4338D1B103`.
- PlatformIO `x4pro_gh_release`, Python 3.13: build succeeded; app flash 5,484,437/6,553,600 bytes (83.7%), RAM 110,152/327,680 bytes (33.6%).
- Both firmware names contain identical bytes. Embedded binary strings include the X4Libris GitHub repository and `X4Libris-ESP32-`; the former third-party OTA URL and old product User-Agent are absent.
- `SHA256SUMS.txt` records the three distributable files. This folder is an approval copy and does not replace the frozen v1.0 artifact.
## Test build — 2026-10-30

The source branch contains a freshly built test image and the paired web bundle in `artifacts/test/X4Libris-X4Pro-v1.0-test-20261030/`. The firmware file is named `X4Libris-X4Pro-v1.0-test-20261030.bin`. It is not a replacement for the frozen v1.0 release. The build succeeded for `x4pro_gh_release` with Python 3.13. RAM use is 110152/327680 bytes; app partition use is 5485761/6553600 bytes. SHA-256 values and the current visual-verification status are in `TEST_BUILD.md` in that folder.
