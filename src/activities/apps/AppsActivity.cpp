#include "AppsActivity.h"

#include <I18n.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>

#include "ClockActivity.h"
#include "MappedInputManager.h"
#include "TodayActivity.h"
#include "fontIds.h"

namespace {

constexpr int kColumns = 2;
constexpr int kTileMargin = 16;
constexpr int kTileGap = 16;
// A full-height tile fills the screen between the shared top and foot margins.
constexpr int kTileHeight = 219;
// Keep the icon and label inside the tile instead of centring oversized art.
constexpr int kIconSize = 88;
constexpr int kIconTop = 21;
constexpr int kNameTop = 135;
constexpr int kRowHeight = 72;
constexpr int kRowGap = 12;
constexpr uint8_t kFileMagic[3] = {'V', 'A', 'P'};

// A calendar leaf showing today: the month across the top, the day below.
void drawClockIcon(const GfxRenderer& renderer, const int x, const int y) {
  renderer.drawRoundedRect(x, y, kIconSize, kIconSize, 3, 12, true);
  renderer.fillRoundedRect(x, y, kIconSize, 30, 12, true, true, false, false, Color::Black);
  calmath::DateTime time;
  if (!clockface::now(time)) {
    gameui::centredText(renderer, UI_12_FONT_ID, {x, y + 30, kIconSize, kIconSize - 30}, "--", true,
                        EpdFontFamily::BOLD);
    return;
  }
  // The first three letters of the month.
  char month[16];
  snprintf(month, sizeof(month), "%s", clockface::monthName(time.date.month));
  size_t length = 0;
  for (int characters = 0; month[length] != '\0'; ++length) {
    if ((static_cast<uint8_t>(month[length]) & 0xC0) != 0x80 && characters++ == 3) break;
  }
  month[length] = '\0';
  gameui::centredText(renderer, SMALL_FONT_ID, {x, y + 2, kIconSize, 26}, month, false, EpdFontFamily::BOLD);
  char day[4];
  snprintf(day, sizeof(day), "%d", static_cast<int>(time.date.day));
  const gameui::DigitSet digits{appart::kClockSmallGlyphs, appart::kClockSmallBits, appart::kClockSmallRows};
  gameui::digitText(renderer, digits, day, x + kIconSize / 2, y + 30 + (kIconSize - 30 - digits.rows) / 2, 4);
}

void drawTodayIcon(const GfxRenderer& renderer, const int x, const int y) {
  renderer.drawRoundedRect(x + 6, y + 8, kIconSize - 12, kIconSize - 16, 3, 10, true);
  renderer.fillRoundedRect(x + 6, y + 8, kIconSize - 12, 24, 8, true, true, false, false, Color::Black);
  renderer.drawLine(x + 30, y + 2, x + 30, y + 18, 4, true);
  renderer.drawLine(x + kIconSize - 30, y + 2, x + kIconSize - 30, y + 18, 4, true);
  constexpr int box = 13;
  const int firstRow = y + 38;
  const int secondRow = y + 56;
  renderer.drawRect(x + 22, firstRow, box, box, 2, true);
  renderer.drawLine(x + 24, firstRow + 6, x + 28, firstRow + 10, 3, true);
  renderer.drawLine(x + 28, firstRow + 10, x + 35, firstRow + 2, 3, true);
  renderer.drawRect(x + 46, firstRow, box, box, 2, true);
  renderer.drawRect(x + 22, secondRow, box, box, 2, true);
  renderer.drawRect(x + 46, secondRow, box, box, 2, true);
}

void drawSwitch(const GfxRenderer& renderer, const int x, const int y, const bool on) {
  constexpr int width = 66;
  constexpr int height = 36;
  if (on) {
    renderer.fillRoundedRect(x, y, width, height, height / 2, Color::Black);
    gameui::disc(renderer, x + width - height / 2, y + height / 2, 13, false);
  } else {
    renderer.drawRoundedRect(x, y, width, height, 2, height / 2, true);
    gameui::disc(renderer, x + height / 2, y + height / 2, 12, true);
  }
}

// The names the app screens give themselves, in the order of AppId.
constexpr const char* kActivityNames[] = {"", "", "", "", "Clock", "", "", "", "Today"};

}  // namespace

