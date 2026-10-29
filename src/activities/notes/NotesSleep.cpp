#include "NotesSleep.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "NotesLibrary.h"
#include "fontIds.h"

namespace notes {

bool readAsleep(AsleepChoice& out) {
  HalFile file;
  if (!Storage.openFileForRead("NOTES", kAsleepFile, file)) return false;
  char buffer[160] = {};
  const int read = file.read(buffer, sizeof(buffer) - 1);
  file.close();
  if (read <= 0) return false;
  return parseAsleep(std::string(buffer, static_cast<size_t>(read)), out);
}

bool writeAsleep(const AsleepChoice& choice) {
  if (!Storage.ensureDirectoryExists("/.crosspoint")) return false;
  const std::string text = formatAsleep(choice);
  HalFile file;
  if (!Storage.openFileForWrite("NOTES", kAsleepFile, file)) {
    LOG_ERR("NOTES", "could not write %s", kAsleepFile);
    return false;
  }
  const size_t wrote = file.write(reinterpret_cast<const uint8_t*>(text.data()), text.size());
  const bool closed = file.close();
  return wrote == text.size() && closed;
}

void clearAsleep() { Storage.remove(kAsleepFile); }

bool drawAsleep(GfxRenderer& renderer) {
  AsleepChoice choice;
  if (!readAsleep(choice)) return false;

  Library library;
  std::string doc;
  if (!library.begin() || !library.load(choice.name, doc)) {
    LOG_INF("NOTES", "sleep screen: selected note '%s' is unavailable", choice.name.c_str());
    return false;
  }

  const std::vector<Line> lines = parse(doc);
  const std::vector<size_t> visible = drawnLines(doc, lines);
  if (visible.empty()) return false;

  const bool checklist = kindOf(lines) == Kind::List;
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int left = 18;
  const int right = width - left;
  const int bottom = height - 18;
  const int titleFont = NOTOSANS_16_FONT_ID;
  const int bodyFont = checklist ? UI_12_FONT_ID : BOOKERLY_12_FONT_ID;

  renderer.clearScreen();
  int y = 18;
  const std::string title = renderer.truncatedText(titleFont, choice.name.c_str(), right - left, EpdFontFamily::BOLD);
  renderer.drawText(titleFont, left, y, title.c_str(), true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(titleFont) + 8;
  renderer.drawLine(left, y, right, y, 2, true);
  y += 10;

  const int lineHeight = renderer.getLineHeight(bodyFont) + 6;
  for (const size_t index : visible) {
    if (y + lineHeight > bottom) break;
    const Line& line = lines[index];
    const std::string text = textOf(doc, line);
    int textX = left;
    int available = right - left;
    if (checklist && line.isTask) {
      const int box = std::max(10, renderer.getLineHeight(bodyFont) - 3);
      renderer.drawRect(left, y + 2, box, box, 2, true);
      if (line.checked) {
        renderer.drawLine(left + 2, y + box / 2, left + box / 2, y + box - 2, 2, true);
        renderer.drawLine(left + box / 2, y + box - 2, left + box - 2, y + 2, 2, true);
      }
      textX += box + 8;
      available -= box + 8;
    }
    const std::string shown = renderer.truncatedText(bodyFont, text.c_str(), available);
    renderer.drawText(bodyFont, textX, y, shown.c_str());
    y += lineHeight;
  }
  return true;
}

}  // namespace notes
