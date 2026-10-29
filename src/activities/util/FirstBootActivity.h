#pragma once
#include "ConfirmationActivity.h"
#include <atomic>
class FirstBootActivity final : public ConfirmationActivity {
 std::atomic<unsigned long> shownAt{0};
 public:
 FirstBootActivity(GfxRenderer& r, MappedInputManager& i);
 void render(RenderLock&& lock) override;
 void loop() override;
 bool preventAutoSleep() override {return true;}
 protected:
 void finishWith(Answer answer) override;
};
