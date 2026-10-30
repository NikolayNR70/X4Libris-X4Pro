#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace FsHelpers {

inline constexpr char BOOK_CACHE_ROOT[] = "/.x4libris/tmp";
inline constexpr char UNPACK_CACHE_ROOT[] = "/.x4libris/unpack";

// Keep per-book folders out of one FAT directory. Library indexing may create one cache folder
// per book; a flat directory makes every later cache lookup scan an ever-growing list of entries.
inline std::string bookCachePath(const std::string& leaf, const uint32_t shardKey) {
  char shard[3];
  std::snprintf(shard, sizeof(shard), "%02x", static_cast<unsigned>(shardKey & 0xffu));
  return std::string(BOOK_CACHE_ROOT) + "/" + shard + "/" + leaf;
}

inline std::string unpackCachePath(const std::string& leaf, const uint32_t shardKey) {
  char shard[3];
  std::snprintf(shard, sizeof(shard), "%02x", static_cast<unsigned>(shardKey & 0xffu));
  return std::string(UNPACK_CACHE_ROOT) + "/" + shard + "/" + leaf;
}

inline bool prepareBookCachePath(const std::string& newPath) {
  const std::string cacheRoot = newPath.compare(0, sizeof(UNPACK_CACHE_ROOT) - 1, UNPACK_CACHE_ROOT) == 0
                                    ? UNPACK_CACHE_ROOT
                                    : BOOK_CACHE_ROOT;
  if (!Storage.ensureDirectoryExists(cacheRoot.c_str())) return false;

  const std::string prefix = cacheRoot + "/";
  const size_t prefixLength = prefix.size();
  if (newPath.compare(0, prefixLength, prefix) != 0) return true;

  // Layouts: /.x4libris/tmp/<bucket>/<book> and /.x4libris/unpack/<bucket>/<book>.
  // mkdir() is one level only, so create each level in order.
  const size_t bucketEnd = newPath.find('/', prefixLength);
  if (bucketEnd == std::string::npos || bucketEnd == prefixLength || bucketEnd + 1 >= newPath.size()) return true;
  const std::string bucketPath = newPath.substr(0, bucketEnd);
  if (!Storage.ensureDirectoryExists(bucketPath.c_str())) return false;
  return true;
}

}  // namespace FsHelpers
