#pragma once

#include <Memory.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>

// Chunked storage avoids one large contiguous allocation while keeping all
// allocations nothrow in the ESP32 -fno-exceptions build.
template <typename T, size_t ElementsPerChunk>
class ChunkedNothrowArray {
  static_assert(ElementsPerChunk > 0);
  using Chunk = std::unique_ptr<T[]>;

  std::unique_ptr<Chunk[]> chunks_;
  size_t size_ = 0;

 public:
  bool reset(const size_t size, const T& initialValue = T{}) {
    chunks_.reset();
    size_ = 0;
    if (size == 0) return true;

    const size_t chunkCount = 1 + (size - 1) / ElementsPerChunk;
    auto chunks = makeUniqueNoThrow<Chunk[]>(chunkCount);
    if (!chunks) return false;

    for (size_t i = 0; i < chunkCount; ++i) {
      const size_t count = std::min(ElementsPerChunk, size - i * ElementsPerChunk);
      chunks[i] = makeUniqueNoThrow<T[]>(count);
      if (!chunks[i]) return false;
      std::fill_n(chunks[i].get(), count, initialValue);
    }

    chunks_ = std::move(chunks);
    size_ = size;
    return true;
  }

  size_t size() const { return size_; }

  T& operator[](const size_t index) { return chunks_[index / ElementsPerChunk][index % ElementsPerChunk]; }
  const T& operator[](const size_t index) const { return chunks_[index / ElementsPerChunk][index % ElementsPerChunk]; }
};
