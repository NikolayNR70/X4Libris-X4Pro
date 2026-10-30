#include "WeatherClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include "WeatherDiagnosticsLog.h"

#include <cstdarg>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <strings.h>

#include "../../src/network/HttpDownloader.h"

namespace {
constexpr char WEATHER_CACHE_FILE[] = "/.crosspoint/weather_cache.json";
constexpr uint32_t WEATHER_REQUEST_TIMEOUT_MS = 12000;
constexpr uint32_t WEATHER_FORECAST_BUDGET_MS = 30000;
std::string urlEncode(const std::string& value);

void recordWeatherDiagnostic(const char* event, const char* format, ...) {
  char details[512];
  va_list args;
  va_start(args, format);
  vsnprintf(details, sizeof(details), format, args);
  va_end(args);
  if (!WeatherDiagnosticsLog::append(event, details)) {
    LOG_ERR("WEA", "Could not append %s to %s", event ? event : "event", WeatherDiagnosticsLog::FILE_PATH);
  }
}

std::string buildForecastUrl(const WeatherSettingsStore& settings) {
  char latitude[24];
  char longitude[24];
  snprintf(latitude, sizeof(latitude), "%.6f", settings.getLatitude());
  snprintf(longitude, sizeof(longitude), "%.6f", settings.getLongitude());
  return std::string("https://wttr.in/") + latitude + "," + longitude + "?format=j1";
}

std::string buildMetNoUrl(const WeatherSettingsStore& settings) {
  char latitude[24];
  char longitude[24];
  snprintf(latitude, sizeof(latitude), "%.6f", settings.getLatitude());
  snprintf(longitude, sizeof(longitude), "%.6f", settings.getLongitude());
  return std::string("https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=") + latitude + "&lon=" + longitude;
}

std::string buildOpenWeatherUrl(const WeatherSettingsStore& settings) {
  char latitude[24];
  char longitude[24];
  snprintf(latitude, sizeof(latitude), "%.6f", settings.getLatitude());
  snprintf(longitude, sizeof(longitude), "%.6f", settings.getLongitude());
  return std::string("https://api.openweathermap.org/data/2.5/forecast?lat=") + latitude + "&lon=" + longitude +
         "&appid=" + urlEncode(settings.getOpenWeatherApiKey()) + "&units=metric&lang=ru";
}

std::string buildOpenMeteoUrl(const WeatherSettingsStore& settings) {
  std::string url = "https://api.open-meteo.com/v1/forecast?";
  url += "latitude=" + std::to_string(settings.getLatitude());
  url += "&longitude=" + std::to_string(settings.getLongitude());
  url +=
      "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
      "weather_code,wind_speed_10m,wind_direction_10m,is_day,precipitation,uv_index";
  url += "&hourly=temperature_2m,precipitation,precipitation_probability,weather_code,is_day";
  url +=
      "&daily=temperature_2m_max,temperature_2m_min,weather_code,"
      "precipitation_sum,sunrise,sunset,uv_index_max";
  url += "&timezone=auto&timeformat=unixtime";
  url += std::string("&temperature_unit=") + settings.getTempUnitParam();
  url += std::string("&wind_speed_unit=") + settings.getWindUnitParam();
  url += std::string("&precipitation_unit=") + settings.getPrecipUnitParam();
  url += "&forecast_days=" + std::to_string(settings.getForecastDays());
  // Request 48 hours of hourly data
  url += "&forecast_hours=48";
  return url;
}

float numberFromJson(JsonVariantConst value, float fallback = 0.0f) {
  if (value.is<const char*>()) {
    const char* text = value.as<const char*>();
    if (text == nullptr || *text == '\0') return fallback;
    char* end = nullptr;
    const float parsed = strtof(text, &end);
    return end == text ? fallback : parsed;
  }
  return value.is<float>() || value.is<int>() || value.is<long>() ? value.as<float>() : fallback;
}

int intFromJson(JsonVariantConst value, int fallback = 0) {
  return static_cast<int>(numberFromJson(value, static_cast<float>(fallback)));
}

time_t civilDateEpoch(const char* date) {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  if (date == nullptr || sscanf(date, "%d-%u-%u", &year, &month, &day) != 3 || month < 1 || month > 12 || day < 1 ||
      day > 31) return 0;
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return static_cast<time_t>((era * 146097 + static_cast<int>(dayOfEra) - 719468) * 86400LL);
}

time_t isoUtcEpoch(const char* value) {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (value == nullptr || sscanf(value, "%d-%u-%uT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6 ||
      hour > 23 || minute > 59 || second > 60) return 0;
  char date[16];
  snprintf(date, sizeof(date), "%04d-%02u-%02u", year, month, day);
  const time_t midnight = civilDateEpoch(date);
  return midnight ? midnight + hour * 3600 + minute * 60 + second : 0;
}

int metSymbolToWmo(const char* symbol) {
  if (!symbol) return 3;
  const std::string code(symbol);
  if (code.find("thunder") != std::string::npos) return code.find("heavy") != std::string::npos ? 99 : 95;
  if (code.find("snow") != std::string::npos) return code.find("heavy") != std::string::npos ? 75 : 71;
  if (code.find("sleet") != std::string::npos) return 66;
  if (code.find("rain") != std::string::npos) {
    if (code.find("showers") != std::string::npos) return code.find("heavy") != std::string::npos ? 82 : 80;
    return code.find("heavy") != std::string::npos ? 65 : (code.find("light") != std::string::npos ? 61 : 63);
  }
  if (code.find("fog") != std::string::npos) return 45;
  if (code.find("partlycloudy") != std::string::npos) return 2;
  if (code.find("fair") != std::string::npos) return 1;
  if (code.find("clearsky") != std::string::npos) return 0;
  return 3;
}

int localUtcOffset(time_t timestamp) {
  struct tm local = {};
  struct tm utc = {};
  localtime_r(&timestamp, &local);
  gmtime_r(&timestamp, &utc);
  return static_cast<int>(difftime(mktime(&local), mktime(&utc)));
}

int clockSeconds(const char* value) {
  if (value == nullptr || *value == '\0' || strcasecmp(value, "no sunrise") == 0 ||
      strcasecmp(value, "no sunset") == 0 || strcasecmp(value, "no moonrise") == 0 ||
      strcasecmp(value, "no moonset") == 0) return -1;
  int hour = 0;
  int minute = 0;
  char meridiem[3] = {};
  if (sscanf(value, "%d:%d %2s", &hour, &minute, meridiem) < 2 || hour < 0 || hour > 12 || minute < 0 || minute > 59)
    return -1;
  if (strcasecmp(meridiem, "PM") == 0 && hour != 12) hour += 12;
  if (strcasecmp(meridiem, "AM") == 0 && hour == 12) hour = 0;
  return hour * 3600 + minute * 60;
}

int wwoToWmo(int code) {
  switch (code) {
    case 113: return 0;
    case 116: return 2;
    case 119: case 122: return 3;
    case 143: case 248: return 45;
    case 260: return 48;
    case 263: case 266: return 51;
    case 281: return 56;
    case 284: return 57;
    case 176: case 293: case 296: return 61;
    case 299: case 302: return 63;
    case 305: case 308: return 65;
    case 185: case 311: case 317: case 362: return 66;
    case 314: case 320: case 365: case 374: case 377: return 67;
    case 179: case 227: case 230: case 323: case 326: case 368: return 71;
    case 329: case 332: return 73;
    case 335: case 338: case 371: return 75;
    case 350: return 77;
    case 353: return 80;
    case 356: return 81;
    case 359: return 82;
    case 386: case 389: return 95;
    case 392: return 96;
    case 395: return 99;
    default: return 3;
  }
}

int openWeatherToWmo(int code) {
  if (code >= 200 && code < 300) return code >= 202 || code >= 232 ? 99 : 95;
  if (code >= 300 && code < 400) return 51;
  if (code == 511 || (code >= 611 && code <= 616)) return 66;
  if (code >= 500 && code < 600) {
    if (code >= 502 || code == 504) return 65;
    if (code == 520) return 80;
    if (code == 521) return 81;
    if (code >= 522) return 82;
    return code == 500 ? 61 : 63;
  }
  if (code >= 600 && code < 700) {
    if (code >= 622) return 75;
    if (code >= 621) return 73;
    return code >= 602 ? 75 : 71;
  }
  if (code >= 700 && code < 800) return code == 701 || code == 741 ? 45 : 3;
  if (code == 800) return 0;
  if (code == 801) return 1;
  if (code == 802) return 2;
  return code >= 803 && code <= 804 ? 3 : 3;
}

float temperatureFromJson(JsonObjectConst object, const char* celsiusKey, const char* fahrenheitKey,
                          bool fahrenheit) {
  return numberFromJson(object[fahrenheit ? fahrenheitKey : celsiusKey]);
}

float windFromKmh(float kmh, WeatherWindUnit unit) {
  switch (unit) {
    case WeatherWindUnit::MS: return kmh / 3.6f;
    case WeatherWindUnit::MPH: return kmh * 0.621371f;
    case WeatherWindUnit::KNOTS: return kmh * 0.539957f;
    case WeatherWindUnit::KMH: default: return kmh;
  }
}

float windFromMs(float metersPerSecond, WeatherWindUnit unit) {
  switch (unit) {
    case WeatherWindUnit::KMH: return metersPerSecond * 3.6f;
    case WeatherWindUnit::MPH: return metersPerSecond * 2.2369363f;
    case WeatherWindUnit::KNOTS: return metersPerSecond * 1.9438445f;
    case WeatherWindUnit::MS: default: return metersPerSecond;
  }
}

float celsiusToConfigured(float value, const WeatherSettingsStore& settings) {
  return settings.getTempUnit() == WeatherTempUnit::FAHRENHEIT ? value * 9.0f / 5.0f + 32.0f : value;
}

float precipitationFromMm(float mm, WeatherPrecipUnit unit) {
  return unit == WeatherPrecipUnit::INCH ? mm / 25.4f : mm;
}

std::string urlEncode(const std::string& value) {
  std::string out;
  out.reserve(value.size() * 3);
  for (unsigned char c : value) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out.push_back(c);
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out.append(buf);
    }
  }
  return out;
}

