#include "KOReaderCachePath.h"

#include <cstdio>
#include <string>

namespace {
bool hasPrefix(const std::string& path, const char* prefix) { return path.find(prefix) != std::string::npos; }
}  // namespace

int main() {
  if (!hasPrefix(KOReaderCachePath::directory("/Books/story.fb2"), "/.crosspoint/tmp/fb2_")) return 1;
  for (const char* path : {"/Books/story.FB2.ZIP", "/Books/story.FBZ", "/Books/story.zip"}) {
    if (!KOReaderCachePath::isFb2Book(path) ||
        !hasPrefix(KOReaderCachePath::directory(path), "/.crosspoint/tmp/fb2_"))
      return 2;
  }
  if (!hasPrefix(KOReaderCachePath::directory("/Books/story.epub"), "/.crosspoint/tmp/epub_")) return 3;
  if (!hasPrefix(KOReaderCachePath::directory("/Books/story.fb2", true), "/.crosspoint/tmp/epub_")) return 4;
  std::puts("KOReader cache directories: FB2/FB2.ZIP -> fb2_, EPUB -> epub_, legacy read path preserved");
  return 0;
}
