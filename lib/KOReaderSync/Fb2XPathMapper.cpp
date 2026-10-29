#include "Fb2XPathMapper.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SaxParser/SaxParser.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

#include "ChapterXPathIndexerInternal.h"
#include "ChapterXPathIndexerState.h"

namespace Fb2XPathMapper {
namespace {
using namespace ChapterXPathIndexerInternal;

struct PathNode {
  std::string name;
  std::string path;
  std::string sourceXPath;
  std::unordered_map<std::string, uint16_t> children;
};

struct GeneratedPathState {
  std::vector<PathNode> path;
  std::string targetPath;
  std::string suffix;
  std::string resultXPath;
  bool found = false;

  static std::string pathFrom(const std::vector<PathNode>& nodes) {
    std::string out;
    for (const auto& node : nodes) out += node.path;
    return out;
  }

  static std::string sourceXPath(const char** attrs) {
    if (!attrs) return {};
    for (const char** attr = attrs; attr[0] && attr[1]; attr += 2) {
      if (std::strcmp(attr[0], "data-fb2-xpath") == 0) return attr[1];
    }
    return {};
  }

  void onStartElement(const char* rawName, const char** attrs) {
    if (found) return;
    const std::string name = rawName ? rawName : "";
    PathNode node;
    node.name = name;
    if (path.empty()) {
      node.path = "/" + name + "[1]";
    } else {
      const uint16_t index = ++path.back().children[name];
      node.path = "/" + name + "[" + std::to_string(index) + "]";
      node.sourceXPath = path.back().sourceXPath;
    }
    const std::string ownSource = sourceXPath(attrs);
    if (!ownSource.empty()) node.sourceXPath = ownSource;
    path.push_back(std::move(node));

    std::string currentPath = pathFrom(path);
    constexpr char kHtmlRoot[] = "/html[1]";
    if (currentPath.compare(0, sizeof(kHtmlRoot) - 1, kHtmlRoot) == 0) {
      currentPath.erase(0, sizeof(kHtmlRoot) - 1);
    }
    if (normalizeXPath(currentPath) == targetPath && !path.back().sourceXPath.empty()) {
      resultXPath = path.back().sourceXPath + suffix;
      found = true;
    }
  }

  void onEndElement() {
    if (!path.empty()) path.pop_back();
  }
};

void generatedStartCb(void* data, const char* name, const char** attrs) {
  static_cast<GeneratedPathState*>(data)->onStartElement(name, attrs);
}
void generatedEndCb(void* data, const char*) { static_cast<GeneratedPathState*>(data)->onEndElement(); }

bool parseGeneratedChapter(const std::string& path, GeneratedPathState& state) {
  HalFile file;
  if (!Storage.openFileForRead("KOSyncFB2Cache", path, file)) return false;
  SaxParser parser;
  if (!parser.init(&state, generatedStartCb, generatedEndCb, nullptr, nullptr, false, SaxParser::Profile::Full))
    return false;
  uint8_t buffer[1024];
  while (file.position() < file.size() && !state.found) {
    const int got = file.read(buffer, sizeof(buffer));
    if (got <= 0 || !parser.feed(buffer, static_cast<size_t>(got))) return false;
  }
  return state.found || parser.finalize();
}

bool mapGeneratedDocFragment(const Epub& epub, const std::string& xpath, std::string& sourceXPath) {
  constexpr char kPrefix[] = "/body/DocFragment[";
  if (xpath.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0) return false;
  const size_t indexStart = sizeof(kPrefix) - 1;
  const size_t indexEnd = xpath.find(']', indexStart);
  if (indexEnd == std::string::npos || indexEnd == indexStart) return false;
  char* end = nullptr;
  const long fragment = std::strtol(xpath.c_str() + indexStart, &end, 10);
  if (end != xpath.c_str() + indexEnd || fragment <= 0) return false;
  const size_t spine = static_cast<size_t>(fragment - 1);
  const auto& book = epub.getFb2ReaderBook();
  if (spine >= book.chapters.size()) return false;

  std::string remainder = xpath.substr(indexEnd + 1);
  if (remainder.compare(0, 6, "/body/") != 0) return false;
  const size_t textAt = remainder.rfind("/text()");
  std::string suffix;
  if (textAt != std::string::npos) {
    suffix = remainder.substr(textAt);
    remainder.erase(textAt);
  }

  GeneratedPathState state;
  state.targetPath = normalizeXPath(remainder);
  state.suffix = suffix;
  const std::string chapterPath = book.cacheDir + "/c" + std::to_string(spine) + ".xhtml";
  if (parseGeneratedChapter(chapterPath, state) && state.found) {
    sourceXPath = std::move(state.resultXPath);
    return !sourceXPath.empty();
  }

  return false;
}

bool mapDocFragmentToNativePath(const Epub& epub, const std::string& xpath, std::string& sourceXPath) {
  constexpr char kPrefix[] = "/body/DocFragment[";
  constexpr char kChapterBodyPrefix[] = "/body/div[1]";
  if (xpath.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0) return false;
  const size_t indexStart = sizeof(kPrefix) - 1;
  const size_t indexEnd = xpath.find(']', indexStart);
  if (indexEnd == std::string::npos || indexEnd == indexStart) return false;
  char* end = nullptr;
  const long fragment = std::strtol(xpath.c_str() + indexStart, &end, 10);
  if (end != xpath.c_str() + indexEnd || fragment <= 0) return false;
  const size_t spine = static_cast<size_t>(fragment - 1);
  const auto& chapters = epub.getFb2ReaderBook().chapters;
  if (spine >= chapters.size()) return false;

  const std::string remainder = xpath.substr(indexEnd + 1);
  if (remainder.compare(0, sizeof(kChapterBodyPrefix) - 1, kChapterBodyPrefix) != 0 ||
      remainder.size() <= sizeof(kChapterBodyPrefix) - 1 ||
      remainder[sizeof(kChapterBodyPrefix) - 1] != '/') {
    return false;
  }
  const std::string& sourceRoot = chapters[spine].sourceXPath;
  if (sourceRoot.empty()) return false;
  sourceXPath = sourceRoot + remainder.substr(sizeof(kChapterBodyPrefix) - 1);
  return true;
}

std::string originalSourcePath(const Epub& epub) {
  const std::string& path = epub.getPath();
  if (FsHelpers::hasFb2ZipExtension(std::string_view(path))) {
    return epub.getFb2ReaderBook().cacheDir + "/archive-source.fb2";
  }
  return path;
}

struct MapState : StackState {
  const std::vector<Fb2Converter::ReaderChapter>& chapters;
  int bodyDepth = 0;
  int sectionDepth = 0;
  int chapter = -1;
  int nextChapter = 0;
  std::vector<PathNode> path;