bool parseOpenWeatherForecast(JsonVariantConst root, WeatherData& data, const WeatherSettingsStore& settings) {
  const JsonArrayConst sourcePoints = root["list"].as<JsonArrayConst>();
  const JsonObjectConst city = root["city"].as<JsonObjectConst>();
  if (sourcePoints.isNull() || sourcePoints.size() == 0 || city.isNull()) {
    data.errorMessage = "OpenWeather response has no forecast";
    return false;
  }

  const int offset = intFromJson(city["timezone"]);
  data.utcOffsetSeconds = offset;
  data.timezone = city["name"] | std::string("OpenWeather local");
  time_t now = 0;
  time(&now);

  struct Point {
    time_t time;
    float temperature;
    float tempMin;
    float tempMax;
    float precipMm;
    int probability;
    int weatherCode;
    bool isDay;
    size_t sourceIndex;
  };
  std::vector<Point> points;
  points.reserve(sourcePoints.size());
  size_t currentIndex = 0;
  time_t closestDistance = std::numeric_limits<time_t>::max();
  for (const JsonObjectConst item : sourcePoints) {
    const time_t timestamp = static_cast<time_t>(intFromJson(item["dt"]));
    const JsonObjectConst main = item["main"].as<JsonObjectConst>();
    const JsonObjectConst wind = item["wind"].as<JsonObjectConst>();
    const JsonObjectConst weather = item["weather"][0].as<JsonObjectConst>();
    if (timestamp <= 0 || main.isNull() || weather.isNull()) continue;
    const JsonObjectConst rain = item["rain"].as<JsonObjectConst>();
    const JsonObjectConst snow = item["snow"].as<JsonObjectConst>();
    const JsonObjectConst sys = item["sys"].as<JsonObjectConst>();
    const float precipitation = numberFromJson(rain["3h"]) + numberFromJson(snow["3h"]);
    const char* part = sys["pod"] | "d";
    points.push_back({timestamp, numberFromJson(main["temp"]), numberFromJson(main["temp_min"]),
                      numberFromJson(main["temp_max"]), precipitation,
                      static_cast<int>(lroundf(numberFromJson(item["pop"]) * 100.0f)),
                      openWeatherToWmo(intFromJson(weather["id"])), part[0] != 'n', points.size()});
    const time_t distance = now > 0 ? static_cast<time_t>(llabs(static_cast<long long>(timestamp - now))) : timestamp;
    if (distance < closestDistance) {
      closestDistance = distance;
      currentIndex = points.size() - 1;
    }
  }
  if (points.empty()) {
    data.errorMessage = "OpenWeather response has no valid forecast points";
    return false;
  }

  const Point& current = points[currentIndex];
  const JsonObjectConst currentSource = sourcePoints[current.sourceIndex].as<JsonObjectConst>();
  const JsonObjectConst currentMain = currentSource["main"].as<JsonObjectConst>();
  const JsonObjectConst currentWind = currentSource["wind"].as<JsonObjectConst>();
  data.current.temperature = celsiusToConfigured(current.temperature, settings);
  data.current.apparentTemperature = celsiusToConfigured(numberFromJson(currentMain["feels_like"], current.temperature), settings);
  data.current.humidity = intFromJson(currentMain["humidity"]);
  data.current.weatherCode = current.weatherCode;
  data.current.windSpeed = windFromMs(numberFromJson(currentWind["speed"]), settings.getWindUnit());
  data.current.windDirection = intFromJson(currentWind["deg"]);
  data.current.precipitation = precipitationFromMm(current.precipMm / 3.0f, settings.getPrecipUnit());
  data.current.uvIndex = 0;
  data.current.isDay = current.isDay;

  const time_t firstLocalDay = ((points.front().time + offset) / 86400) * 86400;
  const time_t todayLocalDay = now > 0 ? ((now + offset) / 86400) * 86400 : firstLocalDay;
  std::vector<int> dailyCodeDistance;
  for (const Point& point : points) {
    const time_t localTime = point.time + offset;
    const time_t localDay = (localTime / 86400) * 86400;
    if (localDay < firstLocalDay || (localDay - firstLocalDay) / 86400 >= settings.getForecastDays()) continue;
    const time_t date = localDay - offset;
    auto found = std::find_if(data.daily.begin(), data.daily.end(), [date](const DailyForecast& d) { return d.date == date; });
    if (found == data.daily.end()) {
      DailyForecast day;
      day.date = date;
      day.tempMin = point.tempMin;
      day.tempMax = point.tempMax;
      day.weatherCode = point.weatherCode;
      data.daily.push_back(day);
      dailyCodeDistance.push_back(INT32_MAX);
      found = data.daily.end() - 1;
    }
    found->tempMin = std::min(found->tempMin, point.tempMin);
    found->tempMax = std::max(found->tempMax, point.tempMax);
    found->precipSum += precipitationFromMm(point.precipMm, settings.getPrecipUnit());
    const int localHour = static_cast<int>((localTime - localDay) / 3600);
    const size_t dayIndex = static_cast<size_t>(found - data.daily.begin());
    if (abs(localHour - 12) < dailyCodeDistance[dayIndex]) {
      found->weatherCode = point.weatherCode;
      dailyCodeDistance[dayIndex] = abs(localHour - 12);
    }
    if (localDay == todayLocalDay) {
      found->sunrise = intFromJson(city["sunrise"]);
      found->sunset = intFromJson(city["sunset"]);
    }
  }
  if (data.daily.empty()) {
    data.errorMessage = "OpenWeather response has no daily forecast";
    return false;
  }

  const time_t firstHour = ((now > 0 ? now : current.time) + 3599) / 3600 * 3600;
  data.hourly.reserve(49);
  size_t right = 0;
  for (int hourOffset = 0; hourOffset <= 48; ++hourOffset) {
    const time_t target = firstHour + hourOffset * 3600;
    while (right < points.size() && points[right].time < target) ++right;
    const size_t before = right == 0 ? 0 : right - 1;
    const size_t after = right >= points.size() ? points.size() - 1 : right;
    const Point& a = points[before];
    const Point& b = points[after];
    const float fraction = b.time == a.time ? 0.0f :
        std::clamp(static_cast<float>(target - a.time) / static_cast<float>(b.time - a.time), 0.0f, 1.0f);
    const Point& nearest = fraction < 0.5f ? a : b;
    HourlyForecast hour;
    hour.time = target;
    hour.temperature = celsiusToConfigured(a.temperature + (b.temperature - a.temperature) * fraction, settings);
    hour.precipitation = precipitationFromMm((a.precipMm + (b.precipMm - a.precipMm) * fraction) / 3.0f,
                                             settings.getPrecipUnit());
    hour.precipitationProbability = static_cast<int>(a.probability + (b.probability - a.probability) * fraction);
    hour.weatherCode = nearest.weatherCode;
    hour.isDay = nearest.isDay;
    data.hourly.push_back(hour);
  }
  return true;
}

