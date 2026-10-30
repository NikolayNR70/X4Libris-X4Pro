#include "TodaySleepScreen.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>

#include "CrossPointSettings.h"

static_assert(CrossPointSettings::NOTE == today_sleep::kNoteMode);
static_assert(CrossPointSettings::TODAY == today_sleep::kTodayMode);

namespace today_sleep {
namespace {

constexpr char kPreviousChoicePath[] = "/tools/today-sleep.txt";
bool readPrevious(uint8_t& mode, uint8_t& quickResume) {
  const String text = Storage.readFile(kPreviousChoicePath);
  if (text.isEmpty()) return false;
  int parsedMode = -1;
  int parsedQuick = -1;
  if (std::sscanf(text.c_str(), "%d %d", &parsedMode, &parsedQuick) != 2 || parsedMode < 0 ||
      parsedMode >= CrossPointSettings::SLEEP_SCREEN_MODE_COUNT || parsedQuick < 0 || parsedQuick > 1) {
    return false;
  }
  mode = static_cast<uint8_t>(parsedMode);
  quickResume = static_cast<uint8_t>(parsedQuick);
  return true;
}

bool writePrevious(const uint8_t mode, const uint8_t quickResume) {
  // mkdir() is exclusive on the SD HAL and returns false when /tools already
  // exists (for example, after Today has saved its task list). Use the
  // idempotent helper so the screensaver toggle works in either order.
  if (!Storage.ensureDirectoryExists("/tools")) return false;
  char text[24];
  const int length = std::snprintf(text, sizeof(text), "%u %u\n", mode, quickResume);
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(text)) return false;
  HalFile file;
  if (!Storage.openFileForWrite("TODAY", kPreviousChoicePath, file)) return false;
  const size_t written = file.write(reinterpret_cast<const uint8_t*>(text), static_cast<size_t>(length));
  file.flush();
  const bool closed = file.close();
  return written == static_cast<size_t>(length) && closed;
}

void restoreQuickResume(const uint8_t previousQuick) {
  SETTINGS.quickResumeSleepScreen = previousQuick ? CrossPointSettings::QUICK_RESUME_AFTER_TIMEOUT
                                                  : CrossPointSettings::QUICK_RESUME_NEVER;
}

}  // namespace

uint8_t selectorIndex() {
  // A note remains selectable through Notes, but is intentionally absent from
  // the general screen-saver list.
  return selectorIndexForMode(SETTINGS.sleepScreen);
}

void selectFromSettings(const uint8_t index) {
  if (index >= kSelectorModeCount) return;
  const uint8_t nextMode = kSelectorModes[index];
  const uint8_t currentMode = SETTINGS.sleepScreen;
  if (currentMode == nextMode) return;

  // Settings can enter/leave Today independently of its own app button. Keep
  // that path in sync so toggling Today off restores the mode that was active
  // immediately before it was selected.
  if (nextMode == CrossPointSettings::TODAY) {
    const uint8_t quick = SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_AFTER_TIMEOUT;
    if (!writePrevious(currentMode, quick)) {
      LOG_ERR("TODAY", "Could not save previous sleep-screen mode from settings");
      return;
    }
  } else if (currentMode == CrossPointSettings::TODAY) {
    Storage.remove(kPreviousChoicePath);
  }

  SETTINGS.sleepScreen = nextMode;
  CrossPointSettings::normalizeDependentSettings(SETTINGS);
}

bool toggleFromTodayApp() {
  if (SETTINGS.sleepScreen == CrossPointSettings::TODAY) {
    uint8_t previousMode = CrossPointSettings::DARK;
    uint8_t previousQuick = 0;
    readPrevious(previousMode, previousQuick);
    if (previousMode >= CrossPointSettings::SLEEP_SCREEN_MODE_COUNT || previousMode == CrossPointSettings::TODAY) {
      previousMode = CrossPointSettings::DARK;
      previousQuick = 0;
    }
    SETTINGS.sleepScreen = previousMode;
    restoreQuickResume(previousQuick);
    Storage.remove(kPreviousChoicePath);
  } else {
    const uint8_t previousMode = SETTINGS.sleepScreen;
    const uint8_t previousQuick = SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_AFTER_TIMEOUT;
    if (!writePrevious(previousMode, previousQuick)) {
      LOG_ERR("TODAY", "Could not save previous sleep-screen mode");
      return false;
    }
    SETTINGS.sleepScreen = CrossPointSettings::TODAY;
    SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_NEVER;
  }
  CrossPointSettings::normalizeDependentSettings(SETTINGS);
  SETTINGS.saveToFile();
  return true;
}

}  // namespace today_sleep
