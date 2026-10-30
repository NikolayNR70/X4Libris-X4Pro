#pragma once

#include <array>
#include <cstdint>

#include "activities/Activity.h"

class TodayActivity final : public Activity {
 public:
  explicit TodayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Todo {
    char text[80]{};
    bool done = false;
  };

  static constexpr size_t kMaxTodos = 20;
  static constexpr char kDataPath[] = "/tools/daily.json";

  void load();
  bool save();
  void addTask(const char* text);
  void removeTask(size_t index);
  void clearCompleted();
  void toggleSleepScreen();
  bool sleepScreenEnabled() const;
  void openAddTask();

  std::array<Todo, kMaxTodos> todos_{};
  size_t count_ = 0;
  size_t selected_ = 0;
  size_t top_ = 0;
  int visibleRows_ = 1;
  char date_[11]{};
};
