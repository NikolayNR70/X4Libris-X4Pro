# Development continuity and clean-disk recovery

This project must remain recoverable if the primary PC is replaced or wiped. GitHub is the off-machine source of truth for approved source, development records, and recovery instructions; `_primary-record` remains the local working journal, not the only copy.

## Every work stage

For each stage that changes code or produces a firmware candidate:

1. Work from a named branch. Keep experiments and reference snapshots outside `main` until explicitly accepted.
2. Commit the source change and a concise record together. The record names the base commit, branch, changed files, target board/environment, exact build command and toolchain, build/check results, image size and SHA-256, test status (emulator vs physical device), unresolved risks, and next concrete step.
3. Push the stage branch to GitHub before starting another independent experiment. Keep failed experiments as labeled commits/branches rather than silently overwriting accepted source.
4. Never put credentials, API keys, Wi-Fi settings, private-user image attachments, or personal absolute paths in tracked records. Redact them before pushing.
5. Never treat a successful compile as proof of hardware behavior. Mark device-tested only when the user confirms the X4 Pro test. Keep an unverified candidate out of a release asset unless explicitly approved.
6. Keep candidate, accepted intermediate, and release identities distinct. Do not overwrite the known-good rollback image or retag a released version to imply unverified changes are stable.

## Recovery from a clean PC

1. Install Git, PlatformIO in the project's Python 3.13 environment, and the ESP32-S3 platform/toolchain used by the target profile.
2. Clone `https://github.com/NikolayNR70/X4Libris-X4Pro` and inspect `README.md`, `BUILD-REPRODUCIBILITY.md`, and the latest `docs/development-log` entries.
3. Fetch the intended branch explicitly. For historical Power/EXT1 investigation: `git fetch origin reference/x4pro-power-ext1-20261030` then `git switch --track origin/reference/x4pro-power-ext1-20261030`.
4. Rebuild only in a fresh working copy and verify the selected environment/device macro and resulting image header with esptool before sharing a BIN.
5. Compare the resulting SHA-256 against the development record. If a listed external archive or artifact is missing, stop and record that gap; do not assume a nearby version is identical.

## Records

Use `docs/development-log/YYYY-MM-DD.md` for chronological stage summaries, with links to source commits/branches. The branch history is an additional trace; the concise records make the work understandable without access to the original chat.
