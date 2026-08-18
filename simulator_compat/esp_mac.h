#pragma once

#include <cstdint>
#include <cstring>

// Native simulator compatibility for newer CrossPoint code that calls the
// ESP-IDF station-MAC API directly. Keep this header off embedded include paths.
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;

enum esp_mac_type_t : int {
  ESP_MAC_WIFI_STA = 0,
};

inline esp_err_t esp_read_mac(uint8_t mac[6], esp_mac_type_t) {
  static constexpr uint8_t SIMULATOR_MAC[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
  std::memcpy(mac, SIMULATOR_MAC, sizeof(SIMULATOR_MAC));
  return ESP_OK;
}

inline esp_err_t esp_efuse_mac_get_default(uint8_t mac[6]) { return esp_read_mac(mac, ESP_MAC_WIFI_STA); }
