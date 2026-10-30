#include <WeatherDiagnosticsLog.h>
#include <gtest/gtest.h>

#include "HalStorage.h"

TEST(WeatherDiagnosticsLog, AppendsReadableEventsToVisibleSdRootFile) {
  Storage.clear();

  ASSERT_TRUE(WeatherDiagnosticsLog::append("forecast_start", "host=api.open-meteo.com"));
  ASSERT_TRUE(WeatherDiagnosticsLog::append("forecast_failed", "stage=http SecureNet rc=-2"));

  const std::string& log = Storage.contents(WeatherDiagnosticsLog::FILE_PATH);
  const size_t start = log.find("[WEATHER] [forecast_start] host=api.open-meteo.com");
  const size_t failed = log.find("[WEATHER] [forecast_failed] stage=http SecureNet rc=-2");
  ASSERT_NE(start, std::string::npos);
  ASSERT_NE(failed, std::string::npos);
  EXPECT_LT(start, failed);
  EXPECT_EQ(log.back(), '\n');
}

TEST(WeatherDiagnosticsLog, RotatesWithoutOverwritingTheMostRecentPreviousLog) {
  Storage.clear();
  const std::string oldLog(WeatherDiagnosticsLog::MAX_FILE_BYTES, 'x');
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("test", WeatherDiagnosticsLog::FILE_PATH, file));
  ASSERT_EQ(file.write(reinterpret_cast<const uint8_t*>(oldLog.data()), oldLog.size()), oldLog.size());
  ASSERT_TRUE(file.close());

  ASSERT_TRUE(WeatherDiagnosticsLog::append("forecast_failed", "fresh failure"));

  const std::string& current = Storage.contents(WeatherDiagnosticsLog::FILE_PATH);
  const std::string& previous = Storage.contents("/weather_diagnostics.previous.log");
  EXPECT_NE(current.find("fresh failure"), std::string::npos);
  EXPECT_EQ(previous, oldLog);
}

TEST(WeatherDiagnosticsLog, DoesNotReportSuccessWhenSdIsUnavailable) {
  Storage.clear();
  Storage.setReady(false);

  EXPECT_FALSE(WeatherDiagnosticsLog::append("forecast_start", "host=api.open-meteo.com"));
  EXPECT_FALSE(Storage.exists(WeatherDiagnosticsLog::FILE_PATH));
}
