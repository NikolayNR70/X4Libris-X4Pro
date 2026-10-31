#include "AlReaderBookKey.h"

#include <HalStorage.h>
#include <Print.h>
#include <ZipFile.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <string_view>

#include "AlReaderUnicodeRanges.h"
#include "AlReaderBookId.h"

namespace AlReaderSync {
namespace {
bool endsWithIgnoreCase(std::string_view value, std::string_view suffix) {
  if (value.size() < suffix.size()) return false;
  value.remove_prefix(value.size() - suffix.size());
  for (size_t i = 0; i < suffix.size(); ++i) {
    const char ch = value[i] >= 'A' && value[i] <= 'Z' ? static_cast<char>(value[i] + ('a' - 'A')) : value[i];
    if (ch != suffix[i]) return false;
  }
  return true;
}

uint32_t decodeUtf8(const std::string_view text, size_t offset, size_t& length) {
  const auto byte = [&](size_t i) { return static_cast<uint8_t>(text[i]); };
  const uint8_t lead = byte(offset);
  if (lead < 0x80) {
    length = 1;
    return lead;
  }
  const size_t remaining = text.size() - offset;
  if ((lead & 0xE0) == 0xC0 && remaining >= 2 && (byte(offset + 1) & 0xC0) == 0x80) {
    length = 2;
    return ((lead & 0x1F) << 6) | (byte(offset + 1) & 0x3F);
  }
  if ((lead & 0xF0) == 0xE0 && remaining >= 3 && (byte(offset + 1) & 0xC0) == 0x80 &&
      (byte(offset + 2) & 0xC0) == 0x80) {
    length = 3;
    return ((lead & 0x0F) << 12) | ((byte(offset + 1) & 0x3F) << 6) | (byte(offset + 2) & 0x3F);
  }
  if ((lead & 0xF8) == 0xF0 && remaining >= 4 && (byte(offset + 1) & 0xC0) == 0x80 &&
      (byte(offset + 2) & 0xC0) == 0x80 && (byte(offset + 3) & 0xC0) == 0x80) {
    length = 4;
    return ((lead & 0x07) << 18) | ((byte(offset + 1) & 0x3F) << 12) | ((byte(offset + 2) & 0x3F) << 6) |
           (byte(offset + 3) & 0x3F);
  }
  length = 1;
  return 0xFFFD;
}

// AlReader calls Character.getType(char) and accepts Java general categories
// represented by mask 574: Lu, Ll, Lt, Lm, Lo, Mc, Nd. Cover the ranges used by
// the Unicode blocks present in book filenames; supplementary code points are
// UTF-16 surrogate units in Java and are consequently treated as non-alphanumeric.
bool isAlReaderLetterOrDigit(const uint16_t c) {
  const auto* begin = std::begin(kLetterDigitRanges);
  const auto* end = std::end(kLetterDigitRanges);
  const auto* range = std::lower_bound(begin, end, c, [](const UnicodeRange& item, const uint16_t value) {
    return item.last < value;
  });
  return range != end && range->first <= c;
}

void appendHex(std::string& output, uint64_t value) {
  static constexpr char digits[] = "0123456789abcdef";
  char buffer[16];
  size_t length = 0;
  do {
    buffer[length++] = digits[value & 0x0F];
    value >>= 4;
  } while (value != 0);
  while (length != 0) output.push_back(buffer[--length]);
}

void appendUtf8CodeUnit(std::string& output, const uint16_t unit) {
  if (unit < 0x80) {
    output.push_back(static_cast<char>(unit));
  } else if (unit < 0x800) {
    output.push_back(static_cast<char>(0xC0 | (unit >> 6)));
    output.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
  } else {
    output.push_back(static_cast<char>(0xE0 | (unit >> 12)));
    output.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
  }
}

// AlFilesEPUB does not hash the ZIP file. It constructs a virtual XML record
// containing the first container.xml entry, and calcInitialCRC hashes that
// record's size and first 1024 bytes. The index is the zero-based ZIP entry
// number (verified against AlReader 1.33's local database on a test EPUB).
class PrefixSink final : public Print {
 public:
  PrefixSink(uint8_t* bytes, size_t capacity) : bytes_(bytes), capacity_(capacity) {}
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    const size_t copy = std::min(size, capacity_ - used_);
    if (copy) std::memcpy(bytes_ + used_, data, copy);
    used_ += copy;
    done = used_ == capacity_;
    return size;
  }
  size_t used() const { return used_; }
  bool done = false;

