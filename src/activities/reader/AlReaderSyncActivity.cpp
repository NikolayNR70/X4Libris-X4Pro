#include "AlReaderSyncActivity.h"

#include <Arduino.h>
#include <Epub/Section.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_system.h>
#include <WiFi.h>

#include <algorithm>

#include "AlReaderSyncSettings.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/NetworkMemoryTrim.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/reader/EpubReaderActivity.h"
#include "SilentRestart.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long kResultHoldMs = 1400;
int32_t localSyncId() {
  const auto id = static_cast<int32_t>(ESP.getEfuseMac() & 0x7fffffffULL);
  return id == 0 ? 1 : id;
}
}

AlReaderSyncActivity::AlReaderSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           std::string bookPath, std::string cachePath, std::string bookKey,
                                           std::string variantSuffix,
                                           const int spineCount, const int localSpine, const uint16_t localPage,
                                           const uint32_t localPosition, const Intent intent)
    : Activity("AlReaderSync", renderer, mappedInput),
      bookPath_(std::move(bookPath)),
      cachePath_(std::move(cachePath)),
      bookKey_(std::move(bookKey)),
      variantSuffix_(std::move(variantSuffix)),
      spineCount_(spineCount),
      localSpine_(localSpine),
      localPage_(localPage),
      localPosition_(localPosition),
      intent_(intent) {}

void AlReaderSyncActivity::onEnter() {
  Activity::onEnter();
  if (!ALREADER_SYNC_SETTINGS.hasIdentifier()) {
    fail("Сначала укажите идентификатор AlReader.com в настройках сети.");
    return;
  }
  if (bookKey_.empty() || spineCount_ <= 0 || localPosition_ > static_cast<uint32_t>(INT32_MAX)) {
    fail("Не удалось подготовить позицию книги для AlReader.");
    return;
  }
  trimMemoryForNetworkSession(renderer, "ARS");
  networkMemoryTrimmed_ = !renderer.hasSecondaryBuffer();
  status_ = "Подключение к AlReader.com…";
  if (WiFi.status() != WL_CONNECTED) {
    status_ = "Подключение к Wi-Fi…";
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                               fail("Подключение к Wi-Fi отменено или не удалось.");
                               return;
                             }
                             WiFi.setSleep(false);
                             status_ = "Подключение к AlReader.com…";
                             if (!post(AlReaderSync::Job::Operation::REGISTER))
                               fail("Не удалось запустить фоновую синхронизацию.");
                             requestUpdate();
                           });
  } else if (!post(AlReaderSync::Job::Operation::REGISTER)) {
    fail("Не удалось запустить фоновую синхронизацию.");
  }
  requestUpdate();
}

void AlReaderSyncActivity::onExit() {
  Activity::onExit();
  if (networkMemoryTrimmed_) {
    // The network-session trim deliberately releases the secondary framebuffer. Match the
    // established KOReader path and reboot before rebuilding reader state, restoring clean RAM.
    silentRestartToReader();
  }
}

bool AlReaderSyncActivity::post(const AlReaderSync::Job::Operation operation) {
  AlReaderSync::Job job;
  job.operation = operation;
  job.identifier = ALREADER_SYNC_SETTINGS.getIdentifier();
  job.bookKey = bookKey_;
  if (operation == AlReaderSync::Job::Operation::UPDATE) {
    job.record.syncId = localSyncId();
    job.record.readPositionStart = localPosition_;
  }
  if (!AlReaderSync::Worker::post(std::move(job), &sequence_)) return false;
  phase_ = operation == AlReaderSync::Job::Operation::REGISTER
               ? Phase::Register
               : (operation == AlReaderSync::Job::Operation::FETCH ? Phase::Fetch : Phase::Update);
  waiting = true;
  return true;
}

void AlReaderSyncActivity::fail(const std::string& message) {
  waiting = false;
  phase_ = Phase::Failed;
  status_ = message;
  completedAt_ = millis();
  requestUpdate();
}

void AlReaderSyncActivity::loop() {
  if (waiting && AlReaderSync::Worker::isDone(sequence_)) {
    auto job = AlReaderSync::Worker::consume(sequence_);
    waiting = false;
    handleResult(std::move(job));
    requestUpdate();
  }

  if (phase_ == Phase::Complete || phase_ == Phase::Failed) {
    ButtonEventManager::ButtonEvent event;
    while (buttonEvents.consumeEvent(event)) {
      if (event.type == ButtonEventManager::PressType::Short) {
        activityManager.goToReader(bookPath_);
        return;
      }
    }
    // Keep failures visible until Back is pressed. A 1.4-second error flash hid the
    // actual cause of a failed sync and looked like a silent jump to the home screen.
    if (phase_ == Phase::Complete && millis() - completedAt_ >= kResultHoldMs)
      activityManager.goToReader(bookPath_);
  }
}

