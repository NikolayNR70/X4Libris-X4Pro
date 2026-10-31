#pragma once

#include <cstdint>
#include <string>

namespace AlReaderSync {

// The AlReader.com service stores a signed sync identifier and a non-negative
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
  LoginRejected,
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

// HTTPS-only AlReader.com protocol as observed in AlReaderXEInk 1.33. Calls
// are synchronous and must run in a worker/activity that keeps the UI responsive.
// They make exactly one request and do not follow redirects.
class AlReaderComClient {
 public:
  static Result registerIdentifier(const std::string& identifier, const std::string& bookKey);
  static Result fetch(const std::string& identifier, const std::string& bookKey);
  static Result update(const std::string& identifier, const std::string& bookKey,
                      const PositionRecord& record);
};

}  // namespace AlReaderSync
