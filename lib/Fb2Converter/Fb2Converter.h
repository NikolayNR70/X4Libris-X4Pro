#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Fb2Converter {
struct ReaderChapter {
  std::string title;
  bool note = false;
  // Absolute element XPath in the original FictionBook DOM. KOReader opens FB2
  // as /FictionBook, not as the temporary EPUB DocFragment tree.
  std::string sourceXPath;
};

struct ReaderImage {
  std::string id;
  std::string type;
  std::string cacheName;
};

struct ReaderBook {
  std::string cacheDir;
  std::string title;
  std::string author;
  std::string language;
  std::string cover;
  uint32_t sourceSize = 0;
  uint32_t sourceCrc = 0;
  std::vector<ReaderChapter> chapters;
  std::vector<ReaderImage> images;
};

// Reads only FictionBook title, first author, and first title-info sequence. A ZIP/FBZ source must
// contain exactly one .fb2 entry. Does not create the reader's rendered cache.
bool readMetadata(const std::string& source, std::string& title, std::string& author, std::string& series,
                  std::string& seriesIndex, std::string& error);

// Parses the original FB2 and writes only private renderer cache files (XHTML
// sections, images, stylesheet and index) under /.x4libris. It never creates
// a sibling EPUB or renames/hides the source book.
bool prepareReaderCache(const std::string& source, ReaderBook& book, std::string& error);

// Publishes a validated sibling EPUB without replacing existing files.
// On success, hides the original by a reversible dot-prefix rename when available.
bool convert(const std::string& source, std::string& epub, std::string& error);
std::string sourcePath(const std::string& epub);
}  // namespace Fb2Converter
