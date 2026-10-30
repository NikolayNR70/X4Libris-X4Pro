#include "SettingsDashboardActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <array>

#include "activities/ActivityManager.h"
#include "activities/settings/SettingsActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/listIcons.h"
#include "components/themes/TapTargets.h"
#include "fontIds.h"

namespace {
constexpr int kCardCount = 6;
constexpr StrId kTitles[kCardCount] = {StrId::STR_DASH_GENERAL, StrId::STR_DASH_SCREEN, StrId::STR_DASH_READING,
                                       StrId::STR_DASH_CONTROLS, StrId::STR_DASH_LIBRARY, StrId::STR_DASH_NETWORK};
constexpr StrId kSummaries[kCardCount] = {StrId::STR_DASH_GENERAL_HINT, StrId::STR_DASH_SCREEN_HINT,
                                          StrId::STR_DASH_READING_HINT, StrId::STR_DASH_CONTROLS_HINT,
                                          StrId::STR_DASH_LIBRARY_HINT, StrId::STR_DASH_NETWORK_HINT};
constexpr int kSettingsTabs[kCardCount] = {0, 1, 2, 3, 4, 5};

void drawCardIcon(const GfxRenderer& renderer, int kind, int x, int y, int size) {
  if (kind == 0) {
    drawSketchGear(renderer, x, y, size);
  } else if (kind == 1) {
    drawSketchMonitor(renderer, x, y, size);
  } else if (kind == 2) {
    drawSketchOpenBook(renderer, x, y, size);
  } else if (kind == 3) {
    drawSketchHand(renderer, x, y, size);
  } else if (kind == 4) {
    drawSketchLogoBook(renderer, x, y, size);
  } else {
    drawSketchGlobe(renderer, x, y, size);
  }
}
}  // namespace

void SettingsDashboardActivity::onEnter() {
  Activity::onEnter();
  selected = 0;
  requestUpdate();
}

