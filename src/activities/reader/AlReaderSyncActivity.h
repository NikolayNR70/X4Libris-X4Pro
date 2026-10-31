#pragma once

#include <AlReaderSyncClient.h>
#include <AlReaderSyncWorker.h>

#include <string>

#include "activities/Activity.h"

class AlReaderSyncActivity final : public Activity {
 public:
  enum class Intent : uint8_t { COMPARE, PULL_REMOTE, PUSH_LOCAL };

  AlReaderSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                       std::string cachePath, std::string bookKey, std::string variantSuffix,
                       int spineCount, int localSpine,
                       uint16_t localPage, uint32_t localPosition, Intent intent);

  void onEnter() override;
  void onExit() override;
  bool usesWifi() const override { return true; }
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return waiting; }

 private:
  enum class Phase : uint8_t { Idle, Register, Fetch, Update, Complete, Failed };

  std::string bookPath_;
  std::string cachePath_;
  std::string bookKey_;
  std::string variantSuffix_;
  int spineCount_ = 0;
  int localSpine_ = 0;
  uint16_t localPage_ = 0;
  uint32_t localPosition_ = 0;
  Intent intent_ = Intent::COMPARE;
  Phase phase_ = Phase::Idle;
  uint64_t sequence_ = 0;
  bool waiting = false;
  bool networkMemoryTrimmed_ = false;
  unsigned long completedAt_ = 0;
  std::string status_;
  AlReaderSync::PositionRecord remote_{};

  bool post(AlReaderSync::Job::Operation operation);
  void fail(const std::string& message);
  void handleResult(AlReaderSync::Job&& job);
  bool applyRemotePosition(uint32_t position);
};
