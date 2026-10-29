#pragma once

#include <cstdint>

namespace today_sleep {

// These are the persisted SLEEP_SCREEN_MODE values. Keep this small policy
// header independent of device/HAL headers so the selector can be host-tested.
inline constexpr uint8_t kNoteMode = 9;
inline constexpr uint8_t kTodayMode = 10;
inline constexpr uint8_t kSelectorModes[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, kTodayMode,
};
inline constexpr uint8_t kSelectorModeCount = sizeof(kSelectorModes) / sizeof(kSelectorModes[0]);

constexpr uint8_t selectorIndexForMode(const uint8_t mode) {
  for (uint8_t i = 0; i < kSelectorModeCount; ++i) {
    if (kSelectorModes[i] == mode) return i;
  }
  return UINT8_MAX;
}

// The general screen-saver selector deliberately omits the note screen: a
// specific note is selected from Notes. TODAY is still an ordinary setting.
uint8_t selectorIndex();
void selectFromSettings(uint8_t index);
bool toggleFromTodayApp();

}  // namespace today_sleep