 private:
  uint8_t* bytes_;
  size_t capacity_;
  size_t used_ = 0;
};

std::string makeEpubBookKey(const std::string& fullPath, const std::string& bookId) {
  ZipFile zip(fullPath);
  std::string containerEntry;
  int entryIndex = -1;
  int index = 0;
  if (!zip.streamCentralDirectoryNames([&](std::string_view name) {
        if (entryIndex < 0 && (name == "META-INF/container.xml" ||
                               (name.size() > 23 && name.substr(name.size() - 23) == "/META-INF/container.xml"))) {
          containerEntry.assign(name.data(), name.size());
          entryIndex = index;
        }
        ++index;
      }) || entryIndex < 0) return {};

  size_t containerSize = 0;
  if (!zip.getInflatedFileSize(containerEntry.c_str(), &containerSize)) return {};
  const std::string recordPath = containerEntry.front() == '/' ? containerEntry : "/" + containerEntry;
  const std::string start = "<alr:extfile numfiles=\"" + std::to_string(entryIndex) +
                            "\" idref=\"1\" id=\"" + recordPath + "\">\r\n";
  static constexpr char end[] = "\r\n</alr:extfile>\r\n";
  const uint64_t virtualSize = static_cast<uint64_t>(start.size()) + containerSize + sizeof(end) - 1;
  std::array<uint8_t, 1024> prefix{};
  const size_t startBytes = std::min(prefix.size(), start.size());
  std::memcpy(prefix.data(), start.data(), startBytes);
  size_t used = startBytes;
  if (used < prefix.size() && containerSize != 0) {
    PrefixSink sink(prefix.data() + used, prefix.size() - used);
    if (!zip.readFileToStream(containerEntry.c_str(), sink, 512, &sink.done)) return {};
    used += sink.used();
    if (sink.used() != std::min(containerSize, prefix.size() - startBytes)) return {};
  }
  if (used < prefix.size() && containerSize <= prefix.size() - startBytes) {
    const size_t count = std::min(sizeof(end) - 1, prefix.size() - used);
    std::memcpy(prefix.data() + used, end, count);
    used += count;
  }
  return makeAlReaderBookKey(fullPath, virtualSize, prefix.data(), used, bookId);
}

std::string makeFb2ZipBookKey(const std::string& fullPath, const std::string& bookId) {
  ZipFile zip(fullPath);
  std::string innerEntry;
  bool multiple = false;
  if (!zip.streamCentralDirectoryNames([&](std::string_view name) {
        if (!endsWithIgnoreCase(name, ".fb2")) return;
        if (!innerEntry.empty()) multiple = true;
        else innerEntry.assign(name.data(), name.size());
      }) || innerEntry.empty() || multiple) return {};
  size_t innerSize = 0;
  if (!zip.getInflatedFileSize(innerEntry.c_str(), &innerSize)) return {};
  std::array<uint8_t, 1024> prefix{};
  const size_t wanted = std::min(innerSize, prefix.size());
  if (wanted != 0) {
    PrefixSink sink(prefix.data(), wanted);
    if (!zip.readFileToStream(innerEntry.c_str(), sink, 512, &sink.done) || sink.used() != wanted) return {};
  }
  return makeAlReaderBookKey(innerEntry, innerSize, prefix.data(), wanted, bookId);
}
}  // namespace

