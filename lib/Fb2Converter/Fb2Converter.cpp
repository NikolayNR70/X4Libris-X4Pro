#include "Fb2Converter.h"

#include <Arduino.h>
#include <BookCachePath.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SaxParser/SaxParser.h>
#include <ZipFile.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <unordered_map>
#include <vector>

namespace {
// FB2 describes semantic roles rather than a fixed page layout. Keep those
// roles in XHTML and supply defaults through the normal EPUB cascade.
constexpr const char* FB2_CSS = R"CSS(
body { margin: 0; }
p { text-indent: 1.2em; margin-top: 0; margin-bottom: 0; }
.fb2-title { font-weight: bold; text-align: center;
  text-indent: 0; margin-top: 1em; margin-bottom: 0.8em; hyphens: none; }
.fb2-title p { text-align: center; text-indent: 0; margin-top: 0; margin-bottom: 0; hyphens: none; }
.fb2-subtitle { font-weight: bold; text-align: center; text-indent: 0;
  margin-top: 0.8em; margin-bottom: 0.5em; hyphens: none; }
.fb2-heading-2 { font-weight: bold; text-align: center; text-indent: 0;
  margin-top: 0.6em; margin-bottom: 0.3em; hyphens: none; }
.fb2-cite { margin-left: 1em; margin-right: 1em; margin-top: 0.5em; margin-bottom: 0.5em; }
.fb2-epigraph { font-style: italic; margin-left: 2em; margin-top: 0.5em; margin-bottom: 0.8em; }
.fb2-text-author, .fb2-date { text-align: right; text-indent: 0; }
.fb2-poem { margin-left: 1em; margin-top: 0.5em; margin-bottom: 0.5em; }
.fb2-stanza { margin-top: 0.5em; margin-bottom: 0.5em; }
.fb2-verse { text-indent: 0; text-align: left; }
.fb2-empty-line { text-indent: 0; margin-top: 0; margin-bottom: 0; }
)CSS";
constexpr size_t MAX_CHAPTERS = 512, MAX_IDS = 4096, MAX_IMAGES = 256;
constexpr uint32_t CRC32_NIBBLE_TABLE[16] = {
    0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu, 0x76dc4190u, 0x6b6b51f4u, 0x4db26158u, 0x5005713cu,
    0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu, 0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu};
uint32_t crcStep(uint32_t crc, const uint8_t* p, size_t n) {
  while (n--) {
    crc ^= *p++;
    crc = (crc >> 4) ^ CRC32_NIBBLE_TABLE[crc & 0x0fu];
    crc = (crc >> 4) ^ CRC32_NIBBLE_TABLE[crc & 0x0fu];
  }
  return crc;
}
std::string escape(const std::string& s) {
  std::string r;
  for (char c : s) switch (c) {
      case '&':
        r += "&amp;";
        break;
      case '<':
        r += "&lt;";
        break;
      case '>':
        r += "&gt;";
        break;
      case '"':
        r += "&quot;";
        break;
      default:
        r += c;
    }
  return r;
}
std::string local(const char* s) {
  const char* c = strrchr(s, ':');
  return c ? c + 1 : s;
}
std::string attr(const char** a, const char* key) {
  for (; a && *a; a += 2)
    if (local(a[0]) == key) return a[1];
  return {};
}
std::string chapterName(size_t n) { return "c" + std::to_string(n) + ".xhtml"; }
std::string imageName(size_t n, const std::string& type) {
  const char* extension = type == "image/png"    ? ".png"
                          : type == "image/jpeg" ? ".jpg"
                          : type == "image/gif"  ? ".gif"
                                                 : "";
  return "i" + std::to_string(n) + extension;
}
bool endsWithIgnoreCase(std::string_view path, std::string_view suffix) {
  if (path.size() < suffix.size()) return false;
  path.remove_prefix(path.size() - suffix.size());
  for (size_t i = 0; i < suffix.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(path[i])) != suffix[i]) return false;
  return true;
}
struct Chapter {
  std::string title;
  bool note = false;
  std::string sourceXPath;
};
struct Anchor {
  std::string id;
  size_t chapter;
};
struct Image {
  std::string id, type, cacheName;
};

// Small buffered writer: character callbacks must not turn an SD write into one byte per call.
struct Writer {
  HalFile f;
  uint8_t data[2048];
  size_t used = 0;
  bool ok = true;
  bool open(const std::string& path) {
    used = 0;
    ok = Storage.openFileForWrite("FB2", path, f);
    return ok;
  }
  void flush() {
    if (used && f.write(data, used) != used) ok = false;
    used = 0;
  }
  void bytes(const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    while (n && ok) {
      size_t k = std::min(n, sizeof(data) - used);
      memcpy(data + used, b, k);
      used += k;
      b += k;
      n -= k;
      if (used == sizeof(data)) flush();
    }
  }
  void put(const std::string& s) { bytes(s.data(), s.size()); }
  bool close() {
    flush();
    if (!f.close()) ok = false;
    return ok;
  }
};

struct Converter {
  std::string dir, error, title, author, series, seriesIndex, language = "ru", cover, embeddedCss;
  std::vector<Chapter> chapters;
  std::vector<Anchor> anchors;
  std::vector<Image> images;
  struct Frame {
    std::string name, tag;
    std::string sourceXPath;
    std::string sourceClass, sourceStyle;
    std::unordered_map<std::string, uint16_t> childCounts;
    bool title = false;
    uint8_t headingLevel = 0;
    bool deferredParagraph = false;
    std::string paragraphId;
  };
  std::vector<Frame> stack;
  std::vector<bool> sectionPreamble;
  Writer out;
  size_t chapter = 0, nextChapter = 0;
  int bodyDepth = 0, sectionDepth = 0, titleInfoDepth = 0, binary = -1;
  int metadataAuthorDepth = 0;
  bool render = false, metadataOnly = false, metadataComplete = false, active = false, root = false, note = false, coverPage = false,
       capturingStylesheet = false;
  unsigned b64 = 0;
  int bits = 0, padding = 0;
  size_t binaryBytes = 0;
  std::string capture, captured, metadataName, metadataNickname;