std::string buildRequestSignature(const WeatherSettingsStore& settings) {
  char latitude[24];
  char longitude[24];
  snprintf(latitude, sizeof(latitude), "%.6f", settings.getLatitude());
  snprintf(longitude, sizeof(longitude), "%.6f", settings.getLongitude());

  std::string signature = "lat=";
  signature += latitude;
  signature += "|lon=";
  signature += longitude;
  signature += "|temp=";
  signature += settings.getTempUnitParam();
  signature += "|wind=";
  signature += settings.getWindUnitParam();
  signature += "|precip=";
  signature += settings.getPrecipUnitParam();
  signature += "|days=";
  signature += std::to_string(settings.getForecastDays());
  const std::string& apiKey = settings.getOpenWeatherApiKey();
  signature += apiKey.empty() ? "|provider=met" : "|provider=openweather|key=";
  if (!apiKey.empty()) {
    uint32_t fingerprint = 2166136261u;
    for (const unsigned char ch : apiKey) fingerprint = (fingerprint ^ ch) * 16777619u;
    signature += std::to_string(fingerprint);
  }
  return signature;
}
}  // namespace

std::string WeatherClient::buildRequestSignature(const WeatherSettingsStore& settings) {
  return ::buildRequestSignature(settings);
}

WeatherData WeatherClient::getWeather(const WeatherSettingsStore& settings, bool forceRefresh) {
  if (!settings.hasLocation()) {
    WeatherData data;
    data.errorMessage = "No location configured";
    return data;
  }

  const std::string requestSignature = buildRequestSignature(settings);

  if (!forceRefresh) {
    WeatherData cached;
    if (loadCache(cached) && cached.valid) {
      if (cached.requestSignature != requestSignature) {
        LOG_DBG("WEA", "Ignoring cache with mismatched request signature");
        Storage.remove(WEATHER_CACHE_FILE);
      } else {
        time_t now;
        time(&now);
        if (now - cached.fetchedAt < CACHE_TTL_SECONDS) {
          LOG_DBG("WEA", "Using cached weather data (age: %ld s)", (long)(now - cached.fetchedAt));
          return cached;
        }
        LOG_DBG("WEA", "Cache expired (age: %ld s)", (long)(now - cached.fetchedAt));
        // Cache-first behavior: return stale cache and let the caller decide
        // whether/when to perform a network refresh.
        return cached;
      }
    }

    LOG_DBG("WEA", "No cache available; caller should establish network and force refresh");
    WeatherData data;
    data.errorMessage = "No cache";
    return data;
  }

  return fetchFromApi(settings);
}

