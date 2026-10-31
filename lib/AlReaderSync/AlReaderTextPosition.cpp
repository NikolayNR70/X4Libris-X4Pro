#include "AlReaderTextPosition.h"

namespace AlReaderSync {
namespace {
bool continuation(const uint8_t byte) { return (byte & 0xC0u) == 0x80u; }

uint32_t decode(const uint8_t* s, const size_t length, size_t& bytes) {
  const uint8_t first = s[0];
  if (first < 0x80) {
    bytes = 1;
    return first;
  }
  if ((first & 0xE0u) == 0xC0u && length >= 2 && continuation(s[1])) {
    bytes = 2;
    return ((first & 0x1Fu) << 6) | (s[1] & 0x3Fu);
  }
  if ((first & 0xF0u) == 0xE0u && length >= 3 && continuation(s[1]) && continuation(s[2])) {
    bytes = 3;
    return ((first & 0x0Fu) << 12) | ((s[1] & 0x3Fu) << 6) | (s[2] & 0x3Fu);
  }
  if ((first & 0xF8u) == 0xF0u && length >= 4 && continuation(s[1]) && continuation(s[2]) && continuation(s[3])) {
    bytes = 4;
    return ((first & 0x07u) << 18) | ((s[1] & 0x3Fu) << 12) | ((s[2] & 0x3Fu) << 6) | (s[3] & 0x3Fu);
  }
  bytes = 1;
  return 0xFFFD;
}
}  // namespace

uint32_t TextPositionCounter::consume(const char* text, const size_t length, const size_t offset,
                                     size_t& byteCount) {
  const uint32_t before = position_;
  if (text == nullptr || offset >= length) {
    byteCount = 0;
    return before;
  }
  const auto* bytes = reinterpret_cast<const uint8_t*>(text + offset);
  const uint32_t cp = decode(bytes, length - offset, byteCount);
  if (byteCount == 0) return before;

  // APK 1.33's AlAXML.doTextChar drops U+00AD before appending to AlParText.
  if (cp == 0x00AD) return before;

  // The engine stores Java `char` values: BMP characters occupy one position;
  // supplementary code points occupy a surrogate pair and therefore two.
  const bool supplementary = cp > 0xFFFF && cp <= 0x10FFFF;
  const char16_t firstUnit = supplementary ? static_cast<char16_t>(0xD800 + ((cp - 0x10000) >> 10))
                                           : static_cast<char16_t>(cp <= 0xFFFF ? cp : 0xFFFD);
  // The inspected AlAXML path normalizes NBSP to its ordinary space handling before
  // appending to AlParText. Preserve one space across SAX chunk boundaries as it does
  // for adjacent whitespace in the parsed text stream.
  const bool collapsibleSpace = firstUnit == u' ' || firstUnit == 0x00A0 || firstUnit == 0x202F;
  const char16_t normalizedUnit = collapsibleSpace ? u' ' : firstUnit;
  if (!(collapsibleSpace && lastUnit_ == u' ')) {
    position_ += supplementary ? 2 : 1;
  }
  lastUnit_ = supplementary ? static_cast<char16_t>(0xDC00 + ((cp - 0x10000) & 0x3FF)) : normalizedUnit;
  return before;
}

uint32_t countTitlePositionUnits(const std::string_view title) {
  TextPositionCounter counter;
  for (size_t offset = 0; offset < title.size();) {
    size_t bytes = 0;
    counter.consume(title.data(), title.size(), offset, bytes);
    if (bytes == 0) break;
    offset += bytes;
  }
  return counter.value();
}

}  // namespace AlReaderSync