  void fail(const char* e) {
    if (error.empty()) error = e;
  }
  void text(const std::string& s) {
    if (render && active) out.put(s);
  }
  void openDeferredParagraph(bool heading) {
    if (stack.empty() || !stack.back().deferredParagraph) return;
    Frame& paragraph = stack.back();
    paragraph.deferredParagraph = false;
    paragraph.tag = "p";
    std::string classes = heading ? "fb2-heading-2 heading-2" : "";
    if (!paragraph.sourceClass.empty()) {
      if (!classes.empty()) classes += ' ';
      classes += paragraph.sourceClass;
    }
    const std::string classAttribute = classes.empty() ? "" : " class=\"" + escape(classes) + "\"";
    const std::string styleAttribute =
        paragraph.sourceStyle.empty() ? "" : " style=\"" + escape(paragraph.sourceStyle) + "\"";
    if (heading) {
      text("<p" + (paragraph.paragraphId.empty() ? std::string() : " id=\"" + escape(paragraph.paragraphId) + "\"") +
           " data-fb2-xpath=\"" + escape(paragraph.sourceXPath) + "\"" + classAttribute + styleAttribute + ">");
    } else {
      if (!sectionPreamble.empty()) sectionPreamble.back() = false;
      text("<p" + (paragraph.paragraphId.empty() ? std::string() : " id=\"" + escape(paragraph.paragraphId) + "\"") +
           " data-fb2-xpath=\"" + escape(paragraph.sourceXPath) + "\"" + classAttribute + styleAttribute + ">");
    }
  }
  void openChapter() {
    if (active) return;
    if (nextChapter >= MAX_CHAPTERS) {
      fail("Too many FB2 chapters (limit 512)");
      return;
    }
    chapter = nextChapter++;
    if (!render)
      chapters.push_back({"Chapter " + std::to_string(chapter + 1), note});
    else {
      if (!out.open(dir + "/" + chapterName(chapter))) {
        fail("Cannot write chapter to SD card");
        return;
      }
      out.put(
          "<?xml version=\"1.0\" encoding=\"utf-8\"?><html xmlns=\"http://www.w3.org/1999/xhtml\" "
          "xmlns:epub=\"http://www.idpf.org/2007/ops\"><head><title>" +
          escape(chapters[chapter].title) +
          "</title><link rel=\"stylesheet\" type=\"text/css\" href=\"fb2.css\"/></head><body>");
    }
    active = true;
  }
  void closeChapter() {
    if (!active) return;
    if (render) {
      out.put("</body></html>");
      if (!out.close()) fail("SD card chapter write failed");
    }
    active = false;
  }
  int imageIndex(const std::string& id) const {
    for (size_t i = 0; i < images.size(); ++i)
      if (images[i].id == id) return static_cast<int>(i);
    return -1;
  }
  std::string href(const std::string& ref) {
    if (ref.empty() || ref[0] != '#') return {};
    const std::string id = ref.substr(1);
    for (const auto& a : anchors)
      if (a.id == id) return chapterName(a.chapter) + ref;
    return ref;
  }
  static void startCb(void* p, const char* name, const char** a) { static_cast<Converter*>(p)->start(local(name), a); }
  static void endCb(void* p, const char*) { static_cast<Converter*>(p)->end(); }
  static void charCb(void* p, const char* s, int n) { static_cast<Converter*>(p)->chars(s, n); }
  void start(const std::string& n, const char** a) {
    if (!error.empty()) return;
    if (metadataOnly) {
      if (stack.empty()) {
        root = n == "FictionBook";
        if (!root) fail("This is not a FictionBook document");
      }
      Frame f;
      f.name = n;
      if (n == "title-info") titleInfoDepth = static_cast<int>(stack.size()) + 1;
      if (titleInfoDepth && n == "sequence" && series.empty()) {
        series = attr(a, "name");
        seriesIndex = attr(a, "number");
      }
      if (titleInfoDepth && n == "author" && metadataAuthorDepth == 0 && author.empty()) {
        metadataAuthorDepth = static_cast<int>(stack.size()) + 1;
        metadataName.clear();
        metadataNickname.clear();
      }
      if (titleInfoDepth &&
          (n == "book-title" || n == "lang" ||
           (metadataAuthorDepth &&
            (n == "first-name" || n == "middle-name" || n == "last-name" || n == "nickname")))) {
        capture = n;
        captured.clear();
      }
      stack.push_back(std::move(f));
      return;
    }
    if (!stack.empty() && stack.back().deferredParagraph) {
      // A number of FB2 books encode chapter subtitles as consecutive paragraphs
      // whose only child is <strong>. Defer their opening tag until the first child
      // so those title lines can use the reader's level-2 heading controls.
      openDeferredParagraph(n == "strong");
    }
    if (stack.empty()) {
      root = n == "FictionBook";
      if (!root) fail("This is not a FictionBook document");
    }
    Frame f;
    f.name = n;
    const bool needXPath = render || n == "FictionBook" || n == "body" ||
                           (bodyDepth && (!active || (n == "section" && sectionDepth == 0)));
    if (stack.empty()) {
      if (needXPath) f.sourceXPath = "/FictionBook[1]";
    } else {
      const uint16_t sibling = ++stack.back().childCounts[n];
      if (needXPath) f.sourceXPath = stack.back().sourceXPath + "/" + n + "[" + std::to_string(sibling) + "]";
    }
    if (render) {
      if (n == "p") f.paragraphId = attr(a, "id");
      f.sourceClass = attr(a, "class");
      f.sourceStyle = attr(a, "style");
    }
    if (n == "title-info") titleInfoDepth = static_cast<int>(stack.size()) + 1;
    if (n == "stylesheet" && !render) {
      capturingStylesheet = true;
      embeddedCss.clear();
    }
    if (titleInfoDepth &&
        (n == "book-title" || n == "first-name" || n == "middle-name" || n == "last-name" || n == "lang")) {
      capture = n;
      captured.clear();
    }
    if (n == "coverpage") coverPage = true;
    if (coverPage && n == "image") {
      auto h = attr(a, "href");
      if (!h.empty() && h[0] == '#') cover = h.substr(1);
    }
    if (n == "body") {
      bodyDepth = static_cast<int>(stack.size()) + 1;
      note = attr(a, "name") == "notes" || attr(a, "name") == "comments";
      sectionDepth = 0;
    }
    if (n == "binary") {
      const auto id = attr(a, "id"), type = attr(a, "content-type");
      if (!render) {
        if (id.empty() || imageIndex(id) >= 0)
          fail("Missing or duplicate FB2 image ID");
        else if (images.size() >= MAX_IMAGES)
          fail("Too many FB2 images (limit 256)");
        else if (type == "image/jpeg" || type == "image/png" || type == "image/gif")
          images.push_back({id, type, imageName(images.size(), type)});
      } else {
        binary = imageIndex(id);
        b64 = 0;
        bits = 0;
        padding = 0;
        binaryBytes = 0;
        if (binary >= 0 && !out.open(dir + "/" + images[binary].cacheName)) fail("Cannot write FB2 image");
      }
    }
    if (bodyDepth && n != "body") {
      if (n == "section") {
        if (sectionDepth == 0) closeChapter();
        ++sectionDepth;
        sectionPreamble.push_back(true);
      }
      openChapter();
      if (!render && chapter < chapters.size() && chapters[chapter].sourceXPath.empty()) {
        chapters[chapter].sourceXPath = f.sourceXPath;
      }
      const auto id = attr(a, "id");
      if (!render && !id.empty()) {
        if (anchors.size() >= MAX_IDS)
          fail("Too many FB2 anchors (limit 4096)");
        else {
          for (const auto& v : anchors)
            if (v.id == id) fail("Duplicate FB2 anchor ID");
          anchors.push_back({id, chapter});
        }
      }
      if (n == "section")
        f.tag = "div";
      else if (n == "title") {
        f.tag = "div";
        f.title = sectionDepth <= 1;
        f.headingLevel = sectionDepth <= 1 ? 1 : 2;
      } else if (n == "p" || n == "v" || n == "subtitle" || n == "text-author" || n == "date")
        f.tag = "p";
      else if (n == "emphasis")
        f.tag = "em";
      else if (n == "strikethrough")
        f.tag = "s";
      else if (n == "strong" || n == "sup" || n == "sub" || n == "code" || n == "a" || n == "table" || n == "tr" ||
               n == "td" || n == "th")
        f.tag = n;
      else if (n == "cite" || n == "epigraph")
        f.tag = "blockquote";
      else if (n == "poem" || n == "stanza" || n == "annotation")
        f.tag = "div";
      else if (n == "empty-line")
        text("<p class=\"fb2-empty-line\"><br/></p>");
      else if (n == "image" && render) {
        auto ref = attr(a, "href");
        const int i = ref.size() > 1 ? imageIndex(ref.substr(1)) : -1;
        if (i >= 0) text("<img src=\"" + images[i].cacheName + "\" alt=\"\"/>");
      }
      if (!f.tag.empty()) {
        std::string tag = "<" + f.tag;
        if (!id.empty()) tag += " id=\"" + escape(id) + "\"";
        tag += " data-fb2-xpath=\"" + escape(f.sourceXPath) + "\"";
        // A title can contain several paragraphs; retain its wrapper so
        // font size is inherited once rather than multiplied for every line.
        std::string classes;
        if (n == "title")
          classes = "fb2-title title heading-" + std::to_string(f.headingLevel);
        else if (n == "subtitle")
          classes = "fb2-subtitle subtitle heading-2";
        else if (n == "v")
          classes = "fb2-verse";
        else if (n == "text-author" || n == "date" || n == "cite" || n == "epigraph" || n == "poem" || n == "stanza" ||
                 n == "annotation")
          classes = "fb2-" + n;
        const auto sourceClass = attr(a, "class");
        if (!sourceClass.empty()) {
          if (!classes.empty()) classes += ' ';
          classes += sourceClass;
        }
        if (!classes.empty()) tag += " class=\"" + escape(classes) + "\"";
        const auto inlineStyle = attr(a, "style");
        if (!inlineStyle.empty()) tag += " style=\"" + escape(inlineStyle) + "\"";
        if (n == "section" && note) tag += " epub:type=\"footnote\"";
        if (n == "a") {
          const auto h = href(attr(a, "href"));
          if (!h.empty()) tag += " href=\"" + escape(h) + "\"";
          if (attr(a, "type") == "note") tag += " epub:type=\"noteref\"";
        }
        if (n == "td" || n == "th")
          for (const char* k : {"colspan", "rowspan"}) {
            auto v = attr(a, k);
            if (!v.empty() && v.size() < 4 && v.find_first_not_of("0123456789") == std::string::npos)
              tag += " " + std::string(k) + "=\"" + v + "\"";
          }
        const bool leadingParagraph = n == "p" && !sectionPreamble.empty() && !stack.empty() &&
                                      stack.back().name == "section" && sectionPreamble.back();
        if (leadingParagraph) {
          f.tag.clear();
          f.deferredParagraph = true;
        } else {
          text(tag + ">");
        }
      }
    }
    stack.push_back(std::move(f));
  }
  void end() {
    if (stack.empty() || !error.empty()) return;
    if (metadataOnly) {
      const auto name = stack.back().name;
      if (name == capture && titleInfoDepth) {
        auto first = captured.find_first_not_of(" \t\r\n");
        auto last = captured.find_last_not_of(" \t\r\n");
        const std::string value = first == std::string::npos ? std::string{} : captured.substr(first, last - first + 1);
        if (capture == "book-title") {
          title = value;
        } else if (capture == "lang") {
          language = value;
        } else if (capture == "nickname") {
          metadataNickname = value;
        } else if (!value.empty()) {
          if (!metadataName.empty()) metadataName += ' ';
          metadataName += value;
        }
        capture.clear();
        captured.clear();
      }
      if (name == "author" && static_cast<int>(stack.size()) == metadataAuthorDepth) {
        author = metadataName.empty() ? metadataNickname : metadataName;
        metadataAuthorDepth = 0;
      }
      if (name == "title-info") {
        titleInfoDepth = 0;
        metadataComplete = true;
      }
      stack.pop_back();
      return;
    }
    if (stack.back().deferredParagraph) openDeferredParagraph(false);
    const auto f = stack.back();
    if (f.name == capture && titleInfoDepth) {
      if (!render) {
        if (capture == "book-title")
          title = captured;
        else if (capture == "lang")
          language = captured;
        else {
          if (!author.empty()) author += ' ';
          author += captured;
        }
      }
      capture.clear();
      captured.clear();
    }
    if (f.name == "title-info") titleInfoDepth = 0;
    if (f.name == "stylesheet") capturingStylesheet = false;
    if (f.name == "coverpage") coverPage = false;
    if (f.name == "binary" && render && binary >= 0) {
      if (bits != 0 && bits != 2 && bits != 4) fail("Invalid FB2 base64 length");
      if ((bits == 2 && padding != 1) || (bits == 4 && padding != 2) || (bits == 0 && padding != 0))
        fail("Invalid FB2 base64 padding");
      if (!binaryBytes) fail("Empty FB2 image");
      if (!out.close()) fail("SD card image write failed");
      binary = -1;
    }
    if (bodyDepth) {
      if (!f.tag.empty()) text("</" + f.tag + ">");
      if (f.name == "section") {
        --sectionDepth;
        if (!sectionPreamble.empty()) sectionPreamble.pop_back();
        if (sectionDepth == 0) closeChapter();
      }
      if (f.name == "body") {
        closeChapter();
        bodyDepth = 0;
      }
    }
    stack.pop_back();
  }
  void chars(const char* s, int n) {
    if (!error.empty()) return;
    if (metadataOnly) {
      if (!capture.empty() && captured.size() < 2048)
        captured.append(s, std::min<size_t>(n, 2048 - captured.size()));
      return;
    }
    if (render && binary >= 0) {
      for (int i = 0; i < n; ++i) {
        const unsigned char c = s[i];
        if (isspace(c)) continue;
        if (c == '=') {
          if (++padding > 2) fail("Invalid FB2 base64 padding");
          continue;
        }
        int v = c >= 'A' && c <= 'Z'   ? c - 'A'
                : c >= 'a' && c <= 'z' ? c - 'a' + 26
                : c >= '0' && c <= '9' ? c - '0' + 52
                : c == '+'             ? 62
                : c == '/'             ? 63
                                       : -1;
        if (v < 0 || padding) {
          fail("Invalid FB2 base64 image");
          break;
        }
        b64 = (b64 << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8) {
          bits -= 8;
          const uint8_t b = static_cast<uint8_t>(b64 >> bits);
          out.bytes(&b, 1);
          ++binaryBytes;
        }
      }
      return;
    }
    if (!stack.empty() && stack.back().deferredParagraph) {
      bool whitespace = true;
      for (int i = 0; i < n; ++i) {
        if (!isspace(static_cast<unsigned char>(s[i]))) {
          whitespace = false;
          break;
        }
      }
      if (whitespace) return;
      openDeferredParagraph(false);
    }
    if (!capture.empty() && captured.size() < 2048) captured.append(s, std::min<size_t>(n, 2048 - captured.size()));
    if (capturingStylesheet && embeddedCss.size() < 16384) {
      const size_t count = std::min<size_t>(n, 16384 - embeddedCss.size());
      embeddedCss.append(s, count);
    }
    if (!bodyDepth) return;
    if (!active) {
      bool nonspace = false;
      for (int i = 0; i < n; ++i)
        if (!isspace(static_cast<unsigned char>(s[i]))) nonspace = true;
      if (!nonspace) return;
      openChapter();
      if (!render && chapter < chapters.size() && chapters[chapter].sourceXPath.empty() && !stack.empty()) {
        chapters[chapter].sourceXPath = stack.back().sourceXPath;
      }
    }
    if (!render)
      for (const auto& f : stack)
        if (f.title) {
          auto& t = chapters[chapter].title;
          if (t == "Chapter " + std::to_string(chapter + 1)) t.clear();
          if (t.size() < 256) t.append(s, std::min<size_t>(n, 256 - t.size()));
          break;
        }
    text(escape(std::string(s, n)));
  }
};

