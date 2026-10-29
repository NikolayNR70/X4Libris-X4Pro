#include "ClockFace.h"

#include <HalClock.h>
#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "CrossPointSettings.h"
#include "GameUi.h"
#include "fontIds.h"

namespace clockface {

namespace {

constexpr char kSavePath[] = "/.crosspoint/games/clock.bin";
constexpr uint8_t kFileMagic[4] = {'V', 'C', 'L', '2'};
constexpr int kGridMargin = 26;
// A clock chip that was never set reports a date from long ago.
constexpr int kEarliestYear = 2024;

constexpr StrId kMonthNames[12] = {StrId::STR_MONTH_1, StrId::STR_MONTH_2,  StrId::STR_MONTH_3,  StrId::STR_MONTH_4,
                                   StrId::STR_MONTH_5, StrId::STR_MONTH_6,  StrId::STR_MONTH_7,  StrId::STR_MONTH_8,
                                   StrId::STR_MONTH_9, StrId::STR_MONTH_10, StrId::STR_MONTH_11, StrId::STR_MONTH_12};
constexpr StrId kWeekdayNames[7] = {StrId::STR_WEEKDAY_1, StrId::STR_WEEKDAY_2, StrId::STR_WEEKDAY_3,
                                    StrId::STR_WEEKDAY_4, StrId::STR_WEEKDAY_5, StrId::STR_WEEKDAY_6,
                                    StrId::STR_WEEKDAY_7};

const gameui::DigitSet kLargeDigits{appart::kClockLargeGlyphs, appart::kClockLargeBits, appart::kClockLargeRows};
const gameui::DigitSet kMediumDigits{appart::kClockMediumGlyphs, appart::kClockMediumBits, appart::kClockMediumRows};

// The first `count` characters of a UTF-8 string.
void leadingCharacters(const char* text, const int count, char* out, const size_t size) {
  size_t length = 0;
  int taken = 0;
  while (text[length] != '\0' && length + 1 < size) {
    const bool startsCharacter = (static_cast<uint8_t>(text[length]) & 0xC0) != 0x80;
    if (startsCharacter && taken++ == count) break;
    ++length;
  }
  memcpy(out, text, length);
  out[length] = '\0';
}

// Column of a weekday (0 = Monday) in a week that starts on the chosen day.
int columnOf(const int weekday, const uint8_t weekStart) {
  const int first = weekStart == Saturday ? 5 : (weekStart == Sunday ? 6 : 0);
  return (weekday - first + 7) % 7;
}

int rowsFor(const calmath::Date& shown, const uint8_t weekStart) {
  return (columnOf(calmath::weekday(shown), weekStart) + calmath::daysInMonth(shown.year, shown.month) + 6) / 7;
}

void drawChevron(const GfxRenderer& renderer, const int x, const int y, const bool pointsLeft) {
  const int tip = pointsLeft ? x - 5 : x + 5;
  const int tail = pointsLeft ? x + 5 : x - 5;
  renderer.drawLine(tail, y - 10, tip, y, 3, true);
  renderer.drawLine(tip, y, tail, y + 10, 3, true);
}

void drawTime(const GfxRenderer& renderer, const Face face, const int top, const calmath::DateTime& time) {
  char text[8];
  const bool twelveHour = SETTINGS.clockFormat12h != 0;
  if (twelveHour) {
    snprintf(text, sizeof(text), "%d:%02d", time.hour % 12 == 0 ? 12 : time.hour % 12, time.minute);
  } else {
    snprintf(text, sizeof(text), "%02d:%02d", time.hour, time.minute);
  }
  const int width = renderer.getScreenWidth();
  const bool large = face == Digital;
  const gameui::DigitSet& digits = large ? kLargeDigits : kMediumDigits;
  const int gap = large ? 8 : 6;
  const int y = large ? top + 8 : top - 6;
  gameui::digitText(renderer, digits, text, width / 2, y, gap);
  if (twelveHour) {
    const int right = width / 2 + gameui::digitTextWidth(digits, text, gap) / 2;
    renderer.drawText(UI_10_FONT_ID, std::min(right + 6, width - 44), y + digits.rows - 22,
                      time.hour < 12 ? "AM" : "PM", true, EpdFontFamily::BOLD);
  }
}

void drawAnalog(const GfxRenderer& renderer, const int top, const calmath::DateTime& time) {
  const int cx = renderer.getScreenWidth() / 2;
  const int cy = top + 118;
  constexpr int radius = 128;
  constexpr float kPi = 3.14159265f;
  gameui::ring(renderer, cx, cy, radius, 4, true);
  for (int mark = 0; mark < 12; ++mark) {
    const float angle = static_cast<float>(mark) * kPi / 6.0f;
    const bool quarter = mark % 3 == 0;
    const float inner = static_cast<float>(radius - (quarter ? 22 : 15));
    const float outer = static_cast<float>(radius - 8);
    renderer.drawLine(cx + static_cast<int>(lroundf(inner * sinf(angle))),
                      cy - static_cast<int>(lroundf(inner * cosf(angle))),
                      cx + static_cast<int>(lroundf(outer * sinf(angle))),
                      cy - static_cast<int>(lroundf(outer * cosf(angle))), quarter ? 5 : 3, true);
  }
  const float hourAngle = (static_cast<float>(time.hour % 12) + static_cast<float>(time.minute) / 60.0f) * kPi / 6.0f;
  const float minuteAngle = static_cast<float>(time.minute) * kPi / 30.0f;
  const struct {
    float angle;
    float length;
    int width;
  } hands[2] = {{hourAngle, 68.0f, 9}, {minuteAngle, 100.0f, 5}};
  for (const auto& hand : hands) {
    renderer.drawLine(cx - static_cast<int>(lroundf(14.0f * sinf(hand.angle))),
                      cy + static_cast<int>(lroundf(14.0f * cosf(hand.angle))),
                      cx + static_cast<int>(lroundf(hand.length * sinf(hand.angle))),
                      cy - static_cast<int>(lroundf(hand.length * cosf(hand.angle))), hand.width, true);
  }
  gameui::disc(renderer, cx, cy, 9, true);
}

// The weekday names above the columns, then the days of the month `shown`.
void drawMonth(const GfxRenderer& renderer, const Options& options, const int weekdayY, const int gridY,
               const int rowHeight, const calmath::Date& shown, const calmath::Date& today) {
  const int width = renderer.getScreenWidth();
  const int cellWidth = (width - 2 * kGridMargin) / 7;
  const int left = (width - 7 * cellWidth) / 2;
  constexpr const char* russianWeekdays[7] = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};
  for (int weekday = 0; weekday < 7; ++weekday) {
    char shortName[12];
    if (I18N.getLanguage() == Language::RU) {
      snprintf(shortName, sizeof(shortName), "%s", russianWeekdays[weekday]);
    } else {
      leadingCharacters(weekdayName(weekday), 2, shortName, sizeof(shortName));
    }
    gameui::centredText(renderer, SMALL_FONT_ID,
                        {left + columnOf(weekday, options.weekStart) * cellWidth, weekdayY, cellWidth, 24}, shortName,
                        true, EpdFontFamily::BOLD);
  }

