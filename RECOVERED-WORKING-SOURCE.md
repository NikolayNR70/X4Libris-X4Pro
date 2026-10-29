# X4Pro recovered working source

Working copy of the nearest preserved source snapshot for X4Pro v3.1.3. Work here only; the protected intermediate and D: archive are left untouched.

The user-confirmed installed app image is preserved separately at outputs/X4Pro-firmware-recovery-20261029/X4Pro-installed-rollback-20261029.bin and marked read-only. Its SHA-256 is $srcHash.

Important: the closest preserved source build does not reproduce the installed BIN exactly. It is a practical source baseline for investigation and changes, not a verified exact source reconstruction. Before any edited firmware is flashed, compare its behavior and board profile, preserve rollback, and identify the build by hash.

Target PlatformIO environment: x4pro_gh_release (FREEINK_DEVICE_X4PRO=1). Power/sleep investigation starts in lib/hal/HalGPIO.cpp, lib/hal/HalPowerManager.cpp, src/main.cpp, and src/CrossPointSettings.h.