// Decode common FB2 encodings before feeding the UTF-8 XML parser.
// The XML declaration is removed so it cannot contradict the normalized byte stream.
bool parse(Converter& c, HalFile& src, uint32_t* sourceCrc = nullptr, uint32_t* sourceBytes = nullptr) {
  if (!src.seek(0)) return false;
  uint8_t first[512];
  int got = src.read(first, sizeof(first));
  if (got <= 0) return false;
  const bool utf16le = got >= 2 && first[0] == 0xff && first[1] == 0xfe;
  const bool utf16be = got >= 2 && first[0] == 0xfe && first[1] == 0xff;
  std::string head;
  if (utf16le || utf16be) {
    for (int i = 2; i + 1 < got && i < 512; i += 2) {
      const uint16_t ch = utf16le ? static_cast<uint16_t>(first[i] | (first[i + 1] << 8))
                                  : static_cast<uint16_t>((first[i] << 8) | first[i + 1]);
      head.push_back(ch < 128 ? static_cast<char>(ch) : '?');
    }
  } else {
    head.assign(reinterpret_cast<char*>(first), got);
  }
  std::string lower = head;
  for (auto& v : lower) v = static_cast<char>(tolower(static_cast<unsigned char>(v)));
  bool cp = false;
  size_t offset = (utf16le || utf16be) ? 2 : (head.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0);
  const size_t declarationOffset = (utf16le || utf16be) ? 0 : offset;
  if (head.compare(declarationOffset, 5, "<?xml") == 0) {
    auto end = head.find("?>", declarationOffset);
    if (end == std::string::npos) {
      c.fail("Invalid XML declaration");
      return false;
    }
    const auto decl = lower.substr(declarationOffset, end - declarationOffset);
    cp = !utf16le && !utf16be &&
         (decl.find("windows-1251") != std::string::npos || decl.find("cp1251") != std::string::npos);
    const bool declaredUtf16 = decl.find("utf-16") != std::string::npos || decl.find("utf16") != std::string::npos;
    if (decl.find("encoding") != std::string::npos && !cp && !declaredUtf16 &&
        decl.find("utf-8") == std::string::npos && decl.find("utf8") == std::string::npos) {
      c.fail("Unsupported FB2 encoding; use UTF-8, UTF-16 or Windows-1251");
      return false;
    }
    offset = (utf16le || utf16be) ? 2 + (end + 2) * 2 : end + 2;
  }
  if (!src.seek(offset)) return false;
  uint32_t rawCrc = sourceCrc ? crcStep(0xffffffffu, first, offset) : 0;
  uint32_t rawBytes = static_cast<uint32_t>(offset);
  SaxParser parser;
  if (!parser.init(&c, Converter::startCb, Converter::endCb, Converter::charCb)) {
    c.fail("Not enough memory for FB2 parser");
    return false;
  }
  static const uint16_t cp1251[64] = {
      0x402, 0x403,  0x201a, 0x453, 0x201e, 0x2026, 0x2020, 0x2021, 0x20ac, 0x2030, 0x409,  0x2039, 0x40a,
      0x40c, 0x40b,  0x40f,  0x452, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0xfffd, 0x2122,
      0x459, 0x203a, 0x45a,  0x45c, 0x45b,  0x45f,  0xa0,   0x40e,  0x45e,  0x408,  0xa4,   0x490,  0xa6,
      0xa7,  0x401,  0xa9,   0x404, 0xab,   0xac,   0xad,   0xae,   0x407,  0xb0,   0xb1,   0x406,  0x456,
      0x491, 0xb5,   0xb6,   0xb7,  0x451,  0x2116, 0x454,  0xbb,   0x458,  0x405,  0x455,  0x457};
  auto buffer = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[4096]);
  if (!buffer) {
    c.fail("Not enough memory for FB2 buffer");
    return false;
  }
  uint8_t* buf = buffer.get();
  uint8_t* utf = buf + 1024;
  uint16_t pendingHighSurrogate = 0;
  while (src.position() < src.size()) {
    const int n = src.read(buf, 1024);
    if (n <= 0) {
      c.fail("Cannot read FB2 from SD card");
      return false;
    }
    if (sourceCrc) {
      rawCrc = crcStep(rawCrc, buf, static_cast<size_t>(n));
      rawBytes += static_cast<uint32_t>(n);
    }
    size_t k = 0;
    if (utf16le || utf16be) {
      if ((n & 1) != 0) {
        c.fail("Malformed UTF-16 FB2: odd byte count");
        return false;
      }
      auto emitUtf8 = [&](uint32_t u) {
        if (u < 0x80) {
          utf[k++] = static_cast<uint8_t>(u);
        } else if (u < 0x800) {
          utf[k++] = static_cast<uint8_t>(0xc0 | (u >> 6));
          utf[k++] = static_cast<uint8_t>(0x80 | (u & 0x3f));
        } else if (u < 0x10000) {
          utf[k++] = static_cast<uint8_t>(0xe0 | (u >> 12));
          utf[k++] = static_cast<uint8_t>(0x80 | ((u >> 6) & 0x3f));
          utf[k++] = static_cast<uint8_t>(0x80 | (u & 0x3f));
        } else {
          utf[k++] = static_cast<uint8_t>(0xf0 | (u >> 18));
          utf[k++] = static_cast<uint8_t>(0x80 | ((u >> 12) & 0x3f));
          utf[k++] = static_cast<uint8_t>(0x80 | ((u >> 6) & 0x3f));
          utf[k++] = static_cast<uint8_t>(0x80 | (u & 0x3f));
        }
      };
      for (int i = 0; i < n; i += 2) {
        const uint16_t u = utf16le ? static_cast<uint16_t>(buf[i] | (buf[i + 1] << 8))
                                   : static_cast<uint16_t>((buf[i] << 8) | buf[i + 1]);
        if (pendingHighSurrogate) {
          if (u < 0xdc00 || u > 0xdfff) {
            c.fail("Malformed UTF-16 FB2: invalid surrogate pair");
            return false;
          }
          emitUtf8(0x10000u + ((pendingHighSurrogate - 0xd800u) << 10) + (u - 0xdc00u));
          pendingHighSurrogate = 0;
        } else if (u >= 0xd800 && u <= 0xdbff) {
          pendingHighSurrogate = u;
        } else if (u >= 0xdc00 && u <= 0xdfff) {
          c.fail("Malformed UTF-16 FB2: unexpected low surrogate");
          return false;
        } else {
          emitUtf8(u);
        }
      }
    } else if (cp)
      for (int i = 0; i < n; ++i) {
        uint32_t u = buf[i] < 128 ? buf[i] : buf[i] >= 192 ? 0x410 + buf[i] - 192 : cp1251[buf[i] - 128];
        if (u < 128)
          utf[k++] = u;
        else if (u < 2048) {
          utf[k++] = 0xc0 | (u >> 6);
          utf[k++] = 0x80 | (u & 63);
        } else {
          utf[k++] = 0xe0 | (u >> 12);
          utf[k++] = 0x80 | ((u >> 6) & 63);
          utf[k++] = 0x80 | (u & 63);
        }
      }
    if (!parser.feed((cp || utf16le || utf16be) ? utf : buf, (cp || utf16le || utf16be) ? k : n)) {
      c.fail("Malformed FB2 XML");
      return false;
    }
    if (!c.error.empty()) return false;
    // For the Library's metadata-only lookup, title-info precedes the potentially huge book body.
    // Stop there instead of scanning every chapter and embedded base64 image on every index refresh.
    if (c.metadataOnly && c.metadataComplete) break;
    delay(1);
  }
  if (c.metadataOnly) {
    if (!c.root || !c.metadataComplete) c.fail("FB2 metadata section is missing or incomplete");
    return c.error.empty();
  }
  if (pendingHighSurrogate) {
    c.fail("Malformed UTF-16 FB2: incomplete surrogate pair");
    return false;
  }
  if (!parser.finalize() || parser.truncationFlags() != 0 || !c.stack.empty()) {
    c.fail("Truncated or overly complex FB2 XML");
    return false;
  }
  if (!c.root || !c.nextChapter) {
    c.fail("FB2 has no readable body");
    return false;
  }
  if (sourceCrc) *sourceCrc = ~rawCrc;
  if (sourceBytes) *sourceBytes = rawBytes;
  return c.error.empty();
}

