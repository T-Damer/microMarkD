#include "ReadingSync.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace readingsync {

namespace {

bool isBookId(std::string_view id) {
  if (id.size() != 32) return false;
  for (char c : id) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

// "owner/name": GitHub's own characters only, so it can go into a URL as is.
bool isRepositoryName(std::string_view name) {
  const size_t slash = name.find('/');
  if (slash == 0 || slash == std::string_view::npos || slash + 1 == name.size() || name.size() > 200) return false;
  for (size_t i = 0; i < name.size(); i++) {
    const char c = name[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                    c == '_' || c == '.' || (c == '/' && i == slash);
    if (!ok) return false;
  }
  return true;
}

}  // namespace

// Walks the catalog's top-level object with a small tokenizer: values of
// interest ("files", each element of "books") are cut out as raw JSON and
// parsed alone; everything else is skipped without being stored.
class CatalogScanner {
 public:
  explicit CatalogScanner(const CatalogReader& read) : read_(read) {}

  // Calls `onEntry(key, raw)` for "files" and for every element of "books"
  // (key "books"); stops early when it returns false. Entries count only after
  // "version": 2, which every catalog writer puts first.
  template <typename OnEntry>
  bool scan(OnEntry&& onEntry) {
    if (!skipSpace() || next() != '{') return false;
    while (true) {
      if (!skipSpace()) return false;
      char c = peek();
      if (c == '}') return true;
      if (c == ',') {
        next();
        continue;
      }
      std::string key;
      if (!readString(key) || !skipSpace() || next() != ':' || !skipSpace()) return false;
      if (key == "version") {
        std::string raw;
        if (!readValue(&raw) || raw != "2") return false;
        version2_ = true;
      } else if (!version2_) {
        if (!readValue(nullptr)) return false;
      } else if (key == "books" && peek() == '[') {
        next();
        while (true) {
          if (!skipSpace()) return false;
          c = peek();
          if (c == ']') {
            next();
            break;
          }
          if (c == ',') {
            next();
            continue;
          }
          std::string raw;
          if (!readValue(&raw)) return false;
          if (!onEntry(key, raw)) return true;
        }
      } else if (key == "files") {
        std::string raw;
        if (!readValue(&raw)) return false;
        if (!onEntry(key, raw)) return true;
      } else if (!readValue(nullptr)) {
        return false;
      }
    }
  }

 private:
  // A single book entry is about a kilobyte; anything far larger is not a catalog.
  static constexpr size_t MAX_VALUE_BYTES = 32 * 1024;

  bool fill() {
    if (pos_ < len_) return true;
    if (eof_) return false;
    len_ = read_.read(read_.context, buffer_, sizeof(buffer_));
    pos_ = 0;
    if (len_ == 0) eof_ = true;
    return len_ > 0;
  }
  char peek() { return fill() ? buffer_[pos_] : '\0'; }
  char next() { return fill() ? buffer_[pos_++] : '\0'; }
  bool skipSpace() {
    while (fill()) {
      const char c = buffer_[pos_];
      if (c != ' ' && c != '\n' && c != '\r' && c != '\t') return true;
      pos_++;
    }
    return false;
  }
  // Reads a JSON string's raw content (escapes kept; keys never need them).
  bool readString(std::string& out) {
    if (next() != '"') return false;
    while (fill()) {
      const char c = next();
      if (c == '\\') {
        out += c;
        out += next();
      } else if (c == '"') {
        return true;
      } else {
        out += c;
      }
    }
    return false;
  }
  // Consumes one value, copying its raw text into `out` when given.
  bool readValue(std::string* out) {
    int depth = 0;
    bool inString = false;
    while (fill()) {
      const char c = peek();
      if (!inString && depth == 0 && (c == ',' || c == '}' || c == ']')) return true;
      next();
      if (out) {
        if (out->size() >= MAX_VALUE_BYTES) return false;
        *out += c;
      }
      if (inString) {
        if (c == '\\') {
          const char escaped = next();
          if (out) *out += escaped;
        } else if (c == '"') {
          inString = false;
          if (depth == 0) return true;
        }
      } else if (c == '"') {
        inString = true;
      } else if (c == '{' || c == '[') {
        depth++;
      } else if (c == '}' || c == ']') {
        if (--depth == 0) return true;
      }
    }
    return depth == 0 && !inString;
  }

  const CatalogReader& read_;
  bool version2_ = false;
  char buffer_[512];
  size_t pos_ = 0;
  size_t len_ = 0;
  bool eof_ = false;
};

struct StringSource {
  std::string_view text;
  size_t offset = 0;
};

size_t readString(void* context, char* buffer, const size_t capacity) {
  auto* source = static_cast<StringSource*>(context);
  const size_t n = std::min(capacity, source->text.size() - source->offset);
  std::memcpy(buffer, source->text.data() + source->offset, n);
  source->offset += n;
  return n;
}

bool findBook(const CatalogReader& read, std::string_view filesPath, BookRef& out) {
  bool found = false;
  CatalogScanner(read).scan([&](std::string_view key, const std::string& raw) {
    if (key != "books") return true;
    JsonDocument doc;
    if (deserializeJson(doc, raw)) return true;
    JsonObjectConst book = doc.as<JsonObjectConst>();
    const char* id = book["id"];
    if (!id || !isBookId(id)) return true;
    const bool pdf = book["format"] == "pdf";
    JsonArrayConst copies = book["xteink"].as<JsonArrayConst>();
    const auto matches = [&](JsonObjectConst file) {
      const char* path = file["path"];
      return path && filesPath == path;
    };
    int firstPage = 1;
    bool hit = matches(book["source"]);
    for (JsonObjectConst copy : copies) {
      if (!hit && matches(copy)) {
        hit = true;
        firstPage = copy["firstPage"] | 1;
      }
    }
    if (!hit) return true;
    out.id = id;
    out.title = book["title"] | "";
    out.pdf = pdf;
    out.firstPage = firstPage > 0 ? firstPage : 1;
    out.multiVolume = copies.size() > 1;
    found = true;
    return false;
  });
  return found;
}

bool findBook(std::string_view catalogJson, std::string_view filesPath, BookRef& out) {
  StringSource source{catalogJson};
  return findBook(CatalogReader{&source, readString}, filesPath, out);
}

std::string filesRepository(const CatalogReader& read) {
  std::string repository;
  CatalogScanner(read).scan([&](std::string_view key, const std::string& raw) {
    if (key != "files") return true;
    JsonDocument doc;
    if (!deserializeJson(doc, raw)) {
      const char* value = doc["repository"];
      if (value && isRepositoryName(value)) repository = value;
    }
    return false;
  });
  return repository;
}

std::string filesRepository(std::string_view catalogJson) {
  StringSource source{catalogJson};
  return filesRepository(CatalogReader{&source, readString});
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
