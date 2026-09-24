#include "WeatherWidget.h"

#ifdef MICROMARKD_APP

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <WiFi.h>

#ifndef SIMULATOR
#include <SecureHttpClient.h>
#endif

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {
constexpr char WEATHER_PATH[] = "/.micromarkd/weather.json";
constexpr char WEATHER_TEMP[] = "/.micromarkd/weather.tmp";
constexpr char FORECAST_PATH[] = "/.micromarkd/weather-forecast.json";
constexpr char FORECAST_TEMP[] = "/.micromarkd/weather-forecast.tmp";
constexpr int64_t MAX_CACHE_AGE = 30LL * 24 * 60 * 60;
constexpr int64_t REFRESH_INTERVAL = 60 * 60;

bool readDaily(const JsonDocument& doc, std::array<char, 11>& date, std::array<int16_t, 16>& high,
               std::array<int16_t, 16>& low, int& count) {
  const JsonArrayConst days = doc["daily"]["time"].as<JsonArrayConst>();
  const JsonArrayConst highs = doc["daily"]["temperature_2m_max"].as<JsonArrayConst>();
  const JsonArrayConst lows = doc["daily"]["temperature_2m_min"].as<JsonArrayConst>();
  if (days.isNull() || days.size() == 0 || days.size() > high.size() || highs.size() != days.size() ||
      lows.size() != days.size())
    return false;
  const char* firstDate = days[0].as<const char*>();
  if (!firstDate || std::strlen(firstDate) < 10) return false;
  std::memcpy(date.data(), firstDate, 10);
  date[10] = '\0';
  for (size_t day = 0; day < days.size(); ++day) {
    if (highs[day].isNull() || lows[day].isNull()) return false;
    const double maximum = highs[day].as<double>();
    const double minimum = lows[day].as<double>();
    if (!std::isfinite(maximum) || !std::isfinite(minimum) || maximum < minimum || maximum < -100 || maximum > 100 ||
        minimum < -100 || minimum > 100)
      return false;
    high[day] = static_cast<int16_t>(std::lround(maximum));
    low[day] = static_cast<int16_t>(std::lround(minimum));
  }
  count = static_cast<int>(days.size());
  return true;
}
}  // namespace

void WeatherWidget::load() {
  HalFile file;
  if (!Storage.openFileForRead("WTH", WEATHER_PATH, file)) return;
  JsonDocument doc;
  if (deserializeJson(doc, file)) return;
  const int mode = doc["mode"] | 0;
  if (mode >= 0 && mode <= 2) mode_ = static_cast<LocationMode>(mode);
  place_ = doc["place"] | "Miami";
  latitude_ = doc["latitude"] | 25.7617;
  longitude_ = doc["longitude"] | -80.1918;
  if (!std::isfinite(latitude_) || !std::isfinite(longitude_) || latitude_ < -90 || latitude_ > 90 ||
      longitude_ < -180 || longitude_ > 180 || place_.size() > 96) {
    latitude_ = 25.7617;
    longitude_ = -80.1918;
    place_ = "Miami";
    mode_ = LocationMode::Unset;
  }
  temperature_ = doc["temperature"] | 0;
  weatherCode_ = doc["weatherCode"] | 0;
  forecastDays_ = 0;
  fetchedAt_ = doc["fetchedAt"] | 0LL;
  const int64_t now = static_cast<int64_t>(time(nullptr));
  hasWeather_ = doc["hasWeather"] | false;
  if (now > 1700000000 && fetchedAt_ > 0 && now - fetchedAt_ > MAX_CACHE_AGE) hasWeather_ = false;
  HalFile forecast;
  if (Storage.openFileForRead("WTH", FORECAST_PATH, forecast)) {
    JsonDocument daily;
    if (deserializeJson(daily, forecast) || !readDaily(daily, todayDate_, high_, low_, forecastDays_))
      forecastDays_ = 0;
  }
}

bool WeatherWidget::save() const {
  if (!Storage.ensureDirectoryExists("/.micromarkd")) return false;
  HalFile file;
  if (!Storage.openFileForWrite("WTH", WEATHER_TEMP, file)) return false;
  JsonDocument doc;
  doc["mode"] = static_cast<int>(mode_);
  doc["place"] = place_;
  doc["latitude"] = latitude_;
  doc["longitude"] = longitude_;
  doc["temperature"] = temperature_;
  doc["weatherCode"] = weatherCode_;
  doc["forecastDays"] = forecastDays_;
  doc["fetchedAt"] = fetchedAt_;
  doc["hasWeather"] = hasWeather_;
  if (serializeJson(doc, file) == 0) return false;
  file.flush();
  file.close();
  Storage.remove(WEATHER_PATH);
  return Storage.rename(WEATHER_TEMP, WEATHER_PATH);
}