struct ZipEntry {
  std::string name;
  uint32_t crc, size, offset;
};
struct Zip {
  Writer out;
  std::vector<ZipEntry> entries;
  uint32_t pos = 0;
  uint32_t checksum = 0xffffffffu;
  void bytes(const void* p, size_t n) {
    out.bytes(p, n);
    checksum = crcStep(checksum, static_cast<const uint8_t*>(p), n);
    pos += n;
  }
  void str(const std::string& s) { bytes(s.data(), s.size()); }
  void u16(uint16_t v) {
    uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
    bytes(b, 2);
  }
  void u32(uint32_t v) {
    u16(v);
    u16(v >> 16);
  }
  void header(const ZipEntry& e) {
    u32(0x04034b50);
    u16(20);
    u16(0x800);
    u16(0);
    u16(0);
    u16(0x21);
    u32(e.crc);
    u32(e.size);
    u32(e.size);
    u16(e.name.size());
    u16(0);
    str(e.name);
  }
  void add(const std::string& name, const std::string& value) {
    ZipEntry e{name, ~crcStep(0xffffffffu, reinterpret_cast<const uint8_t*>(value.data()), value.size()),
               static_cast<uint32_t>(value.size()), pos};
    header(e);
    str(value);
    entries.push_back(e);
  }
  bool file(const std::string& name, const std::string& path) {
    HalFile f;
    if (!Storage.openFileForRead("FB2", path, f) || f.size() > 0x7fffffffu) return false;
    ZipEntry e{name, 0xffffffffu, static_cast<uint32_t>(f.size()), pos};
    uint8_t b[2048];
    while (f.position() < e.size) {
      int n = f.read(b, sizeof(b));
      if (n <= 0) return false;
      e.crc = crcStep(e.crc, b, n);
      delay(1);
    }
    e.crc = ~e.crc;
    if (!f.seek(0)) return false;
    header(e);
    uint32_t readback = 0xffffffffu;
    while (f.position() < e.size) {
      int n = f.read(b, sizeof(b));
      if (n <= 0) return false;
      readback = crcStep(readback, b, n);
      bytes(b, n);
      delay(1);
    }
    entries.push_back(e);
    return out.ok && ~readback == e.crc;
  }
  bool finish() {
    const uint32_t start = pos;
    for (const auto& e : entries) {
      u32(0x02014b50);
      u16(20);
      u16(20);
      u16(0x800);
      u16(0);
      u16(0);
      u16(0x21);
      u32(e.crc);
      u32(e.size);
      u32(e.size);
      u16(e.name.size());
      u16(0);
      u16(0);
      u16(0);
      u16(0);
      u32(0);
      u32(e.offset);
      str(e.name);
    }
    const uint32_t size = pos - start;
    u32(0x06054b50);
    u16(0);
    u16(0);
    u16(entries.size());
    u16(entries.size());
    u32(size);
    u32(start);
    u16(0);
    return out.close();
  }
  // Read back every byte, including ZIP headers, directory and footer. Compare
  // with the complete archive emitted by this writer, not just a ZIP signature.
  static bool verify(const std::string& path, uint32_t pos, uint32_t checksum) {
    HalFile f;
    if (!Storage.openFileForRead("FB2", path, f) || f.size() != pos) return false;
    uint8_t buffer[2048];
    uint32_t crc = 0xffffffffu;
    size_t remaining = pos;
    while (remaining) {
      const int n = f.read(buffer, std::min(remaining, sizeof(buffer)));
      if (n <= 0) return false;
      crc = crcStep(crc, buffer, n);
      remaining -= n;
      delay(1);
    }
    return crc == checksum;
  }
};
bool package(Converter& c, const std::string& path, uint32_t& size, uint32_t& checksum) {
  auto holder = std::unique_ptr<Zip>(new (std::nothrow) Zip);
  if (!holder) return false;
  auto& zip = *holder;
  if (!zip.out.open(path)) return false;
  zip.add("mimetype", "application/epub+zip");
  zip.add("fb2.css", FB2_CSS);
  zip.add("META-INF/container.xml",
          "<?xml version=\"1.0\"?><container version=\"1.0\" "
          "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles><rootfile full-path=\"content.opf\" "
          "media-type=\"application/oebps-package+xml\"/></rootfiles></container>");
  const std::string title = escape(c.title.empty() ? "FB2 book" : c.title);
  std::string opf =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?><package version=\"2.0\" unique-identifier=\"uid\" "
      "xmlns=\"http://www.idpf.org/2007/opf\"><metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:identifier "
      "id=\"uid\">" +
      escape(c.dir) + "</dc:identifier><dc:title>" + title + "</dc:title><dc:creator>" + escape(c.author) +
      "</dc:creator><dc:language>" + escape(c.language) + "</dc:language>";
  const int cover = c.imageIndex(c.cover);
  if (cover >= 0) opf += "<meta name=\"cover\" content=\"i" + std::to_string(cover) + "\"/>";
  opf += "</metadata><manifest><item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>";
  opf += "<item id=\"fb2-css\" href=\"fb2.css\" media-type=\"text/css\"/>";
  std::string spine = "</manifest><spine toc=\"ncx\">";
  std::string ncx =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?><ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" "
      "version=\"2005-1\"><head><meta name=\"dtb:uid\" content=\"" +
      escape(c.dir) + "\"/></head><docTitle><text>" + title + "</text></docTitle><navMap>";
  for (size_t i = 0; i < c.chapters.size(); ++i) {
    const auto id = "c" + std::to_string(i), name = chapterName(i);
    opf += "<item id=\"" + id + "\" href=\"" + name + "\" media-type=\"application/xhtml+xml\"/>";
    spine += "<itemref idref=\"" + id + "\"/>";
    ncx += "<navPoint id=\"" + id + "\" playOrder=\"" + std::to_string(i + 1) + "\"><navLabel><text>" +
           escape(c.chapters[i].title) + "</text></navLabel><content src=\"" + name + "\"/></navPoint>";
    if (!zip.file(name, c.dir + "/" + name)) return false;
  }
  for (size_t i = 0; i < c.images.size(); ++i) {
    const auto name = "i" + std::to_string(i);
    const auto& cacheName = c.images[i].cacheName;
    opf += "<item id=\"" + name + "\" href=\"" + cacheName + "\" media-type=\"" + c.images[i].type + "\"/>";
    if (!zip.file(cacheName, c.dir + "/" + cacheName)) return false;
  }
  zip.add("content.opf", opf + spine + "</spine></package>");
  zip.add("toc.ncx", ncx + "</navMap></ncx>");
  if (!zip.finish() || !Zip::verify(path, zip.pos, zip.checksum)) return false;
  size = zip.pos;
  checksum = zip.checksum;
  return true;
}
}  // namespace

