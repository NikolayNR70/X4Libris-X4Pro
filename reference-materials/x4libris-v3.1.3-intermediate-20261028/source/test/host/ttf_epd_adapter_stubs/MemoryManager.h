#pragma once

#include <cstddef>

namespace freeink {
class MemoryManager {
 public:
  static MemoryManager& instance() {
    static MemoryManager manager;
    return manager;
  }
  size_t ensureFree(size_t) { return 0; }
};
}  // namespace freeink
