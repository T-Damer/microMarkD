#include "Provisioning.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstring>
#include <string>

#include "Provisioning.generated.h"
#include "WifiCredentialStore.h"
#ifdef MICROMARKD_APP
#include "activities/micromarkd/MarkdownSyncActivity.h"
#endif

namespace Provisioning {

namespace {

constexpr char MODULE[] = "PRV";
constexpr char MARKER_PATH[] = "/.crosspoint/provisioned";
constexpr char SETTINGS_PATH[] = "/.crosspoint/settings.json";

bool alreadyApplied() {
  HalFile file;
  if (!Storage.openFileForRead(MODULE, MARKER_PATH, file)) return false;
  char stored[sizeof(PROVISIONING_ID)] = {};
  const int n = file.read(stored, sizeof(stored) - 1);
  return n == static_cast<int>(sizeof(PROVISIONING_ID) - 1) && std::strcmp(stored, PROVISIONING_ID) == 0;
}

void ensureParent(const char* path) {
  const char* slash = std::strrchr(path, '/');
  if (!slash || slash == path) return;
  Storage.ensureDirectoryExists(std::string(path, slash - path).c_str());
}

// Written to a sibling first so a power cut never leaves a half-written file.
bool writeFile(const char* path, const std::string& text) {
  ensureParent(path);
  const std::string temporary = std::string(path) + ".prv";
  {
    HalFile file;
    if (!Storage.openFileForWrite(MODULE, temporary.c_str(), file)) return false;
    if (file.write(text.data(), text.size()) != text.size()) {
      file.close();
      Storage.remove(temporary.c_str());
      return false;
    }
    file.flush();
    file.close();
  }
  Storage.remove(path);
  return Storage.rename(temporary.c_str(), path);
}

void applyWifi(JsonArrayConst networks) {
  if (networks.isNull()) return;
  WIFI_STORE.loadFromFile();
  for (JsonObjectConst network : networks) {
    const char* ssid = network["ssid"];
    if (!ssid || !*ssid) continue;
    if (!WIFI_STORE.addCredential(ssid, network["password"] | "")) LOG_ERR(MODULE, "Wi-Fi %s not saved", ssid);
  }
  LOG_INF(MODULE, "Wi-Fi networks: %u", static_cast<unsigned>(networks.size()));
}

void applySettings(JsonObjectConst values) {
  if (values.isNull()) return;
  JsonDocument settings;
  {
    HalFile file;
    if (Storage.openFileForRead(MODULE, SETTINGS_PATH, file) && deserializeJson(settings, file)) settings.clear();
  }
  for (JsonPairConst value : values) settings[value.key()] = value.value();
  std::string text;
  serializeJson(settings, text);
  if (!writeFile(SETTINGS_PATH, text)) LOG_ERR(MODULE, "settings.json not written");
}

void applyFiles(JsonObjectConst files) {
  for (JsonPairConst file : files) {
    std::string text;
    if (file.value().is<const char*>()) {
      text = file.value().as<const char*>();
    } else {
      serializeJson(file.value(), text);
    }
    if (!writeFile(file.key().c_str(), text)) LOG_ERR(MODULE, "%s not written", file.key().c_str());
  }
}

}  // namespace

void apply() {
  if (PROVISIONING_JSON[0] == '\0' || alreadyApplied()) return;
  JsonDocument doc;
  if (deserializeJson(doc, PROVISIONING_JSON)) {
    LOG_ERR(MODULE, "Provisioning data unreadable");
    return;
  }
  applyWifi(doc["wifi"].as<JsonArrayConst>());
#ifdef MICROMARKD_APP
  JsonObjectConst git = doc["git"];
  if (!git.isNull() && !MarkdownSyncActivity::saveSyncCredentials(git["url"] | "", git["token"] | "")) {
    LOG_ERR(MODULE, "Git remote not saved");
  }
#endif
  applySettings(doc["settings"].as<JsonObjectConst>());
  applyFiles(doc["files"].as<JsonObjectConst>());
  if (!writeFile(MARKER_PATH, PROVISIONING_ID)) LOG_ERR(MODULE, "Marker not written");
  LOG_INF(MODULE, "Applied provisioning %s", PROVISIONING_ID);
}

}  // namespace Provisioning
