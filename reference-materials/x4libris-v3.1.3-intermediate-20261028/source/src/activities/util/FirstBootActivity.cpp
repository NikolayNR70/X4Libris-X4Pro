#include "FirstBootActivity.h"
#include "FirstBootGuard.h"
#include "activities/ActivityManager.h"
#include <Arduino.h>
FirstBootActivity::FirstBootActivity(GfxRenderer& r, MappedInputManager& i)
 : ConfirmationActivity(r,i,Question{"",tr(STR_TRIAL_TITLE),
    FirstBootGuard::hasFallback()?tr(STR_TRIAL_BODY):tr(STR_TRIAL_NO_FALLBACK),
    StrId::STR_TRIAL_REVERT,StrId::STR_TRIAL_KEEP}) {}
void FirstBootActivity::render(RenderLock&& lock) {
 ConfirmationActivity::render(std::move(lock));
 if(!shownAt) shownAt=millis();
}
void FirstBootActivity::loop() {
 // X4 Pro has no physical front strip. Give the two real side buttons explicit
 // meanings independent of orientation, user remaps, and touch settings.
 if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
  finishWith(Answer::Accept);
  return;
 }
 if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
  finishWith(Answer::Cancel);
  return;
 }
 ConfirmationActivity::loop();
}
void FirstBootActivity::finishWith(Answer answer) {
 if(answer!=Answer::Accept) {FirstBootGuard::rollback();return;}
 // Ignore the inherited wake contact and require a visible, settled screen.
 if(!shownAt || millis()-shownAt<5000) return;
 if(FirstBootGuard::accept()) {ESP.restart();}
}