WeatherData WeatherClient::fetchFromApi(const WeatherSettingsStore& settings) {
  WeatherData data;
  data.requestSignature = buildRequestSignature(settings);
  std::string url = buildMetNoUrl(settings);
  recordWeatherDiagnostic("forecast_start", "primary=%s fallback=api.met.no,api.open-meteo.com,wttr.in location_configured=1",
                          settings.getOpenWeatherApiKey().empty() ? "api.met.no (OpenWeather key not set)" : "api.openweathermap.org");
  LOG_DBG("WEA", "fetchFromApi[1] start");
  LOG_DBG("WEA", "fetchFromApi[2] url length=%zu", url.size());
  LOG_DBG("WEA", "Fetching weather from API");

  std::string response;
  std::string networkError;
  WeatherData parsed;
  parsed.requestSignature = data.requestSignature;
  const uint32_t forecastStartedAtMs = millis();
  const auto tryProvider = [&](const char* provider, const std::string& requestUrl) {
    const uint32_t elapsedMs = millis() - forecastStartedAtMs;
    if (elapsedMs >= WEATHER_FORECAST_BUDGET_MS) {
      recordWeatherDiagnostic("forecast_failed", "provider=%s stage=deadline elapsed_ms=%lu", provider,
                              static_cast<unsigned long>(elapsedMs));
      return false;
    }
    const uint32_t timeoutMs = std::min<uint32_t>(WEATHER_REQUEST_TIMEOUT_MS, WEATHER_FORECAST_BUDGET_MS - elapsedMs);
    const uint32_t providerStartedAtMs = millis();
    response.clear();
    networkError.clear();
    LOG_DBG("WEA", "Fetching forecast from %s", provider);
    const bool isWttr = std::string(provider) == "wttr.in";
    bool jsonComplete = false;
    bool inString = false;
    bool escaped = false;
    int objectDepth = 0;
    const auto collectJson = [&](const uint8_t* bytes, size_t length) {
      for (size_t i = 0; i < length; ++i) {
        const char ch = static_cast<char>(bytes[i]);
        response.push_back(ch);
        if (inString) {
          if (escaped) escaped = false;
          else if (ch == '\\') escaped = true;
          else if (ch == '"') inString = false;
          continue;
        }
        if (ch == '"') inString = true;
        else if (ch == '{') ++objectDepth;
        else if (ch == '}' && objectDepth > 0 && --objectDepth == 0) {
          jsonComplete = true;
          return false;
        }
      }
      return true;
    };
    const bool httpOk = isWttr
                            ? HttpDownloader::fetchUrlWithPolicy(requestUrl, collectJson, true,
                                                                 HttpDownloader::TlsPolicy::Unverified, &networkError,
                                                                 timeoutMs)
                            : HttpDownloader::fetchUrl(requestUrl, response, "", "",
                                                       HttpDownloader::TlsPolicy::Unverified, &networkError,
                                                       timeoutMs);
    if (!httpOk || (isWttr && !jsonComplete)) {
      recordWeatherDiagnostic("forecast_failed", "provider=%s stage=http elapsed_ms=%lu error=%s response_bytes=%zu",
                              provider, static_cast<unsigned long>(millis() - providerStartedAtMs),
                              networkError.c_str(), response.size());
      return false;
    }
    parsed = WeatherData{};
    parsed.requestSignature = data.requestSignature;
    if (!parseWeatherJson(response, parsed, settings)) {
      recordWeatherDiagnostic("forecast_failed", "provider=%s stage=parse elapsed_ms=%lu error=%s response_bytes=%zu",
                              provider, static_cast<unsigned long>(millis() - providerStartedAtMs),
                              parsed.errorMessage.c_str(), response.size());
      return false;
    }
    recordWeatherDiagnostic("forecast_response", "provider=%s status=200 elapsed_ms=%lu response_bytes=%zu", provider,
                            static_cast<unsigned long>(millis() - providerStartedAtMs), response.size());
    return true;
  };

  bool fetched = false;
  const char* provider = "api.openweathermap.org";
  if (!settings.getOpenWeatherApiKey().empty()) {
    fetched = tryProvider(provider, buildOpenWeatherUrl(settings));
  } else {
    recordWeatherDiagnostic("forecast_provider_skipped", "provider=api.openweathermap.org reason=api_key_not_configured");
  }
  if (!fetched) {
    provider = "api.met.no";
    fetched = tryProvider(provider, url);
  }
  if (!fetched) {
    provider = "api.open-meteo.com";
    fetched = tryProvider(provider, buildOpenMeteoUrl(settings));
  }
  if (!fetched) {
    provider = "wttr.in";
    fetched = tryProvider(provider, buildForecastUrl(settings));
  }
  if (!fetched) {
    data.errorMessage = "Network error";
    LOG_ERR("WEA", "Weather providers failed: api.openweathermap.org, api.met.no, api.open-meteo.com and wttr.in");
    return data;
  }

  data = std::move(parsed);
  data.requestSignature = buildRequestSignature(settings);
  data.valid = true;
  time(&data.fetchedAt);
  saveCache(data);
  recordWeatherDiagnostic("forecast_ok", "provider=%s fetched_at=%lld daily=%zu hourly=%zu",
                          provider,
                          static_cast<long long>(data.fetchedAt), data.daily.size(), data.hourly.size());
  LOG_DBG("WEA", "fetchFromApi[7] Weather data fetched and cached");
  return data;
}

