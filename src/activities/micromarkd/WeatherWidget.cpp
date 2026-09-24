#include "WeatherWidget.h"

#ifdef MICROMARKD_APP

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <WiFi.h>

#ifndef SIMULATOR
#include <SecureHttpClient.h>
#endif

#include <cmath>
#include <cstdio>
#include <ctime>

namespace {
constexpr char WEATHER_PATH[] = "/.micromarkd/weather.json";
constexpr char WEATHER_TEMP[] = "/.micromarkd/weather.tmp";
constexpr char FORECAST_PATH[] = "/.micromarkd/weather-forecast.json";
constexpr char FORECAST_TEMP[] = "/.micromarkd/weather-forecast.tmp";
constexpr int64_t MAX_CACHE_AGE = 30LL * 24 * 60 * 60;
constexpr int64_t REFRESH_INTERVAL = 60 * 60;
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
  forecastDays_ = doc["forecastDays"] | 0;
  fetchedAt_ = doc["fetchedAt"] | 0LL;
  const int64_t now = static_cast<int64_t>(time(nullptr));
  hasWeather_ = doc["hasWeather"] | false;
  if (now > 1700000000 && fetchedAt_ > 0 && now - fetchedAt_ > MAX_CACHE_AGE) hasWeather_ = false;
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
  (void)url;
  (void)body;
  return false;
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
  if (hasWeather_ && now > 1700000000 && fetchedAt_ > 0 && now >= fetchedAt_ && now - fetchedAt_ < REFRESH_INTERVAL)
    return true;
  char url[280];
  std::snprintf(url, sizeof(url),
                "https://api.open-meteo.com/v1/"
                "forecast?latitude=%.5f&longitude=%.5f&current=temperature_2m,weather_code&daily=temperature_2m_max,"
                "temperature_2m_min,weather_code&forecast_days=16&timezone=auto",
                latitude_, longitude_);
  std::string body;
  if (!requestJson(url, body)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body) || doc["current"]["temperature_2m"].isNull()) return false;
  temperature_ = static_cast<int>(std::lround(doc["current"]["temperature_2m"].as<double>()));
  weatherCode_ = doc["current"]["weather_code"] | 0;
  forecastDays_ = doc["daily"]["time"].as<JsonArrayConst>().size();
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