namespace {
constexpr size_t MAX_READER_CACHE_META = 256 * 1024;

bool readExact(HalFile& file, void* out, size_t size) {
  auto* bytes = static_cast<uint8_t*>(out);
  while (size) {
    const int got = file.read(bytes, size);
    if (got <= 0) return false;
    bytes += got;
    size -= static_cast<size_t>(got);
  }
  return true;
}

bool readU32(HalFile& file, uint32_t& out) {
  uint8_t b[4];
  if (!readExact(file, b, sizeof(b))) return false;
  out = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) | (static_cast<uint32_t>(b[2]) << 16) |
        (static_cast<uint32_t>(b[3]) << 24);
  return true;
}

bool readString(HalFile& file, std::string& out, const size_t maxBytes = 65535) {
  uint32_t size = 0;
  if (!readU32(file, size) || size > maxBytes || file.position() > file.size() || size > file.size() - file.position())
    return false;
  out.resize(size);
  return size == 0 || readExact(file, out.data(), size);
}

void writeU32(Writer& writer, uint32_t value) {
  const uint8_t bytes[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                            static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
  writer.bytes(bytes, sizeof(bytes));
}

void writeString(Writer& writer, const std::string& value) {
  writeU32(writer, static_cast<uint32_t>(value.size()));
  writer.bytes(value.data(), value.size());
}

bool readReaderMeta(const std::string& path, Fb2Converter::ReaderBook& book) {
  HalFile file;
  if (!Storage.openFileForRead("FB2", path, file) || file.size() < 16 || file.size() > MAX_READER_CACHE_META)
    return false;
  char magic[8];
  if (!readExact(file, magic, sizeof(magic)) || memcmp(magic, "FB2READ2", 8) != 0) return false;
  if (!readU32(file, book.sourceSize) || !readU32(file, book.sourceCrc) || !readString(file, book.title) ||
      !readString(file, book.author) || !readString(file, book.language) || !readString(file, book.cover))
    return false;
  uint32_t count = 0;
  if (!readU32(file, count) || count == 0 || count > MAX_CHAPTERS) return false;
  book.chapters.clear();
  book.chapters.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    Fb2Converter::ReaderChapter chapter;
    uint8_t note = 0;
    if (!readString(file, chapter.title) || !readExact(file, &note, 1) || !readString(file, chapter.sourceXPath))
      return false;
    chapter.note = note != 0;
    book.chapters.push_back(std::move(chapter));
  }
  if (!readU32(file, count) || count > MAX_IMAGES) return false;
  book.images.clear();
  book.images.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    Fb2Converter::ReaderImage image;
    if (!readString(file, image.id) || !readString(file, image.type)) return false;
    image.cacheName = imageName(i, image.type);
    book.images.push_back(std::move(image));
  }
  return file.position() == file.size();
}

