#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class SettingsDashboardActivity final : public Activity {
  ButtonNavigator navigator;
  int selected = 0;
  bool returnToParent = false;

  void openCard(int index);

 public:
  explicit SettingsDashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool returnToParent = false)
      : Activity("SettingsDashboard", renderer, mappedInput), returnToParent(returnToParent) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
