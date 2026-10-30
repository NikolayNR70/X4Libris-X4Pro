#include "SettingsActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <Logging.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontGlobals.h"
#include "SettingActionDispatch.h"
#include "SettingsList.h"
#include "SettingsSubmenuActivity.h"
#include "TouchUi.h"
#include "UiFontScale.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/PerfectInkIcons.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr int SETTINGS_FRAME_INSET = 14;
constexpr int SETTINGS_FRAME_CONTENT_INSET = 24;
constexpr int SETTINGS_BACK_TITLE_GAP = 44;

fui::BitmapRef sketchSettingIcon(const StrId id) {
  const freeink::Icon* icon = nullptr;
  bool rotateLeft = false;
  switch (id) {
    case StrId::STR_UI_THEME:
      icon = &perfectink_image_24;
      break;
    case StrId::STR_SLEEP_SCREEN:
      icon = &perfectink_open_book_24;
      break;
    case StrId::STR_REFRESH_FREQ:
    case StrId::STR_TIME_TO_SLEEP:
      icon = &perfectink_recent_24;
      break;
    case StrId::STR_TEXT_DARKNESS:
    case StrId::STR_FONT_FAMILY:
    case StrId::STR_TXT_FONT_FAMILY:
    case StrId::STR_FONT_SIZE:
    case StrId::STR_TXT_FONT_SIZE:
      icon = &perfectink_case_24;
      break;
    case StrId::STR_LINE_SPACING:
      icon = &perfectink_spacing_24;
      break;
    case StrId::STR_PARA_ALIGNMENT:
    case StrId::STR_HEADING_ALIGNMENT:
      icon = &perfectink_alignment_24;
      rotateLeft = true;
      break;
    case StrId::STR_BOOK_PROGRESS_PERCENTAGE:
      icon = &perfectink_recent_24;
      break;
    default:
      return {};
  }
  if (rotateLeft) {
    return fui::BitmapRef{perfectink_alignment_24_rotated_ccw_bits, 24, 24, fui::BitmapFormat::Mask1, true};
  }
  return fui::BitmapRef{icon->bits, icon->w, icon->h, fui::BitmapFormat::Mask1, true};
}
}  // namespace

bool SettingsActivity::isRowSelectable(const int index) const {
  return index >= 0 && index < settingsCount && !(*currentSettings)[index].isSeparator;
}

bool SettingsActivity::releaseBuriedState() {
  // The six tabs' rows, the submenus and the visible window's labels: ~80 SettingInfo rows with
  // their label vectors, in many small blocks across the heap. onEnter() builds them all again.
  std::vector<SettingInfo>().swap(displaySettings);
  std::vector<SettingInfo>().swap(readerSettings);
  std::vector<SettingInfo>().swap(controlsSettings);
  std::vector<SettingInfo>().swap(generalSettings);
  std::vector<SettingInfo>().swap(librarySettings);
  std::vector<SettingInfo>().swap(networkSettings);
  std::vector<SettingInfo::SubmenuData>().swap(submenuData);
  for (std::string& label : windowLabels) std::string().swap(label);
  for (std::string& value : windowValues) std::string().swap(value);
  currentSettings = nullptr;
  settingsCount = 0;
  windowCount = 0;
  return true;
}