bool writeReaderMeta(const std::string& path, const Fb2Converter::ReaderBook& book) {
  Writer writer;
  if (!writer.open(path)) return false;
  writer.bytes("FB2READ2", 8);
  writeU32(writer, book.sourceSize);
  writeU32(writer, book.sourceCrc);
  writeString(writer, book.title);
  writeString(writer, book.author);
  writeString(writer, book.language);
  writeString(writer, book.cover);
  writeU32(writer, static_cast<uint32_t>(book.chapters.size()));
  for (const auto& chapter : book.chapters) {
    writeString(writer, chapter.title);
    const uint8_t note = chapter.note ? 1 : 0;
    writer.bytes(&note, 1);
    writeString(writer, chapter.sourceXPath);
  }
  writeU32(writer, static_cast<uint32_t>(book.images.size()));
  for (const auto& image : book.images) {
    writeString(writer, image.id);
    writeString(writer, image.type);
  }
  return writer.close();
}

bool computeSourceCrc(HalFile& file, uint32_t& size, uint32_t& crc) {
  size = static_cast<uint32_t>(file.size());
  if (!file.seek(0)) return false;
  uint8_t bytes[2048];
  crc = 0xffffffffu;
  while (file.position() < file.size()) {
    const int got = file.read(bytes, sizeof(bytes));
    if (got <= 0) return false;
    crc = crcStep(crc, bytes, static_cast<size_t>(got));
  }
  crc = ~crc;
  return file.seek(0);
}

bool isFb2Entry(std::string_view path) { return endsWithIgnoreCase(path, ".fb2"); }

bool extractFb2Archive(const std::string& archivePath, const std::string& destination, uint32_t& size, uint32_t& crc,
                       std::string& error) {
  ZipFile zip(archivePath);
  std::string entryName;
  bool multiple = false;
  if (!zip.streamCentralDirectoryNames([&](std::string_view name) {
        if (!isFb2Entry(name)) return;
        if (!entryName.empty())
          multiple = true;
        else
          entryName.assign(name.data(), name.size());
      })) {
    error = "Cannot read FB2 ZIP directory";
    return false;
  }
  if (multiple) {
    error = "FB2 ZIP contains more than one .fb2 book";
    return false;
  }
  if (entryName.empty()) {
    error = "FB2 ZIP contains no .fb2 file";
    return false;
  }

  ZipFile::FileStatSlim stat{};
  if (!zip.loadFileStatSlim(entryName.c_str(), &stat) || stat.uncompressedSize == 0 ||
      stat.uncompressedSize > 128u * 1024 * 1024) {
    error = "FB2 ZIP entry is missing, empty, or larger than 128 MB";
    return false;
  }
  ZipFile::EntryReader reader(zip, 1024);
  if (!reader.open(stat)) {
    error = "Cannot open compressed FB2 entry";
    return false;
  }
  Writer out;
  if (!out.open(destination)) {
    error = "Cannot create temporary FB2 source";
    return false;
  }
  uint8_t buffer[1024];
  size = 0;
  // EntryReader validates the complete ZIP entry against the central-directory CRC.
  // Reusing that trusted value avoids hashing the same decompressed bytes a second time.
  crc = stat.crc32;
  bool done = false;
  bool ok = true;
  while (!done) {
    size_t produced = 0;
    if (!reader.step(buffer, sizeof(buffer), &produced, &done)) {
      ok = false;
      break;
    }
    out.bytes(buffer, produced);
    size += static_cast<uint32_t>(produced);
    if (size > stat.uncompressedSize) {
      ok = false;
      break;
    }
  }
  if (!out.close()) ok = false;
  reader.close();
  if (!ok || size != stat.uncompressedSize) {
    Storage.remove(destination.c_str());
    error = "FB2 ZIP entry failed size or CRC validation";
    return false;
  }
  return true;
}

// Metadata is in FB2's opening title-info section. Read at most this prefix from ZIP/FBZ rather than
// inflating the entire book and writing it to the SD card just to discover its author.
bool extractFb2MetadataPrefix(const std::string& archivePath, const std::string& destination, uint32_t& size,
                              std::string& error) {
  constexpr size_t METADATA_PREFIX_LIMIT = 128u * 1024;
  ZipFile zip(archivePath);
  std::string entryName;
  bool multiple = false;
  if (!zip.streamCentralDirectoryNames([&](std::string_view name) {
        if (!isFb2Entry(name)) return;
        if (!entryName.empty())
          multiple = true;
        else
          entryName.assign(name.data(), name.size());
      })) {
    error = "Cannot read FB2 ZIP directory";
    return false;
  }
  if (multiple || entryName.empty()) {
    error = multiple ? "FB2 ZIP contains more than one .fb2 book" : "FB2 ZIP contains no .fb2 file";
    return false;
  }

  ZipFile::FileStatSlim stat{};
  if (!zip.loadFileStatSlim(entryName.c_str(), &stat) || stat.uncompressedSize == 0 ||
      stat.uncompressedSize > 128u * 1024 * 1024) {
    error = "FB2 ZIP entry is missing, empty, or larger than 128 MB";
    return false;
  }
  const bool wholeEntryFits = stat.uncompressedSize <= METADATA_PREFIX_LIMIT;
  ZipFile::EntryReader reader(zip, 1024);
  if (!reader.open(stat, METADATA_PREFIX_LIMIT, wholeEntryFits)) {
    error = "Cannot open compressed FB2 entry";
    return false;
  }
  Writer out;
  if (!out.open(destination)) {
    error = "Cannot create temporary FB2 metadata prefix";
    reader.close();
    return false;
  }
  uint8_t buffer[1024];
  size = 0;
  bool done = false;
  bool ok = true;
  while (!done) {
    size_t produced = 0;
    if (!reader.step(buffer, sizeof(buffer), &produced, &done)) {
      ok = false;
      break;
    }
    out.bytes(buffer, produced);
    size += static_cast<uint32_t>(produced);
  }
  if (!out.close()) ok = false;
  reader.close();
  const uint32_t expectedSize = static_cast<uint32_t>(std::min<size_t>(stat.uncompressedSize, METADATA_PREFIX_LIMIT));
  if (!ok || size != expectedSize || size == 0) {
    Storage.remove(destination.c_str());
    error = "Could not read the FB2 metadata prefix";
    return false;
  }
  // A complete small entry was CRC-checked. For larger books only the prefix is read, so their full
  // entry CRC is intentionally not traversed during a metadata lookup.
  return true;
}
}  // namespace