bool WeatherWidget::requestJson(const std::string& url, std::string& body) const {
#ifdef SIMULATOR
  if (url.find("api.open-meteo.com/v1/forecast") == std::string::npos) return false;
  // Offline emulator data exercises the real forecast parser and graph without a network dependency.
  body = "{\"current\":{\"temperature_2m\":27,\"weather_code\":2},\"daily\":{\"time\":[";
  for (int day = 0; day < 16; ++day) {
    const std::time_t when = std::time(nullptr) + day * 86400;
    const std::tm* date = std::gmtime(&when);
    if (!date) return false;
    char formatted[16];
    std::strftime(formatted, sizeof(formatted), "%Y-%m-%d", date);
    if (day) body += ',';
    body += '"';
    body += formatted;
    body += '"';
  }
  body += "],\"temperature_2m_max\":[";
  for (int day = 0; day < 16; ++day) {
    if (day) body += ',';
    body += std::to_string(29 + (day % 5) - day / 5);
  }
  body += "],\"temperature_2m_min\":[";
  for (int day = 0; day < 16; ++day) {
    if (day) body += ',';
    body += std::to_string(21 + (day % 4) - day / 6);
  }
  body += "]}}";
  return true;
#else
  if (WiFi.status() != WL_CONNECTED) return false;
  freeink::SecureHttpClient http;
  http.setInsecure();  // Public weather data only; this client has no CA bundle.
  http.setTimeout(10000);
  if (!http.begin(url) || http.GET() != 200 || !http.responseComplete()) return false;
  body = http.getString();
  return body.size() <= 8192;
#endif
}

std::string WeatherWidget::encodeQuery(const std::string& value) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(value.size() * 3);
  for (const unsigned char ch : value) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_') {
      result.push_back(static_cast<char>(ch));
    } else {
      result.push_back('%');
      result.push_back(hex[ch >> 4]);
      result.push_back(hex[ch & 15]);
    }
  }
  return result;
}

bool WeatherWidget::useAutomatic() {
  std::string body;
  if (!requestJson("https://ipwho.is/", body)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  if (doc["latitude"].isNull() || doc["longitude"].isNull()) return false;
  latitude_ = doc["latitude"].as<double>();
  longitude_ = doc["longitude"].as<double>();
  if (!std::isfinite(latitude_) || !std::isfinite(longitude_) || latitude_ < -90 || latitude_ > 90 ||
      longitude_ < -180 || longitude_ > 180)
    return false;
  place_ = doc["city"] | "Current region";
  mode_ = LocationMode::Automatic;
  hasWeather_ = false;
  fetchedAt_ = 0;
  save();
  return refresh();
}

bool WeatherWidget::setManual(const std::string& place) {
  if (place.empty() || place.size() > 96) return false;
  double latitude = 0;
  double longitude = 0;
  char trailing = 0;
  if (std::sscanf(place.c_str(), "%lf,%lf %c", &latitude, &longitude, &trailing) != 2) {
    std::string body;
    if (!requestJson("https://geocoding-api.open-meteo.com/v1/search?name=" + encodeQuery(place) + "&count=1", body))
      return false;
    JsonDocument doc;
    if (deserializeJson(doc, body) || doc["results"][0]["latitude"].isNull() || doc["results"][0]["longitude"].isNull())
      return false;
    latitude = doc["results"][0]["latitude"].as<double>();
    longitude = doc["results"][0]["longitude"].as<double>();
    place_ = doc["results"][0]["name"] | place.c_str();
  } else {
    place_ = place;
  }
  if (!std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90 || latitude > 90 || longitude < -180 ||
      longitude > 180)
    return false;
  latitude_ = latitude;
  longitude_ = longitude;
  mode_ = LocationMode::Manual;
  hasWeather_ = false;
  fetchedAt_ = 0;
  save();
  return refresh();
}

bool WeatherWidget::refresh() {
  const int64_t now = static_cast<int64_t>(time(nullptr));
  if (hasWeather_ && forecastDays_ > 0 && now > 1700000000 && fetchedAt_ > 0 && now >= fetchedAt_ &&
      now - fetchedAt_ < REFRESH_INTERVAL)
    return true;
  char coordinates[52];
  std::snprintf(coordinates, sizeof(coordinates), "%.5f&longitude=%.5f", latitude_, longitude_);
  // This hourly network URL uses heap storage so the ESP32-C3 stack stays below its small frame budget.
  std::string url;
  url.reserve(224);
  url += "https://api.open-meteo.com/v1/forecast?latitude=";
  url += coordinates;
  url +=
      "&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min,weather_code"
      "&forecast_days=16&timezone=auto";
  std::string body;
  if (!requestJson(url, body)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body) || doc["current"]["temperature_2m"].isNull()) return false;
  const double current = doc["current"]["temperature_2m"].as<double>();
  if (!std::isfinite(current) || current < -100 || current > 100) return false;
  std::array<char, 11> date{};
  std::array<int16_t, 16> high{};
  std::array<int16_t, 16> low{};
  int days = 0;
  if (!readDaily(doc, date, high, low, days)) return false;
  temperature_ = static_cast<int>(std::lround(current));
  weatherCode_ = doc["current"]["weather_code"] | 0;
  todayDate_ = date;
  high_ = high;
  low_ = low;
  forecastDays_ = days;
  fetchedAt_ = now;
  hasWeather_ = true;
  if (!save()) return false;
  HalFile forecast;
  if (!Storage.openFileForWrite("WTH", FORECAST_TEMP, forecast)) return false;
  if (forecast.write(body.data(), body.size()) != body.size()) return false;
  forecast.flush();
  forecast.close();
  Storage.remove(FORECAST_PATH);
  return Storage.rename(FORECAST_TEMP, FORECAST_PATH);
}

#endif