void SettingsActivity::onEnter() {
  needsHalfRefresh = true;

  // Build per-category vectors from the shared settings list.
  // addTo tracks the last subcategory per vector and automatically inserts a separator
  // row whenever a setting carries a new subcategory label.
  displaySettings.clear();
  readerSettings.clear();
  controlsSettings.clear();
  generalSettings.clear();
  librarySettings.clear();
  networkSettings.clear();
  submenuData.clear();
  displaySettings.reserve(20);
  readerSettings.reserve(30);
  controlsSettings.reserve(8);
  generalSettings.reserve(20);
  librarySettings.reserve(8);
  networkSettings.reserve(12);
  submenuData.reserve(4);

  // Rows go in plain; the subcategory separators are inserted at the END, once prepareSubmenus
  // has moved whatever it is going to move into submenus.
  //
  // Inserting them here instead put headings in front of rows that were about to leave. The
  // gesture rows are the case that showed it: four groups' worth of headings went into the
  // Controls tab, then all twenty rows moved into the Gesture actions submenu, and three of the
  // four headings stayed behind with nothing underneath them. Separators describe a list, so
  // they can only be computed once the list has stopped changing -- which is the order
  // MenuListActivity has always used.
  auto addTo = [](std::vector<SettingInfo>& vec, const SettingInfo& s) { vec.push_back(s); };
  auto addToMoved = [](std::vector<SettingInfo>& vec, SettingInfo s) { vec.push_back(std::move(s)); };

  bool sawReaderFontSection = false;
  bool insertedFontDownload = false;
  bool sawIncludeBetaUpdates = false;
  SettingInfo includeBetaUpdatesSetting{};

  auto insertFontDownloadBelowFontSection = [&]() {
    auto fontDownload = SettingInfo::Action(StrId::STR_FONT_MANAGER, SettingAction::DownloadFonts);
    fontDownload.withSubcategory(StrId::STR_MENU_READER_FONT);
    addToMoved(readerSettings, std::move(fontDownload));
    insertedFontDownload = true;
  };

  forEachSetting([&](const SettingInfo& setting) {
    if (setting.category == StrId::STR_NONE_OPT) return;
    // These three rows are redundant with the fixed X4Libris home/dashboard design.
    // Keep their schema and persisted values available to the web/API settings layer,
    // but do not expose them in the on-device settings lists.
    if (setting.nameId == StrId::STR_APPLICATIONS_FOLDER || setting.nameId == StrId::STR_UI_THEME ||
        setting.submenu == StrId::STR_MENU_DISP_HOME) {
      return;
    }
    if (setting.category == StrId::STR_CAT_SYSTEM &&
        (setting.nameId == StrId::STR_USE_CLOCK || setting.nameId == StrId::STR_CLOCK_FORMAT ||
         setting.nameId == StrId::STR_TIMEZONE)) {
      return;
    }
    // Enrich font-family entries with SD card families discovered at boot.
    // The row is the walk's temporary; the screen keeps its own copy, and only that is enriched.
    SettingInfo enriched = setting;
    // This redesign changes only the headings shown in the on-device Screen page. Keep the shared
    // settings schema (and its web/API grouping) intact while matching the sketch's two sections.
    if (enriched.category == StrId::STR_CAT_DISPLAY) {
      if (enriched.nameId == StrId::STR_UI_THEME || enriched.nameId == StrId::STR_SLEEP_SCREEN) {
        enriched.subcategory = StrId::STR_SKETCH_SCREEN_APPEARANCE;
      } else if (enriched.nameId == StrId::STR_REFRESH_FREQ ||
                 enriched.nameId == StrId::STR_REFRESH_AFTER_IMAGE_PAGES) {
        enriched.subcategory = StrId::STR_SKETCH_SCREEN_PARAMETERS;
        // The sketch exposes refresh frequency directly on the Screen page. Keep its action and
        // value intact; only remove the presentation-only submenu wrapper in this copied list.
        enriched.submenu = StrId::STR_NONE_OPT;
      }
    }
    if (setting.key &&
        (std::strcmp(setting.key, "fontFamily") == 0 || std::strcmp(setting.key, "txtFontFamily") == 0)) {
      const uint8_t n = fontFamilyOptionCount();
      enriched.enumLabels.clear();
      enriched.enumLabels.reserve(n);
      for (uint8_t i = 0; i < n; i++) enriched.enumLabels.push_back(fontFamilyOptionLabel(i));
    }
    if (enriched.nameId == StrId::STR_INCLUDE_BETA_UPDATES) {
      includeBetaUpdatesSetting = enriched;
      sawIncludeBetaUpdates = true;
      return;
    }
    const bool isReaderFontEntry =
        enriched.category == StrId::STR_CAT_READER &&
        (enriched.submenu == StrId::STR_MENU_READER_FONT || enriched.submenu == StrId::STR_MENU_TXT_FONT);

    if (!insertedFontDownload && sawReaderFontSection && !isReaderFontEntry) {
      insertFontDownloadBelowFontSection();
    }

    if (enriched.category == StrId::STR_CAT_DISPLAY) {
      addTo(displaySettings, enriched);
    } else if (enriched.category == StrId::STR_CAT_READER) {
      addTo(readerSettings, enriched);
    } else if (enriched.category == StrId::STR_CAT_CONTROLS) {
      addTo(controlsSettings, enriched);
    } else if (enriched.category == StrId::STR_CAT_SYSTEM) {
      // Keep the shared settings schema intact; only arrange the device UI into
      // the six destinations shown by the new dashboard.
      switch (enriched.nameId) {
        case StrId::STR_SHOW_HIDDEN_FILES:
        case StrId::STR_SHOW_FILE_EXTENSIONS:
        case StrId::STR_BROWSER_VIEW:
          addTo(librarySettings, enriched);
          break;
        case StrId::STR_SKIP_HTTPS_VALIDATION:
          addTo(networkSettings, enriched);
          break;
        default:
          addTo(generalSettings, enriched);
          break;
      }
    }
    if (isReaderFontEntry) sawReaderFontSection = true;

    // Web-only categories (KOReader Sync, OPDS Browser) are skipped for device UI
  });

  if (!insertedFontDownload && sawReaderFontSection) {
    insertFontDownloadBelowFontSection();
  }

  // Device-only ACTION items — subcategory drives separator insertion automatically.
  controlsSettings.insert(controlsSettings.begin(),
                          SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  controlsSettings.insert(controlsSettings.begin(), SettingInfo::Separator(StrId::STR_MENU_BTN_PHYSICAL));

  // Button Actions overview lives at the end of the Button Actions section (same subcategory as
  // the per-button submenus, so no new separator is inserted).
  addToMoved(controlsSettings,
             std::move(SettingInfo::Action(StrId::STR_BTN_ACTIONS_OVERVIEW, SettingAction::ButtonActionsOverview)
                           .withSubcategory(StrId::STR_MENU_BTN_ACTIONS)));

#if CP_TOUCH_UI
  // The same thing for touch: where the zones are and what each currently does. Sits beside
  // the gesture rows it summarises. Inside the CP_TOUCH_UI bracket with every other touch-UI
  // row, so a non-touch build does not construct a SettingInfo it will only delete again --
  // the runtime requiring() below is for a multi-board binary, not a substitute for the gate.
  addToMoved(controlsSettings,
             std::move(SettingInfo::Action(StrId::STR_GEST_ACTIONS_OVERVIEW, SettingAction::GestureActionsOverview)
                           .withSubcategory(StrId::STR_MENU_GESTURE_ACTIONS)
                           .requiring(SettingRequires::TouchPanel)));
#endif  // CP_TOUCH_UI

  addToMoved(readerSettings, SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));

  addToMoved(generalSettings, SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language));
  addToMoved(generalSettings, SettingInfo::Action(StrId::STR_KEYBOARD_LAYOUTS, SettingAction::KeyboardLayouts));
  addToMoved(networkSettings, std::move(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network)
                                           .withSubcategory(StrId::STR_MENU_SYS_NETWORK)));
  addToMoved(networkSettings, std::move(SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync)
                                           .withSubcategory(StrId::STR_MENU_SYS_NETWORK)));
  addToMoved(networkSettings, std::move(SettingInfo::Action(StrId::STR_OPDS_BROWSER, SettingAction::OPDSBrowser)
                                           .withSubcategory(StrId::STR_MENU_SYS_NETWORK)));
  addToMoved(generalSettings, std::move(SettingInfo::Action(StrId::STR_CLOCK_SETTINGS, SettingAction::ClockSettings)
                                           .withSubcategory(StrId::STR_MENU_SYS_TOOLS)));
  addToMoved(networkSettings, std::move(SettingInfo::Action(StrId::STR_WEATHER_SETTINGS, SettingAction::Weather)
                                           .withSubcategory(StrId::STR_MENU_SYS_NETWORK)));
  // Every file on the card, images and sidecars included. Housekeeping rather than reading, so it
  // lives here and not on Home, whose browser lists only books.
  addToMoved(librarySettings, std::move(SettingInfo::Action(StrId::STR_ALL_FILES, SettingAction::AllFiles)
                                           .withSubcategory(StrId::STR_MENU_SYS_TOOLS)));
  addToMoved(generalSettings, std::move(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache)
                                           .withSubcategory(StrId::STR_MENU_SYS_SYSTEM)));
  addToMoved(generalSettings, std::move(SettingInfo::Action(StrId::STR_SCREEN_REPAIR, SettingAction::ScreenRepair)
                                           .withSubcategory(StrId::STR_MENU_SYS_SYSTEM)));

  addToMoved(generalSettings,
             std::move(SettingInfo::Separator(StrId::STR_SYSTEM_UPDATE_TYPE1).withSubmenu(StrId::STR_SYSTEM_UPDATE)));
  addToMoved(generalSettings, std::move(SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates)
                                           .withSubmenu(StrId::STR_SYSTEM_UPDATE)));
  if (sawIncludeBetaUpdates) {
    addToMoved(generalSettings, std::move(includeBetaUpdatesSetting.withSubmenu(StrId::STR_SYSTEM_UPDATE)));
  }
  addToMoved(generalSettings,
             std::move(SettingInfo::Separator(StrId::STR_SYSTEM_UPDATE_TYPE2).withSubmenu(StrId::STR_SYSTEM_UPDATE)));
  addToMoved(generalSettings, std::move(SettingInfo::Action(StrId::STR_READING_STATS, SettingAction::ReadingStats)
                                           .withSubcategory(StrId::STR_MENU_SYS_SYSTEM)));
  addToMoved(generalSettings,
             std::move(SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate)
                           .withSubmenu(StrId::STR_SYSTEM_UPDATE)));
  // addToMoved(systemSettings,
  //            std::move(SettingInfo::Action(StrId::STR_SWITCH_TO_USB_DRIVE, SettingAction::SwitchToUsbDrive)
  //                          .withSubmenu(StrId::STR_SYSTEM_UPDATE)));
  addToMoved(generalSettings, std::move(SettingInfo::Action(StrId::STR_SYSTEM_INFO, SettingAction::SystemInfo)
                                           .withSubcategory(StrId::STR_MENU_SYS_SYSTEM)));
  // Next to System Information, which is where anyone chasing a "the power button did
  // nothing" report already looks.
  addToMoved(generalSettings, std::move(SettingInfo::Action(StrId::STR_BOOT_DIAGNOSTICS, SettingAction::BootDiagnostics)
                                           .withSubcategory(StrId::STR_MENU_SYS_SYSTEM)));
  // A comparison screen, not a setting: it decides whether the size ladder can ship fewer real
  // faces and scale the rest, which the coverage metrics in bench/font_main.cpp cannot settle.
  addToMoved(generalSettings, std::move(SettingInfo::Action(StrId::STR_FONT_SCALING_TEST, SettingAction::FontScalingTest)
                                           .withSubcategory(StrId::STR_MENU_SYS_SYSTEM)));

  // Put the controls pictured in the Screen mockup first. This only changes the
  // on-device presentation order; every SettingInfo, stored key, and action is kept.
  const auto screenMockupOrder = [](const SettingInfo& setting) {
    switch (setting.nameId) {
      case StrId::STR_UI_THEME:
        return 0;
      case StrId::STR_SLEEP_SCREEN:
        return 1;
      case StrId::STR_REFRESH_FREQ:
        return 2;
      case StrId::STR_TIME_TO_SLEEP:
        return 3;
      default:
        return 4;
    }
  };
  std::stable_sort(displaySettings.begin(), displaySettings.end(), [&](const SettingInfo& left, const SettingInfo& right) {
    return screenMockupOrder(left) < screenMockupOrder(right);
  });

  SettingInfo::prepareSubmenus(displaySettings, submenuData);
  SettingInfo::prepareSubmenus(readerSettings, submenuData);
  SettingInfo::prepareSubmenus(controlsSettings, submenuData);
  SettingInfo::prepareSubmenus(generalSettings, submenuData);
  SettingInfo::prepareSubmenus(librarySettings, submenuData);
  SettingInfo::prepareSubmenus(networkSettings, submenuData);

  // Now that the tabs hold exactly the rows they will show, group them. prepareSubmenus has done
  // the same for each submenu's own rows.
  SettingInfo::insertSubcategorySeparators(displaySettings);
  SettingInfo::insertSubcategorySeparators(readerSettings);
  SettingInfo::insertSubcategorySeparators(controlsSettings);
  SettingInfo::insertSubcategorySeparators(generalSettings);
  SettingInfo::insertSubcategorySeparators(librarySettings);
  SettingInfo::insertSubcategorySeparators(networkSettings);

  // The dashboard chooses a category before opening this page. Reader formatting opens the
  // Screen category directly. UiListActivity wires the existing list actions and touch routing.
  enterCategory(initialCategory);
  UiListActivity::onEnter();
  if (initialCategory == 1) {
    // The sketch shows Sleep Screen focused, with the other values still visible in the same
    // framed list. Start there instead of on the non-selectable section heading at index zero.
    for (int i = 0; i < settingsCount; ++i) {
      if (!(*currentSettings)[i].isSeparator && (*currentSettings)[i].nameId == StrId::STR_SLEEP_SCREEN) {
        activeNav().requestSelection(i);
        break;
      }
    }
  }
}

