#pragma once

#include <cstddef>
#include <string>

/** Append concise weather/network diagnostics to a visible text file on the SD card. */
class WeatherDiagnosticsLog {
 public:
  static constexpr const char* FILE_PATH = "/weather_diagnostics.log";
  static constexpr size_t MAX_FILE_BYTES = 64 * 1024;

  static bool append(const char* event, const std::string& details);
};
