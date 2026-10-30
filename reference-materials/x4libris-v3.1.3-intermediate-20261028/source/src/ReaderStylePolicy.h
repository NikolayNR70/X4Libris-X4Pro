#pragma once
#include <cstdint>
// The legacy master flag is accepted for source compatibility, but never gates a control.
// Publisher CSS is always loaded; each explicit option changes only its own property.
struct ReaderStylePolicy {
  bool overrides;
  bool embedded(int8_t, uint8_t) const { return true; }
  bool option(int8_t book, uint8_t global) const { return book >= 0 ? book != 0 : global != 0; }
  uint8_t alignment(int8_t book, uint8_t global) const { return book >= 0 ? static_cast<uint8_t>(book) : global; }
  uint8_t images(int8_t book, uint8_t global) const { return book >= 0 ? static_cast<uint8_t>(book) : global; }
  bool extra(uint8_t value) const { return value != 0; }
  int16_t indent(uint8_t enabled, uint8_t value) const { return enabled ? (value > 250 ? 250 : value) : -1; }
  uint8_t gap(uint8_t value) const { return value > 150 ? 150 : value; }
  float scale(uint8_t value) const { return (value < 80 ? 80 : value > 160 ? 160 : value) / 100.0f; }
};