void SettingsActivity::onExit() {
  UiListActivity::onExit();
  // Both read SETTINGS, so they are independent — but they are a pair: the ladder binds the UI
  // fonts and reload() re-derives the metrics sized to hold them. Changing one without the other
  // leaves rows that clip their own text.
  applyUiFontScale();               // Re-bind the UI font ladder in case the size was changed
  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
}

void SettingsActivity::enterCategory(const int categoryIndex) {
  initialCategory = std::clamp(categoryIndex, 0, categoryCount - 1);
  switch (initialCategory) {
    case 0:
      currentSettings = &generalSettings;
      break;
    case 1:
      currentSettings = &displaySettings;
      break;
    case 2:
      currentSettings = &readerSettings;
      break;
    case 3:
      currentSettings = &controlsSettings;
      break;
    case 4:
      currentSettings = &librarySettings;
      break;
    case 5:
      currentSettings = &networkSettings;
      break;
  }
  settingsCount = static_cast<int>(currentSettings->size());
  listTapActivation.reset();
}

void SettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= settingsCount) {
    return;
  }
  app.clearTapFlash();

  const auto& setting = (*currentSettings)[index];
  if (setting.isSeparator) return;

  if (setting.usesSelectorActivity) {
    auto selector = createSelectorActivity(setting, renderer, mappedInput);
    if (selector) {
      startActivityForResult(std::move(selector), [this](const ActivityResult&) {
        CrossPointSettings::normalizeDependentSettings(SETTINGS);
        SETTINGS.saveToFile();
        needsHalfRefresh = true;
      });
    }
    return;
  }

  if (setting.type == SettingType::ACTION && tryOpenSliderFor(setting.action, [this] { needsHalfRefresh = true; })) {
    return;
  }

  if (setting.type == SettingType::ACTION) {
    auto resultHandler = [this](const ActivityResult& result, const bool usedWifi) {
      CrossPointSettings::normalizeDependentSettings(SETTINGS);
      SETTINGS.saveToFile();
      if (usedWifi) restartToSettingsIfRadioLeftOn();
      needsHalfRefresh = true;
      const auto* menuResult = std::get_if<MenuResult>(&result.data);
      if (menuResult && menuResult->action != -1) {
        auto activity = createActivityForAction(static_cast<SettingAction>(menuResult->action), renderer, mappedInput);
        if (activity) {
          const bool nextUsedWifi = activity->usesWifi();
          startActivityForResult(std::move(activity), [this, nextUsedWifi](const ActivityResult&) {
            CrossPointSettings::normalizeDependentSettings(SETTINGS);
            SETTINGS.saveToFile();
            if (nextUsedWifi) restartToSettingsIfRadioLeftOn();
            needsHalfRefresh = true;
          });
        }
      }
    };

    if (setting.action == SettingAction::Submenu) {
      auto it = std::find_if(submenuData.begin(), submenuData.end(),
                             [&setting](const SettingInfo::SubmenuData& d) { return d.id == setting.nameId; });
      if (it != submenuData.end()) {
        startActivityForResult(
            std::make_unique<SettingsSubmenuActivity>(renderer, mappedInput, setting.nameId, it->items),
            [this](const ActivityResult&) { needsHalfRefresh = true; });
      }
    } else {
      auto activity = createActivityForAction(setting.action, renderer, mappedInput);
      if (activity) {
        const bool usedWifi = activity->usesWifi();
        startActivityForResult(std::move(activity), [resultHandler, usedWifi](const ActivityResult& result) {
          resultHandler(result, usedWifi);
        });
      }
    }
    return;
  }

  setting.toggleValue();
  CrossPointSettings::normalizeDependentSettings(SETTINGS);
  SETTINGS.saveToFile();
  // Repaint: nothing else will. Every other way this list changes asks for an
  // update -- moveSelectionTo(), the swipe handler, routeListTouch() -- but the
  // Confirm path in the list controller calls activateIndex() and
  // returns, so an inline toggle would change and persist the value while the
  // row kept showing the old one until some later event forced a render.
  // MenuListActivity::toggleCurrentItem() does this too, which is why the
  // submenus were never affected.
  //
  // Whole-screen rather than the one row on purpose: normalizeDependentSettings()
  // may have changed other rows' values as well.
  requestUpdate();
}

