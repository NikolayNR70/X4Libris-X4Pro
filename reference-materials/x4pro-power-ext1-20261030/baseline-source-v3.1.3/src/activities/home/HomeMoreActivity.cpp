#include "HomeMoreActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "GlobalBookmarkIndex.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"

HomeMoreActivity::HomeMoreActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : MenuListActivity("HomeMore", renderer, mappedInput) {
  const HomeMenuAvailability availability{.hasBookmarks = !GLOBAL_BOOKMARKS.isEmpty(),
                                          .applicationsEnabled = SETTINGS.useApplicationsFolder != 0};
  std::vector<HomeMenuEntry> entries;
  entries.reserve(5);
  if (renderer.getScreenHeight() > renderer.getScreenWidth()) {
    // The portrait sketch pins Files, Library, Applications and Wi-Fi/USB to the six home tiles.
    // Keep the remaining destinations reachable from More even when their legacy placement
    // toggles are enabled; the fixed sketch has no extra home tile to host them.
    entries.push_back({HomeMenuAction::ReadingStats, StrId::STR_READING_STATS, Stats});
    if (availability.hasBookmarks) {
      entries.push_back({HomeMenuAction::GlobalBookmarks, StrId::STR_GLOBAL_BOOKMARKS, Book});
    }
    if (SETTINGS.useWeather != 0) {
      entries.push_back({HomeMenuAction::Weather, StrId::STR_WEATHER, Weather});
    }
  } else {
    collectHomeMenuEntries(HomeMenuPlacement::More, availability, entries);
  }

  menuItems.reserve(entries.size());
  actions.reserve(entries.size());
  for (const HomeMenuEntry& entry : entries) {
    menuItems.push_back(SettingInfo::Action(entry.label, SettingAction::None));
    actions.push_back(entry.action);
  }
}

void HomeMoreActivity::onActionSelected(const int index) {
  if (index < 0 || index >= static_cast<int>(actions.size())) return;
  activityManager.goToHomeMenuAction(actions[index]);
}

void HomeMoreActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = listContentRect();

  GUI.drawHeader(renderer, listHeaderRect(), tr(STR_MORE));

  const int contentTop = contentRect.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = contentRect.y + contentRect.height - contentTop - metrics.verticalSpacing * 2;
  drawMenuList(Rect{contentRect.x, contentTop, contentRect.width, contentHeight});

  drawListHints();

  renderer.displayBuffer();
}
