# X4 Pro: one visible text state per page turn

## Observed path

The EPUB/FB2 reader currently sends a B/W page first and then a separate gray AA
activation. `EpubReaderActivity::renderNormalPass()` calls `displayBuffer()` on
UC8179 and then `renderGrayscalePlanesSequential()`. The pre-rendered page path
does the same. TXT and Markdown also display B/W before the gray pass. The
roughly one-second visible change reported by the user is therefore structural,
not a delay in the toggle or font cache.

The one-waveform `supportsGrayFrame()` path is unavailable on UC8179: its
driver returns `false` by default and its overlay grayscale capability declares
`GrayscaleBase::Separate`. UC8179 also has a direct absolute-gray mode, but that
uses a different full grayscale waveform and forces the next B/W refresh to
resynchronize. It is not a drop-in fast page-turn replacement.

## Target behavior

- AA off: retain the existing single B/W page refresh.
- AA on: produce the final text in the B/W framebuffer *before* the same single
  page refresh, using a stable ordered monochrome coverage pattern for the 2-bit
  font edge values. Do not schedule or replay the gray overlay for text.
- Apply the same selected mode to normal and pre-rendered EPUB/FB2 pages and to
  TXT/Markdown. Keep images on their existing separate grayscale path where
  appropriate; a text-only AA choice must not silently discard image tones.
- Ensure coordinates for the ordered pattern are physical screen coordinates,
  including rotated text and scaled glyphs. Keep the glyph core black and map
  only light/dark edge samples. Preserve the existing text darkness choices.
- The setting must take effect on the next rendered page and on a manual redraw.

## Acceptance gate

Build the X4 Pro OTA in isolation and inspect both AA states in the emulator.
Before any release, compare on a physical UC8179 X4 Pro against published v1.0:
normal and rapid page turns, 1 s and 10 s after each turn, all orientations,
scaled EPUB/FB2 fonts, images, and the existing ghost-clearing cadence. Measure
render time and confirm that exactly one panel activation occurs for a text page
in either AA setting. The current bounded-boot candidate is still unverified;
do not layer this change into its OTA or replace the published v1.0 on a source
review alone.
