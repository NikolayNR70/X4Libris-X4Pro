#include "HeaderDate.h"

#include <GfxRenderer.h>
#include <HalClock.h>

#include <cstdio>
#include <ctime>

#include "components/UITheme.h"
#include "fontIds.h"

bool formatHeaderDateText(char* buffer, const size_t length) {
  if (buffer == nullptr || length == 0) return false;
  const time_t epoch = HalClock::now();
  if (epoch < 1577836800) return false;
  struct tm local {};
  if (localtime_r(&epoch, &local) == nullptr) return false;
  return std::strftime(buffer, length, "%d.%m.%Y", &local) != 0;
}

int headerDateReservedWidth(const GfxRenderer& renderer) {
  char text[16]{};
  return formatHeaderDateText(text, sizeof(text)) ? renderer.getTextWidth(UI_10_FONT_ID, text) + 12 : 0;
}

int headerDateLineBottomY(const GfxRenderer&, const ThemeMetrics& metrics, const int headerHeight) {
  return metrics.topPadding + (headerHeight > 0 ? headerHeight : metrics.headerHeight) - 8;
}

void drawHeaderDateAtBaseline(const GfxRenderer& renderer, const int pageWidth, const int baselineY) {
  char text[16]{};
  if (!formatHeaderDateText(text, sizeof(text))) return;
  const int width = renderer.getTextWidth(UI_10_FONT_ID, text);
  renderer.drawText(UI_10_FONT_ID, pageWidth - width - 12,
                    baselineY - renderer.getFontAscenderSize(UI_10_FONT_ID), text);
}

void drawHeaderDateAtLineBottom(const GfxRenderer& renderer, const int pageWidth, const int lineBottomY) {
  drawHeaderDateAtBaseline(renderer, pageWidth, lineBottomY - renderer.getLineHeight(UI_10_FONT_ID) +
                                                   renderer.getFontAscenderSize(UI_10_FONT_ID));
}

void drawHeaderDate(const GfxRenderer& renderer, const int pageWidth, const ThemeMetrics& metrics,
                    const int headerHeight) {
  drawHeaderDateAtLineBottom(renderer, pageWidth, headerDateLineBottomY(renderer, metrics, headerHeight));
}