bool WeatherClient::parseWeatherJson(const std::string& json, WeatherData& data,
                                     const WeatherSettingsStore& settings) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    data.errorMessage = "JSON parse error";
    LOG_ERR("WEA", "JSON parse error: %s", error.c_str());
    return false;
  }

  // Check for API error
  if (doc["error"] | false) {
    data.errorMessage = doc["reason"] | "API error";
    LOG_ERR("WEA", "API error: %s", data.errorMessage.c_str());
    return false;
  }

  // OpenWeatherMap reports API failures as {"cod":"401","message":"..."}.
  // Do not echo the response body: request URLs contain the private API key.
  if (doc["cod"].is<const char*>() || doc["cod"].is<int>()) {
    const int status = intFromJson(doc["cod"]);
    if (status != 0 && status != 200) {
      data.errorMessage = status == 401 ? "OpenWeather API key rejected" : "OpenWeather request failed";
      LOG_ERR("WEA", "%s (status=%d)", data.errorMessage.c_str(), status);
      return false;
    }
  }

  if (doc["list"].is<JsonArrayConst>()) {
    return parseOpenWeatherForecast(doc.as<JsonVariantConst>(), data, settings);
  }

  // MET Norway's compact Locationforecast response has an explicit Content-Length,
  // so the complete JSON can be read reliably on the ESP32 Wi-Fi transport.
  JsonArrayConst metSeries = doc["properties"]["timeseries"].as<JsonArrayConst>();
  if (!metSeries.isNull()) {
    if (metSeries.size() == 0) {
      data.errorMessage = "Weather response is empty";
      return false;
    }
    time_t now = 0;
    time(&now);
    size_t currentIndex = 0;
    time_t closestPast = 0;
    data.utcOffsetSeconds = now > 0 ? localUtcOffset(now) : 0;
    data.timezone = "device-local";
    struct MetPoint { time_t time; float temp; float precip; int code; int humidity; float wind; int windDir; size_t sourceIndex; };
    std::vector<MetPoint> points;
    points.reserve(metSeries.size());
    for (size_t i = 0; i < metSeries.size(); ++i) {
      const JsonObjectConst item = metSeries[i].as<JsonObjectConst>();
      const time_t timestamp = isoUtcEpoch(item["time"] | "");
      const JsonObjectConst instant = item["data"]["instant"]["details"].as<JsonObjectConst>();
      if (timestamp == 0 || instant.isNull()) continue;
      const JsonObjectConst nextHour = item["data"]["next_1_hours"].as<JsonObjectConst>();
      const JsonObjectConst nextSix = item["data"]["next_6_hours"].as<JsonObjectConst>();
      const JsonObjectConst nextTwelve = item["data"]["next_12_hours"].as<JsonObjectConst>();
      const char* symbol = nextHour["summary"]["symbol_code"] |
                           nextSix["summary"]["symbol_code"] |
                           nextTwelve["summary"]["symbol_code"] | "";
      const float temp = numberFromJson(instant["air_temperature"]);
      const int code = metSymbolToWmo(symbol);
      const JsonVariantConst hourlyPrecip = nextHour["details"]["precipitation_amount"];
      const float precip = hourlyPrecip.isNull()
                               ? numberFromJson(nextSix["details"]["precipitation_amount"])
                               : numberFromJson(hourlyPrecip);
      const int humidity = intFromJson(instant["relative_humidity"]);
      const float wind = numberFromJson(instant["wind_speed"]);
      const int windDir = static_cast<int>(lroundf(numberFromJson(instant["wind_from_direction"])));
      points.push_back({timestamp, temp, precip, code, humidity, wind, windDir, i});
      if (now > 0 && timestamp <= now && timestamp >= closestPast) {
        closestPast = timestamp;
        currentIndex = points.size() - 1;
      }
    }
    if (points.empty()) {
      data.errorMessage = "Weather response has no forecast";
      return false;
    }
    const MetPoint& current = points[currentIndex];
    data.current.temperature = current.temp;
    data.current.apparentTemperature = current.temp;
    data.current.humidity = current.humidity;
    data.current.weatherCode = current.code;
    data.current.windSpeed = windFromKmh(current.wind * 3.6f, settings.getWindUnit());
    data.current.windDirection = current.windDir;
    data.current.precipitation = precipitationFromMm(current.precip, settings.getPrecipUnit());
    data.current.uvIndex = 0;
    const JsonObjectConst currentItem = metSeries[current.sourceIndex].as<JsonObjectConst>();
    const char* currentSymbol = currentItem["data"]["next_1_hours"]["summary"]["symbol_code"] |
                               currentItem["data"]["next_6_hours"]["summary"]["symbol_code"] | "";
    data.current.isDay = strstr(currentSymbol, "_night") == nullptr;

    const int requestedDays = std::max(1, static_cast<int>(settings.getForecastDays()));
    const time_t firstLocal = points.front().time + data.utcOffsetSeconds;
    const time_t firstDayLocal = (firstLocal / 86400) * 86400;
    data.daily.reserve(requestedDays);
    std::vector<int> dailyCodeDistance;
    dailyCodeDistance.reserve(requestedDays);
    for (const MetPoint& point : points) {
      const time_t localTime = point.time + data.utcOffsetSeconds;
      const time_t localDate = (localTime / 86400) * 86400;
      if (localDate < firstDayLocal || (localDate - firstDayLocal) / 86400 >= requestedDays) continue;
      const time_t date = localDate - data.utcOffsetSeconds;
      auto found = std::find_if(data.daily.begin(), data.daily.end(), [date](const DailyForecast& day) {
        return day.date == date;
      });
      if (found == data.daily.end()) {
        DailyForecast day;
        day.date = date;
        day.tempMin = day.tempMax = point.temp;
        day.weatherCode = point.code;
        data.daily.push_back(day);
        dailyCodeDistance.push_back(INT32_MAX);
        found = data.daily.end() - 1;
      }
      found->tempMin = std::min(found->tempMin, point.temp);
      found->tempMax = std::max(found->tempMax, point.temp);
      const int hourOfDay = static_cast<int>((localTime - localDate) / 3600);
      const size_t dailyIndex = static_cast<size_t>(found - data.daily.begin());
      if (abs(hourOfDay - 12) < dailyCodeDistance[dailyIndex]) {
        found->weatherCode = point.code;
        dailyCodeDistance[dailyIndex] = abs(hourOfDay - 12);
      }
      if (hourOfDay % 6 == 0) found->precipSum += precipitationFromMm(point.precip, settings.getPrecipUnit());
    }
    data.hourly.reserve(49);
    for (const MetPoint& point : points) {
      if (point.time < current.time || point.time > current.time + 48 * 3600) continue;
      HourlyForecast hour;
      hour.time = point.time;
      hour.temperature = point.temp;
      hour.precipitation = precipitationFromMm(point.precip, settings.getPrecipUnit());
      hour.precipitationProbability = 0;
      hour.weatherCode = point.code;
      const JsonObjectConst item = metSeries[point.sourceIndex].as<JsonObjectConst>();
      const char* symbol = item["data"]["next_1_hours"]["summary"]["symbol_code"] |
                           item["data"]["next_6_hours"]["summary"]["symbol_code"] | "";
      hour.isDay = strstr(symbol, "_night") == nullptr;
      data.hourly.push_back(hour);
    }
    if (data.daily.empty() || data.hourly.empty()) {
      data.errorMessage = "Weather response has no forecast";
      return false;
    }
    return true;
  }

  // wttr.in returns a compact, key-free forecast and is reachable from networks where the
  // Open-Meteo API origin is not. Its wall-clock forecast times are encoded against UTC so the
  // existing renderer can display the provider's local date and clock labels without guessing
  // the location's time zone.
  if (doc["current_condition"].is<JsonArrayConst>() && doc["weather"].is<JsonArrayConst>()) {
    JsonArrayConst currentArray = doc["current_condition"].as<JsonArrayConst>();
    JsonArrayConst days = doc["weather"].as<JsonArrayConst>();
    if (currentArray.size() == 0 || days.size() == 0) {
      data.errorMessage = "Weather response is empty";
      return false;
    }

    const bool fahrenheit = settings.getTempUnit() == WeatherTempUnit::FAHRENHEIT;
    const JsonObjectConst current = currentArray[0].as<JsonObjectConst>();
    const JsonObjectConst today = days[0].as<JsonObjectConst>();
    const JsonObjectConst astronomy = today["astronomy"][0].as<JsonObjectConst>();
    const time_t todayEpoch = civilDateEpoch(today["date"] | "");
    if (todayEpoch == 0) {
      data.errorMessage = "Weather response has no valid date";
      return false;
    }

    data.timezone = "location-local";
    data.utcOffsetSeconds = 0;
    data.current.temperature = temperatureFromJson(current, "temp_C", "temp_F", fahrenheit);
    data.current.apparentTemperature = temperatureFromJson(current, "FeelsLikeC", "FeelsLikeF", fahrenheit);
    data.current.humidity = intFromJson(current["humidity"]);
    data.current.weatherCode = wwoToWmo(intFromJson(current["weatherCode"]));
    data.current.windSpeed = windFromKmh(numberFromJson(current["windspeedKmph"]), settings.getWindUnit());
    data.current.windDirection = intFromJson(current["winddirDegree"]);
    data.current.precipitation = precipitationFromMm(numberFromJson(current["precipMM"]), settings.getPrecipUnit());
    data.current.uvIndex = numberFromJson(current["uvIndex"]);

    const int sunriseSeconds = clockSeconds(astronomy["sunrise"] | "");
    const int sunsetSeconds = clockSeconds(astronomy["sunset"] | "");
    const int observationSeconds = clockSeconds(current["observation_time"] | "");
    data.current.isDay = observationSeconds >= 0 && sunriseSeconds >= 0 && sunsetSeconds >= 0 &&
                         observationSeconds >= sunriseSeconds && observationSeconds < sunsetSeconds;

    const size_t dailyCount = std::min<size_t>(days.size(), settings.getForecastDays());
    data.daily.reserve(dailyCount);
    struct ForecastPoint {
      time_t time;
      float temperature;
      float precipitation;
      int precipitationProbability;
      int weatherCode;
      int dayIndex;
    };
    std::vector<ForecastPoint> points;
    points.reserve(days.size() * 8);

    for (size_t dayIndex = 0; dayIndex < dailyCount; ++dayIndex) {
      const JsonObjectConst sourceDay = days[dayIndex].as<JsonObjectConst>();
      const time_t dateEpoch = civilDateEpoch(sourceDay["date"] | "");
      if (dateEpoch == 0) continue;
      DailyForecast day;
      day.date = dateEpoch;
      day.tempMax = temperatureFromJson(sourceDay, "maxtempC", "maxtempF", fahrenheit);
      day.tempMin = temperatureFromJson(sourceDay, "mintempC", "mintempF", fahrenheit);
      day.uvIndexMax = numberFromJson(sourceDay["uvIndex"]);
      const JsonObjectConst dayAstronomy = sourceDay["astronomy"][0].as<JsonObjectConst>();
      const int rise = clockSeconds(dayAstronomy["sunrise"] | "");
      const int set = clockSeconds(dayAstronomy["sunset"] | "");
      day.sunrise = rise >= 0 ? dateEpoch + rise : 0;
      day.sunset = set >= 0 ? dateEpoch + set : 0;
      const char* phaseName = dayAstronomy["moon_phase"] | "";
      if (strcasecmp(phaseName, "New Moon") == 0) day.moonPhase = 0.0f;
      else if (strcasecmp(phaseName, "Waxing Crescent") == 0) day.moonPhase = 0.15f;
      else if (strcasecmp(phaseName, "First Quarter") == 0) day.moonPhase = 0.25f;
      else if (strcasecmp(phaseName, "Waxing Gibbous") == 0) day.moonPhase = 0.4f;
      else if (strcasecmp(phaseName, "Full Moon") == 0) day.moonPhase = 0.5f;
      else if (strcasecmp(phaseName, "Waning Gibbous") == 0) day.moonPhase = 0.6f;
      else if (strcasecmp(phaseName, "Last Quarter") == 0) day.moonPhase = 0.75f;
      else if (strcasecmp(phaseName, "Waning Crescent") == 0) day.moonPhase = 0.9f;

      JsonArrayConst hourly = sourceDay["hourly"].as<JsonArrayConst>();
      int bestCodeDistance = INT32_MAX;
      for (JsonObjectConst sourceHour : hourly) {
        const int hhmm = intFromJson(sourceHour["time"]);
        const int hour = hhmm / 100;
        const int minute = hhmm % 100;
        if (hour > 23 || minute > 59) continue;
        const int code = wwoToWmo(intFromJson(sourceHour["weatherCode"]));
        const int distance = abs(hour * 60 + minute - 720);
        if (distance < bestCodeDistance) {
          day.weatherCode = code;
          bestCodeDistance = distance;
        }
        day.precipSum += precipitationFromMm(numberFromJson(sourceHour["precipMM"]), settings.getPrecipUnit());
        points.push_back({dateEpoch + hour * 3600 + minute * 60,
                          temperatureFromJson(sourceHour, "tempC", "tempF", fahrenheit),
                          numberFromJson(sourceHour["precipMM"]),
                          intFromJson(sourceHour["chanceofrain"]), code, static_cast<int>(dayIndex)});
      }
      data.daily.push_back(day);
    }

    if (data.daily.empty() || points.size() < 2) {
      data.errorMessage = "Weather response has no forecast";
      return false;
    }

    const int startClock = observationSeconds >= 0 ? observationSeconds : 0;
    const time_t startTime = todayEpoch + ((startClock + 1800) / 3600) * 3600;
    data.hourly.reserve(49);
    size_t right = 0;
    for (int hourOffset = 0; hourOffset <= 48; ++hourOffset) {
      const time_t target = startTime + hourOffset * 3600;
      while (right < points.size() && points[right].time < target) ++right;
      const size_t before = right == 0 ? 0 : right - 1;
      const size_t after = right >= points.size() ? points.size() - 1 : right;
      const ForecastPoint& a = points[before];
      const ForecastPoint& b = points[after];
      const float fraction = b.time == a.time ? 0.0f :
          std::clamp(static_cast<float>(target - a.time) / static_cast<float>(b.time - a.time), 0.0f, 1.0f);
      const ForecastPoint& nearest = fraction < 0.5f ? a : b;
      HourlyForecast hour;
      hour.time = target;
      hour.temperature = a.temperature + (b.temperature - a.temperature) * fraction;
      hour.precipitation = precipitationFromMm((a.precipitation + (b.precipitation - a.precipitation) * fraction) / 3.0f,
                                               settings.getPrecipUnit());
      hour.precipitationProbability = static_cast<int>(a.precipitationProbability +
          (b.precipitationProbability - a.precipitationProbability) * fraction);
      hour.weatherCode = nearest.weatherCode;
      const size_t dayIndex = static_cast<size_t>(std::max(0, nearest.dayIndex));
      const time_t localSeconds = target - data.daily[std::min(dayIndex, data.daily.size() - 1)].date;
      const DailyForecast& forecastDay = data.daily[std::min(dayIndex, data.daily.size() - 1)];
      hour.isDay = forecastDay.sunrise > 0 && forecastDay.sunset > 0 && localSeconds >= forecastDay.sunrise - forecastDay.date &&
                   localSeconds < forecastDay.sunset - forecastDay.date;
      data.hourly.push_back(hour);
    }
    data.current.isDay = observationSeconds >= sunriseSeconds && sunriseSeconds >= 0 && sunsetSeconds >= 0 &&
                         observationSeconds < sunsetSeconds;
    return true;
  }

  data.timezone = doc["timezone"] | std::string("");
  data.utcOffsetSeconds = doc["utc_offset_seconds"] | 0;

  // Parse current weather
  JsonObject current = doc["current"];
  if (current) {
    data.current.temperature = current["temperature_2m"] | 0.0f;
    data.current.apparentTemperature = current["apparent_temperature"] | 0.0f;
    data.current.humidity = current["relative_humidity_2m"] | 0;
    data.current.weatherCode = current["weather_code"] | 0;
    data.current.windSpeed = current["wind_speed_10m"] | 0.0f;
    data.current.windDirection = current["wind_direction_10m"] | 0;
    data.current.precipitation = current["precipitation"] | 0.0f;
    data.current.uvIndex = current["uv_index"] | 0.0f;
    data.current.isDay = (current["is_day"] | 1) != 0;
  }

  // Parse daily forecast
  JsonObject daily = doc["daily"];
  if (daily) {
    JsonArray times = daily["time"];
    JsonArray tempMax = daily["temperature_2m_max"];
    JsonArray tempMin = daily["temperature_2m_min"];
    JsonArray codes = daily["weather_code"];
    JsonArray precip = daily["precipitation_sum"];
    JsonArray sunrise = daily["sunrise"];
    JsonArray sunset = daily["sunset"];
    JsonArray uvMax = daily["uv_index_max"];

    size_t count = times.size();
    data.daily.reserve(count);
    for (size_t i = 0; i < count; i++) {
      DailyForecast day;
      day.date = times[i] | (time_t)0;
      day.tempMax = tempMax[i] | 0.0f;
      day.tempMin = tempMin[i] | 0.0f;
      day.weatherCode = codes[i] | 0;
      day.precipSum = precip[i] | 0.0f;
      day.sunrise = sunrise[i] | (time_t)0;
      day.sunset = sunset[i] | (time_t)0;
      day.uvIndexMax = uvMax[i] | 0.0f;
      day.moonPhase = daily["moon_phase"] ? (daily["moon_phase"][i] | -1.0f) : -1.0f;
      data.daily.push_back(day);
    }
  }

  // Parse hourly forecast
  JsonObject hourly = doc["hourly"];
  if (hourly) {
    JsonArray times = hourly["time"];
    JsonArray temp = hourly["temperature_2m"];
    JsonArray precip = hourly["precipitation"];
    JsonArray precipProb = hourly["precipitation_probability"];
    JsonArray codes = hourly["weather_code"];
    JsonArray isDay = hourly["is_day"];

    size_t count = times.size();
    data.hourly.reserve(count);
    for (size_t i = 0; i < count; i++) {
      HourlyForecast hour;
      hour.time = times[i] | (time_t)0;
      hour.temperature = temp[i] | 0.0f;
      hour.precipitation = precip[i] | 0.0f;
      hour.precipitationProbability = precipProb[i] | 0;
      hour.weatherCode = codes[i] | 0;
      hour.isDay = (isDay[i] | 1) != 0;
      data.hourly.push_back(hour);
    }
  }

  return true;
}

