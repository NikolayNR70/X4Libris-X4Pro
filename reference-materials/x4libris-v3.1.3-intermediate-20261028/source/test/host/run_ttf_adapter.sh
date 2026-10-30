#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
FONT_ROOT="$REPO_ROOT/freeink-sdk/libs/font/FreeInkFont"
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/ttf-adapter.XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT HUP INT TERM

INCLUDES="-I$SCRIPT_DIR/ttf_epd_adapter_stubs -I$REPO_ROOT/test/shims -I$REPO_ROOT/lib -I$REPO_ROOT/lib/EpdFont -I$REPO_ROOT/lib/Utf8 -I$FONT_ROOT/include -I$FONT_ROOT/third_party/freetype/include -I$FONT_ROOT/third_party/stb"
DEFINES='-DBOARD_HAS_PSRAM=1 -DFREEINK_FONT_ENABLE_CFF=1 -DFREEINK_FONT_ENABLE_PSNAMES=1'

for source in "$FONT_ROOT"/src/freetype/*.c; do
  object="$BUILD_DIR/$(basename "$source" .c).o"
  # shellcheck disable=SC2086
  gcc -std=c11 -O2 $INCLUDES $DEFINES -c "$source" -o "$object"
done

# shellcheck disable=SC2086
g++ -std=c++20 -O2 $INCLUDES $DEFINES \
  "$SCRIPT_DIR/TtfEpdAdapterTest.cpp" \
  "$REPO_ROOT/lib/EpdFont/TtfEpdFont.cpp" \
  "$REPO_ROOT/lib/EpdFont/EpdFont.cpp" \
  "$REPO_ROOT/lib/EpdFont/EpdFontFamily.cpp" \
  "$REPO_ROOT/lib/EpdFont/GlyphFallback.cpp" \
  "$REPO_ROOT/lib/EpdFont/RawTtfFontManager.cpp" \
  "$REPO_ROOT/lib/EpdFont/SdCardFontRegistry.cpp" \
  "$REPO_ROOT/lib/Utf8/Utf8.cpp" \
  "$REPO_ROOT/lib/FsHelpers/FsHelpers.cpp" \
  "$REPO_ROOT/lib/FsHelpers/NaturalSort.cpp" \
  "$FONT_ROOT/src/FtFont.cpp" "$FONT_ROOT/src/FontAlloc.c" \
  "$FONT_ROOT/src/Gpos.cpp" "$FONT_ROOT/src/Gsub.cpp" \
  "$BUILD_DIR"/*.o -o "$BUILD_DIR/ttf-adapter-test"

"$BUILD_DIR/ttf-adapter-test" \
  "$REPO_ROOT/lib/EpdFont/builtinFonts/source/Ubuntu/Ubuntu-Regular.ttf" \
  "$REPO_ROOT/lib/EpdFont/builtinFonts/source/Ubuntu/Ubuntu-Bold.ttf" \
  "$REPO_ROOT/lib/EpdFont/builtinFonts/source/OpenDyslexic/OpenDyslexic-Regular.otf" \
  "$REPO_ROOT/lib/EpdFont/builtinFonts/source/OpenDyslexic/OpenDyslexic-Bold.otf" \
  "$REPO_ROOT/lib/EpdFont/builtinFonts/source/OpenDyslexic/OpenDyslexic-Italic.otf" \
  "$REPO_ROOT/lib/EpdFont/builtinFonts/source/OpenDyslexic/OpenDyslexic-BoldItalic.otf" \
  "$REPO_ROOT/lib/EpdFont/builtinFonts/source/Inter/Inter-Regular.ttf" \
  "$REPO_ROOT/lib/EpdFont/builtinFonts/source/Inter/Inter-Bold.ttf"
