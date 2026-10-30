#pragma once

#include <HalStorage.h>

#include <string>

namespace FsHelpers {

inline constexpr char BOOK_CACHE_ROOT[] = "/.crosspoint/tmp";

inline bool prepareBookCachePath(const std::string& newPath) {
  if (!Storage.ensureDirectoryExists(BOOK_CACHE_ROOT)) return false;

  constexpr char prefix[] = "/.crosspoint/tmp/";
  constexpr size_t prefixLength = sizeof(prefix) - 1;
  if (newPath.compare(0, prefixLength, prefix) != 0 || Storage.exists(newPath.c_str())) return true;

  const std::string legacyPath = std::string("/.crosspoint/") + newPath.substr(prefixLength);
  if (!Storage.exists(legacyPath.c_str())) return true;

  // Move the whole cache so progress, bookmarks, sync IDs and rendered pages survive
  // the one-time relocation. Never replace anything already at the destination.
  if (Storage.renameNoReplace(legacyPath.c_str(), newPath.c_str())) return true;
  return Storage.exists(newPath.c_str());
}

}  // namespace FsHelpers
