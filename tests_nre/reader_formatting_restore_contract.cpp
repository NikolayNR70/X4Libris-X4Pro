#include <cassert>
#include "../src/ReaderFormatting.h"

// A book style must be independently recoverable without resetting other edits.
int main() {
  ReaderFormatting f;
  constexpr uint8_t globalLine = 4;
  assert(ReaderFormatting::choice(f.lineHeight, globalLine) == globalLine);
  f.lineHeight = 6;
  f.indent = 1; // explicit zero indentation, not original CSS
  f.headingSize = 3;
  assert(ReaderFormatting::linePercent(ReaderFormatting::choice(f.lineHeight, globalLine)) == 150);
  assert(ReaderFormatting::indentPercent(ReaderFormatting::choice(f.indent, 3)) == 0);
  f.lineHeight = 0; // undo spacing override: original publisher CSS
  assert(ReaderFormatting::linePercent(ReaderFormatting::choice(f.lineHeight, globalLine)) == -1);
  assert(f.indent == 1 && f.headingSize == 3); // independent controls
  f.indent = 0;
  assert(ReaderFormatting::indentPercent(ReaderFormatting::choice(f.indent, 3)) == -1);
  f.lineHeight = -1; // inherit global, which may NOT be publisher CSS
  assert(ReaderFormatting::choice(f.lineHeight, globalLine) == globalLine);
  assert(ReaderFormatting::linePercent(ReaderFormatting::choice(f.lineHeight, globalLine)) == 130);
}