AppsActivity::AppsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const uint8_t resumeId)
    : Activity("Apps", renderer, mappedInput), resumeId(resumeId) {}

uint8_t AppsActivity::resumeIdFor(const std::string& activityName) {
  static_assert(sizeof(kActivityNames) / sizeof(kActivityNames[0]) == kAppCount, "one name for every app");
  if (activityName == "Apps") return kResumeList;
  for (const int app : kOrder) {
    if (activityName == kActivityNames[app]) return static_cast<uint8_t>(kResumeList + 1 + app);
  }
  return kResumeNone;
}

void AppsActivity::onEnter() {
  Activity::onEnter();
  pointer.begin(mappedInput);
  uint8_t bytes[5] = {};
  if (gameui::readSave(kSavePath, bytes, 4) && memcmp(bytes, kFileMagic, 3) == 0) hiddenMask = bytes[3];
  if (gameui::readSave(kSavePath, bytes, sizeof(bytes)) && memcmp(bytes, kFileMagic, 3) == 0) {
    hiddenMask = static_cast<uint16_t>(bytes[3]) | static_cast<uint16_t>(bytes[4]) << 8;
  }
  mode = Mode::Tiles;
  clockface::loadOptions(clockOptions);

  // Woken from a sleep that began inside an app: straight back into it. The
  // tiles are read and drawn when that app is left.
  const int resumeApp = static_cast<int>(resumeId) - (kResumeList + 1);
  resumeId = kResumeNone;
  for (const int app : kOrder) {
    if (resumeApp == app && !hidden(app)) {
      open(static_cast<AppId>(app));
      return;
    }
  }

  requestUpdate();
}

void AppsActivity::saveHidden() const {
  const uint8_t bytes[5] = {kFileMagic[0], kFileMagic[1], kFileMagic[2], static_cast<uint8_t>(hiddenMask),
                            static_cast<uint8_t>(hiddenMask >> 8)};
  gameui::writeSave(kSavePath, bytes, sizeof(bytes));
}

void AppsActivity::layout() {
  const int width = renderer.getScreenWidth();
  const int top = gameui::contentTop(renderer, mappedInput);
  const int tileWidth = (width - 2 * kTileMargin - (kColumns - 1) * kTileGap) / kColumns;
  int visible = 0;
  for (const int app : kOrder) visible += hidden(app) ? 0 : 1;
  // Up to three rows keep the full tiles; four or more rows use compact tiles
  // (icon and name, no status line) so all apps fit without clipping artwork.
  const int tileRows = std::max(1, (visible + kColumns - 1) / kColumns);
  const int availableTileHeight = (gameui::contentBottom(renderer) - top - (tileRows - 1) * kTileGap) / tileRows;
  const int tileHeight =
      tileRows <= 3 ? (availableTileHeight < kTileHeight ? availableTileHeight : kTileHeight) : availableTileHeight;
  shownCount = 0;
  int row = 0;
  for (const int app : kOrder) {
    rows[app] = {kTileMargin, top + row++ * (kRowHeight + kRowGap), width - 2 * kTileMargin, kRowHeight};
    if (hidden(app)) continue;
    tiles[shownCount] = {kTileMargin + (shownCount % kColumns) * (tileWidth + kTileGap),
                         top + (shownCount / kColumns) * (tileHeight + kTileGap), tileWidth, tileHeight};
    shown[shownCount++] = app;
  }
  manageBox = gameui::headerCornerBox(renderer, mappedInput, tr(STR_APPS_MANAGE));

  // Settings, on the Clock's row in Manage, to the left of its switch.
  const gameui::Box& clockRow = rows[Clock];
  const int settingsWidth = renderer.getTextWidth(UI_10_FONT_ID, tr(STR_SETTINGS_SHORT), EpdFontFamily::BOLD) + 40;
  clockSettingsBox = {clockRow.x + clockRow.w - 66 - 22 - 14 - settingsWidth, clockRow.y + (clockRow.h - 50) / 2,
                      settingsWidth, 50};
  for (int clockRowIndex = 0; clockRowIndex < kClockRowCount; ++clockRowIndex) {
    clockRows[clockRowIndex] = {kTileMargin, top + clockRowIndex * (84 + kRowGap), width - 2 * kTileMargin, 84};
  }
}

