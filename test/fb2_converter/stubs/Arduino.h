#pragma once
#include <cstdint>
#include <string>
struct String : std::string {
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  size_t length() const { return size(); }
};
inline void delay(int) {}
inline uint32_t millis() { return 0; }