void AlReaderSyncActivity::handleResult(AlReaderSync::Job&& job) {
  if (!job.result) {
    fail(job.result.message.empty() ? "Синхронизация AlReader.com не выполнена." : job.result.message);
    completedAt_ = millis();
    return;
  }

  if (phase_ == Phase::Register) {
    status_ = "Проверка позиции книги…";
    if (!post(AlReaderSync::Job::Operation::FETCH)) fail("Не удалось запустить запрос позиции.");
    return;
  }

  if (phase_ == Phase::Fetch) {
    if (job.result.hasRecord) {
      remote_ = job.result.record;
      if (intent_ == Intent::COMPARE && localPosition_ == remote_.readPositionStart) {
        status_ = "Позиции чтения совпадают.";
        phase_ = Phase::Complete;
        completedAt_ = millis();
        return;
      }
      if (intent_ == Intent::PUSH_LOCAL ||
          (intent_ == Intent::COMPARE && localPosition_ > remote_.readPositionStart)) {
        status_ = "Передача позиции на AlReader.com…";
        if (!post(AlReaderSync::Job::Operation::UPDATE)) fail("Не удалось запустить запись позиции.");
        return;
      }
      if (intent_ == Intent::PULL_REMOTE || intent_ == Intent::COMPARE) {
        if (!applyRemotePosition(remote_.readPositionStart)) {
          fail("Позиция AlReader получена, но её нельзя точно сопоставить с кэшем книги.");
          completedAt_ = millis();
          return;
        }
        status_ = "Позиция получена с AlReader.com.";
        phase_ = Phase::Complete;
        completedAt_ = millis();
        return;
      }
    }

    if (intent_ == Intent::PULL_REMOTE) {
      fail("На AlReader.com ещё нет позиции этой книги.");
      return;
    }
    // An empty successful GET means that no position exists yet. Seed the shared record with
    // this device's position so the next reader can fetch it.
    status_ = "Сохранение первой позиции книги…";
    if (!post(AlReaderSync::Job::Operation::UPDATE)) fail("Не удалось запустить запись позиции.");
    return;
  }

  if (phase_ == Phase::Update) {
    status_ = "Позиция сохранена на AlReader.com.";
    phase_ = Phase::Complete;
    completedAt_ = millis();
  }
}

bool AlReaderSyncActivity::applyRemotePosition(const uint32_t position) {
  const auto target = Section::getCachedPageForAlReaderTextPosition(cachePath_, spineCount_, position, variantSuffix_);
  if (!target) return false;
  const auto [spine, page] = *target;
  // Progress is persisted before re-opening the existing reader activity. The reader then performs
  // its normal layout/cache validation and clamps the page to the current layout if needed.
  if (!EpubReaderActivity::writeReaderProgressCache(cachePath_, spine, page, 0, 0)) return false;
  LOG_INF("ARS", "Applied AlReader position to cached page spine=%d page=%u", spine, page);
  return true;
}

void AlReaderSyncActivity::render(RenderLock&&) {
  const Rect contentRect = UITheme::getContentRect(renderer, true, false);
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, contentRect.y + 18, "AlReader.com", true, EpdFontFamily::BOLD);
  const int textX = contentRect.x + 12;
  const int maxWidth = contentRect.width - 24;
  int textY = contentRect.y + 72;
  std::string line;
  size_t start = 0;
  while (start < status_.size()) {
    const size_t end = status_.find(' ', start);
    const std::string word = status_.substr(start, end == std::string::npos ? std::string::npos : end - start);
    const std::string candidate = line.empty() ? word : line + " " + word;
    if (!line.empty() && renderer.getTextWidth(UI_10_FONT_ID, candidate.c_str()) > maxWidth) {
      renderer.drawText(UI_10_FONT_ID, textX, textY, line.c_str());
      textY += renderer.getLineHeight(UI_10_FONT_ID) + 4;
      line = word;
    } else {
      line = candidate;
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  if (!line.empty()) renderer.drawText(UI_10_FONT_ID, textX, textY, line.c_str());
  if (phase_ == Phase::Failed || phase_ == Phase::Complete) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