void AppsActivity::changeClockSetting(const int row) {
  switch (row) {
    case FaceRow:
      clockOptions.face = static_cast<uint8_t>((clockOptions.face + 1) % clockface::kFaceCount);
      break;
    case WeekStartRow:
      clockOptions.weekStart = static_cast<uint8_t>((clockOptions.weekStart + 1) % clockface::kWeekStartCount);
      break;
    default:
      clockOptions.weekNumber = !clockOptions.weekNumber;
      break;
  }
  clockface::saveOptions(clockOptions);
}

void AppsActivity::open(const AppId app) {
  std::unique_ptr<Activity> activity;
  switch (app) {
    case Today:
      activity = std::make_unique<TodayActivity>(renderer, mappedInput);
      break;
    default:
      activity = std::make_unique<ClockActivity>(renderer, mappedInput);
      break;
  }
  startActivityForResult(std::move(activity), [this](const ActivityResult&) { requestUpdate(); });
}

void AppsActivity::loop() {
  if (TouchHeaderBackButton::wasTapped(mappedInput, renderer) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (mode != Mode::Tiles) {
      // One step back: from the Clock settings to the switches, from there to the tiles.
      mode = mode == Mode::ClockSettings ? Mode::Manage : Mode::Tiles;
      requestUpdate();
      return;
    }
    setResult(ActivityResult{});
    finish();
    return;
  }

  int tx = 0;
  int ty = 0;
  bool moved = false;
  bool hit;
  {
    RenderLock lock(*this);
    hit = pointer.tapped(mappedInput, tx, ty, moved);
  }
  if (moved) requestUpdate();
  if (!hit) return;
  if (mode == Mode::ClockSettings) {
    for (int row = 0; row < kClockRowCount; ++row) {
      if (!clockRows[row].contains(tx, ty)) continue;
      changeClockSetting(row);
      requestUpdate();
      return;
    }
    return;
  }
  if (mode == Mode::Manage) {
    if (clockSettingsBox.grown(6).contains(tx, ty)) {
      mode = Mode::ClockSettings;
      requestUpdate();
      return;
    }
    for (const int app : kOrder) {
      if (!rows[app].contains(tx, ty)) continue;
      hiddenMask ^= static_cast<uint16_t>(1u << app);
      saveHidden();
      requestUpdate();
      return;
    }
    return;
  }
  if (manageBox.grown(6).contains(tx, ty)) {
    mode = Mode::Manage;
    requestUpdate();
    return;
  }
  for (int i = 0; i < shownCount; ++i) {
    if (tiles[i].contains(tx, ty)) {
      open(static_cast<AppId>(shown[i]));
      return;
    }
  }
}

void AppsActivity::drawIcon(const int app, const int x, const int y) const {
  switch (app) {
    case Today:
      drawTodayIcon(renderer, x, y);
      break;
    default:
      drawClockIcon(renderer, x, y);
      break;
  }
}

void AppsActivity::drawTiles() const {
  const char* names[kAppCount] = {"", "", "", "", tr(STR_APP_CLOCK), "", "", "", tr(STR_TODAY)};
  for (int i = 0; i < shownCount; ++i) {
    const int app = shown[i];
    const gameui::Box& tile = tiles[i];
    renderer.drawRoundedRect(tile.x, tile.y, tile.w, tile.h, 2, 12, true);
    const bool compact = tile.h < kTileHeight;
    const int iconTop = compact ? (tile.h - kIconSize - 34) / 2 : kIconTop;
    drawIcon(app, tile.x + (tile.w - kIconSize) / 2, tile.y + iconTop);
    gameui::centredText(renderer, UI_12_FONT_ID,
                        {tile.x, tile.y + (compact ? iconTop + kIconSize + 4 : kNameTop), tile.w, 30}, names[app], true,
                        EpdFontFamily::BOLD);
  }
  if (shownCount == 0) {
    gameui::centredText(renderer, UI_10_FONT_ID, {0, tiles[0].y + 40, renderer.getScreenWidth(), 30},
                        tr(STR_APPS_ALL_HIDDEN));
  }
}

