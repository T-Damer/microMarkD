#include "MarkdownIndexStorage.h"

#ifdef MICROMARKD_APP

#include <HalStorage.h>
#include <Logging.h>

#include <cstddef>
#include <string>
#include <utility>

namespace {
constexpr char MODULE[] = "MDI";
constexpr char INDEX_ROOT[] = "/.micromarkd/index";
constexpr char TEMPORARY_SUFFIX[] = ".tmp";
constexpr size_t MAX_INDEX_RECORD_BYTES = 64 * 1024;

bool ensureIndexRoot() {
  if (!Storage.exists(INDEX_ROOT)) return Storage.mkdir(INDEX_ROOT, true);

  auto directory = Storage.open(INDEX_ROOT);
  const bool valid = directory && directory.isDirectory();
  if (directory) directory.close();
  return valid;
}

bool removeIfPresent(const std::string& path) { return !Storage.exists(path.c_str()) || Storage.remove(path.c_str()); }

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

bool loadMarkdownIndexRecord(const std::string& notePath, const uint64_t sourceSize,
                             micromarkd::MarkdownIndexRecord& record) {
  const std::string cachePath = micromarkd::markdownIndexCachePath(notePath);
  if (!Storage.exists(cachePath.c_str())) return false;

  HalFile file;
  if (!Storage.openFileForRead(MODULE, cachePath, file)) return false;
  const uint64_t encodedSize = file.fileSize64();
  if (encodedSize == 0 || encodedSize > MAX_INDEX_RECORD_BYTES) {
    file.close();
    discardInvalidRecord(cachePath, "invalid size");
    return false;
  }

  std::string encoded(static_cast<size_t>(encodedSize), '\0');
  const int bytesRead = file.read(encoded.data(), encoded.size());
  file.close();
  if (bytesRead != static_cast<int>(encoded.size())) {
    discardInvalidRecord(cachePath, "short read");
    return false;
  }

  micromarkd::MarkdownIndexRecord decoded;
  if (!micromarkd::decodeMarkdownIndexRecord(encoded, decoded)) {
    discardInvalidRecord(cachePath, "invalid record");
    return false;
  }
  if (decoded.path != notePath || decoded.sourceSize != sourceSize) {
    discardInvalidRecord(cachePath, "stale source");
    return false;
  }

  record = std::move(decoded);
  return true;
}

bool removeMarkdownIndexRecord(const std::string& notePath) {
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
