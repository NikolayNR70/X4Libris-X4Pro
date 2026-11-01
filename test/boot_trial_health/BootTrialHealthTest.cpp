#include "../../src/BootTrialHealth.h"

#include <cassert>
#include <cstdint>

int main() {
  // A normal trial is accepted at the deadline, but never before it.
  BootTrialHealth normal;
  normal.reset(0);
  for (uint32_t t = 100; t <= 30000; t += 100) normal.observe(t, true);
  assert(normal.healthyAtDeadline(30000));

  // Restoring a book can occupy a main-loop iteration for several seconds.
  // A responsive UI for the remainder of the trial must recover from it.
  BootTrialHealth restoredBook;
  restoredBook.reset(0);
  for (uint32_t t = 100; t <= 12000; t += 100) restoredBook.observe(t, true);
  restoredBook.observe(16600, true);
  for (uint32_t t = 16700; t <= 30000; t += 100) restoredBook.observe(t, true);
  assert(restoredBook.healthyAtDeadline(30000));

  // A late stall without five seconds of recovery, or a loop still blocked at
  // the deadline, must continue to trigger rollback.
  BootTrialHealth lateStall;
  lateStall.reset(0);
  for (uint32_t t = 100; t <= 27000; t += 100) lateStall.observe(t, true);
  lateStall.observe(29050, true);
  for (uint32_t t = 29150; t <= 29950; t += 100) lateStall.observe(t, true);
  assert(!lateStall.healthyAtDeadline(30000));
  assert(!lateStall.healthyAtDeadline(33000));

  BootTrialHealth hung;
  hung.reset(0);
  for (uint32_t t = 100; t <= 25000; t += 100) hung.observe(t, true);
  assert(!hung.healthyAtDeadline(30000));

  BootTrialHealth rendering;
  rendering.reset(0);
  for (uint32_t t = 100; t <= 30000; t += 100) rendering.observe(t, t < 29900);
  assert(!rendering.healthyAtDeadline(30000));
  assert(rendering.mayWaitForUi(30000));
  for (uint32_t t = 30100; t <= 36000; t += 100) rendering.observe(t, t >= 35000);
  assert(rendering.healthyAtDeadline(36000));

  BootTrialHealth neverFinished;
  neverFinished.reset(0);
  for (uint32_t t = 100; t <= 30000; t += 100) neverFinished.observe(t, false);
  assert(neverFinished.mayWaitForUi(30000));
  for (uint32_t t = 30100; t <= 60000; t += 100) neverFinished.observe(t, false);
  assert(!neverFinished.mayWaitForUi(60000));
  assert(!neverFinished.healthyAtDeadline(60000));
  assert(!hung.mayWaitForUi(30000));

  // Unsigned tick arithmetic must remain correct across millis() wraparound.
  BootTrialHealth wrapped;
  constexpr uint32_t start = UINT32_MAX - 20000;
  wrapped.reset(start);
  for (uint32_t elapsed = 100; elapsed <= 30000; elapsed += 100)
    wrapped.observe(start + elapsed, true);
  assert(wrapped.healthyAtDeadline(start + 30000));
}