  int wantedSpine = -1;
  uint32_t wantedOffset = 0;
  std::string wantedRoot;
  std::string targetParent;
  std::string wantedParent;
  int wantedTextNode = 0;
  size_t wantedChar = 0;
  bool reverse = false;
  bool targetIsText = false;
  bool insideTarget = false;
  bool found = false;
  bool exact = false;
  uint32_t resultOffset = 0;
  std::string resultXPath;

  explicit MapState(const std::vector<Fb2Converter::ReaderChapter>& c) : chapters(c) { path.reserve(32); }

  static std::string pathFrom(const std::vector<PathNode>& p) {
    std::string out;
    for (const auto& n : p) out += n.path;
    return out;
  }

  void setTargetPath(const std::string& xpath) {
    reverse = true;
    targetParent = xpath;
    const std::string textPart = "/text()";
    const size_t textAt = targetParent.rfind(textPart);
    if (textAt != std::string::npos) {
      size_t cursor = textAt + textPart.size();
      wantedTextNode = 1;
      if (cursor < targetParent.size() && targetParent[cursor] == '[') {
        const size_t close = targetParent.find(']', cursor);
        if (close != std::string::npos) {
          wantedTextNode = std::max(1, atoi(targetParent.substr(cursor + 1, close - cursor - 1).c_str()));
          cursor = close + 1;
        }
      }
      if (cursor < targetParent.size() && targetParent[cursor] == '.') {
        ++cursor;
        wantedChar = static_cast<size_t>(strtoul(targetParent.c_str() + cursor, nullptr, 10));
      }
      targetParent = normalizeXPath(targetParent.substr(0, textAt));
      targetIsText = true;
    } else {
      targetParent = normalizeXPath(targetParent);
    }

    // Select the converted chapter whose original FB2 root is the deepest
    // ancestor of the requested KOReader locator.
    for (size_t i = 0; i < chapters.size(); ++i) {
      const std::string root = normalizeXPath(chapters[i].sourceXPath);
      if (!root.empty() &&
          (targetParent == root ||
           (targetParent.size() > root.size() && targetParent.compare(0, root.size(), root) == 0 &&
            targetParent[root.size()] == '/')) &&
          root.size() > wantedRoot.size()) {
        wantedRoot = root;
        wantedSpine = static_cast<int>(i);
      }
    }
  }

