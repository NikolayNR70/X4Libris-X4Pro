#pragma once

#include <FsHelpers.h>

#include <functional>
#include <string>

namespace KOReaderCachePath {

inline bool isFb2Book(const std::string& filePath) {
  return FsHelpers::hasFb2Extension(std::string_view(filePath));
}

inline std::string directory(const std::string& filePath, bool legacyEpubPrefix = false) {
  const bool fb2 = isFb2Book(filePath);
  const char* prefix = fb2 && !legacyEpubPrefix ? "fb2_" : "epub_";
  return std::string("/.crosspoint/tmp/") + prefix + std::to_string(std::hash<std::string>{}(filePath));
}

}  // namespace KOReaderCachePath
