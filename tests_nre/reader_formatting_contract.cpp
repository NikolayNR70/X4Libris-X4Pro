#include <cassert>
#include "../src/ReaderFormatting.h"
int main() {
  ReaderFormatting format;
  assert(format.lineHeight == -1);
  assert(ReaderFormatting::choice(-1, 3) == 3);
  assert(ReaderFormatting::choice(0, 3) == 0);
  assert(ReaderFormatting::choice(1, 3) == 1);
  assert(ReaderFormatting::linePercent(0) == -1);
  assert(ReaderFormatting::linePercent(3) == 120);
  assert(ReaderFormatting::indentPercent(0) == -1);
  assert(ReaderFormatting::indentPercent(1) == 0);
  assert(ReaderFormatting::headingPercent(0) == -1);
  assert(ReaderFormatting::gapPercent(1) == 0);
  assert(ReaderFormatting::marginPixels(0) == 5);
}
