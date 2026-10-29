#pragma once
#include <cstdint>
// Brief redraws are normal. A blocked loop or a render stalled for over two
// seconds restarts the window; the independent deadline still handles hangs.
class BootTrialHealth {
  uint32_t since = 0, last = 0, lastReady = 0;
  unsigned samples = 0;
 public:
  bool observe(uint32_t now, bool ready) {
    if (samples && (static_cast<uint32_t>(now-last)>2000 ||
                    static_cast<uint32_t>(now-lastReady)>2000)) samples=0;
    last=now;
    if(!ready)return false;
    if(!samples)since=now;
    lastReady=now;
    ++samples;
    return samples>=100 && static_cast<uint32_t>(now-since)>=30000;
  }
};
