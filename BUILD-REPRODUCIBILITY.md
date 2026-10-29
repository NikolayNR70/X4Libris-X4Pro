# X4Libris v1.0 build record

- Frozen label: X4Libris v1.0 (trial candidate), 2026-10-29.
- Source snapshot: this repository's recovered X4Pro project, derived from the nearest locally preserved Witchhunt 3.1.3 source snapshot. This is not original source recovered from machine code.
- Trial app image: `artifacts/X4Libris-X4Pro-v1.0.bin`, 5,491,200 bytes, SHA-256 `1157856C029773BED9FA835591EDA7A0ABADA40B49393C20086C9BF58724EC01`.
- User-supplied PerfectInk BIN: 5,491,184 bytes, SHA-256 `4D51696A11AD4D6B440189496A9989AFA8A206B26A732019C0DFEBE2FBAD9C0F`; embedded ELF SHA-256 `77D4433D47EE1E162CCBEC43E37CA7E7D526337E9638AA60E8579EBC7122F29E`.
- Trial build embedded ELF SHA-256: `ED554F94076DE46D506AEE272349294F274C2121C35E0BDDC30E117947D35720`. Therefore the trial image is not an exact rebuild of the supplied BIN.
- Build succeeded with PlatformIO `x4pro_gh_release` (Python 3.13): RAM 110152/327680, application flash 5486185/6553600. esptool 5.3.0 verified ESP32-S3 format, checksum, and validation hash.
- The binary has not been tested on a physical X4 Pro or QEMU. It remains a trial candidate.
- Build environment caveats: source copy had no `.git` metadata; `terser` was unavailable, making the generated web assets approximately 17% larger than the CI release bundle. Result still fits the app partition.