  void onStartElement(const char* rawName) {
    const std::string name = rawName ? rawName : "";
    if (name == "body") {
      bodyDepth = static_cast<int>(path.size()) + 1;
      sectionDepth = 0;
    } else if (bodyDepth > 0) {
      if (name == "section") {
        if (sectionDepth == 0) {
          chapter = nextChapter++;
          totalTextBytes = 0;
          insideTarget = false;
        }
        ++sectionDepth;
      }
      if (chapter < 0) {
        chapter = nextChapter++;
        totalTextBytes = 0;
        insideTarget = false;
      }
    }

    PathNode node;
    node.name = name;
    if (path.empty()) {
      node.path = "/" + name + "[1]";
    } else {
      const uint16_t index = ++path.back().children[name];
      node.path = "/" + name + "[" + std::to_string(index) + "]";
    }
    path.push_back(std::move(node));
    pushElement(rawName);

    const std::string currentPath = normalizeXPath(pathFrom(path));
    if (!wantedRoot.empty() && chapter == wantedSpine && currentPath == wantedRoot) insideTarget = true;
    if (reverse && insideTarget && currentPath == targetParent && !targetIsText) {
      resultOffset = static_cast<uint32_t>(totalTextBytes);
      exact = true;
      found = true;
    }
  }

  void onEndElement() {
    if (path.empty()) return;
    const std::string current = normalizeXPath(pathFrom(path));
    const bool closingChapterRoot =
        insideTarget && chapter >= 0 && chapter < static_cast<int>(chapters.size()) && current == wantedRoot;
    if (closingChapterRoot) insideTarget = false;
    if (path.back().name == "section") {
      --sectionDepth;
      if (sectionDepth == 0) {
        chapter = -1;
        insideTarget = false;
      }
    }
    if (path.back().name == "body") {
      bodyDepth = 0;
      sectionDepth = 0;
      chapter = -1;
      insideTarget = false;
    }
    popElement();
    path.pop_back();
  }