void SettingsActivity::materializeListWindow() {
  windowFirst = static_cast<uint16_t>(std::max(0, std::min(activeNav().top, settingsCount)));
  windowCount = static_cast<uint16_t>(
      std::min(static_cast<size_t>(settingsCount - windowFirst), static_cast<size_t>(LIST_WINDOW_CAPACITY)));
  for (uint16_t offset = 0; offset < windowCount; ++offset) {
    const size_t index = windowFirst + offset;
    const auto& setting = (*currentSettings)[index];
    windowLabels[offset] =
        setting.isSeparator && setting.nameId != StrId::STR_NONE_OPT ? I18N.get(setting.nameId) : setting.getTitle();
    // A TOGGLE row gets fui::list's switch rather than the words ON/OFF: ListItem::toggle
    // replaces the value slot with the same widget ToggleRowProps draws, and the switch visuals
    // are already inside list(), so this is a flag rather than new drawing code. The value string
    // is skipped for those rows -- list() ignores it when toggle is set, so building it would be
    // a std::string per row per render for nothing.
    const bool isToggle = !setting.isSeparator && setting.type == SettingType::TOGGLE;
    windowValues[offset] = isToggle ? std::string{} : setting.getDisplayValue();
    auto& row = windowItems[offset];
    row = {};
    row.label = windowLabels[offset].c_str();
    row.value = windowValues[offset].empty() ? nullptr : windowValues[offset].c_str();
    row.toggle = isToggle;
    row.toggleChecked = isToggle && setting.getToggleState();
    row.actionValue = static_cast<int16_t>(index);
    row.enabled = !setting.isSeparator;
    row.isHeader = setting.isSeparator;
    row.icon = sketchSettingIcon(setting.nameId);
  }
}

void SettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = listContentRect();
  const int statusBottom = std::max(UITheme::getStatusBarTopHeight(true) + 5,
                                    metrics.topPadding + std::max(metrics.batteryHeight, 18) + 16);
  const int frameX = contentRect.x + SETTINGS_FRAME_INSET;
  const int frameW = std::max(1, contentRect.width - 2 * SETTINGS_FRAME_INSET);
  const int frameY = std::max(contentRect.y + 2, statusBottom);
  const int frameBottom = contentRect.y + contentRect.height - 4;
  const int pad = SETTINGS_FRAME_CONTENT_INSET;
  const int listLeft = frameX + pad;
  const int listRight = frameX + frameW - pad;
  const int listTop = frameY + 8 + renderer.getLineHeight(BOOKERLY_16_FONT_ID) + 4 + 10;
  const int listBottom = frameBottom - 8;
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(listTop), static_cast<int16_t>(renderer.getScreenWidth() - listRight),
      static_cast<int16_t>(renderer.getScreenHeight() - listBottom), static_cast<int16_t>(listLeft)});

  auto props = listProps(screen);
  props.count = static_cast<uint16_t>(settingsCount);
  props.labelText.font = fui::GfxRendererTarget::FONT_BODY;
  props.valueText.font = fui::GfxRendererTarget::FONT_SMALL;
  props.headerText.font = fui::GfxRendererTarget::FONT_BODY;
  props.headerText.bold = true;
  props.headerText.maxLines = 1;
  props.separatorPaint = fui::Paint::solid(fui::Color::LightGray);
  props.rowHeight = 46;
  props.rowPaddingY = 6;
  props.rowGap = 0;
  props.rowRadius = 4;
  props.rowStyles = fui::defaultListRowStyles();
  props.rowStyles.selected.background = fui::Paint::solid(fui::Color::Black);
  props.rowStyles.selected.foreground = fui::Paint::solid(fui::Color::White);
  props.rowStyles.selected.border = fui::Paint::none();
  props.rowStyles.selected.borderWidth = 0;
  props.rowStyles.selected.radius = 4;
  syncListViewport(screen, props);
  materializeListWindow();
  props.items = windowItems.data();
  props.itemsWindowFirst = windowFirst;
  props.itemsWindowCount = windowCount;
  screen.list(props);
}

