#pragma once

#include <cstddef>
#include <limits>

namespace epub_memory_policy {

inline constexpr size_t kDequeNodeBytes = 512;
inline constexpr size_t kBatchLookupReserveBytes = 32 * 1024;

// std::deque stores elements in 512-byte nodes and grows a contiguous map of
// node pointers. Under -fno-exceptions, a failed allocation through its
// throwing allocator terminates the firmware, so the batch path must check
// both total free memory and the largest block before constructing either
// deque. The extra two map slots and one node are deliberate headroom for
// implementation details and allocator metadata.
constexpr size_t dequeLargestBlockRequired(const size_t elementBytes, const size_t elements,
                                           const size_t pointerBytes = sizeof(void*)) {
  if (elementBytes == 0 || pointerBytes == 0) return std::numeric_limits<size_t>::max();
  const size_t elementsPerNode = elementBytes < kDequeNodeBytes ? kDequeNodeBytes / elementBytes : 1;
  const size_t nodeCountFloor = elements / elementsPerNode;
  if (nodeCountFloor == std::numeric_limits<size_t>::max()) return std::numeric_limits<size_t>::max();
  const size_t nodes = nodeCountFloor + 1;  // libstdc++ allocates one node even for an empty deque.
  if (nodes > (std::numeric_limits<size_t>::max() - 2) / 2) return std::numeric_limits<size_t>::max();
  const size_t estimatedMapPointers = 2 * nodes + 2;  // covers libstdc++ map growth while resizing.
  const size_t mapPointers = estimatedMapPointers < 8 ? 8 : estimatedMapPointers;
  const size_t nodeBytes = elementBytes > kDequeNodeBytes ? elementBytes : kDequeNodeBytes;
  if (mapPointers > (std::numeric_limits<size_t>::max() - nodeBytes) / pointerBytes) {
    return std::numeric_limits<size_t>::max();
  }
  return mapPointers * pointerBytes + nodeBytes;
}

constexpr bool canUseBatchDequeLookup(const size_t spineCount, const size_t freeBytes, const size_t largestFreeBlock,
                                      const size_t firstElementBytes, const size_t secondElementBytes,
                                      const size_t pointerBytes = sizeof(void*)) {
  if (spineCount == 0 || firstElementBytes > std::numeric_limits<size_t>::max() - secondElementBytes) return false;
  const size_t bytesPerSpine = firstElementBytes + secondElementBytes;
  if (bytesPerSpine == 0) return false;
  if (spineCount > (std::numeric_limits<size_t>::max() - kBatchLookupReserveBytes) / bytesPerSpine) return false;

  const size_t tableAndReserve = spineCount * bytesPerSpine + kBatchLookupReserveBytes;
  const size_t firstMap = dequeLargestBlockRequired(firstElementBytes, spineCount, pointerBytes);
  const size_t secondMap = dequeLargestBlockRequired(secondElementBytes, spineCount, pointerBytes);
  const size_t mapRequired = firstMap > secondMap ? firstMap : secondMap;
  return freeBytes > tableAndReserve && largestFreeBlock >= mapRequired;
}

constexpr bool canUseBatchSpineSizeLookup(const size_t spineCount, const size_t freeBytes,
                                          const size_t largestFreeBlock, const size_t targetElementBytes,
                                          const size_t sizeElementBytes, const size_t pointerBytes = sizeof(void*)) {
  return canUseBatchDequeLookup(spineCount, freeBytes, largestFreeBlock, targetElementBytes, sizeElementBytes,
                                pointerBytes);
}

}  // namespace epub_memory_policy
