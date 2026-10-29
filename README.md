# X4Libris v1.0 — X4 Pro

This repository freezes the X4Libris v1.0 trial build for Xteink X4 Pro and its working source snapshot.

## Status

This is a **trial build**, not a physically validated release. It was compiled from the nearest preserved X4Pro source snapshot, which includes the EXT1 wake-cause classification change. The source snapshot is not a byte-for-byte reconstruction of the separately supplied PerfectInk firmware image. The image identifiers and ELF SHA-256 values differ; details are in [BUILD-REPRODUCIBILITY.md](BUILD-REPRODUCIBILITY.md).

The published app image is in `artifacts/X4Libris-X4Pro-v1.0.bin`.

- SHA-256: `1157856C029773BED9FA835591EDA7A0ABADA40B49393C20086C9BF58724EC01`
- Size: 5,491,200 bytes
- Target: ESP32-S3 / Xteink X4 Pro (`x4pro_gh_release`)
- Build: PlatformIO Core 6.2.0, Python 3.13; RAM 110,152/327,680 bytes; application flash 5,486,185/6,553,600 bytes.
- esptool 5.3.0 validated the ESP32-S3 image checksum and validation hash.
- No physical-device or QEMU verification is claimed.

## Build

Use Python 3.13 and PlatformIO Core. From the repository root:

```powershell
py -3.13 -m platformio run -e x4pro_gh_release
```

The source tree is a recovered working snapshot. A clean clone may generate a different web bundle identifier because the original build had no Git metadata and Terser was unavailable. Do not treat a fresh build as byte-identical without comparing the resulting image and ELF hashes.

## Provenance

The upstream project lineage and license are preserved in `LICENSE`, `CHANGELOG-NRE.txt`, and `docs/`. X4Libris v1.0 is the product/version label assigned to this trial snapshot. No GitHub Actions workflows are included in this repository snapshot.