void SettingsActivity::onBackButton() {
  SETTINGS.saveToFile();
  if (returnToParent) {
    finish();
  } else {
    onGoHome();
  }
}

void SettingsActivity::homeFromList() { onBackButton(); }

bool SettingsActivity::handleCustomInput() {
#if CP_TOUCH_UI
  if (mappedInput.hasTouch()) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasScreenTapped(x, y)) {
      const auto& metrics = UITheme::getInstance().getMetrics();
      const Rect content = listContentRect();
      const int statusBottom = std::max(UITheme::getStatusBarTopHeight(true) + 5,
                                        metrics.topPadding + std::max(metrics.batteryHeight, 18) + 16);
      const int frameX = content.x + SETTINGS_FRAME_INSET;
      const int frameY = std::max(content.y + 2, statusBottom);
      const int backLeft = frameX + 8;
      const int backRight = backLeft + 28;
      const int backTop = frameY + 2;
      const int backBottom = backTop + 38;
      if (x >= backLeft && x <= backRight && y >= backTop && y <= backBottom) {
        onBackButton();
        return true;
      }
    }
  }
#endif
  return false;
}

void SettingsActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = listContentRect();
  GUI.drawHeader(renderer, UITheme::getHeaderRect(renderer), nullptr);

  const int statusBottom = std::max(UITheme::getStatusBarTopHeight(true) + 5,
                                    metrics.topPadding + std::max(metrics.batteryHeight, 18) + 16);
  const int frameX = contentRect.x + SETTINGS_FRAME_INSET;
  const int frameW = std::max(1, contentRect.width - 2 * SETTINGS_FRAME_INSET);
  const int frameY = std::max(contentRect.y + 2, statusBottom);
  const int frameBottom = contentRect.y + contentRect.height - 4;
  renderer.drawRect(frameX, frameY, frameW, std::max(1, frameBottom - frameY), true);

  const int pad = SETTINGS_FRAME_CONTENT_INSET;
  const int innerX = frameX + pad;
  const int innerW = frameW - 2 * pad;
  const int titleY = frameY + 8;
  const int arrowX = innerX + 2;
  const int arrowY = titleY + renderer.getLineHeight(BOOKERLY_16_FONT_ID) / 2;
  renderer.drawLine(arrowX + 10, arrowY - 8, arrowX + 2, arrowY, true);
  renderer.drawLine(arrowX + 2, arrowY, arrowX + 10, arrowY + 8, true);
  const int titleX = innerX + SETTINGS_BACK_TITLE_GAP;
  const int bookW = 34;
  const int titleWidth = std::max(1, innerW - 24 - bookW - 12);
  const char* rawTitle = I18N.get(categoryNames[initialCategory]);
  const std::string title = renderer.truncatedText(BOOKERLY_16_FONT_ID, rawTitle, titleWidth,
                                                    EpdFontFamily::BOLD);
  renderer.drawText(BOOKERLY_16_FONT_ID, titleX, titleY, title.c_str(), true, EpdFontFamily::BOLD);

  const int bookX = frameX + frameW - pad - bookW;
  const int bookY = titleY - 5;
  drawSketchCornerBook(renderer, bookX, bookY, 32);

  const int lineY = titleY + renderer.getLineHeight(BOOKERLY_16_FONT_ID) + 4;
  const int titleTextW = renderer.getTextWidth(BOOKERLY_16_FONT_ID, title.c_str(), EpdFontFamily::BOLD);
  renderer.drawLine(titleX + titleTextW + 8, lineY, bookX - 7, lineY, true);
}

