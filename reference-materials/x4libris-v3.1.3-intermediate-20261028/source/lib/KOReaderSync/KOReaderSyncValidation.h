#pragma once

#include <cmath>
#include <string_view>

namespace KOReaderSyncValidation {

inline bool isValidDocumentId(const std::string_view document) {
  if (document.size() != 32) return false;
  for (const char ch : document) {
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) return false;
  }
  return true;
}

inline bool isValidPosition(const char* xpath, const float percentage) {
  return xpath != nullptr && xpath[0] != '\0' && std::isfinite(percentage) && percentage >= 0.0f &&
         percentage <= 1.0f;
}

}  // namespace KOReaderSyncValidation
