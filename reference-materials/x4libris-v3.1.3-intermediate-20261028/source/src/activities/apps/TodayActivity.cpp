#include "TodayActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <variant>

#include "CrossPointSettings.h"
#include "TodaySleepScreen.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char kTempPath[] = "/tools/daily.json.part";
constexpr char kBackupPath[] = "/tools/daily.json.bak";

bool localDate(char (&out)[11]) {
  const time_t now = HalClock::now();
  if (now < 1577836800) return false;
  struct tm local{};
  if (localtime_r(&now, &local) == nullptr) return false;
  std::snprintf(out, sizeof(out), "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
  return true;
}

}  // namespace

constexpr char TodayActivity::kDataPath[];

TodayActivity::TodayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Today", renderer, mappedInput) {}

void TodayActivity::load() {
  count_ = 0;
  std::fill(std::begin(date_), std::end(date_), '\0');
  String raw = Storage.readFile(kDataPath);
  if (!raw.isEmpty() && raw.length() <= 4096) {
    JsonDocument doc;
    if (deserializeJson(doc, raw) == DeserializationError::Ok) {
      const char* savedDate = doc["date"] | "";
      std::snprintf(date_, sizeof(date_), "%s", savedDate);
      const JsonArrayConst items = doc["todos"].as<JsonArrayConst>();
      for (const JsonObjectConst item : items) {
        if (count_ == kMaxTodos) break;
        const char* text = item["text"] | "";
        if (text[0] == '\0') continue;
        std::snprintf(todos_[count_].text, sizeof(todos_[count_].text), "%s", text);
        todos_[count_].done = item["done"].is<bool>() && item["done"].as<bool>();
        ++count_;
      }
    } else {
      LOG_ERR("TODAY", "Ignoring invalid daily task file");
    }
  }

  char currentDate[sizeof(date_)]{};
  if (localDate(currentDate) && std::strcmp(currentDate, date_) != 0) {
    size_t kept = 0;
    for (size_t i = 0; i < count_; ++i) {
      if (!todos_[i].done) todos_[kept++] = todos_[i];
    }
    count_ = kept;
    std::snprintf(date_, sizeof(date_), "%s", currentDate);
    save();
  }
}

bool TodayActivity::save() {
  if (!Storage.ensureDirectoryExists("/tools")) return false;
  JsonDocument doc;
  doc["date"] = date_;
  JsonArray items = doc["todos"].to<JsonArray>();
  for (size_t i = 0; i < count_; ++i) {
    JsonObject item = items.add<JsonObject>();
    item["text"] = todos_[i].text;
    item["done"] = todos_[i].done;
  }

  Storage.remove(kTempPath);
  HalFile file;
  if (!Storage.openFileForWrite("TODAY", kTempPath, file)) return false;
  const size_t expected = measureJson(doc);
  const size_t written = serializeJson(doc, file);
  file.flush();
  const bool closed = file.close();
  if (written != expected || !closed) {
    Storage.remove(kTempPath);
    LOG_ERR("TODAY", "Short write saving daily list (%u/%u)", static_cast<unsigned>(written),
            static_cast<unsigned>(expected));
    return false;
  }

  Storage.remove(kBackupPath);
  const bool hadPrevious = Storage.exists(kDataPath);
  if (hadPrevious && !Storage.rename(kDataPath, kBackupPath)) {
    Storage.remove(kTempPath);
    return false;
  }
  if (!Storage.rename(kTempPath, kDataPath)) {
    if (hadPrevious) Storage.rename(kBackupPath, kDataPath);
    Storage.remove(kTempPath);
    return false;
  }
  Storage.remove(kBackupPath);
  return true;
}

void TodayActivity::onEnter() {
  Activity::onEnter();
  load();
  selected_ = 0;
  top_ = 0;
  requestUpdate();
}

void TodayActivity::addTask(const char* text) {
  if (!text || !text[0] || count_ >= kMaxTodos) return;
  std::snprintf(todos_[count_].text, sizeof(todos_[count_].text), "%s", text);
  todos_[count_].done = false;
  ++count_;
  selected_ = count_ - 1;
  if (!save()) LOG_ERR("TODAY", "Could not save new task");
  requestUpdate();
}

void TodayActivity::removeTask(const size_t index) {
  if (index >= count_) return;
  for (size_t i = index; i + 1 < count_; ++i) todos_[i] = todos_[i + 1];
  --count_;
  selected_ = std::min(selected_, count_ + 2);
  if (!save()) LOG_ERR("TODAY", "Could not save task removal");
  requestUpdate();
}

void TodayActivity::clearCompleted() {
  size_t kept = 0;
  for (size_t i = 0; i < count_; ++i) {
    if (!todos_[i].done) todos_[kept++] = todos_[i];
  }
  count_ = kept;
  selected_ = std::min(selected_, count_ + 2);
  if (!save()) LOG_ERR("TODAY", "Could not save completed-task cleanup");
  requestUpdate();
}

bool TodayActivity::sleepScreenEnabled() const {
  return SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::TODAY;
}

void TodayActivity::toggleSleepScreen() {
  if (!today_sleep::toggleFromTodayApp()) return;
  requestUpdate();
}

