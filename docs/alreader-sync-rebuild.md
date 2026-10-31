# AlReader.com synchronization candidate

This document records the isolated `feat/alreader-sync-rebuild-20261031` branch. It is a work in progress, not a release. Published X4Libris v1.0 is unchanged.

## What was checked

- AlReader XE Ink 1.33 in an Android emulator supplied the book identifiers and saved text positions for synthetic EPUB, FB2 and FB2.ZIP books. Tests did not use a personal synchronization identifier.
- The book key follows the APK's virtual `container.xml` record for EPUB, the decompressed inner file for FB2.ZIP, and its UUID suffix rule. The synthetic keys agree with the APK's local book database.
- Simple EPUB chapters `ABCD` and `EFGH` in separate spine documents start at AlReader positions 0 and 4. On X4 Pro QEMU, the corresponding section caches report lengths 4 and 4, and the reader displays the same two chapters.
- Simple FB2 with title `T` and body `ABCD` has AlReader book size 8; the X4 Pro section contains 4 body units. The title contributes one unit, and the simple fixture has three terminal units. An FB2 title `Long Title` shifts the saved body position by ten units.
- The AlReader.com service was probed with a synthetic identifier only: `setUser` and `put` return `1 ok`; a missing `get` returns `0 value not found`; an existing `get` returns `1 !syncID!position`. The client now parses these results.
- X4 Pro QEMU booted the candidate and opened synthetic EPUB and FB2 books without firmware log errors. With the first EPUB chapter's page cache deliberately removed and the book saved in chapter two, selecting an AlReader upload rebuilt the missing chapter cache before the Wi-Fi screen. The rebuild was sliced and did not stall the UI.

## What still needs verification

- A same-book network round trip between X4 Pro and Android AlReader. The available QEMU Wi-Fi scan reports zero access points, so the device-side HTTP path has not been exercised.
- Positions in complex books: page text, hidden nodes, notes and normalization can differ from the simple controlled fixtures.
- Independent KOReader network regression and automatic AlReader synchronization behavior. KOReader code is gated by the selected provider; this is not yet a runtime regression result.
- Physical X4 Pro timing and memory behavior, especially on a large book with many unopened chapters. The current missing-chapter recovery builds renderer caches in short slices, which is bounded per loop but can take time across a large book.

No candidate binary should replace the published v1.0 until these checks pass.
