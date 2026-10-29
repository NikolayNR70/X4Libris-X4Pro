#pragma once

#include <GfxRenderer.h>

#include <cstddef>
#include <cstdint>

#include "util/CalendarMath.h"

// What the Clock app and the Clock & Calendar sleep screen draw: the time or a
// large date above a month. Kept apart from the app's screen so the sleep
// screen can paint the same calendar without it.
namespace clockface {

enum Face : uint8_t { Digital = 0, Analog = 1, kFaceCount = 2 };
enum WeekStart : uint8_t { Monday = 0, Saturday = 1, Sunday = 2, kWeekStartCount = 3 };

// The Clock settings (Apps > Manage > Clock > Settings), kept on the card.
struct Options {
  uint8_t face = Digital;
  // Kept in the on-card record for compatibility with earlier versions. Persian
  // calendar display is disabled in this build and this value is always false.
  bool persianDate = false;
  uint8_t weekStart = Monday;
  bool weekNumber = true;

  Face shownFace() const { return face >= kFaceCount ? Digital : static_cast<Face>(face); }
};

void loadOptions(Options& options);
void saveOptions(const Options& options);

// The local date and time; false while the clock has never been set.
bool now(calmath::DateTime& out);
const char* monthName(int month);      // 1..12
const char* weekdayName(int weekday);  // 0 = Monday

struct Layout {
  int dateY = 0;
  int monthY = 0;
  int weekdayY = 0;
  int gridY = 0;
  int rowHeight = 0;
  int rows = 5;
};

// Where the parts of the app's screen sit below `top` for the month `shown` (its first day).
Layout appLayout(const Options& options, int top, const calmath::Date& shown);

// The app: clock, date lines, month title with arrows and the month `shown`.
void drawApp(const GfxRenderer& renderer, const Options& options, int top, const calmath::DateTime& time,
             const calmath::Date& shown, bool showsCurrentMonth);
// The sleep screen: weekday, a large day number, the month and its calendar.
// No time, so it stays right until midnight.
void drawSleepCalendar(const GfxRenderer& renderer, const Options& options, const calmath::DateTime& time);

}  // namespace clockface