void TodayActivity::openAddTask() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TODAY_ADD_TASK), "", 79);
  if (!keyboard) return;
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (!result.isCancelled) {
      if (const auto* value = std::get_if<KeyboardResult>(&result.data)) addTask(value->text.c_str());
    }
    requestUpdate();
  });
}

void TodayActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  const size_t rows = count_ + 3;
  // The Today hints label the front-strip previous/next keys Up/Down. In portrait
  // those are Left/Right, not the separate side Up/Down pins; the hint-strip touch
  // handler injects the front-strip buttons too. Accept both sources so the labels,
  // emulator controls, and the X4 Pro's side buttons all navigate.
  const bool moveUp = mappedInput.wasPressed(MappedInputManager::frontStripPrevious()) ||
                      mappedInput.wasPressed(MappedInputManager::Button::Up);
  const bool moveDown = mappedInput.wasPressed(MappedInputManager::frontStripNext()) ||
                        mappedInput.wasPressed(MappedInputManager::Button::Down);
  if (moveUp) {
    selected_ = (selected_ + rows - 1) % rows;
    requestUpdate();
  } else if (moveDown) {
    selected_ = (selected_ + 1) % rows;
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selected_ < count_) {
      todos_[selected_].done = !todos_[selected_].done;
      if (!save()) LOG_ERR("TODAY", "Could not save task state");
      requestUpdate();
    } else if (selected_ == count_) {
      openAddTask();
    } else if (selected_ == count_ + 1) {
      clearCompleted();
    } else {
      toggleSleepScreen();
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::PageBack) && selected_ < count_) {
    removeTask(selected_);
  }
}

void TodayActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_TODAY));
  const int left = metrics.contentSidePadding;
  const int right = width - metrics.contentSidePadding;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  char clock[16]{};
  HalClock::formatTime(clock, sizeof(clock), SETTINGS.clockFormat12h == 0);
  // Keep the clock and date in wide, fixed-height bands. Clear each band before
  // drawing so old e-ink pixels cannot make the two strings look superimposed.
  const int clockY = y + 4;
  renderer.fillRect(left - 4, clockY - 3, right - left + 8, 52, false);
  const int clockWidth = renderer.getTextWidth(NOTOSANS_18_FONT_ID, clock, EpdFontFamily::BOLD);
  renderer.drawText(NOTOSANS_18_FONT_ID, left + std::max(0, (right - left - clockWidth) / 2), clockY, clock, true,
                    EpdFontFamily::BOLD);
  const int dateY = clockY + 68;
  if (date_[0]) {
    renderer.fillRect(left - 4, dateY - 3, right - left + 8, 26, false);
    const int dateWidth = renderer.getTextWidth(UI_10_FONT_ID, date_);
    renderer.drawText(UI_10_FONT_ID, left + std::max(0, (right - left - dateWidth) / 2), dateY, date_);
    y = dateY + 28;
  } else {
    y = clockY + 52;
  }
  renderer.drawLine(left, y, right, y);
  y += 6;

  const int rowHeight = renderer.getLineHeight(UI_12_FONT_ID) + 10;
  const int footerHeight = metrics.buttonHintsHeight + metrics.verticalSpacing;
  visibleRows_ = std::max(1, (height - footerHeight - y - 4) / rowHeight);
  const size_t rows = count_ + 3;
  if (selected_ < top_) top_ = selected_;
  if (selected_ >= top_ + static_cast<size_t>(visibleRows_)) top_ = selected_ - visibleRows_ + 1;
  for (size_t i = top_; i < rows && i < top_ + static_cast<size_t>(visibleRows_); ++i) {
    const int rowY = y + static_cast<int>(i - top_) * rowHeight;
    const bool selected = i == selected_;
    const bool ink = !selected;
    if (selected) renderer.fillRect(left - 4, rowY, right - left + 8, rowHeight - 1, true);
    const int textY = rowY + 5;
    if (i < count_) {
      const int box = renderer.getLineHeight(UI_12_FONT_ID) - 4;
      renderer.drawRect(left, textY + 2, box, box, 2, ink);
      if (todos_[i].done) {
        renderer.drawLine(left + 2, textY + box / 2, left + box / 2, textY + box - 2, 2, ink);
        renderer.drawLine(left + box / 2, textY + box - 2, left + box - 2, textY + 2, 2, ink);
      }
      const int tx = left + box + 10;
      const auto shown = renderer.truncatedText(UI_12_FONT_ID, todos_[i].text, right - tx, EpdFontFamily::REGULAR);
      renderer.drawText(UI_12_FONT_ID, tx, textY, shown.c_str(), ink);
    } else {
      const char* label = i == count_       ? tr(STR_TODAY_ADD_TASK)
                          : i == count_ + 1 ? tr(STR_TODAY_CLEAR_DONE)
                                            : (sleepScreenEnabled() ? tr(STR_TODAY_SLEEP_OFF) : tr(STR_TODAY_SLEEP_ON));
      renderer.drawText(UI_12_FONT_ID, left, textY, label, ink, EpdFontFamily::BOLD);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
