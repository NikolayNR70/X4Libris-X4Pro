# Witchhunt-NREdition v1.1 — Xteink X4 Pro

Firmware based on Witch Reader 2.40, with EPUB/FB2 reading and automatic first-boot rollback.

The complete v1 source snapshot is retained as commit `175cf7ad63d70d3c8a4019b8d61071c17b2d48c6`, tag `Witchhunt-NREdition.v1`. Earlier development patches and their Git ancestry are retained. No history is rewritten.

## v1.1

- Formatting pickers no longer expose “Как в книге”. Select an active property value again to remove that override. Per-book removal inherits the global profile; global removal restores publisher CSS. Stored v1 sentinels and explicit zero values remain compatible.
- Legal hyphenation continues across page boundaries. Old section caches are invalidated (version 82).
- X4 Pro verifies complete firmware bytes using the hardware-assisted ESP-IDF SHA-256 port, instead of hashing them with software WolfSSL before every boot. The fixed 250 ms USB startup stall is also omitted on X4 Pro. Power thresholds, debounce and hardware configuration remain unchanged.
- Automatic rollback and unattended healthy-UI acceptance remain enabled; no confirmation screen is introduced.

Build: `pio run -e x4pro_gh_release`.

Russian instructions and validation: `docs/INSTALL-v1.1-ru.txt`, `docs/RELEASE-REPORT-v1.1.txt`. Cumulative changes: `CHANGELOG-NRE.txt`. Russian CHM sources: `docs/nre-help-ru`; compile `help.hhp` with chmcmd or HTML Help Workshop.

Hardware installation requires the owner's separate authorization. Emulator timing is not a physical-device timing measurement. See the validation report for exact evidence and limits.

Upstream documentation is preserved in `docs/UPSTREAM-README.md`. Historical engineering notes under `docs/engineering` describe earlier work and are superseded by the current release report.
