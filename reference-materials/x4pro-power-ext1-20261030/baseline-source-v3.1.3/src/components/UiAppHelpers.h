#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>
#include <FreeInkUIIcon.h>

#include <algorithm>
#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "components/UIThemeTokens.h"
#include "components/icons/RestoredLogoBookIcon.h"
#include "components/icons/PerfectInkIcons.h"
#include "components/icons/cover.h"

#include "fontIds.h"

inline freeink::ui::GfxRendererTarget makeUiTarget(const GfxRenderer& renderer) {
  freeink::ui::GfxRendererTarget target(renderer);
  target.setFont(freeink::ui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);
  target.setFont(freeink::ui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  target.setFont(freeink::ui::GfxRendererTarget::FONT_TITLE, UI_12_FONT_ID);
  return target;
}

// Keep the selected card's outline and shape unchanged, and show selection as a uniform pale-gray
// fill, matching the sketch UI's card treatment. Use the renderer's standard LightGray fill so the
// 1-bit panel produces an even 25% gray instead of isolated, conspicuous dots.
inline void fillSoftSelection(const GfxRenderer& renderer, const int x, const int y, const int width,
                              const int height, const int cornerRadius = 0) {
  if (width <= 0 || height <= 0) return;
  const int radius = std::max(0, std::min({cornerRadius, width / 2, height / 2}));
  renderer.fillRoundedRect(x, y, width, height, radius, Color::LightGray);
}

inline freeink::ui::InputSnapshot touchSnapshotFrom(const MappedInputManager& mappedInput) {
  freeink::ui::InputSnapshot snapshot{};
  int touchX = 0;
  int touchY = 0;

  if (mappedInput.isScreenTouchHeld(touchX, touchY)) {
    snapshot.touchHeld = true;
    snapshot.touchX = static_cast<int16_t>(touchX);
    snapshot.touchY = static_cast<int16_t>(touchY);
  }
  if (mappedInput.wasScreenTouchPressed(touchX, touchY)) {
    snapshot.touchPressed = true;
    snapshot.touchX = static_cast<int16_t>(touchX);
    snapshot.touchY = static_cast<int16_t>(touchY);
  }
  if (mappedInput.wasScreenTapped(touchX, touchY)) {
    snapshot.touchReleased = true;
    snapshot.touchX = static_cast<int16_t>(touchX);
    snapshot.touchY = static_cast<int16_t>(touchY);
  } else if (mappedInput.wasScreenTouchReleased()) {
    snapshot.touchReleased = true;
    snapshot.touchX = -1;
    snapshot.touchY = -1;
  }
  // Some touch backends publish the swipe before the raw release edge. Cancel the pressed row
  // now so it cannot remain armed after the list scrolls and interfere with the next tap.
  if (!snapshot.touchReleased && mappedInput.wasSwipe() != MappedInputManager::SwipeDir::None) {
    snapshot.touchReleased = true;
    snapshot.touchX = -1;
    snapshot.touchY = -1;
  }
  return snapshot;
}

// Draw a monochrome bitmap icon through FreeInkUI so legacy GfxRenderer orientation
// handling is consistent with the other firmware screens.
inline void drawLucideIcon(const GfxRenderer& renderer, const freeink::Icon& icon, const int x, const int y,
                           const bool foregroundBlack = true) {
  freeink::ui::GfxRendererTarget target(renderer);
  target.bitmap(freeink::ui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(icon.w),
                                  static_cast<int16_t>(icon.h)},
                freeink::ui::bitmapFromIcon(icon), freeink::ui::BitmapMode::Center,
                freeink::ui::Paint::solid(foregroundBlack ? freeink::ui::Color::Black : freeink::ui::Color::White));
}

