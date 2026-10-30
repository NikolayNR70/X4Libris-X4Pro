#pragma once
#include <cstdint>

// Per-property selection: -1 follows the global profile, 0 keeps publisher CSS,
// positive choices are explicit reader values. Never infer intent from a value
// such as zero indentation: zero is a valid, explicit override.
struct ReaderFormatting {
  int8_t lineHeight = -1;
  int8_t indent = -1;
  int8_t gap = -1;
  int8_t headingAlign = -1;
  int8_t headingSize = -1;
  int8_t margin = -1;
  static uint8_t choice(int8_t book, uint8_t global) { return book < 0 ? global : static_cast<uint8_t>(book); }
  static int16_t linePercent(uint8_t choice) { return choice ? 90 + 10 * choice : -1; }
  static int16_t indentPercent(uint8_t choice) { return choice ? 25 * (choice - 1) : -1; }
  static int16_t gapPercent(uint8_t choice) { return choice ? 25 * (choice - 1) : -1; }
  static int16_t headingPercent(uint8_t choice) { return choice ? 60 + 20 * choice : -1; }
  static uint8_t marginPixels(uint8_t choice) { return choice ? 5 * (choice - 1) : 5; }
  bool operator==(const ReaderFormatting& b) const {
    return lineHeight == b.lineHeight && indent == b.indent && gap == b.gap &&
           headingAlign == b.headingAlign && headingSize == b.headingSize && margin == b.margin;
  }
};