bool WeatherClient::saveCache(const WeatherData& data) {
  Storage.mkdir("/.crosspoint");

  JsonDocument doc;
  doc["requestSignature"] = data.requestSignature;
  doc["fetchedAt"] = data.fetchedAt;
  doc["timezone"] = data.timezone;
  doc["utcOffsetSeconds"] = data.utcOffsetSeconds;

  // Current
  JsonObject cur = doc["current"].to<JsonObject>();
  cur["temperature"] = data.current.temperature;
  cur["apparentTemperature"] = data.current.apparentTemperature;
  cur["humidity"] = data.current.humidity;
  cur["weatherCode"] = data.current.weatherCode;
  cur["windSpeed"] = data.current.windSpeed;
  cur["windDirection"] = data.current.windDirection;
  cur["precipitation"] = data.current.precipitation;
  cur["uvIndex"] = data.current.uvIndex;
  cur["isDay"] = data.current.isDay;

  // Daily
  JsonArray dailyArr = doc["daily"].to<JsonArray>();
  for (const auto& day : data.daily) {
    JsonObject d = dailyArr.add<JsonObject>();
    d["date"] = day.date;
    d["tempMax"] = day.tempMax;
    d["tempMin"] = day.tempMin;
    d["weatherCode"] = day.weatherCode;
    d["precipSum"] = day.precipSum;
    d["uvIndexMax"] = day.uvIndexMax;
    d["sunrise"] = day.sunrise;
    d["sunset"] = day.sunset;
    d["moonPhase"] = day.moonPhase;
    d["moonPhaseApiName"] = "moon_phase";  // for compatibility/tracing
  }

  // Hourly
  JsonArray hourlyArr = doc["hourly"].to<JsonArray>();
  for (const auto& hour : data.hourly) {
    JsonObject h = hourlyArr.add<JsonObject>();
    h["time"] = hour.time;
    h["temperature"] = hour.temperature;
    h["precipitation"] = hour.precipitation;
    h["precipProb"] = hour.precipitationProbability;
    h["weatherCode"] = hour.weatherCode;
    h["isDay"] = hour.isDay;
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(WEATHER_CACHE_FILE, json);
}

bool WeatherClient::loadCache(WeatherData& data) {
  if (!Storage.exists(WEATHER_CACHE_FILE)) {
    return false;
  }

  String json = Storage.readFile(WEATHER_CACHE_FILE);
  if (json.isEmpty()) {
    return false;
  }

  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("WEA", "Cache parse error: %s", error.c_str());
    return false;
  }

  data.fetchedAt = doc["fetchedAt"] | (time_t)0;
  data.requestSignature = doc["requestSignature"] | std::string("");
  data.timezone = doc["timezone"] | std::string("");
  data.utcOffsetSeconds = doc["utcOffsetSeconds"] | 0;

  // Current
  JsonObject cur = doc["current"];
  if (cur) {
    data.current.temperature = cur["temperature"] | 0.0f;
    data.current.apparentTemperature = cur["apparentTemperature"] | 0.0f;
    data.current.humidity = cur["humidity"] | 0;
    data.current.weatherCode = cur["weatherCode"] | 0;
    data.current.windSpeed = cur["windSpeed"] | 0.0f;
    data.current.windDirection = cur["windDirection"] | 0;
    data.current.precipitation = cur["precipitation"] | 0.0f;
    data.current.uvIndex = cur["uvIndex"] | 0.0f;
    data.current.isDay = cur["isDay"] | true;
  }

  // Daily
  JsonArray dailyArr = doc["daily"].as<JsonArray>();
  for (JsonObject d : dailyArr) {
    DailyForecast day;
    day.date = d["date"] | (time_t)0;
    day.tempMax = d["tempMax"] | 0.0f;
    day.tempMin = d["tempMin"] | 0.0f;
    day.weatherCode = d["weatherCode"] | 0;
    day.precipSum = d["precipSum"] | 0.0f;
    day.uvIndexMax = d["uvIndexMax"] | 0.0f;
    day.sunrise = d["sunrise"] | (time_t)0;
    day.sunset = d["sunset"] | (time_t)0;
    day.moonPhase = d["moonPhase"] | -1.0f;
    data.daily.push_back(day);
  }

  // Hourly
  JsonArray hourlyArr = doc["hourly"].as<JsonArray>();
  for (JsonObject h : hourlyArr) {
    HourlyForecast hour;
    hour.time = h["time"] | (time_t)0;
    hour.temperature = h["temperature"] | 0.0f;
    hour.precipitation = h["precipitation"] | 0.0f;
    hour.precipitationProbability = h["precipProb"] | 0;
    hour.weatherCode = h["weatherCode"] | 0;
    hour.isDay = h["isDay"] | true;
    data.hourly.push_back(hour);
  }

  data.valid = true;
  LOG_DBG("WEA", "Cache loaded, fetchedAt=%ld", (long)data.fetchedAt);
  return true;
}