inline void drawLucideIconScaled(const GfxRenderer& renderer, const freeink::Icon& icon, const int x, const int y,
                                 const int size, const bool foregroundBlack = true) {
  if (size <= 0) return;
  freeink::ui::GfxRendererTarget target(renderer);
  target.bitmap(freeink::ui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(size),
                                  static_cast<int16_t>(size)},
                freeink::ui::bitmapFromIcon(icon), freeink::ui::BitmapMode::Contain,
                freeink::ui::Paint::solid(foregroundBlack ? freeink::ui::Color::Black : freeink::ui::Color::White));
}

// Choose a mask rasterized from the same SVG at the destination size. This avoids
// nearest-neighbor stretching, which made 32px strokes uneven in 30px and 54px UI slots. The 15px
// Wi-Fi/USB pair uses the 24px source, which preserves more stroke weight. The frame-corner and cover-placeholder book marks
// deliberately keep their original 32px mask via drawSketchCornerBook().
inline const freeink::Icon& lucideIconForUiSize(const int size, const freeink::Icon& icon24,
                                                const freeink::Icon& icon30, const freeink::Icon& icon32,
                                                const freeink::Icon& icon54) {
  if (size <= 27) return icon24;
  if (size <= 31) return icon30;
  if (size <= 43) return icon32;
  return icon54;
}

inline void drawLucideIconAtUiSize(const GfxRenderer& renderer, const freeink::Icon& icon24,
                                   const freeink::Icon& icon30, const freeink::Icon& icon32,
                                   const freeink::Icon& icon54, const int x, const int y, const int size) {
  drawLucideIconScaled(renderer, lucideIconForUiSize(size, icon24, icon30, icon32, icon54), x, y, size);
}

// Draw interface symbols from one generated Lucide mask set. The protected
// title-corner book remains on its original 32px artwork and destination.
inline void drawSketchCornerBook(const GfxRenderer& r, int x, int y, int n) {
  r.drawIcon(CoverIcon, x, y, n, n);
}
inline void drawSketchOpenBook(const GfxRenderer& r, int x, int y, int n) {
  drawLucideIconAtUiSize(r, perfectink_open_book_24, perfectink_open_book_30,
                         perfectink_open_book_32, perfectink_open_book_54, x, y, n);
}
inline void drawSketchBookStack(const GfxRenderer& r, int x, int y, int n) {
  drawLucideIconAtUiSize(r, perfectink_library_24, perfectink_library_30,
                         perfectink_library_32, perfectink_library_54, x, y, n);
}
inline void drawSketchFilledBookStack(const GfxRenderer& r, int x, int y, int n) { drawSketchBookStack(r, x, y, n); }
inline void drawSketchLogoBook(const GfxRenderer& r, int x, int y, int n) {
  const auto& icon = n <= 32 ? icon_sketch_logo_book_32 : n <= 34 ? icon_sketch_logo_book_34
                                                                   : icon_sketch_logo_book_54;
  drawLucideIcon(r, icon, x, y);
}
inline void drawSketchFolder(const GfxRenderer& r, int x, int y, int n) {
  drawLucideIconAtUiSize(r, perfectink_folder_24, perfectink_folder_30,
                         perfectink_folder_32, perfectink_folder_54, x, y, n);
}
inline void drawSketchWifiUsb(const GfxRenderer& r, int x, int y, int n) {
  if (n <= 0) return;
  const int side = n / 2;
  drawLucideIconScaled(r, perfectink_wifi_24, x, y + (n - side) / 2, side);
  drawLucideIconScaled(r, perfectink_usb_24, x + n - side, y + (n - side) / 2, side);
}
inline void drawSketchGlobe(const GfxRenderer& r, int x, int y, int n) {
  drawLucideIconAtUiSize(r, perfectink_globe_24, perfectink_globe_30,
                         perfectink_globe_32, perfectink_globe_54, x, y, n);
}
inline void drawSketchGear(const GfxRenderer& r, int x, int y, int n) {
  drawLucideIconAtUiSize(r, perfectink_settings_24, perfectink_settings_30,
                         perfectink_settings_32, perfectink_settings_54, x, y, n);
}
inline void drawSketchSmallGear(const GfxRenderer& r, int x, int y, int n) { drawSketchGear(r, x, y, n); }
inline void drawSketchMonitor(const GfxRenderer& r, int x, int y, int n) {
  drawLucideIconAtUiSize(r, perfectink_monitor_24, perfectink_monitor_30,
                         perfectink_monitor_32, perfectink_monitor_54, x, y, n);
}
inline void drawSketchHand(const GfxRenderer& r, int x, int y, int n) {
  drawLucideIconAtUiSize(r, perfectink_hand_24, perfectink_hand_30,
                         perfectink_hand_32, perfectink_hand_54, x, y, n);
}
inline void drawSketchGrid(const GfxRenderer& r, int x, int y, int n) {
  drawLucideIconAtUiSize(r, perfectink_grid_24, perfectink_grid_30,
                         perfectink_grid_32, perfectink_grid_54, x, y, n);
}
inline void drawSketchEllipsis(const GfxRenderer& r, int x, int y, int n) {
  drawLucideIconAtUiSize(r, perfectink_ellipsis_24, perfectink_ellipsis_30,
                         perfectink_ellipsis_32, perfectink_ellipsis_54, x, y, n);
}

