#pragma once

#ifdef MICROMARKD_APP

#include <cstdint>
#include <string>

class WeatherWidget {
 public:
  enum class LocationMode : uint8_t { Unset, Automatic, Manual };

  void load();
  bool useAutomatic();
  bool setManual(const std::string& place);
  bool refresh();

  LocationMode mode() const { return mode_; }
  const std::string& place() const { return place_; }
  bool hasWeather() const { return hasWeather_; }
  int temperature() const { return temperature_; }
  int weatherCode() const { return weatherCode_; }
  int forecastDays() const { return forecastDays_; }

 private:
  bool save() const;
  bool requestJson(const std::string& url, std::string& body) const;
  static std::string encodeQuery(const std::string& value);

  LocationMode mode_ = LocationMode::Unset;
  std::string place_ = "Miami";
  double latitude_ = 25.7617;
  double longitude_ = -80.1918;
  int temperature_ = 0;
  int weatherCode_ = 0;
  int forecastDays_ = 0;
  int64_t fetchedAt_ = 0;
  bool hasWeather_ = false;
};

#endif