std::vector<GeocodingResult> WeatherClient::searchCity(const std::string& query) {
  std::vector<GeocodingResult> results;

  std::string url = "https://geocoding-api.open-meteo.com/v1/search?name=" + urlEncode(query);
  url += "&count=5&language=en";

  std::string response;
  std::string networkError;
  recordWeatherDiagnostic("geocoding_start", "host=geocoding-api.open-meteo.com query_bytes=%zu", query.size());
  // Public geocoding lookup; see fetchFromApi() for why this is not verified.
  if (!HttpDownloader::fetchUrl(url, response, "", "", HttpDownloader::TlsPolicy::Unverified, &networkError)) {
    recordWeatherDiagnostic("geocoding_failed", "stage=http error=%s response_bytes=%zu", networkError.c_str(),
                            response.size());
    LOG_ERR("WEA", "Geocoding request failed");
    return results;
  }

  JsonDocument doc;
  auto error = deserializeJson(doc, response);
  if (error) {
    recordWeatherDiagnostic("geocoding_failed", "stage=parse error=%s response_bytes=%zu", error.c_str(),
                            response.size());
    LOG_ERR("WEA", "Geocoding parse error: %s", error.c_str());
    return results;
  }

  JsonArray arr = doc["results"].as<JsonArray>();
  for (JsonObject obj : arr) {
    GeocodingResult r;
    r.name = obj["name"] | std::string("");
    r.country = obj["country"] | std::string("");
    r.admin1 = obj["admin1"] | std::string("");
    r.latitude = obj["latitude"] | 0.0f;
    r.longitude = obj["longitude"] | 0.0f;
    results.push_back(r);
  }

  LOG_DBG("WEA", "Geocoding found %zu results for '%s'", results.size(), query.c_str());
  recordWeatherDiagnostic("geocoding_ok", "results=%zu", results.size());
  return results;
}
