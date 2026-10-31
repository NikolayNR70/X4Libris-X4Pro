#pragma once

#include <AlReaderSyncClient.h>
#include <AlReaderSyncWorker.h>
#include <Epub/Section.h>

#include <memory>
#include <string>

#include "activities/Activity.h"

class AlReaderSyncActivity final : public Activity {
 public:
  enum class Intent : uint8_t { COMPARE, PULL_REMOTE, PUSH_LOCAL, AUTO_PUSH };

  AlReaderSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                       std::shared_ptr<Epub> epub, Section::BuildParams buildParams, std::string bookPath,
                       std::string cachePath, std::string bookKey, std::string variantSuffix,
                       int spineCount, int localSpine,
                       uint16_t localPage, uint32_t localPosition, uint32_t titlePositionUnits, Intent intent);

  void onEnter() override;
  void onExit() override;
  bool usesWifi() const override { return true; }
  // Section preparation still needs the reader's selected SD font. The activity
  // manager unloads that font when replacing a reader with a non-reader screen.
  bool isReaderActivity() const override { return true; }
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return waiting || phase_ == Phase::PrepareLocal || phase_ == Phase::PrepareRemote; }

 private:
  enum class Phase : uint8_t { Idle, PrepareLocal, Register, Fetch, Conflict, PrepareRemote, Update, VerifyUpdate,
                               Complete, Failed };

  std::shared_ptr<Epub> epub_;
  Section::BuildParams buildParams_;
  std::unique_ptr<Section> preparationSection_;
  int preparationSpine_ = 0;
  uint64_t preparedLength_ = 0;
  uint32_t remoteBodyPosition_ = 0;
  std::string bookPath_;
  std::string cachePath_;
  std::string bookKey_;
  std::string variantSuffix_;
  int spineCount_ = 0;
  int localSpine_ = 0;
  uint16_t localPage_ = 0;
  uint32_t localPosition_ = 0;
  uint32_t titlePositionUnits_ = 0;
  Intent intent_ = Intent::COMPARE;
  Phase phase_ = Phase::Idle;
  uint64_t sequence_ = 0;
  bool waiting = false;
  bool networkMemoryTrimmed_ = false;
  unsigned long completedAt_ = 0;
  unsigned long phaseStartedAt_ = 0;
  unsigned long preparationStartedAt_ = 0;
  bool preparationFontUnloaded_ = false;
  std::string status_;
  AlReaderSync::PositionRecord remote_{};

  bool post(AlReaderSync::Job::Operation operation);
  void startNetwork();
  void prepareStep();
  void fail(const std::string& message);
  void handleResult(AlReaderSync::Job&& job);
  bool applyRemotePosition(uint32_t position);
  void beginRemotePull();
  void leaveSync();
};
