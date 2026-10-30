#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

// Nothrow versions of std::make_unique. Return nullptr on allocation failure
// instead of calling abort() (the default when exceptions are disabled on ESP32).
//
// Adapted from the sibling crosspoint-reader project (lib/Memory/Memory.h),
// introduced here alongside the flat-TextBlock port (crosspoint-reader PR #2547).
//
// Single object:
//   auto obj = makeUniqueNoThrow<PNG>();
//   if (!obj) { LOG_ERR("TAG", "OOM"); return false; }
//
// Array:
//   auto buf = makeUniqueNoThrow<uint8_t[]>(size);
//   if (!buf) { LOG_ERR("TAG", "OOM"); return false; }
//   buf[0] = 0xFF;
//   someApi(buf.get(), size);
//

template <typename T, typename... Args>
  requires(!std::is_array_v<T>)
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}

template <typename T>
  requires std::is_unbounded_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  using Elem = std::remove_extent_t<T>;
  return std::unique_ptr<T>(new (std::nothrow) Elem[count]());
}

// Byte buffer API used by current VARAGH game apps. Allocation remains
// nothrow and prefers PSRAM so search tables do not consume internal RAM.
struct HeapByteBufferDeleter {
  void operator()(uint8_t* ptr) const noexcept {
#ifdef ESP_PLATFORM
    heap_caps_free(ptr);
#else
    std::free(ptr);
#endif
  }
};
using HeapByteBuffer = std::unique_ptr<uint8_t[], HeapByteBufferDeleter>;

inline HeapByteBuffer makePsramByteBufferNoThrow(const size_t count) {
  if (count == 0) return {};
#ifdef ESP_PLATFORM
  return HeapByteBuffer(static_cast<uint8_t*>(heap_caps_malloc(count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
#else
  return HeapByteBuffer(static_cast<uint8_t*>(std::malloc(count)));
#endif
}