  void onCharData(const char* text, int len) {
    if (found || shouldSkipText(len) || !insideTarget || stack.empty()) return;
    const TextRun run = onTextChunk(text, len);
    const size_t visible = countVisibleBytes(text, len);

    if (reverse) {
      if (targetIsText && normalizeXPath(pathFrom(path)) == targetParent && run.counts &&
          run.nodeIndex == wantedTextNode) {
        bool carry = run.spaceBefore;
        const size_t cps = collapsedCodepoints(text, len, preDepth == 0, carry);
        if (wantedChar >= run.codepointsBefore && wantedChar <= run.codepointsBefore + cps) {
          resultOffset = static_cast<uint32_t>(
              totalTextBytes + visibleBytesBeforeCollapsedCodepoint(text, len, wantedChar - run.codepointsBefore,
                                                                    preDepth == 0, run.spaceBefore));
          exact = true;
          found = true;
        }
      }
      totalTextBytes += visible;
      return;
    }

    if (visible > 0 && totalTextBytes + visible > wantedOffset) {
      const size_t byteInChunk = wantedOffset - totalTextBytes;
      const size_t cpInChunk = collapsedCodepointAtVisibleByte(text, len, byteInChunk, preDepth == 0, run.spaceBefore);
      resultXPath = pathFrom(path) + "/text()[" + std::to_string(run.nodeIndex) + "]." +
                    std::to_string(run.codepointsBefore + cpInChunk);
      found = true;
      return;
    }
    totalTextBytes += visible;
  }
};

void startCb(void* data, const char* name, const char**) { static_cast<MapState*>(data)->onStartElement(name); }
void endCb(void* data, const char*) { static_cast<MapState*>(data)->onEndElement(); }
void charCb(void* data, const char* text, int len) { static_cast<MapState*>(data)->onCharData(text, len); }
void defaultCb(void* data, const char* text, int len) {
  if (isEntityRef(text, len)) static_cast<MapState*>(data)->onCharData(text, len);
}

// Decode CP1251 before handing the source to the strict UTF-8 SAX parser. The
// conversion mirrors Fb2Converter so XPath character offsets use the same text.
constexpr uint16_t kCp1251[64] = {
    0x402, 0x403,  0x201a, 0x453, 0x201e, 0x2026, 0x2020, 0x2021, 0x20ac, 0x2030, 0x409,  0x2039, 0x40a,
    0x40c, 0x40b,  0x40f,  0x452, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0xfffd, 0x2122,
    0x459, 0x203a, 0x45a,  0x45c, 0x45b,  0x45f,  0xa0,   0x40e,  0x45e,  0x408,  0xa4,   0x490,  0xa6,
    0xa7,  0x401,  0xa9,   0x404, 0xab,   0xac,   0xad,   0xae,   0x407,  0xb0,   0xb1,   0x406,  0x456,
    0x491, 0xb5,   0xb6,   0x451, 0x2116, 0x454,  0xbb,   0x458,  0x405,  0x455,  0x457};

bool parseSource(const std::string& source, MapState& state) {
  HalFile file;
  if (!Storage.openFileForRead("KOSyncFB2", source, file)) return false;
  uint8_t head[512];
  const int headLen = file.read(head, sizeof(head));
  if (headLen <= 0) return false;
  std::string header(reinterpret_cast<char*>(head), static_cast<size_t>(headLen));
  std::string lower = header;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  bool cp1251 = lower.find("windows-1251") != std::string::npos || lower.find("cp1251") != std::string::npos;
  size_t offset = header.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
  if (header.compare(offset, 5, "<?xml") == 0) {
    const size_t end = header.find("?>", offset);
    if (end == std::string::npos) return false;
    offset = end + 2;
  }
  if (!file.seek(offset)) return false;

  SaxParser parser;
  if (!parser.init(&state, startCb, endCb, charCb, defaultCb, false, SaxParser::Profile::Full)) return false;
  std::unique_ptr<uint8_t[]> raw(new (std::nothrow) uint8_t[2048]);
  std::unique_ptr<uint8_t[]> utf(new (std::nothrow) uint8_t[6144]);
  if (!raw || !utf) return false;
  while (file.position() < file.size() && !state.found) {
    const int got = file.read(raw.get(), 1024);
    if (got <= 0) return false;
    if (!cp1251) {
      if (!parser.feed(raw.get(), static_cast<size_t>(got))) return false;
      continue;
    }
    size_t out = 0;
    for (int i = 0; i < got; ++i) {
      const uint8_t c = raw[i];
      const uint32_t cp = c < 128 ? c : c >= 192 ? 0x410 + c - 192 : kCp1251[c - 128];
      if (cp < 128)
        utf[out++] = static_cast<uint8_t>(cp);
      else if (cp < 2048) {
        utf[out++] = static_cast<uint8_t>(0xc0 | (cp >> 6));
        utf[out++] = static_cast<uint8_t>(0x80 | (cp & 63));
      } else {
        utf[out++] = static_cast<uint8_t>(0xe0 | (cp >> 12));
        utf[out++] = static_cast<uint8_t>(0x80 | ((cp >> 6) & 63));
        utf[out++] = static_cast<uint8_t>(0x80 | (cp & 63));
      }
    }
    if (!parser.feed(utf.get(), out)) return false;
  }
  return state.found || parser.finalize();
}

}  // namespace

std::string findXPathForVisibleOffset(const Epub& epub, const int spineIndex, const uint32_t visibleOffset) {
  const auto& chapters = epub.getFb2ReaderBook().chapters;
  if (!epub.isDirectFb2() || spineIndex < 0 || spineIndex >= static_cast<int>(chapters.size())) return {};
  MapState state(chapters);
  state.wantedSpine = spineIndex;
  state.wantedOffset = visibleOffset;
  state.wantedRoot = normalizeXPath(chapters[spineIndex].sourceXPath);
  if (state.wantedRoot.empty() || !parseSource(originalSourcePath(epub), state)) return {};
  return state.resultXPath;
}

bool resolveXPath(const Epub& epub, const std::string& xpath, int& spineIndex, uint32_t& visibleOffset,
                  bool& exactMatch) {
  spineIndex = -1;
  visibleOffset = 0;
  exactMatch = false;
  if (!epub.isDirectFb2() || xpath.empty()) return false;
  const auto resolveSourcePath = [&](const std::string& sourcePath) {
    MapState state(epub.getFb2ReaderBook().chapters);
    state.setTargetPath(sourcePath);
    if (state.wantedSpine < 0 || !parseSource(originalSourcePath(epub), state) || !state.found) return false;
    spineIndex = state.wantedSpine;
    visibleOffset = state.resultOffset;
    exactMatch = state.exact;
    return true;
  };

  if (xpath.rfind("/body/DocFragment[", 0) == 0) {
    std::string sourcePath;
    if (mapGeneratedDocFragment(epub, xpath, sourcePath) && resolveSourcePath(sourcePath)) return true;

    // A foreign converter may omit XHTML wrappers that ours adds. Its p[n] can then
    // correspond to a different generated p[n] locally. Retry against the source section
    // encoded by DocFragment only after the generated-path mapping did not resolve.
    if (mapDocFragmentToNativePath(epub, xpath, sourcePath)) {
      LOG_DBG("ProgressMapper", "DocFragment XHTML path did not resolve; trying source XPath %s",
              sourcePath.c_str());
      return resolveSourcePath(sourcePath);
    }
    return false;
  }

  return resolveSourcePath(xpath);
}

}  // namespace Fb2XPathMapper
