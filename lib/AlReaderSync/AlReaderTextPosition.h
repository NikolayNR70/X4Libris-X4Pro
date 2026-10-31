#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace AlReaderSync {

// Streaming counter for the position unit used by AlReader XE 1.33: UTF-16
// code units appended to AlReader's main text partition. It accepts SAX-decoded
// UTF-8 (entities have already been expanded) and keeps state across chunks.
// Soft hyphens are discarded by AlReader's parser; repeated ordinary spaces
// collapse to one. Paragraph boundaries do not add a synthetic character.
class TextPositionCounter {
 public:
  uint32_t value() const { return position_; }

  // Returns the UTF-16 position before the code point at `offset`, then consumes
  // that UTF-8 code point. Call once at each leading byte; continuation bytes
  // must not be passed separately.
  uint32_t consume(const char* text, size_t length, size_t offset, size_t& byteCount);

 private:
  uint32_t position_ = 0;
  char16_t lastUnit_ = 0;
};

// AlReader prepends the FB2 book title to its reading-position stream.
uint32_t countTitlePositionUnits(std::string_view title);

}  // namespace AlReaderSync
