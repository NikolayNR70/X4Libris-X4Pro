#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "images/LogoX4Libris.h"

void BootActivity::onEnter() {
  Activity::onEnter();
  RenderLock lock(*this);

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const int logoX = (pageWidth - LogoX4LibrisWidth) / 2;
  const int logoY = pageHeight / 2 - LogoX4LibrisHeight / 2 - 60;

  renderer.clearScreen();
  drawX4LibrisLogo(renderer, logoX, logoY);

  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 110, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
