#include "WeatherDiagnosticsLog.h"

#include <HalStorage.h>

#include <ctime>

namespace {
constexpr char ROTATED_FILE_PATH[] = "/weather_diagnostics.previous.log";
constexpr char MODULE[] = "WEA";

bool openAppendFile(HalFile& file, uint64_t& currentSize) {
  if (Storage.exists(WeatherDiagnosticsLog::FILE_PATH)) {
    if (!Storage.openFileForUpdate(MODULE, WeatherDiagnosticsLog::FILE_PATH, file)) return false;
    currentSize = file.size64();
    return file.seek64(currentSize);
  }

  currentSize = 0;
  return Storage.openFileForWrite(MODULE, WeatherDiagnosticsLog::FILE_PATH, file);
}
}  // namespace

bool WeatherDiagnosticsLog::append(const char* event, const std::string& details) {
  if (!Storage.ready()) return false;

  const char* safeEvent = event ? event : "event";
  time_t now = 0;
  time(&now);
  std::string line = "[unix=" + std::to_string(static_cast<long long>(now)) + "] [WEATHER] [";
  line += safeEvent;
  line += "] ";
  line += details;
  line += '\n';

  if (line.size() > MAX_FILE_BYTES) return false;

  HalFile file;
  uint64_t currentSize = 0;
  if (!openAppendFile(file, currentSize)) return false;

  if (currentSize + line.size() > MAX_FILE_BYTES) {
    if (!file.close()) return false;
    Storage.remove(ROTATED_FILE_PATH);
    if (!Storage.rename(FILE_PATH, ROTATED_FILE_PATH)) return false;
    if (!Storage.openFileForWrite(MODULE, FILE_PATH, file)) return false;
  }

  const size_t written = file.write(reinterpret_cast<const uint8_t*>(line.data()), line.size());
  file.flush();
  const bool closed = file.close();
  return written == line.size() && closed;
}
