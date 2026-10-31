#include "AlReaderBookId.h"

#include <HalStorage.h>
#include <Print.h>
#include <SaxParser/SaxParser.h>
#include <ZipFile.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string_view>

#include "Epub/parsers/ContainerParser.h"

namespace AlReaderSync {
namespace {
constexpr size_t kMaxMetadataBytes = 256 * 1024;

bool endsWithIgnoreCase(std::string_view value, std::string_view suffix) {
  if (value.size() < suffix.size()) return false;
  value.remove_prefix(value.size() - suffix.size());
  for (size_t i = 0; i < suffix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(value[i])) != suffix[i]) return false;
  }
  return true;
}

const char* localName(const char* name) {
  const char* colon = std::strrchr(name, ':');
  return colon ? colon + 1 : name;
}

std::string normalizedId(std::string_view value) {
  const size_t colon = value.find_last_of(':');
  if (colon != std::string_view::npos) value.remove_prefix(colon + 1);
  std::string result;
  result.reserve(36);
  for (const char ch : value) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (std::isxdigit(c) || ch == '-') result.push_back(static_cast<char>(std::tolower(c)));
  }
  return result.size() == 36 ? result : std::string{};
}

class IdentifierParser final : public Print {
 public:
  explicit IdentifierParser(bool fb2) : fb2_(fb2) {}
  bool setup() { return sax_.init(this, onStart, onEnd, onCharacters, nullptr, false, SaxParser::Profile::Lean); }
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* buffer, size_t length) override {
    if (done) return length;
    if (seen_ >= kMaxMetadataBytes) {
      done = true;
      return length;
    }
    const size_t permitted = std::min(length, kMaxMetadataBytes - seen_);
    if (!sax_.feed(buffer, permitted)) done = true;
    seen_ += permitted;
    if (seen_ >= kMaxMetadataBytes) done = true;
    return length;
  }
  std::string id;
  bool done = false;

 private:
  static void onStart(void* context, const char* name, const char**) {
    auto& self = *static_cast<IdentifierParser*>(context);
    const char* element = localName(name);
    if (self.fb2_) {
      if (std::strcmp(element, "document-info") == 0) self.inDocumentInfo_ = true;
      if (self.inDocumentInfo_ && std::strcmp(element, "id") == 0) {
        self.inIdentifier_ = true;
        self.value_.clear();
      }
    } else if (std::strcmp(element, "identifier") == 0) {
      self.inIdentifier_ = true;
      self.value_.clear();
    }
  }
  static void onEnd(void* context, const char* name) {
    auto& self = *static_cast<IdentifierParser*>(context);
    const char* element = localName(name);
    if (self.inIdentifier_ && std::strcmp(element, self.fb2_ ? "id" : "identifier") == 0) {
      self.id = normalizedId(self.value_);
      self.inIdentifier_ = false;
      if (!self.id.empty()) {
        self.done = true;
        self.sax_.stop();
      }
    }
    if (self.fb2_ && std::strcmp(element, "document-info") == 0) self.inDocumentInfo_ = false;
  }
  static void onCharacters(void* context, const char* text, int length) {
    auto& self = *static_cast<IdentifierParser*>(context);
    if (self.inIdentifier_ && length > 0 && self.value_.size() < 256) {
      self.value_.append(text, std::min<size_t>(static_cast<size_t>(length), 256 - self.value_.size()));
    }
  }
  SaxParser sax_;
  bool fb2_ = false;
  bool inDocumentInfo_ = false;
  bool inIdentifier_ = false;
  size_t seen_ = 0;
  std::string value_;
};

std::string readZipEntryId(ZipFile& zip, const std::string& entry, bool fb2) {
  IdentifierParser parser(fb2);
  if (!parser.setup()) return {};
  if (!zip.readFileToStream(entry.c_str(), parser, 1024, &parser.done)) return {};
  return parser.id;
}
}  // namespace

std::string findAlReaderBookId(const std::string& path) {
  if (endsWithIgnoreCase(path, ".fb2")) {
    HalFile file = Storage.open(path.c_str());
    if (!file) return {};
    IdentifierParser parser(true);
    if (!parser.setup()) return {};
    uint8_t bytes[1024];
    while (!parser.done && file.available()) {
      const int count = file.read(bytes, sizeof(bytes));
      if (count <= 0) break;
      parser.write(bytes, static_cast<size_t>(count));
    }
    return parser.id;
  }

  ZipFile zip(path);
  if (endsWithIgnoreCase(path, ".fb2.zip") || endsWithIgnoreCase(path, ".zip")) {
    std::string fb2Entry;
    bool multiple = false;
    if (!zip.streamCentralDirectoryNames([&](std::string_view name) {
          if (!endsWithIgnoreCase(name, ".fb2")) return;
          if (!fb2Entry.empty()) multiple = true;
          else fb2Entry.assign(name.data(), name.size());
        }) || multiple || fb2Entry.empty()) return {};
    return readZipEntryId(zip, fb2Entry, true);
  }
  if (!endsWithIgnoreCase(path, ".epub")) return {};
  size_t containerSize = 0;
  if (!zip.getInflatedFileSize("META-INF/container.xml", &containerSize)) return {};
  ContainerParser container(containerSize);
  if (!container.setup() || !zip.readFileToStream("META-INF/container.xml", container, 512) ||
      container.fullPath.empty()) return {};
  return readZipEntryId(zip, container.fullPath, false);
}
}  // namespace AlReaderSync
