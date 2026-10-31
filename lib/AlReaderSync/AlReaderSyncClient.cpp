#include "AlReaderSyncClient.h"

#include <HalClock.h>
#include <SecureHttpClient.h>

#include <charconv>
#include <limits>
#include <string_view>

#include "CrossPointRoots.h"

namespace AlReaderSync {
namespace {
constexpr char kFolder[] = "AlReaderX-01Sync";
constexpr char kUserAgent[] = "X4Libris-AlReaderWebDAV/1";
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

std::string pathEncode(const std::string& input) {
  static constexpr char hex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(input.size() * 3);
  for (const uint8_t c : input) {
    if (isUnreserved(c)) {
      out.push_back(static_cast<char>(c));
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

bool validBaseUrlImpl(const std::string& url) {
  if (url.size() < 9 || url.size() > 256 || url.compare(0, 8, "https://") != 0) return false;
  const size_t hostEnd = url.find('/', 8);
  const std::string_view host(url.data() + 8, (hostEnd == std::string::npos ? url.size() : hostEnd) - 8);
  if (host.empty() || host.find('@') != std::string_view::npos || host.find(':') == 0) return false;
  if (url.find_first_of("?#") != std::string::npos) return false;
  for (const unsigned char c : url) if (c < 0x21 || c == 0x7f || c == '\\') return false;
  return true;
}

std::string folderUrl(std::string base) {
  if (base.back() != '/') base += '/';
  return base + kFolder + '/';
}

void configure(crosspoint::SecureHttpClient& http, const std::string& username, const std::string& password) {
  http.setCACert(CROSSPOINT_ROOTS_PEM);
  http.setAllowInsecureFallback(false);
  http.setAllowCertificateDateErrors(!HalClock::isPlausibleForTls());
  http.setTimeout(5000);
  http.setTotalTimeout(12000);
  http.setMaxBufferedBody(kMaxReplyBytes);
  http.setMaxRedirects(0);
  http.setUserAgent(kUserAgent);
  http.setQuietRequests(true);
  http.setBasicAuth(username, password);
}

Result request(const bool update, const std::string& baseUrl, const std::string& username,
               const std::string& password, const std::string& bookKey, const PositionRecord* record) {
  Result result;
  if (!validBaseUrlImpl(baseUrl) || !validToken(username, 160) || !validToken(password, 160) ||
      !validToken(bookKey, 256) || bookKey == "." || bookKey == "..") {
    result.error = Error::InvalidInput;
    result.message = "Invalid WebDAV address, credentials or book key";
    return result;
  }
  if (record && record->readPositionStart > static_cast<uint32_t>(INT32_MAX)) {
    result.error = Error::InvalidInput;
    result.message = "AlReader text position is out of range";
    return result;
  }

  const std::string folder = folderUrl(baseUrl);
  const std::string file = folder + pathEncode(bookKey);
  crosspoint::SecureHttpClient http;
  configure(http, username, password);
  if (update) {
    // The folder is created lazily, as in AlReader's own WebDAV provider.
    const int mkdirStatus = http.request("MKCOL", folder);
    if (mkdirStatus != 201 && mkdirStatus != 405 && mkdirStatus != 200 && mkdirStatus != 204) {
      result.httpStatus = mkdirStatus >= 100 ? mkdirStatus : 0;
      result.error = mkdirStatus < 100 ? Error::Transport : Error::HttpStatus;
      result.message = "Could not create AlReader WebDAV folder";
      return result;
    }
    http.addHeader("Content-Type", "*/*");
  } else {
    http.addHeader("Accept", "text/plain, */*");
  }
  const int status = update ? http.PUT(file, encodePositionRecord(*record)) : http.GET(file);
  result.httpStatus = status >= 100 ? status : 0;
  if (status < 100) {
    result.error = Error::Transport;
    result.message = "WebDAV connection failed";
    return result;
  }
  if (!update && status == 404) return result;
  if (update ? (status != 200 && status != 201 && status != 204) : status != 200) {
    result.error = Error::HttpStatus;
    result.message = status == 401 || status == 403 ? "WebDAV credentials rejected" : "WebDAV HTTP error";
    return result;
  }
  if (!update && http.bodyTruncated()) {
    result.error = Error::InvalidReply;
    result.message = "WebDAV position record is too large";
    return result;
  }
  if (!update && !decodePositionRecord(std::string(trimmed(http.getBody())), result.record)) {
    result.error = Error::InvalidReply;
    result.message = "WebDAV position record is invalid";
    return result;
  }
  result.hasRecord = !update;
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
  if (!parseSigned(text, left, split, parsed.syncId) ||
      !parseUnsigned(text, split + 1, right, parsed.readPositionStart) ||
      parsed.readPositionStart > static_cast<uint32_t>(INT32_MAX)) {
    return false;
  }
  out = parsed;
  return true;
}

bool AlReaderWebDavClient::validBaseUrl(const std::string& baseUrl) {
  return validBaseUrlImpl(baseUrl);
}

Result AlReaderWebDavClient::fetch(const std::string& baseUrl, const std::string& username,
                                  const std::string& password, const std::string& bookKey) {
  return request(false, baseUrl, username, password, bookKey, nullptr);
}

Result AlReaderWebDavClient::update(const std::string& baseUrl, const std::string& username,
                                   const std::string& password, const std::string& bookKey,
                                   const PositionRecord& record) {
  return request(true, baseUrl, username, password, bookKey, &record);
}

}  // namespace AlReaderSync
