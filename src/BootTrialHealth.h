#pragma once
#include <cstdint>
#include <atomic>

// A new image is accepted only after 30 seconds from its first boot with a
// usable UI and a responsive main loop. Iteration counts are deliberately not
// part of the decision.
class BootTrialHealth {
 public:
  static constexpr uint32_t kHealthyWindowMs = 30000;

 private:
  static constexpr uint32_t kLoopStallLimitMs = 2000;
  std::atomic<uint32_t> since{0};
  std::atomic<uint32_t> lastLoopAt{0};
  std::atomic<bool> started{false};
  std::atomic<bool> lastLoopReady{false};
  std::atomic<bool> sawReady{false};
  std::atomic<bool> haveLoop{false};
  std::atomic<bool> faulted{false};

 public:
  void reset(uint32_t now) {
    since.store(now, std::memory_order_relaxed);
    lastLoopAt.store(now, std::memory_order_relaxed);
    lastLoopReady.store(false, std::memory_order_relaxed);
    sawReady.store(false, std::memory_order_relaxed);
    haveLoop.store(false, std::memory_order_relaxed);
    faulted.store(false, std::memory_order_relaxed);
    started.store(true, std::memory_order_release);
  }

  void observe(uint32_t now, bool ready) {
    if (!started) reset(now);
    if (haveLoop.load(std::memory_order_acquire)) {
      const uint32_t previousLoop = lastLoopAt.load(std::memory_order_relaxed);
      if (static_cast<uint32_t>(now - previousLoop) > kLoopStallLimitMs)
        faulted.store(true, std::memory_order_release);
    }
    lastLoopAt.store(now, std::memory_order_relaxed);
    lastLoopReady.store(ready, std::memory_order_release);
    if (ready) sawReady.store(true, std::memory_order_release);
    haveLoop.store(true, std::memory_order_release);
  }

  bool healthyAtDeadline(uint32_t now) const {
    if (!started.load(std::memory_order_acquire)) return false;
    const uint32_t startAt = since.load(std::memory_order_relaxed);
    const uint32_t lastAt = lastLoopAt.load(std::memory_order_relaxed);
    const bool ready = lastLoopReady.load(std::memory_order_acquire);
    return !faulted.load(std::memory_order_acquire) && sawReady.load(std::memory_order_acquire) &&
           static_cast<uint32_t>(now - startAt) >= kHealthyWindowMs && ready &&
           static_cast<uint32_t>(now - lastAt) <= kLoopStallLimitMs;
  }
};
