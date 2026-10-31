#pragma once

#include <cstdint>
#include <string>

namespace AlReaderSync {

// AlReader stores a signed sync identifier and a non-negative
// integer `readPositionStart`. APK 1.33 copies this from AlBookEng.bookPosition,
// which indexes AlReader's parsed mainPartition char[] buffer. It is not a page
// number or source-file byte offset. Its text-normalization rules have not yet
// been proven equivalent to X4Libris' EPUB/FB2 text streams.
struct PositionRecord {
  int64_t syncId = 0;
  uint32_t readPositionStart = 0;
};

enum class Error : uint8_t {
  None,
  InvalidInput,
  Transport,
  HttpStatus,
  InvalidReply,
};

struct Result {
  Error error = Error::None;
  int httpStatus = 0;
  PositionRecord record{};
  bool hasRecord = false;
  // Service replies are small status strings/records. Never put request URL,
  // identifier, book key, sync ID, or position in this diagnostic field.
  std::string message;
  explicit operator bool() const { return error == Error::None; }
};

// Encode/decode only the per-book position record. This deliberately has no
// API for AlReader's unrelated bookmark database or other Sync/ artifacts.
std::string encodePositionRecord(const PositionRecord& record);
bool decodePositionRecord(const std::string& text, PositionRecord& out);

// AlReaderXEInk 1.33 WebDAV transport: AlReaderX-01Sync/<book key> contains
// the UTF-8 position record. These calls run only on the network worker.
class AlReaderWebDavClient {
 public:
  static bool validBaseUrl(const std::string& baseUrl);
  static Result fetch(const std::string& baseUrl, const std::string& username,
                      const std::string& password, const std::string& bookKey);
  static Result update(const std::string& baseUrl, const std::string& username,
                       const std::string& password, const std::string& bookKey,
                       const PositionRecord& record);
};

}  // namespace AlReaderSync
