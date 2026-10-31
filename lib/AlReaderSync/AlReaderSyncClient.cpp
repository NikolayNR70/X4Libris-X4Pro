#include "AlReaderSyncClient.h"

#include <HalClock.h>
#include <SecureHttpClient.h>

#include <charconv>
#include <limits>
#include <string_view>

#include "CrossPointRoots.h"

namespace AlReaderSync {
namespace {
constexpr char kEndpoint[] = "https://alreader.com/aldb/index2.php";
constexpr char kUserAgent[] = "X4Libris-AlReaderSync/1";
constexpr size_t kMaxReplyBytes = 256;

std::string_view trimmed(std::string_view value) {
  while (!value.empty() && static_cast<unsigned char>(value.front()) <= 0x20) value.remove_prefix(1);
  while (!value.empty() && static_cast<unsigned char>(value.back()) <= 0x20) value.remove_suffix(1);
  return value;
}

bool isUnreserved(const uint8_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
         c == '.' || c == '~';
}

std::string formEncode(const std::string& input) {
  static constexpr char hex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(input.size() * 3);
  for (const uint8_t c : input) {
    if (isUnreserved(c)) {
      out.push_back(static_cast<char>(c));
    } else if (c == ' ') {
      out.push_back('+');
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0x0f]);
    }
  }
  return out;
}

bool validToken(const std::string& s, const size_t maxLength) {
  if (s.empty() || s.size() > maxLength) return false;
  for (const unsigned char c : s) {
    if (c < 0x20 || c == 0x7f) return false;
  }
  return true;
}

bool parseSigned(const std::string& s, const size_t begin, const size_t end, int64_t& out) {
  if (begin >= end) return false;
  const char* first = s.data() + begin;
  const char* last = s.data() + end;
  const auto parsed = std::from_chars(first, last, out, 10);
  return parsed.ec == std::errc{} && parsed.ptr == last;
}

bool parseUnsigned(const std::string& s, const size_t begin, const size_t end, uint32_t& out) {
  if (begin >= end) return false;
  uint64_t value = 0;
  const char* first = s.data() + begin;
  const char* last = s.data() + end;
  const auto parsed = std::from_chars(first, last, value, 10);
  if (parsed.ec != std::errc{} || parsed.ptr != last || value > std::numeric_limits<uint32_t>::max()) return false;
  out = static_cast<uint32_t>(value);
  return true;
}

Result request(const std::string& operation, const std::string& identifier, const std::string& bookKey,
               const PositionRecord* record) {
  Result result;
  if (!validToken(identifier, 160) || !validToken(bookKey, 256)) {
    result.error = Error::InvalidInput;
    result.message = "Invalid sync identifier or book key";
    return result;
  }
  if (record && (record->syncId < INT32_MIN || record->syncId > INT32_MAX ||
                 record->readPositionStart > static_cast<uint32_t>(INT32_MAX))) {
    result.error = Error::InvalidInput;
    result.message = "AlReader text position is out of range";
    return result;
  }

  // Match the AlReader client contract: empty-body POST, parameters in the
  // query, explicit zero content length, fixed HTTPS host, redirects disabled.
  std::string url = std::string(kEndpoint) + "?o=" + operation + "&u=" + formEncode(identifier) +
                    "&p=null&n=" + formEncode(bookKey) + "&a=AlReaderX";
  if (record) url += "&v=" + formEncode(encodePositionRecord(*record));

  crosspoint::SecureHttpClient http;
  http.setCACert(CROSSPOINT_ROOTS_PEM);
  http.setAllowInsecureFallback(false);
  http.setAllowCertificateDateErrors(!HalClock::isPlausibleForTls());
  http.setTimeout(5000);
  http.setTotalTimeout(12000);
  http.setMaxBufferedBody(kMaxReplyBytes);
  http.setMaxRedirects(0);
  http.setUserAgent(kUserAgent);
  http.setQuietRequests(true);
  http.addHeader("Accept", "text/plain, */*");
  http.addHeader("Content-Length", "0");

  const int status = http.POST(url, "");
  result.httpStatus = status >= 100 ? status : 0;
  if (status < 100) {
    result.error = Error::Transport;
    result.message = "AlReader.com connection failed";
    return result;
  }
  if (status != 200) {
    result.error = Error::HttpStatus;
    result.message = "AlReader.com returned an HTTP error";
    return result;
  }
  if (http.bodyTruncated()) {
    result.error = Error::InvalidReply;
    result.message = "AlReader.com reply exceeded the expected size";
    return result;
  }

  const std::string_view reply = trimmed(http.getBody());
  if (operation == "get") {
    if (reply == "0 login incorrect") {
      result.error = Error::LoginRejected;
      result.message = "AlReader.com rejected the sync identifier";
      return result;
    }
    if (reply == "0 value not found") {
      result.hasRecord = false;
      return result;
    }
    if (reply.size() > 2 && reply.substr(0, 2) == "1 " &&
        decodePositionRecord(std::string(trimmed(reply.substr(2))), result.record)) {
      result.hasRecord = true;
      return result;
    }
    result.error = Error::InvalidReply;
    result.message = "AlReader.com returned an unrecognized position record";
    return result;
  }

  // The live service returns `1 ok` on setUser/put and can report failure with
  // HTTP 200. Do not treat that failure as a successful upload.
  if (reply != "1 ok") {
    result.error = Error::InvalidReply;
    result.message = "AlReader.com rejected the sync operation";
  }
  return result;
}
}  // namespace

std::string encodePositionRecord(const PositionRecord& record) {
  return "!" + std::to_string(record.syncId) + "!" + std::to_string(record.readPositionStart);
}

bool decodePositionRecord(const std::string& text, PositionRecord& out) {
  size_t first = 0;
  size_t last = text.size();
  while (first < last && static_cast<unsigned char>(text[first]) <= 0x20) ++first;
  while (last > first && static_cast<unsigned char>(text[last - 1]) <= 0x20) --last;
  if (first == last) return false;

  size_t left = first;
  size_t right = last;
  const char separator = text[left] == '!' ? '!' :
                         (text.find('#', left) < right ? '#' : '!');
  if (text[left] == '!') {
    ++left;
    if (right > left && text[right - 1] == '!') --right;
  }
  const size_t split = text.find(separator, left);
  if (split == std::string::npos || split >= right) return false;

  PositionRecord parsed;
  if (!parseSigned(text, left, split, parsed.syncId) || parsed.syncId < INT32_MIN || parsed.syncId > INT32_MAX ||
      !parseUnsigned(text, split + 1, right, parsed.readPositionStart) ||
      parsed.readPositionStart > static_cast<uint32_t>(INT32_MAX)) {
    return false;
  }
  out = parsed;
  return true;
}

Result AlReaderComClient::registerIdentifier(const std::string& identifier, const std::string& bookKey) {
  return request("setUser", identifier, bookKey, nullptr);
}

Result AlReaderComClient::fetch(const std::string& identifier, const std::string& bookKey) {
  return request("get", identifier, bookKey, nullptr);
}

Result AlReaderComClient::update(const std::string& identifier, const std::string& bookKey,
                                 const PositionRecord& record) {
  return request("put", identifier, bookKey, &record);
}

}  // namespace AlReaderSync