bool Fb2Converter::readMetadata(const std::string& source, std::string& title, std::string& author,
                                std::string& series, std::string& seriesIndex, std::string& error) {
  title.clear();
  author.clear();
  series.clear();
  seriesIndex.clear();
  error.clear();

  const bool zippedFb2 = endsWithIgnoreCase(source, ".zip") || endsWithIgnoreCase(source, ".fbz");
  if (!zippedFb2 && !endsWithIgnoreCase(source, ".fb2")) {
    error = "Invalid FB2 source extension";
    return false;
  }

  HalFile sourceFile;
  if (!Storage.openFileForRead("FB2META", source, sourceFile)) {
    error = "Cannot open FB2";
    return false;
  }
  if (sourceFile.size() > 128u * 1024 * 1024) {
    error = "FB2 exceeds the 128 MB limit";
    return false;
  }

  HalFile extractedSource;
  HalFile* parseSource = &sourceFile;
  std::string extractedPath;
  if (zippedFb2) {
    if (!Storage.ensureDirectoryExists("/.crosspoint/tmp")) {
      error = "Cannot prepare temporary FB2 folder";
      return false;
    }
    char name[96];
    snprintf(name, sizeof(name), "/.crosspoint/tmp/fb2-meta-%08lx-%08lx.fb2",
             static_cast<unsigned long>(std::hash<std::string>{}(source)), static_cast<unsigned long>(millis()));
    extractedPath = name;
    uint32_t extractedSize = 0;
    if (!extractFb2MetadataPrefix(source, extractedPath, extractedSize, error) ||
        !Storage.openFileForRead("FB2META", extractedPath, extractedSource)) {
      if (error.empty()) error = "Cannot reopen temporary FB2 source";
      Storage.remove(extractedPath.c_str());
      return false;
    }
    parseSource = &extractedSource;
  }

  auto converter = std::unique_ptr<Converter>(new (std::nothrow) Converter);
  if (!converter) {
    error = "Not enough memory for FB2 metadata";
  } else {
    converter->metadataOnly = true;
    const bool ok = parse(*converter, *parseSource);
    if (!ok || !converter->root) {
      error = converter->error.empty() ? "Cannot parse FB2 metadata" : converter->error;
    } else {
      title = converter->title;
      author = converter->author;
      series = converter->series;
      seriesIndex = converter->seriesIndex;
    }
  }

  if (zippedFb2) {
    extractedSource.close();
    Storage.remove(extractedPath.c_str());
  }
  return error.empty();
}

bool Fb2Converter::prepareReaderCache(const std::string& source, ReaderBook& book, std::string& error) {
  book = {};
  error.clear();
  // Plain .zip is a possible FB2 archive; extractFb2Archive validates its contents.
  const bool zippedFb2 = endsWithIgnoreCase(source, ".zip") || endsWithIgnoreCase(source, ".fbz");
  if (!zippedFb2 && !endsWithIgnoreCase(source, ".fb2")) {
    error = "Invalid FB2 source extension";
    return false;
  }

  HalFile src;
  if (!Storage.openFileForRead("FB2", source, src)) {
    error = "Cannot open FB2";
    return false;
  }
  if (src.size() > 128u * 1024 * 1024) {
    error = "FB2 exceeds the 128 MB limit";
    return false;
  }
  const uint32_t fingerprintStart = millis();
  if (!computeSourceCrc(src, book.sourceSize, book.sourceCrc)) {
    error = "Cannot read FB2";
    return false;
  }
  LOG_DBG("FB2", "Source CRC pass: %lu ms", static_cast<unsigned long>(millis() - fingerprintStart));
  const uint32_t archiveSize = book.sourceSize;
  const uint32_t archiveCrc = book.sourceCrc;

  const size_t sourceHash = std::hash<std::string>{}(source);
  char key[64];
  snprintf(key, sizeof(key), "fb2-reader-v3-%08lx-%08lx", static_cast<unsigned long>(sourceHash),
           static_cast<unsigned long>(book.sourceCrc));
  book.cacheDir = FsHelpers::unpackCachePath(key, static_cast<uint32_t>(sourceHash));
  if (!FsHelpers::prepareBookCachePath(book.cacheDir)) {
    error = "Cannot prepare FB2 cache folder on SD card";
    return false;
  }
  const std::string metaPath = book.cacheDir + "/reader.meta";
  ReaderBook cached;
  cached.cacheDir = book.cacheDir;
  if (Storage.exists(metaPath.c_str()) && readReaderMeta(metaPath, cached) && cached.sourceSize == book.sourceSize &&
      cached.sourceCrc == book.sourceCrc && Storage.exists((book.cacheDir + "/fb2.css").c_str())) {
    bool filesExist = true;
    for (size_t i = 0; i < cached.chapters.size(); ++i)
      filesExist &= Storage.exists((book.cacheDir + "/" + chapterName(i)).c_str());
    for (const auto& image : cached.images)
      filesExist &= Storage.exists((book.cacheDir + "/" + image.cacheName).c_str());
    if (filesExist) {
      book = std::move(cached);
      return true;
    }
  }

  Storage.removeDir(book.cacheDir.c_str());
  if (!Storage.mkdir(book.cacheDir.c_str())) {
    error = "Cannot create FB2 reader cache on SD card";
    return false;
  }
  auto holder = std::unique_ptr<Converter>(new (std::nothrow) Converter);
  if (!holder) {
    error = "Not enough memory to open FB2";
    Storage.removeDir(book.cacheDir.c_str());
    return false;
  }
  auto& converter = *holder;
  converter.dir = book.cacheDir;
  HalFile extractedSource;
  HalFile* parseSource = &src;
  uint32_t parseSourceSize = archiveSize;
  uint32_t parseSourceCrc = archiveCrc;
  if (zippedFb2) {
    const std::string extractedPath = book.cacheDir + "/archive-source.fb2";
    if (!extractFb2Archive(source, extractedPath, parseSourceSize, parseSourceCrc, error) ||
        !Storage.openFileForRead("FB2", extractedPath, extractedSource)) {
      if (error.empty()) error = "Cannot reopen temporary FB2 source";
      Storage.removeDir(book.cacheDir.c_str());
      return false;
    }
    parseSource = &extractedSource;
  }
  const uint32_t metadataStart = millis();
  bool ok = parse(converter, *parseSource);
  LOG_DBG("FB2", "Metadata parse pass: %lu ms", static_cast<unsigned long>(millis() - metadataStart));
  uint32_t finalSize = 0, finalCrc = 0;
  if (ok) {
    converter.render = true;
    converter.nextChapter = 0;
    converter.bodyDepth = 0;
    converter.sectionDepth = 0;
    converter.stack.clear();
    const uint32_t renderStart = millis();
    ok = parse(converter, *parseSource, &finalCrc, &finalSize);
    LOG_DBG("FB2", "Render + source verification pass: %lu ms", static_cast<unsigned long>(millis() - renderStart));
  }
  if (converter.out.f.isOpen()) converter.out.f.close();
  if (ok && (finalSize != parseSourceSize || finalCrc != parseSourceCrc)) {
    char detail[96];
    snprintf(detail, sizeof(detail), "FB2 changed while opening (%lu/%08lx -> %lu/%08lx)",
             static_cast<unsigned long>(parseSourceSize), static_cast<unsigned long>(parseSourceCrc),
             static_cast<unsigned long>(finalSize), static_cast<unsigned long>(finalCrc));
    error = detail;
    ok = false;
  }
  if (ok && zippedFb2) {
    uint32_t finalArchiveSize = 0, finalArchiveCrc = 0;
    ok = computeSourceCrc(src, finalArchiveSize, finalArchiveCrc) && finalArchiveSize == archiveSize &&
         finalArchiveCrc == archiveCrc;
    if (!ok) error = "FB2 ZIP changed while opening";
  }
  if (ok) {
    Writer css;
    ok = css.open(book.cacheDir + "/fb2.css");
    if (ok) {
      css.put(FB2_CSS);
      if (!converter.embeddedCss.empty()) {
        css.put("\n/* Embedded FictionBook stylesheet */\n");
        css.put(converter.embeddedCss);
        css.put("\n");
      }
      ok = css.close();
    }
    if (!ok) error = "Cannot write FB2 reader stylesheet";
  }
  if (ok) {
    book.sourceSize = archiveSize;
    book.sourceCrc = archiveCrc;
    book.title = converter.title;
    book.author = converter.author;
    book.language = converter.language;
    book.cover = converter.cover;
    book.chapters.reserve(converter.chapters.size());
    for (const auto& chapter : converter.chapters)
      book.chapters.push_back({chapter.title, chapter.note, chapter.sourceXPath});
    book.images.reserve(converter.images.size());
    for (const auto& image : converter.images) book.images.push_back({image.id, image.type, image.cacheName});
    if (book.chapters.empty()) {
      error = "FB2 has no readable chapters";
      ok = false;
    } else if (!writeReaderMeta(metaPath, book)) {
      error = "Cannot write FB2 reader index";
      ok = false;
    }
  }
  if (!ok) {
    if (error.empty()) error = converter.error.empty() ? "FB2 reader preparation failed" : converter.error;
    Storage.removeDir(book.cacheDir.c_str());
    return false;
  }
  return true;
}