void AppsActivity::drawManage() const {
  const char* names[kAppCount] = {"", "", "", "", tr(STR_APP_CLOCK), "", "", "", tr(STR_TODAY)};
  for (const int app : kOrder) {
    const gameui::Box& row = rows[app];
    renderer.drawRoundedRect(row.x, row.y, row.w, row.h, 2, 12, true);
    const gameui::Box label{row.x + 24, row.y + 6, 190, row.h - 12};
    renderer.drawText(UI_12_FONT_ID, label.x, gameui::centredTextY(renderer, UI_12_FONT_ID, label), names[app], true,
                      EpdFontFamily::BOLD);
    if (hidden(app)) gameui::fade(renderer, label);
    drawSwitch(renderer, row.x + row.w - 66 - 22, row.y + (row.h - 36) / 2, !hidden(app));
    if (app == Clock) gameui::button(renderer, clockSettingsBox, tr(STR_SETTINGS_SHORT), false);
  }
  const int y = rows[kOrder[kVisibleAppCount - 1]].y + kRowHeight + 14;
  int lineY = y;
  for (const auto& line :
       renderer.wrappedText(SMALL_FONT_ID, tr(STR_APPS_HIDDEN_NOTE), renderer.getScreenWidth() - 60, 3)) {
    gameui::centredText(renderer, SMALL_FONT_ID, {0, lineY, renderer.getScreenWidth(), 24}, line.c_str());
    lineY += 24;
  }
}

void AppsActivity::drawClockSettings() const {
  const char* faces[clockface::kFaceCount] = {tr(STR_CLOCK_FACE_DIGITAL), tr(STR_CLOCK_FACE_ANALOG)};
  // The weekdays are numbered from Monday.
  const int firstDay =
      clockOptions.weekStart == clockface::Saturday ? 5 : (clockOptions.weekStart == clockface::Sunday ? 6 : 0);
  const char* labels[kClockRowCount] = {tr(STR_CLOCK_FACE), tr(STR_CLOCK_WEEK_START), tr(STR_CLOCK_WEEK_NUMBER)};
  const char* values[kClockRowCount] = {
      faces[clockOptions.shownFace()], clockface::weekdayName(firstDay),
      clockOptions.weekNumber ? tr(STR_STATE_ON) : tr(STR_STATE_OFF)};
  for (int row = 0; row < kClockRowCount; ++row) {
    const gameui::Box& box = clockRows[row];
    renderer.drawRoundedRect(box.x, box.y, box.w, box.h, 2, 12, true);
    renderer.drawText(UI_12_FONT_ID, box.x + 24, gameui::centredTextY(renderer, UI_12_FONT_ID, box), labels[row], true,
                      EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, box.x + box.w - 24 - renderer.getTextWidth(UI_10_FONT_ID, values[row]),
                      gameui::centredTextY(renderer, UI_10_FONT_ID, box), values[row]);
  }
  int y = clockRows[kClockRowCount - 1].y + 84 + 14;
  for (const char* note : {tr(STR_CLOCK_TAP_ROW), tr(STR_CLOCK_FORMAT_NOTE)}) {
    for (const auto& line : renderer.wrappedText(SMALL_FONT_ID, note, renderer.getScreenWidth() - 60, 3)) {
      gameui::centredText(renderer, SMALL_FONT_ID, {0, y, renderer.getScreenWidth(), 24}, line.c_str());
      y += 24;
    }
  }
}

void AppsActivity::render(RenderLock&&) {
  layout();
  renderer.clearScreen();
  const char* title = mode == Mode::ClockSettings ? tr(STR_CLOCK_SETTINGS)
                                                  : (mode == Mode::Manage ? tr(STR_APPS_MANAGE_TITLE) : tr(STR_APPS));
  gameui::drawHeader(renderer, mappedInput, title);
  if (mode == Mode::ClockSettings) {
    drawClockSettings();
  } else if (mode == Mode::Manage) {
    drawManage();
  } else {
    gameui::cornerButton(renderer, manageBox, tr(STR_APPS_MANAGE));
    drawTiles();
  }
  pointer.clear();
  if (mode == Mode::ClockSettings) {
    for (const auto& row : clockRows) pointer.add(row);
  } else if (mode == Mode::Manage) {
    for (const int app : kOrder) pointer.add(rows[app]);
    pointer.add(clockSettingsBox);
  } else {
    for (int i = 0; i < shownCount; ++i) pointer.add(tiles[i]);
    pointer.add(manageBox);
  }
  pointer.draw(renderer);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