void SettingsDashboardActivity::openCard(const int index) {
  if (index < 0 || index >= kCardCount) return;
  selected = index;
  startActivityForResult(std::make_unique<SettingsActivity>(renderer, mappedInput, kSettingsTabs[index], true),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void SettingsDashboardActivity::loop() {
  navigator.onNextList(selected, kCardCount, [this] { requestUpdate(); }, 2);
  navigator.onPreviousList(selected, kCardCount, [this] { requestUpdate(); }, 2);
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openCard(selected);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (returnToParent) {
      finish();
    } else {
      onGoHome();
    }
    return;
  }
#if CP_TOUCH_UI
  if (mappedInput.hasTouch()) {
    int x = 0, y = 0;
    if (mappedInput.wasScreenTouchDown(x, y)) {
      (void)TapTargets::screenActions().hitTest(x, y);
    }
    if (mappedInput.wasScreenTapped(x, y)) {
      const int target = TapTargets::screenActions().hitTest(x, y);
      if (target == kCardCount) {
        if (returnToParent) {
          finish();
        } else {
          onGoHome();
        }
        return;
      }
      if (target >= 0 && target < kCardCount) {
        const auto result = listTapActivation.applyPreference(
            target, ListRowTap::apply(target, kCardCount, selected),
            SETTINGS.touchListActivation == CrossPointSettings::TOUCH_LIST_ACTIVATE_IMMEDIATELY);
        if (result == ListRowTap::Result::Selected) {
          selected = target;
          requestUpdate();
        } else if (result == ListRowTap::Result::Activate) {
          openCard(target);
        }
      }
    }
  }
#endif
}

void SettingsDashboardActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = UITheme::getContentRect(renderer, false, false);
  GUI.drawHeader(renderer, UITheme::getHeaderRect(renderer), nullptr);

  const int statusBottom = std::max(UITheme::getStatusBarTopHeight(true) + 5,
                                    metrics.topPadding + std::max(metrics.batteryHeight, 18) + 16);
  constexpr int frameInset = 14;
  const int frameX = content.x + frameInset;
  const int frameW = std::max(1, content.width - 2 * frameInset);
  const int frameY = std::max(content.y + 2, statusBottom);
  const int frameBottom = content.y + content.height - 4;
  const int frameH = std::max(1, frameBottom - frameY);
  renderer.drawRect(frameX, frameY, frameW, frameH, true);

  const int pad = 22;
  const int innerX = frameX + pad;
  const int innerW = std::max(1, frameW - 2 * pad);
  const int titleY = frameY + 8;
  const int arrowX = innerX + 2;
  const int arrowY = titleY + renderer.getLineHeight(UI_12_FONT_ID) / 2;
  renderer.drawLine(arrowX + 10, arrowY - 8, arrowX + 2, arrowY, true);
  renderer.drawLine(arrowX + 2, arrowY, arrowX + 10, arrowY + 8, true);
  const int titleX = innerX + 24;
  const int bookW = 34;
  const int titleWidth = std::max(1, innerW - 24 - bookW - 12);
  const char* title = tr(STR_SETTINGS_TITLE);
  const float titleScale = textScaleToFit(renderer, UI_12_FONT_ID, title, titleWidth, EpdFontFamily::BOLD);
  renderer.drawTextScaled(UI_12_FONT_ID, titleX, titleY, title, true, EpdFontFamily::BOLD, titleScale);
  const int bookX = frameX + frameW - pad - bookW;
  const int bookY = titleY - 5;
  drawSketchCornerBook(renderer, bookX, bookY, 32);
  const int titleLineHeight = std::max(1, static_cast<int>(std::ceil(renderer.getLineHeight(UI_12_FONT_ID) * titleScale)));
  const int lineY = titleY + titleLineHeight + 4;
  const int titleTextW = renderer.getTextWidthScaled(UI_12_FONT_ID, title, EpdFontFamily::BOLD, titleScale);
  renderer.drawLine(titleX + titleTextW + 8, lineY, bookX - 7, lineY, true);

  const int gridTop = lineY + 10;
  const int bottom = frameBottom - 16;
  const int gap = 16;
  const int cardW = (innerW - gap) / 2;
  const int cardH = std::max(1, (bottom - gridTop - 2 * gap) / 3);
  const int gridX = innerX;
  TapTargets::Recorder::Builder targets;
  targets.add(frameX + pad - 2, titleY - 4, 24, renderer.getLineHeight(UI_12_FONT_ID) + 8, kCardCount);
  for (int i = 0; i < kCardCount; ++i) {
    const int row = i / 2;
    const int col = i % 2;
    const int x = gridX + col * (cardW + gap);
    const int y = gridTop + row * (cardH + gap);
    targets.add(x, y, cardW, cardH, i);
    if (i == selected) {
      fillSoftSelection(renderer, x + 1, y + 1, cardW - 2, cardH - 2, 6);
    }
    renderer.drawRoundedRect(x, y, cardW, cardH, 1, 7, true);
    const int iconSize = std::min(54, std::max(30, cardW / 3));
    drawCardIcon(renderer, i, x + (cardW - iconSize) / 2, y + 8, iconSize);
    const int titleYCard = y + 8 + iconSize + 5;
    const char* cardTitle = I18N.get(kTitles[i]);
    const float cardTitleScale = textScaleToFit(renderer, UI_12_FONT_ID, cardTitle, cardW - 14,
                                                 EpdFontFamily::REGULAR);
    const int cardTitleWidth = renderer.getTextWidthScaled(UI_12_FONT_ID, cardTitle, EpdFontFamily::REGULAR,
                                                            cardTitleScale);
    renderer.drawTextScaled(UI_12_FONT_ID, x + std::max(0, (cardW - cardTitleWidth) / 2), titleYCard,
                            cardTitle, true, EpdFontFamily::REGULAR, cardTitleScale);
    const int cardTitleHeight = std::max(1, static_cast<int>(std::ceil(renderer.getLineHeight(UI_12_FONT_ID) * cardTitleScale)));
    const int descY = titleYCard + cardTitleHeight + 3;
    const char* summaryText = I18N.get(kSummaries[i]);
    const auto summary = wrappedTextToFit(renderer, UI_10_FONT_ID, summaryText, cardW - 18, 4,
                                           EpdFontFamily::REGULAR, 0.85f);
    int line = descY;
    const int summaryLineHeight = std::max(1, static_cast<int>(std::ceil(renderer.getLineHeight(UI_10_FONT_ID) * summary.scale)));
    for (const auto& part : summary.lines) {
      const int lineWidth = renderer.getTextWidthScaled(UI_10_FONT_ID, part.c_str(), EpdFontFamily::REGULAR,
                                                         summary.scale);
      renderer.drawTextScaled(UI_10_FONT_ID, x + std::max(0, (cardW - lineWidth) / 2), line, part.c_str(),
                              true, EpdFontFamily::REGULAR, summary.scale);
      line += summaryLineHeight;
    }
  }
  TapTargets::screenActions().record(targets);
  renderer.displayBuffer();
}