inline float textScaleToFit(const GfxRenderer& renderer, const int fontId, const char* text, const int maxWidth,
                            const EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                            const float minScale = 0.60f) {
  if (!text || !*text || maxWidth <= 0) return 1.0f;
  const int usableWidth = std::max(1, maxWidth - 2);
  for (int percent = 100; percent >= static_cast<int>(minScale * 100.0f); percent -= 2) {
    const float scale = percent / 100.0f;
    if (renderer.getTextWidthScaled(fontId, text, style, scale) <= usableWidth) return scale;
  }
  return minScale;
}

inline void drawTextFittedInRect(const GfxRenderer& renderer, const int fontId, const char* text, const Rect& rect,
                                const int y, const bool black = true,
                                const EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                                const float minScale = 0.60f) {
  const float scale = textScaleToFit(renderer, fontId, text, rect.width, style, minScale);
  const int width = renderer.getTextWidthScaled(fontId, text, style, scale);
  const int x = rect.x + std::max(0, (rect.width - width) / 2);
  renderer.drawTextScaled(fontId, x, y, text, black, style, scale);
}

inline std::string normalizeWrappedText(const std::string& text) {
  std::string normalized;
  normalized.reserve(text.size());
  bool previousSpace = true;
  for (const char ch : text) {
    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
      if (!previousSpace) normalized.push_back(' ');
      previousSpace = true;
    } else {
      normalized.push_back(ch);
      previousSpace = false;
    }
  }
  if (!normalized.empty() && normalized.back() == ' ') normalized.pop_back();
  return normalized;
}

inline bool wrappedTextIsComplete(const char* original, const std::vector<std::string>& lines) {
  if (!original) return lines.empty();
  std::string joined;
  for (const auto& line : lines) {
    if (!joined.empty()) joined.push_back(' ');
    joined += line;
  }
  return normalizeWrappedText(joined) == normalizeWrappedText(original);
}

struct FittedWrappedText {
  std::vector<std::string> lines;
  float scale = 1.0f;
  bool complete = true;
};

inline FittedWrappedText wrappedTextToFit(const GfxRenderer& renderer, const int fontId, const char* text,
                                          const int maxWidth, const int maxLines,
                                          const EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                                          const float minScale = 0.60f) {
  FittedWrappedText result;
  if (!text || !*text || maxWidth <= 0 || maxLines <= 0) return result;
  for (int percent = 100; percent >= static_cast<int>(minScale * 100.0f); percent -= 5) {
    const float scale = percent / 100.0f;
    const int logicalWidth = std::max(1, static_cast<int>(maxWidth / scale) - 2);
    auto lines = renderer.wrappedText(fontId, text, logicalWidth, maxLines, style);
    if (wrappedTextIsComplete(text, lines)) return FittedWrappedText{std::move(lines), scale, true};
    result = FittedWrappedText{std::move(lines), scale, false};
  }
  return result;
}
