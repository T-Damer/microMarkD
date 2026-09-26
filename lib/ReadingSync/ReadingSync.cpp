#include "ReadingSync.h"

#include <ArduinoJson.h>

#include <cmath>

namespace readingsync {

namespace {

bool isBookId(std::string_view id) {
  if (id.size() != 32) return false;
  for (char c : id) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

}  // namespace

bool findBook(std::string_view catalogJson, std::string_view filesPath, BookRef& out) {
  JsonDocument doc;
  if (deserializeJson(doc, catalogJson) || doc["version"] != 2) return false;
  for (JsonObjectConst book : doc["books"].as<JsonArrayConst>()) {
    const char* id = book["id"];
    if (!id || !isBookId(id)) continue;
    const bool pdf = book["format"] == "pdf";
    JsonArrayConst copies = book["xteink"].as<JsonArrayConst>();
    const auto matches = [&](JsonObjectConst file) {
      const char* path = file["path"];
      return path && filesPath == path;
    };
    int firstPage = 1;
    bool found = matches(book["source"]);
    for (JsonObjectConst copy : copies) {
      if (!found && matches(copy)) {
        found = true;
        firstPage = copy["firstPage"] | 1;
      }
    }
    if (!found) continue;
    out.id = id;
    out.title = book["title"] | "";
    out.pdf = pdf;
    out.firstPage = firstPage > 0 ? firstPage : 1;
    out.multiVolume = copies.size() > 1;
    return true;
  }
  return false;
}

bool readPosition(std::string_view json, const std::string& bookId, const std::string& deviceId, Position& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json) || doc["version"] != 1 || doc["bookId"] != bookId || doc["device"]["id"] != deviceId) {
    return false;
  }
  JsonObjectConst position = doc["position"];
  if (position.isNull() || !position["updatedAt"].is<int64_t>() || !position["pct"].is<double>()) return false;
  const int64_t updatedAt = position["updatedAt"];
  const double pct = position["pct"];
  if (updatedAt < 0 || !std::isfinite(pct) || pct < 0 || pct > 1) return false;
  out.updatedAt = updatedAt;
  out.pct = pct;
  out.page = position["page"] | 0;
  out.deviceId = deviceId;
  return true;
}

bool newestFromOthers(const std::vector<DeviceFile>& files, const std::string& bookId, const std::string& selfId,
                      Position& out) {
  bool found = false;
  for (const auto& file : files) {
    if (file.deviceId == selfId) continue;
    Position candidate;
    if (!readPosition(file.json, bookId, file.deviceId, candidate)) continue;
    // Same tie-break as the Obsidian plugin: newest, then larger device id.
    if (!found || candidate.updatedAt > out.updatedAt ||
        (candidate.updatedAt == out.updatedAt && candidate.deviceId > out.deviceId)) {
      out = candidate;
      found = true;
    }
  }
  return found;
}

std::string writeDeviceFile(const BookRef& book, const std::string& deviceId, const Position& position,
                            std::string_view previousJson) {
  JsonDocument doc;
  if (deserializeJson(doc, previousJson) || doc["bookId"] != book.id || doc["device"]["id"] != deviceId) {
    doc.clear();
  }
  doc["version"] = 1;
  doc["bookId"] = book.id;
  if (!book.title.empty()) doc["title"] = book.title;
  doc["device"]["id"] = deviceId;
  doc["device"]["platform"] = "xteink";
  JsonObject pos = doc["position"].to<JsonObject>();
  pos["updatedAt"] = position.updatedAt;
  pos["pct"] = position.pct;
  if (position.page > 0) pos["page"] = position.page;
  if (!doc["highlights"].is<JsonObject>()) doc["highlights"].to<JsonObject>();
  std::string out;
  serializeJsonPretty(doc, out);
  out += '\n';
  return out;
}

}  // namespace readingsync
