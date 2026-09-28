#ifdef MICROMARKD_APP

#include "ReadingSyncBridge.h"

#include <HalStorage.h>
#include <Logging.h>
#include <esp_mac.h>
#include <sys/time.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace ReadingSyncBridge {

namespace {

constexpr char MODULE[] = "RSY";
constexpr char LIBRARY_PREFIX[] = "/books/";
constexpr char FILES_PREFIX[] = "/books/Books/files/";
constexpr char CATALOG_PATH[] = "/vault/Books/catalog.json";
constexpr char SYNC_ROOT[] = "/vault/Books/sync";
constexpr size_t MAX_JSON_BYTES = 512 * 1024;

std::string readText(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead(MODULE, path.c_str(), file)) return "";
  const size_t size = file.fileSize();
  if (size == 0 || size > MAX_JSON_BYTES) return "";
  std::string text(size, '\0');
  if (file.read(text.data(), size) != static_cast<int>(size)) return "";
  return text;
}

bool writeText(const std::string& path, const std::string& text) {
  const std::string temporary = path + ".tmp";
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
  Storage.remove(path.c_str());
  return Storage.rename(temporary.c_str(), path.c_str());
}

// Stable per device: derived from the factory MAC address.
const std::string& deviceId() {
  static std::string id;
  if (id.empty()) {
    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "xteink-%02x%02x%02x", mac[3], mac[4], mac[5]);
    id = buffer;
  }
  return id;
}

int64_t nowMs() {
  timeval now{};
  gettimeofday(&now, nullptr);
  return static_cast<int64_t>(now.tv_sec) * 1000 + now.tv_usec / 1000;
}

// The catalog is streamed from the SD card; it outgrows the heap as the library grows.
size_t readCatalog(void* context, char* buffer, const size_t capacity) {
  const int n = static_cast<HalFile*>(context)->read(buffer, capacity);
  return n > 0 ? static_cast<size_t>(n) : 0;
}

readingsync::CatalogReader catalogReader(HalFile& file) { return {&file, readCatalog}; }

std::string bookDir(const readingsync::BookRef& book) { return std::string(SYNC_ROOT) + "/" + book.id; }

std::string ownFile(const readingsync::BookRef& book) { return bookDir(book) + "/" + deviceId() + ".json"; }

}  // namespace

bool resolve(const std::string& bookPath, readingsync::BookRef& book) {
  if (bookPath.rfind(FILES_PREFIX, 0) != 0) return false;
  HalFile catalog;
  if (!Storage.openFileForRead(MODULE, CATALOG_PATH, catalog)) return false;
  return readingsync::findBook(catalogReader(catalog), std::string_view(bookPath).substr(sizeof(LIBRARY_PREFIX) - 1),
                               book);
}

std::string booksRemoteUrl(const std::string& vaultUrl) {
  HalFile catalog;
  if (!Storage.openFileForRead(MODULE, CATALOG_PATH, catalog)) return vaultUrl;
  const std::string repository = readingsync::filesRepository(catalogReader(catalog));
  if (repository.empty()) return vaultUrl;
  return "https://github.com/" + repository + ".git";
}

void exportPosition(const std::string& bookPath, const double pct, const int page) {
  const int64_t now = nowMs();
  if (now < readingsync::MIN_VALID_TIME_MS || !std::isfinite(pct)) return;
  readingsync::BookRef book;
  if (!resolve(bookPath, book)) return;
  const std::string path = ownFile(book);
  const std::string previous = readText(path);
  readingsync::Position own;
  if (readingsync::readPosition(previous, book.id, deviceId(), own) && std::fabs(own.pct - pct) < 1e-4 &&
      own.page == page) {
    return;  // unchanged: keep the file (and the vault) untouched
  }
  readingsync::Position position;
  position.updatedAt = now;
  position.pct = std::fmin(1.0, std::fmax(0.0, pct));
  position.page = page;
  if (!Storage.ensureDirectoryExists(bookDir(book).c_str()) ||
      !writeText(path, readingsync::writeDeviceFile(book, deviceId(), position, previous))) {
    LOG_ERR(MODULE, "Failed to save shared position for %s", book.id.c_str());
  }
}

bool newerElsewhere(const std::string& bookPath, readingsync::BookRef& book, readingsync::Position& position) {
  if (!resolve(bookPath, book)) return false;
  const std::string dir = bookDir(book);
  HalFile directory = Storage.open(dir.c_str());
  if (!directory || !directory.isDirectory()) return false;
  std::vector<readingsync::DeviceFile> files;
  char name[256];
  for (HalFile entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    if (entry.isDirectory() || entry.getName(name, sizeof(name)) == 0) continue;
    std::string file = name;
    constexpr char SUFFIX[] = ".json";
    if (file.size() <= sizeof(SUFFIX) - 1 ||
        file.compare(file.size() - (sizeof(SUFFIX) - 1), std::string::npos, SUFFIX) != 0)
      continue;
    file.resize(file.size() - (sizeof(SUFFIX) - 1));
    files.push_back({file, readText(dir + "/" + name)});
  }
  if (!readingsync::newestFromOthers(files, book.id, deviceId(), position)) return false;
  readingsync::Position own;
  const bool hasOwn = readingsync::readPosition(readText(ownFile(book)), book.id, deviceId(), own);
  return !hasOwn || position.updatedAt > own.updatedAt;
}

}  // namespace ReadingSyncBridge

#endif  // MICROMARKD_APP
