#pragma once
#include <cstdint>
#include <atomic>

// A new image is accepted only after 30 seconds from its first boot with a
// usable UI and a responsive main loop. Iteration counts are deliberately not
// part of the decision.
class BootTrialHealth {
 public:
  static constexpr uint32_t kHealthyWindowMs = 30000;
  static constexpr uint32_t kMaxUiWaitMs = 60000;

 private:
  static constexpr uint32_t kLoopStallLimitMs = 2000;
  // Book restoration can legitimately block the main loop for several seconds.
  // Require a sustained recovery before accepting, rather than permanently
  // condemning an otherwise responsive trial for that one operation.
  static constexpr uint32_t kRecoveryWindowMs = 5000;
  std::atomic<uint32_t> since{0};
  std::atomic<uint32_t> lastLoopAt{0};
  std::atomic<uint32_t> lastStallAt{0};
  std::atomic<bool> started{false};
  std::atomic<bool> lastLoopReady{false};
  std::atomic<bool> sawReady{false};
  std::atomic<bool> haveLoop{false};
  std::atomic<bool> sawStall{false};

 public:
  void reset(uint32_t now) {
    since.store(now, std::memory_order_relaxed);
    lastLoopAt.store(now, std::memory_order_relaxed);
    lastStallAt.store(now, std::memory_order_relaxed);
    lastLoopReady.store(false, std::memory_order_relaxed);
    sawReady.store(false, std::memory_order_relaxed);
    haveLoop.store(false, std::memory_order_relaxed);
    sawStall.store(false, std::memory_order_relaxed);
    started.store(true, std::memory_order_release);
  }

  void observe(uint32_t now, bool ready) {
    if (!started) reset(now);
    if (haveLoop.load(std::memory_order_acquire)) {
      const uint32_t previousLoop = lastLoopAt.load(std::memory_order_relaxed);
      if (static_cast<uint32_t>(now - previousLoop) > kLoopStallLimitMs) {
        lastStallAt.store(now, std::memory_order_relaxed);
        sawStall.store(true, std::memory_order_release);
      }
    }
    lastLoopAt.store(now, std::memory_order_relaxed);
    lastLoopReady.store(ready, std::memory_order_release);
    if (ready) sawReady.store(true, std::memory_order_release);
    haveLoop.store(true, std::memory_order_release);
  }

  const char* failureAtDeadline(uint32_t now) const {
    if (!started.load(std::memory_order_acquire)) return "health observation never started";
    const uint32_t startAt = since.load(std::memory_order_relaxed);
    const uint32_t lastAt = lastLoopAt.load(std::memory_order_relaxed);
    const bool ready = lastLoopReady.load(std::memory_order_acquire);
    if (static_cast<uint32_t>(now - startAt) < kHealthyWindowMs) return "trial window not elapsed";
    if (!sawReady.load(std::memory_order_acquire)) return "no rendered, ready UI";
    if (!haveLoop.load(std::memory_order_acquire) ||
        static_cast<uint32_t>(now - lastAt) > kLoopStallLimitMs) return "main loop not responsive";
    if (!ready) return "UI not ready";
    if (sawStall.load(std::memory_order_acquire) &&
        static_cast<uint32_t>(now - lastStallAt.load(std::memory_order_relaxed)) < kRecoveryWindowMs)
      return "main loop has not recovered for five seconds";
    return nullptr;
  }

  bool healthyAtDeadline(uint32_t now) const { return failureAtDeadline(now) == nullptr; }

  uint32_t elapsedMs(uint32_t now) const {
    return static_cast<uint32_t>(now - since.load(std::memory_order_relaxed));
  }

  // A new render can be in progress exactly at the 30-second deadline. Give a
  // responsive main loop a bounded chance to finish it; an actual hang still
  // fails immediately, and a perpetually unfinished UI fails by 60 seconds.
  bool mayWaitForUi(uint32_t now) const {
    if (!started.load(std::memory_order_acquire) || elapsedMs(now) >= kMaxUiWaitMs ||
        !haveLoop.load(std::memory_order_acquire)) return false;
    return static_cast<uint32_t>(now - lastLoopAt.load(std::memory_order_relaxed)) <= kLoopStallLimitMs;
  }
};
