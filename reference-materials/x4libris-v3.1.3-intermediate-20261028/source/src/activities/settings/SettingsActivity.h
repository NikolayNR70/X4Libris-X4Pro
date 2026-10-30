#pragma once
#include <I18n.h>

#include <array>
#include <string>
#include <vector>

#include "SettingInfo.h"
#include "activities/UiListActivity.h"

class SettingsActivity final : public UiListActivity {
  int settingsCount = 0;
  int initialCategory = 0;
  bool returnToParent = false;

  // Per-category settings derived from shared list + device-only actions
  std::vector<SettingInfo> displaySettings;
  std::vector<SettingInfo> readerSettings;
  std::vector<SettingInfo> controlsSettings;
  std::vector<SettingInfo> generalSettings;
  std::vector<SettingInfo> librarySettings;
  std::vector<SettingInfo> networkSettings;
  const std::vector<SettingInfo>* currentSettings = nullptr;

  static constexpr int categoryCount = 6;
  // constexpr, not `static const` with an out-of-line definition: a compile-time table has no
  // reason to occupy .data and be initialized at startup when it can sit in flash.
  static constexpr StrId categoryNames[categoryCount] = {StrId::STR_DASH_GENERAL, StrId::STR_DASH_SCREEN,
                                                         StrId::STR_DASH_READING, StrId::STR_DASH_CONTROLS,
                                                         StrId::STR_DASH_LIBRARY, StrId::STR_DASH_NETWORK};

  std::vector<SettingInfo::SubmenuData> submenuData;
  bool needsHalfRefresh = false;

  void enterCategory(int categoryIndex);

  static constexpr size_t LIST_WINDOW_CAPACITY = 24;
  std::array<std::string, LIST_WINDOW_CAPACITY> windowLabels;
  std::array<std::string, LIST_WINDOW_CAPACITY> windowValues;
  std::array<freeink::ui::ListItem, LIST_WINDOW_CAPACITY> windowItems;
  uint16_t windowFirst = 0;
  uint16_t windowCount = 0;

  void materializeListWindow();

  // UiListActivity / TabbedUiListActivity overrides
  int listCount() const override { return settingsCount; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  void homeFromList() override;
  bool handleCustomInput() override;
  [[nodiscard]] bool isRowSelectable(int index) const override;
  void drawChrome() override;
  void drawFooter() override {}
  [[nodiscard]] Rect listContentRect() const override;
  [[nodiscard]] const char* footerConfirmLabel() const override;
  // Overridden only to ship the frame with this screen's refresh mode; see the definition.
  void render(RenderLock&&) override;

 public:
  explicit SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int category = 0,
                            bool returnToParent = false)
      : UiListActivity("Settings", renderer, mappedInput), initialCategory(category),
        returnToParent(returnToParent) {}
  void onEnter() override;
  void onExit() override;
  bool releaseBuriedState() override;
};
