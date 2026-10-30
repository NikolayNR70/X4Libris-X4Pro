#pragma once

namespace SleepCalendar {

struct Month {
  int firstWeekdayMondayZero;
  int days;
};

// Month is 1..12. Weekdays are Monday-first to match the calendar on screen.
Month month(int year, int month);

}  // namespace SleepCalendar
