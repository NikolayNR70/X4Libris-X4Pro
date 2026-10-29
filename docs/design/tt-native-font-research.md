# TT family: direct TTF/OTF support research

This work starts from the released Witchhunt family 3 baseline (`Witchhunt-NREdition-X4Pro-v3.0.1`) in an isolated branch. No code or binaries from the abandoned TT worktree are being carried over.

## Implementations reviewed

- **CrossPoint PR #3646** added direct TrueType rendering on PSRAM boards in the existing CrossPoint reader. It registers original files from SD and rasterizes glyphs at runtime through FreeType, with an `EpdFont` adapter, on-demand glyph handling, and a bounded glyph cache. It was merged into `develop` on 2026-09-23. This is the closest fit for Witchhunt because it retains the established reader/layout pipeline.
- **XPoint** supports direct TTF/OTF/collection fonts on PSRAM devices, but its current design places font selection and pagination in a native `FreeInkBook` reader chain. Its docs describe fractional metrics, per-face caches, on-demand glyph rasterization, and explicit safeguards for streamed large fonts. That full reader-engine change is broader than needed for the first TT implementation.
- **XT Licorice** documents direct TTF support on X4 Pro, currently limited to one-file TrueType (no variable/color fonts). It is useful as a device-level validation point, but it does not publish the parser implementation.

## Initial implementation direction

Use the existing FreeInkFont/FreeType code already present in this checkout and the smaller CrossPoint adapter model. Restrict the feature to PSRAM-equipped X4 Pro builds. Preserve the existing book layout and bitmap rendering API by exposing dynamically rasterized font glyphs through an adapter, while keeping original font files on SD. Do not generate `.cpfont` or another intermediate font file. Keep the bitmap font path available for the other board family and as a fallback.

The initial test matrix must distinguish TrueType `glyf` outlines and OpenType CFF outlines, static versus variable files, missing face styles, and large-file streaming. OTF acceptance must be based on a real CFF fixture and target build, not just the `.otf` extension.

## Sources

- CrossPoint PR #3646: https://github.com/crosspoint-reader/crosspoint-reader/pull/3646
- CrossPoint SD font documentation: https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/docs/sd-card-fonts.md
- XPoint repository and TTF design comparison: https://github.com/Belphemur/XPoint and https://github.com/Belphemur/XPoint/blob/develop/docs/ttf.md
- XT Licorice TTF announcement: https://www.reddit.com/r/xteinkereader/comments/1wiu4dq/meet_xt_licorice_new_official_firmware_for_x4_pro/
