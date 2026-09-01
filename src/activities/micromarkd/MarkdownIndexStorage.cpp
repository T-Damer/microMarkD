#include "MarkdownIndexStorage.h"

#ifdef MICROMARKD_APP

#include <HalStorage.h>
#include <Logging.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "activities/micromarkd/MarkdownCatalogStorage.h"

namespace {
constexpr char MODULE[] = "MDI";
constexpr char INDEX_ROOT[] = "/.micromarkd/index";
constexpr char TEMPORARY_SUFFIX[] = ".tmp";
constexpr char READY_PATH[] = "/.micromarkd/index/.ready";
constexpr char READY_TEMPORARY_PATH[] = "/.micromarkd/index/.ready.tmp";
constexpr std::string_view READY_MAGIC_COMPLETE = "MMDIDXREADY\t1\t0\n";
constexpr std::string_view READY_MAGIC_PARTIAL = "MMDIDXREADY\t1\t1\n";
constexpr size_t MAX_INDEX_RECORD_BYTES = 64 * 1024;

bool ensureIndexRoot() {
  if (!Storage.exists(INDEX_ROOT)) return Storage.mkdir(INDEX_ROOT, true);

  auto directory = Storage.open(INDEX_ROOT);
  const bool valid = directory && directory.isDirectory();
  if (directory) directory.close();
  return valid;
}

bool removeIfPresent(const std::string& path) { return !Storage.exists(path.c_str()) || Storage.remove(path.c_str()); }

bool removeIfPresent(const char* path) { return !Storage.exists(path) || Storage.remove(path); }

void discardInvalidRecord(const std::string& cachePath, const char* reason) {
  LOG_ERR(MODULE, "Discarding Markdown index (%s): %s", reason, cachePath.c_str());
  if (!removeIfPresent(cachePath)) LOG_ERR(MODULE, "Failed to remove invalid Markdown index: %s", cachePath.c_str());
}
}  // namespace

bool writeMarkdownIndexRecord(const micromarkd::MarkdownIndexRecord& record) {
  if (record.path.empty()) return false;

  const std::string encoded = micromarkd::encodeMarkdownIndexRecord(record);
  if (encoded.empty() || encoded.size() > MAX_INDEX_RECORD_BYTES) {
    LOG_ERR(MODULE, "Markdown index exceeds cache limit: %s", record.path.c_str());
    return false;
  }
  if (!ensureIndexRoot()) {
    LOG_ERR(MODULE, "Failed to create Markdown index directory");
    return false;
  }

  const std::string cachePath = micromarkd::markdownIndexCachePath(record.path);
  const std::string temporaryPath = cachePath + TEMPORARY_SUFFIX;
  if (!removeIfPresent(temporaryPath)) {
    LOG_ERR(MODULE, "Failed to remove stale Markdown index temporary: %s", temporaryPath.c_str());
    return false;
  }

  HalFile file;
  if (!Storage.openFileForWrite(MODULE, temporaryPath, file)) {
    LOG_ERR(MODULE, "Failed to open Markdown index temporary: %s", temporaryPath.c_str());
    return false;
  }
  if (file.write(encoded.data(), encoded.size()) != encoded.size()) {
    LOG_ERR(MODULE, "Short write creating Markdown index: %s", temporaryPath.c_str());
    file.close();
    removeIfPresent(temporaryPath);
    return false;
  }
  file.flush();
  if (!file.close()) {
    LOG_ERR(MODULE, "Failed to close Markdown index temporary: %s", temporaryPath.c_str());
    removeIfPresent(temporaryPath);
    return false;
  }

  if (!removeIfPresent(cachePath)) {
    LOG_ERR(MODULE, "Failed to replace previous Markdown index: %s", cachePath.c_str());
    removeIfPresent(temporaryPath);
    return false;
  }
  if (!Storage.rename(temporaryPath.c_str(), cachePath.c_str())) {
    LOG_ERR(MODULE, "Failed to promote Markdown index temporary: %s", temporaryPath.c_str());
    removeIfPresent(temporaryPath);
    return false;
  }
  return true;
}

bool markdownIndexCatalogReady(bool* partial) {
  HalFile file;
  if (!Storage.openFileForRead(MODULE, READY_PATH, file)) return false;

  const uint64_t size = file.fileSize64();
  if (size != READY_MAGIC_COMPLETE.size()) {
    file.close();
    return false;
  }
  std::array<char, READY_MAGIC_COMPLETE.size()> marker{};
  const int bytesRead = file.read(marker.data(), marker.size());
  file.close();
  if (bytesRead != static_cast<int>(marker.size())) return false;

  const std::string_view value(marker.data(), marker.size());
  if (value == READY_MAGIC_COMPLETE) {
    if (partial != nullptr) *partial = false;
    return true;
  }
  if (value == READY_MAGIC_PARTIAL) {
    if (partial != nullptr) *partial = true;
    return true;
  }
  return false;
}

bool writeMarkdownIndexCatalogReady(const bool partial) {
  if (!ensureIndexRoot()) {
    LOG_ERR(MODULE, "Failed to create Markdown index directory for ready marker");
    return false;
  }
  if (!removeIfPresent(READY_TEMPORARY_PATH)) {
    LOG_ERR(MODULE, "Failed to remove stale Markdown index ready marker");
    return false;
  }

  HalFile file;
  if (!Storage.openFileForWrite(MODULE, READY_TEMPORARY_PATH, file)) {
    LOG_ERR(MODULE, "Failed to open Markdown index ready marker");
    return false;
  }
  const std::string_view marker = partial ? READY_MAGIC_PARTIAL : READY_MAGIC_COMPLETE;
  if (file.write(marker.data(), marker.size()) != marker.size()) {
    file.close();
    removeIfPresent(READY_TEMPORARY_PATH);
    LOG_ERR(MODULE, "Short write creating Markdown index ready marker");
    return false;
  }
  file.flush();
  if (!file.close()) {
    removeIfPresent(READY_TEMPORARY_PATH);
    LOG_ERR(MODULE, "Failed to close Markdown index ready marker");
    return false;
  }

  if (!removeIfPresent(READY_PATH) || !Storage.rename(READY_TEMPORARY_PATH, READY_PATH)) {
    removeIfPresent(READY_TEMPORARY_PATH);
    LOG_ERR(MODULE, "Failed to promote Markdown index ready marker");
    return false;
  }
  return true;
}

void invalidateMarkdownIndexCatalog() {
  invalidateMarkdownGraphCache();
  removeIfPresent(READY_PATH);
  removeIfPresent(READY_TEMPORARY_PATH);
}

bool removeMarkdownIndexRecord(const std::string& notePath) {
  invalidateMarkdownIndexCatalog();
  const std::string cachePath = micromarkd::markdownIndexCachePath(notePath);
  const std::string temporaryPath = cachePath + TEMPORARY_SUFFIX;
  bool success = true;
  if (!removeIfPresent(cachePath)) {
    LOG_ERR(MODULE, "Failed to remove Markdown index: %s", cachePath.c_str());
    success = false;
  }
  if (!removeIfPresent(temporaryPath)) {
    LOG_ERR(MODULE, "Failed to remove Markdown index temporary: %s", temporaryPath.c_str());
    success = false;
  }
  return success;
}

#endif  // MICROMARKD_APP
