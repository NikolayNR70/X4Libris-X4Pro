add_executable(WeatherDiagnosticsTest
  WeatherDiagnosticsTest.cpp
  ${REPO_ROOT}/lib/Weather/WeatherDiagnosticsLog.cpp
)

target_include_directories(WeatherDiagnosticsTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/lib/Weather
)

target_link_libraries(WeatherDiagnosticsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WeatherDiagnosticsTest)
