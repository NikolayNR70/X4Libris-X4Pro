add_executable(SleepCalendarTest
  SleepCalendarTest.cpp
  ${REPO_ROOT}/src/activities/games/calendar/SleepCalendar.cpp
)
target_include_directories(SleepCalendarTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(SleepCalendarTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SleepCalendarTest)