  const calmath::Date first = calmath::addDays(shown, -columnOf(calmath::weekday(shown), options.weekStart));
  const int rows = rowsFor(shown, options.weekStart);
  for (int i = 0; i < rows * 7; ++i) {
    const calmath::Date day = calmath::addDays(first, i);
    const gameui::Box cell{left + (i % 7) * cellWidth, gridY + (i / 7) * rowHeight, cellWidth, rowHeight};
    const bool isToday = day == today;
    const bool inMonth = day.month == shown.month;
    char number[4];
    snprintf(number, sizeof(number), "%d", static_cast<int>(day.day));
    if (isToday) {
      const int side = std::min(cell.w, cell.h) - 8;
      renderer.fillRoundedRect(cell.x + (cell.w - side) / 2, cell.y + (cell.h - side) / 2, side, side, 12,
                               Color::Black);
    }
    gameui::centredText(renderer, UI_12_FONT_ID, cell, number, !isToday,
                        isToday ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    if (!inMonth && !isToday) gameui::fade(renderer, cell);
  }
}

void drawWeekNumber(const GfxRenderer& renderer, const int y, const calmath::Date& today) {
  if (y + 24 > renderer.getScreenHeight()) return;
  char week[32];
  snprintf(week, sizeof(week), tr(STR_CLOCK_WEEK), calmath::isoWeek(today));
  gameui::centredText(renderer, SMALL_FONT_ID, {0, y, renderer.getScreenWidth(), 24}, week);
}

}  // namespace

void loadOptions(Options& options) {
  uint8_t bytes[8];
  if (!gameui::readSave(kSavePath, bytes, sizeof(bytes)) || memcmp(bytes, kFileMagic, 4) != 0) return;
  options.face = bytes[4] < kFaceCount ? bytes[4] : Digital;
  // Preserve the old save-record layout, but ignore the retired Persian calendar flag.
  options.persianDate = false;
  options.weekStart = bytes[6] < kWeekStartCount ? bytes[6] : Monday;
  options.weekNumber = bytes[7] != 0;
}

void saveOptions(const Options& options) {
  const uint8_t bytes[8] = {kFileMagic[0],
                            kFileMagic[1],
                            kFileMagic[2],
                            kFileMagic[3],
                            options.face,
                            0,
                            options.weekStart,
                            static_cast<uint8_t>(options.weekNumber ? 1 : 0)};
  gameui::writeSave(kSavePath, bytes, sizeof(bytes));
}

const char* monthName(const int month) { return month >= 1 && month <= 12 ? I18N.get(kMonthNames[month - 1]) : ""; }

const char* weekdayName(const int weekday) {
  return weekday >= 0 && weekday <= 6 ? I18N.get(kWeekdayNames[weekday]) : "";
}

bool now(calmath::DateTime& out) {
  const time_t epoch = HalClock::now();
  if (epoch < 1704067200) return false;
  struct tm local {};
  if (localtime_r(&epoch, &local) == nullptr || local.tm_year + 1900 < kEarliestYear) return false;
  out = calmath::DateTime{calmath::Date{static_cast<int16_t>(local.tm_year + 1900),
                                       static_cast<uint8_t>(local.tm_mon + 1),
                                       static_cast<uint8_t>(local.tm_mday)},
                          static_cast<uint8_t>(local.tm_hour), static_cast<uint8_t>(local.tm_min)};
  return true;
}

Layout appLayout(const Options& options, const int top, const calmath::Date& shown) {
  Layout layout;
  // A month that starts late in the week and is long enough needs a sixth row.
  layout.rows = rowsFor(shown, options.weekStart);
  constexpr int lift = 18;
  switch (options.shownFace()) {
    case Analog:
      layout.dateY = top + 264;
      layout.monthY = top + 342 - lift;
      layout.weekdayY = top + 384 - lift;
      layout.gridY = top + 412 - lift;
      layout.rowHeight = layout.rows == 6 ? 46 : 50;
      break;
    default:
      layout.dateY = top + 144;
      layout.monthY = top + 240 - lift;
      layout.weekdayY = top + 288 - lift;
      layout.gridY = top + 318 - lift;
      layout.rowHeight = layout.rows == 6 ? 58 : 60;
      break;
  }
  return layout;
}

void drawApp(const GfxRenderer& renderer, const Options& options, const int top, const calmath::DateTime& time,
             const calmath::Date& shown, const bool showsCurrentMonth) {
  const int width = renderer.getScreenWidth();
  const Face face = options.shownFace();
  const Layout layout = appLayout(options, top, shown);
  if (face == Analog) {
    drawAnalog(renderer, top, time);
  } else {
    drawTime(renderer, face, top, time);
  }

  char line[96];
  snprintf(line, sizeof(line), tr(STR_CLOCK_DATE_FORMAT), weekdayName(calmath::weekday(time.date)),
           static_cast<int>(time.date.day), monthName(time.date.month), static_cast<int>(time.date.year));
  gameui::fittedText(renderer, {12, layout.dateY, width - 24, 34}, line);
  if (face != Analog) renderer.drawLine(40, layout.monthY - 14, width - 40, layout.monthY - 14, true);

  // The month's name between the arrows that turn the months.
  char title[48];
  snprintf(title, sizeof(title), "%s %d", monthName(shown.month), static_cast<int>(shown.year));
  drawChevron(renderer, 24, layout.monthY + 17, true);
  drawChevron(renderer, width - 24, layout.monthY + 17, false);
  gameui::centredText(renderer, UI_12_FONT_ID, {0, layout.monthY, width, 34}, title, true, EpdFontFamily::BOLD);

  drawMonth(renderer, options, layout.weekdayY, layout.gridY, layout.rowHeight, shown, time.date);
  if (options.weekNumber && face != Analog && showsCurrentMonth) {
    drawWeekNumber(renderer, layout.gridY + layout.rows * layout.rowHeight + 6, time.date);
  }
}

void drawSleepCalendar(const GfxRenderer& renderer, const Options& options, const calmath::DateTime& time) {
  const int width = renderer.getScreenWidth();
  gameui::centredText(renderer, UI_12_FONT_ID, {0, 44, width, 34}, weekdayName(calmath::weekday(time.date)), true,
                      EpdFontFamily::BOLD);
  char text[96];
  snprintf(text, sizeof(text), "%d", static_cast<int>(time.date.day));
  gameui::digitText(renderer, kLargeDigits, text, width / 2, 104, 8);
  snprintf(text, sizeof(text), "%s %d", monthName(time.date.month), static_cast<int>(time.date.year));
  gameui::centredText(renderer, UI_12_FONT_ID, {0, 246, width, 34}, text, true, EpdFontFamily::BOLD);
  const int ruleY = 300;
  renderer.drawLine(40, ruleY, width - 40, ruleY, true);

  calmath::Date shown = time.date;
  shown.day = 1;
  constexpr int rowHeight = 62;
  drawMonth(renderer, options, ruleY + 20, ruleY + 52, rowHeight, shown, time.date);
  if (options.weekNumber) {
    drawWeekNumber(renderer, ruleY + 52 + rowsFor(shown, options.weekStart) * rowHeight + 10, time.date);
  }
}

}  // namespace clockface
