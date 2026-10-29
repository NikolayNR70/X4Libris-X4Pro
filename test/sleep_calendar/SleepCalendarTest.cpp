#include <gtest/gtest.h>

#include "activities/games/calendar/SleepCalendar.h"

TEST(SleepCalendar, GregorianMonthBoundariesAndWeekStart) {
  EXPECT_EQ(SleepCalendar::month(2024, 2).days, 29);
  EXPECT_EQ(SleepCalendar::month(1900, 2).days, 28);
  EXPECT_EQ(SleepCalendar::month(1900, 2).firstWeekdayMondayZero, 3);  // Thursday
  EXPECT_EQ(SleepCalendar::month(2000, 2).days, 29);
  EXPECT_EQ(SleepCalendar::month(2026, 10).days, 31);
  EXPECT_EQ(SleepCalendar::month(2026, 10).firstWeekdayMondayZero, 3);  // Thursday
}

TEST(SleepCalendar, RejectsInvalidMonths) {
  EXPECT_EQ(SleepCalendar::month(2026, 0).days, 0);
  EXPECT_EQ(SleepCalendar::month(2026, 13).days, 0);
}