const char* SettingsActivity::footerConfirmLabel() const {
  return tr(STR_SELECT);
}

Rect SettingsActivity::listContentRect() const {
  // The sketch uses the complete framed page without a reserved side-hint gutter. Physical keys
  // and touch navigation remain active; removing the hint strips gives rows the sketch's width.
  return UITheme::getContentRect(renderer, /*hasBottomHints=*/false, /*hasSideHints=*/false);
}

// Identical to UiListActivity::render() apart from the last line: this screen returns from a
// settings change that may have altered the theme, and on X3 the first frame after that needs a
// HALF refresh to clear the previous look rather than the FAST one the base ships.
void SettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();
  for (int pass = 0; activeNav().consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }
  publishListWindow();
  drawFooter();

  // Only spend the HALF if enough FAST refreshes have piled up since the last one to have
  // reintroduced ghosting. needsHalfRefresh is armed on entry AND on returning from every
  // child activity, so without this gate bouncing in and out of a submenu paid a 2186 ms HALF
  // per return against a FAST's 435 ms (X3-measured), even when the previous HALF was three
  // updates ago. Correctness HALFs are armed via setNextDisplayRefreshMode() and bypass this
  // entirely -- see GfxRenderer::ghostClearingHalfWorthwhile().
  const bool halfRefresh =
      renderer.ghostsUnderFastRefresh() && needsHalfRefresh && renderer.ghostClearingHalfWorthwhile();
  // Cleared whether or not it was spent, i.e. a gated request is DROPPED, not deferred. Deferring
  // would land the 2.2 s stall a few cursor moves into the screen, which reads as a freeze; on
  // entry it reads as the screen change it is. The ghosting cost of dropping is marginal because
  // the gate only fires when a HALF ran within the last few updates.
  needsHalfRefresh = false;
  renderer.displayBuffer(halfRefresh ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}
