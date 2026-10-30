#pragma once

#include <cstdint>
#include <string>

#include "ClockFace.h"
#include "GameUi.h"
#include "activities/Activity.h"

// Home > Apps: a tile per built-in app with its icon, its name and a short
// line about what is going on in it. Manage (top right) lists every app with a
// switch; an app that is switched off is only hidden from the tiles - it stays
// in the firmware and costs no memory or battery while it is not open. The
// Clock's row also leads to its settings.
class AppsActivity final : public Activity {
 public:
  // What sleep notes about the screen it began on, so that waking returns there
  // the way it returns to a book: nothing, this list, or one app (numbers above
  // kResumeList). They are kept in the state file and must not change.
  static constexpr uint8_t kResumeNone = 0;
  static constexpr uint8_t kResumeList = 1;
  // The resume number of the screen with this activity name; kResumeNone if it
  // is neither the list nor an app.
  static uint8_t resumeIdFor(const std::string& activityName);

  // resumeId: one app's resume number opens that app on top of the list.
  AppsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, uint8_t resumeId = kResumeNone);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // The numbers are kept in files (resume, hidden apps): a new app gets the next one.
  enum AppId {
    Wordle = 0,
    Sudoku = 1,
    Chess = 2,
    Checkers = 3,
    Clock = 4,
    TicTacToe = 5,
    Go = 6,
    ReservedCalculator = 7,  // Keep the persisted ID unused after removing the app.
    Today = 8,
    kAppCount = 9,  // Keep persisted app IDs stable; removed games remain reserved.
    kVisibleAppCount = 2
  };
  // Removed game IDs stay reserved to keep saved app settings compatible.
  static constexpr AppId kOrder[kVisibleAppCount] = {Clock, Today};
  static constexpr char kSavePath[] = "/.crosspoint/games/apps.bin";

  void layout();
  void open(AppId app);
  bool hidden(int app) const { return (hiddenMask & (1u << app)) != 0; }
  void saveHidden() const;
  void drawIcon(int app, int x, int y) const;
  void drawTiles() const;
  void drawManage() const;
  void drawClockSettings() const;
  void changeClockSetting(int row);

  enum class Mode : uint8_t { Tiles, Manage, ClockSettings };
  enum ClockRow { FaceRow = 0, WeekStartRow = 1, WeekNumberRow = 2, kClockRowCount = 3 };

  Mode mode = Mode::Tiles;
  uint8_t resumeId;
  clockface::Options clockOptions;
  gameui::Box clockSettingsBox;
  gameui::Box clockRows[kClockRowCount];
  uint16_t hiddenMask = 0;
  int shownCount = 0;
  int shown[kAppCount] = {};
  gameui::Box tiles[kAppCount];
  gameui::Box rows[kAppCount];
  gameui::Box manageBox;
  gameui::ButtonPointer pointer{};
};
