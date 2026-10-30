#include "SleepCalendar.h"

namespace SleepCalendar {
namespace {
bool leapYear(const int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
}  // namespace

Month month(const int year, const int monthNumber) {
  if (monthNumber < 1 || monthNumber > 12) return {0, 0};
  constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (year < 1600 || year > 9999) return {0, 0};
  int daysSinceEpoch = 0;  // 1970-01-01 was Thursday (Monday-first index 3).
  if (year >= 1970) {
    for (int y = 1970; y < year; ++y) daysSinceEpoch += leapYear(y) ? 366 : 365;
  } else {
    for (int y = 1969; y >= year; --y) daysSinceEpoch -= leapYear(y) ? 366 : 365;
  }
  for (int m = 1; m < monthNumber; ++m) daysSinceEpoch += kDays[m - 1] + (m == 2 && leapYear(year) ? 1 : 0);
  const int days = kDays[monthNumber - 1] + (monthNumber == 2 && leapYear(year) ? 1 : 0);
  const int weekday = (3 + daysSinceEpoch) % 7;
  return {(weekday + 7) % 7, days};
}

}  // namespace SleepCalendar
