#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
FONT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/freeink-outline-formats.XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT HUP INT TERM

INCLUDES="-I$FONT_ROOT/include -I$FONT_ROOT/third_party/freetype/include -I$FONT_ROOT/third_party/stb"
DEFINES='-DFREEINK_FONT_ENABLE_CFF=1 -DFREEINK_FONT_ENABLE_PSNAMES=1'

for source in "$FONT_ROOT"/src/freetype/*.c; do
  object="$BUILD_DIR/$(basename "$source" .c).o"
  # shellcheck disable=SC2086
  cc -std=c11 -O2 $INCLUDES $DEFINES -c "$source" -o "$object"
done

# shellcheck disable=SC2086
c++ -std=c++17 -O2 $INCLUDES $DEFINES \
  "$FONT_ROOT/src/FtFont.cpp" "$FONT_ROOT/src/FontAlloc.c" \
  "$FONT_ROOT/src/Gpos.cpp" "$FONT_ROOT/src/Gsub.cpp" \
  "$SCRIPT_DIR/FtFontOutlineFormatsTest.cpp" "$BUILD_DIR"/*.o \
  -o "$BUILD_DIR/outline-formats-test"

"$BUILD_DIR/outline-formats-test" "$@"