bool Fb2Converter::convert(const std::string& source, std::string& epub, std::string& error) {
  epub.clear();
  error.clear();
  HalFile src;
  if (!Storage.openFileForRead("FB2", source, src)) {
    error = "Cannot open FB2";
    return false;
  }
  if (src.size() > 128u * 1024 * 1024) {
    error = "FB2 exceeds the 128 MB limit";
    return false;
  }
  const uint32_t sourceSize = src.size();
  uint8_t b[2048];
  uint32_t crc = 0xffffffffu;
  while (src.position() < src.size()) {
    int n = src.read(b, sizeof(b));
    if (n <= 0) {
      error = "Cannot read FB2";
      return false;
    }
    crc = crcStep(crc, b, n);
    delay(1);
  }
  char key[64];
  snprintf(
      key, sizeof(key), "/fb2-publish-v1-%08lx-%08lx",
      static_cast<unsigned long>(~crcStep(0xffffffffu, reinterpret_cast<const uint8_t*>(source.data()), source.size())),
      static_cast<unsigned long>(~crc));
  auto holder = std::unique_ptr<Converter>(new (std::nothrow) Converter);
  if (!holder) {
    error = "Not enough memory for FB2 conversion";
    return false;
  }
  auto& c = *holder;
  c.dir = std::string("/.crosspoint") + key;
  // Publish the converted book next to its source, never in the hidden cache.
  // An existing sibling EPUB may be unrelated: refuse rather than overwrite
  // it or silently open a different book.
  if (source.size() < 4 || source[source.size() - 4] != '.' ||
      std::tolower(static_cast<unsigned char>(source[source.size() - 3])) != 'f' ||
      std::tolower(static_cast<unsigned char>(source[source.size() - 2])) != 'b' || source[source.size() - 1] != '2') {
    error = "Invalid FB2 source extension";
    return false;
  }
  epub = source.substr(0, source.size() - 4) + ".epub";
  if (Storage.exists(epub.c_str())) {
    error = "EPUB already exists beside FB2; existing file was not modified";
    return false;
  }
  Storage.removeDir(c.dir.c_str());
  if (!Storage.mkdir(c.dir.c_str())) {
    error = "Cannot create FB2 cache on SD card";
    return false;
  }
  bool ok = parse(c, src);
  if (ok) {
    c.render = true;
    c.nextChapter = 0;
    c.bodyDepth = 0;
    c.sectionDepth = 0;
    c.stack.clear();
    ok = parse(c, src);
  }
  const std::string staged = c.dir + "/book.epub.part";
  uint32_t archiveSize = 0, archiveChecksum = 0;
  if (ok) ok = package(c, staged, archiveSize, archiveChecksum);
  // Recheck the destination after conversion: rename must not replace a
  // user-supplied EPUB that appeared while we were working.
  if (ok && Storage.exists(epub.c_str())) {
    error = "EPUB destination appeared during conversion";
    ok = false;
  }
  if (ok) ok = Storage.renameNoReplace(staged.c_str(), epub.c_str());
  if (c.out.f.isOpen()) c.out.f.close();
  if (!ok) {
    if (error.empty()) error = c.error.empty() ? "FB2 conversion failed (check SD card free space)" : c.error;
    Storage.removeDir(c.dir.c_str());
    return false;
  }
  // Verify again through the published path before hiding the source. A failed
  // verification leaves the FB2 visible and the suspect EPUB available for recovery.
  if (!Zip::verify(epub, archiveSize, archiveChecksum)) {
    error = "Published EPUB failed validation; FB2 was preserved";
    Storage.removeDir(c.dir.c_str());
    return false;
  }
  src.close();
  const auto slash = source.find_last_of('/');
  const std::string hidden = source.substr(0, slash + 1) + "." + source.substr(slash + 1);
  // Renaming is reversible and does not truncate the source. A backup collision
  // or rename failure leaves both books visible; conversion itself succeeded.
  if (Zip::verify(source, sourceSize, crc) && !Storage.exists(hidden.c_str())) {
    Storage.renameNoReplace(source.c_str(), hidden.c_str());
  }
  // Storage.renameNoReplace records content changes for the library freshness generation.
  // Retain only the finished EPUB and hidden original.
  for (size_t i = 0; i < c.chapters.size(); ++i) Storage.remove((c.dir + "/" + chapterName(i)).c_str());
  for (size_t i = 0; i < c.images.size(); ++i) Storage.remove((c.dir + "/i" + std::to_string(i)).c_str());
  Storage.remove((c.dir + "/source.txt").c_str());
  Storage.removeDir(c.dir.c_str());
  return true;
}
std::string Fb2Converter::sourcePath(const std::string& epub) {
  if (epub.rfind("/.crosspoint/fb2-v1-", 0) != 0 && epub.rfind("/.crosspoint/fb2-v2-", 0) != 0) return epub;
  auto path = epub.substr(0, epub.find_last_of('/')) + "/source.txt";
  const String original = Storage.readFile(path.c_str());
  return original.length() ? std::string(original.c_str()) : epub;
}