std::string makeAlReaderBookKey(const std::string& fullPath, const uint64_t fileSize, const uint8_t* firstBytes,
                                const size_t firstBytesLength, const std::string& bookId) {
  if (fileSize != 0 && (firstBytes == nullptr || firstBytesLength == 0)) return {};

  const size_t slash = fullPath.find_last_of("/\\");
  const std::string_view basename(fullPath.data() + (slash == std::string::npos ? 0 : slash + 1),
                                  fullPath.size() - (slash == std::string::npos ? 0 : slash + 1));
  uint64_t hash31 = 0;
  uint64_t hash5 = 0;
  uint64_t charSum = 0;
  uint64_t javaLength = 0;
  std::string safePrefix;
  safePrefix.reserve(32);
  for (size_t offset = 0, chars = 0; offset < basename.size();) {
    size_t bytes = 0;
    const uint32_t cp = decodeUtf8(basename, offset, bytes);
    if (bytes == 0) break;
    const uint16_t units[2] = {
        static_cast<uint16_t>(cp <= 0xFFFF ? cp : 0xD800 + ((cp - 0x10000) >> 10)),
        static_cast<uint16_t>(0xDC00 + ((cp - 0x10000) & 0x3FF))};
    const int unitCount = cp > 0xFFFF && cp <= 0x10FFFF ? 2 : 1;
    javaLength += static_cast<uint64_t>(unitCount);
    for (int unitIndex = 0; unitIndex < unitCount; ++unitIndex, ++chars) {
      const uint16_t unit = units[unitIndex];
      hash31 = hash31 * 31u + unit;
      hash5 = hash5 * 5u + unit;
      charSum += unit;
      if (chars < 32) {
        if (isAlReaderLetterOrDigit(unit)) appendUtf8CodeUnit(safePrefix, unit);
        else safePrefix.push_back('_');
      }
    }
    offset += bytes;
  }

  uint64_t byteHash = 0;
  const size_t bytesToHash = std::min<size_t>({firstBytesLength, static_cast<size_t>(1024),
                                               static_cast<size_t>(std::min<uint64_t>(fileSize, 1024))});
  for (size_t i = 0; i < bytesToHash; ++i) {
    const uint8_t byte = firstBytes[i];
    if (byte != '/' && byte != '\\') byteHash = 403839348u ^ (byteHash * 5u + byte);
  }

  std::string key;
  key.reserve(128 + safePrefix.size());
  key += safePrefix;
  key.push_back('.');
  appendHex(key, hash31);
  key += "..";
  // AlFormat.updateCRC() replaces everything after the *first* dot of this
  // double-dot marker when the parsed document UUID exists. This also makes
  // a direct FB2 and the same book inside fb2.zip share one sync record.
  if (!bookId.empty()) {
    key.pop_back();
    key.push_back('!');
    key += bookId;
    return key;
  }
  appendHex(key, hash5);
  key.push_back('.');
  appendHex(key, charSum);
  key.push_back('.');
  appendHex(key, javaLength);
  key.push_back('.');
  appendHex(key, fileSize);
  key.push_back('.');
  appendHex(key, byteHash);
  return key;
}

std::string makeAlReaderBookKeyFromFile(const std::string& fullPath, const std::string& bookId) {
  const std::string resolvedId = bookId.empty() ? findAlReaderBookId(fullPath) : bookId;
  if (endsWithIgnoreCase(fullPath, ".epub")) return makeEpubBookKey(fullPath, resolvedId);
  if (endsWithIgnoreCase(fullPath, ".fb2.zip") || endsWithIgnoreCase(fullPath, ".zip"))
    return makeFb2ZipBookKey(fullPath, resolvedId);
  HalFile file = Storage.open(fullPath.c_str());
  if (!file) return {};
  const uint64_t size = file.size64();
  std::array<uint8_t, 1024> prefix{};
  const size_t wanted = static_cast<size_t>(std::min<uint64_t>(size, prefix.size()));
  size_t received = 0;
  while (received < wanted) {
    const int count = file.read(prefix.data() + received, wanted - received);
    if (count <= 0) return {};
    received += static_cast<size_t>(count);
  }
  return makeAlReaderBookKey(fullPath, size, prefix.data(), received, resolvedId);
}

}  // namespace AlReaderSync
