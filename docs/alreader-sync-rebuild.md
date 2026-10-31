# Historical AlReader.com experiment (inactive)

This document records the earlier service experiment. The current isolated
candidate uses WebDAV; see [alreader-webdav-sync.md](alreader-webdav-sync.md).

This document records the isolated `feat/alreader-sync-rebuild-20261031` branch. It is a work in progress, not a release. Published X4Libris v1.0 is unchanged.

## What was checked

- AlReader XE Ink 1.33 in an Android emulator supplied the book identifiers and saved text positions for synthetic EPUB, FB2 and FB2.ZIP books. Tests did not use a personal synchronization identifier.
- The book key follows the APK's virtual `container.xml` record for EPUB, the decompressed inner file for FB2.ZIP, and its UUID suffix rule. The synthetic keys agree with the APK's local book database.
- Simple EPUB chapters `ABCD` and `EFGH` in separate spine documents start at AlReader positions 0 and 4. On X4 Pro QEMU, the corresponding section caches report lengths 4 and 4, and the reader displays the same two chapters.
- Simple FB2 with title `T` and body `ABCD` has AlReader book size 8; the X4 Pro section contains 4 body units. The title contributes one unit, and the simple fixture has three terminal units. An FB2 title `Long Title` shifts the saved body position by ten units.
- The AlReader.com service was probed with a synthetic identifier only: `setUser` and `put` return `1 ok`; a missing `get` returns `0 value not found`; an existing `get` returns `1 !syncID!position`. The client now parses these results.
- X4 Pro QEMU booted the candidate and opened synthetic EPUB and FB2 books without firmware log errors. With the first EPUB chapter's page cache deliberately removed and the book saved in chapter two, selecting an AlReader upload rebuilt the missing chapter cache before the Wi-Fi screen. The rebuild was sliced and did not stall the UI.
- The latest `x4pro_gh_release` build succeeds and produces both an ESP32-S3 app image and factory image. Its app image contains the `CROSSPOINT-BOARD-V1:x4pro;` tag and no X4 tag. The KOReader client, worker and activity have no source diff from the protected v1.0 base.
- A TLS handshake to `alreader.com` verifies against the firmware's own CA bundle. This checks the current certificate chain on the PC, not the device's HTTPS request.
- In X4 Pro QEMU, selecting the AlReader provider routes to its own activity; selecting KOReader routes to the existing KOReader activity. The former can rebuild a missing earlier chapter cache, and cancelling Wi-Fi displays a persistent failure before restarting the reader. QEMU's Wi-Fi scan sees no access points, so this is not a network round trip.
- The device's Network settings now open a text keyboard for the AlReader.com identifier. Previously the row was present but tapping it did nothing. Exiting an AlReader network session now disconnects Wi-Fi before the existing reader restart, matching the KOReader cleanup path. These changes address a reported unusable identifier row and slowdown after leaving reading; physical-device recovery remains to be measured.
- The AlReader Wi-Fi selection now returns a visible failure after 30 seconds if no access point appears. The timeout is specific to this call; normal network setup and KOReader selection retain their existing behavior. It does not interrupt a visible network choice or password entry.
- A later X4 Pro emulator report showed chapter preparation stuck at `2/12` with repeated `GFX Font ... not found` errors. The activity manager was unloading the selected SD font when it replaced the reader with the AlReader sync screen, although sync then tried to paginate missing chapters with that font ID. The sync screen now retains the reader font during local preparation, releases it before network allocation, and reloads it only if a downloaded position needs another chapter build. It also rejects an unavailable font and times out preparation after 45 seconds instead of looping indefinitely. A synthetic missing-chapter X4 Pro QEMU run reached Wi-Fi with zero GFX errors; the specific user's external-font book still needs a device check.
- In a separate clean Android emulator, AlReader XE Ink 1.33 accepted a synthetic SyncID, opened a synthetic FB2 and automatically uploaded its position through its default network sync. Its own log reported `net_write_ok (1 ok)` and the same FB2 key form implemented by this candidate. This verifies the Android sender and live service for synthetic data; it does not yet verify an X4 Pro network round trip.
- A longer synthetic FB2 in that clean Android emulator reached page 11/39 (26.53%), and AlReader uploaded `1 !-1822344490!3586` to its service. A synthetic remote update to `1 !1234567!9000` was then downloaded by the Android app; it offered the other-device position, and accepting it moved to page 26/39 (66.58%). Screenshots and the fixture are in `_primary-record/artifacts/2026-10-31/alreader-sync-clean-avd/`. This verifies the service record and the Android receiver with a visible position change. It does not establish exact X4Libris text-coordinate parity.
- The user subsequently reported that synchronization completed and the reading offset/progress was received. The test build and whether the resulting page matches AlReader remain to be confirmed. Their preceding screenshot showed QEMU running the older `2390af9` image, which lacks the later SD-font lifetime fix in `c5b3d66`.

## What still needs verification

- A confirmed same-book network round trip between X4 Pro and Android AlReader, including the actual landing page and both directions. The available X4 Pro QEMU Wi-Fi scan reports zero access points, including when a saved synthetic network is configured; its firmware log reports `NO_AP_FOUND`. A user-reported success awaits the build/page details above. The isolated test SD images contain no personal network or sync credentials.
- Positions in complex books: page text, hidden nodes, notes and normalization can differ from the simple controlled fixtures.
- Independent KOReader network regression and automatic AlReader synchronization behavior. KOReader code is gated by the selected provider; this is not yet a runtime regression result.
- Physical X4 Pro timing and memory behavior, especially on a large book with many unopened chapters. The current missing-chapter recovery builds renderer caches in short slices, which is bounded per loop but can take time across a large book.

No candidate binary should replace the published v1.0 until these checks pass.
