#include "MarkdownCatalogStorage.h"

#ifdef MICROMARKD_APP

#include <HalStorage.h>
#include <Logging.h>
#include <MarkdownDocument.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace {
constexpr char MODULE[] = "MDC";
constexpr char INDEX_ROOT[] = "/.micromarkd/index";
constexpr char INDEX_PREFIX[] = "/.micromarkd/index/";
constexpr size_t NAME_BUFFER_SIZE = 96;
constexpr size_t MAX_INDEX_RECORD_BYTES = 64 * 1024;

bool hasIndexExtension(const std::string_view name) {
  constexpr std::string_view extension = ".midx";
  return name.size() > extension.size() && name.substr(name.size() - extension.size()) == extension;
}

bool readIndexFile(const std::string& cachePath, std::string& encoded) {
  HalFile file;
  if (!Storage.openFileForRead(MODULE, cachePath, file)) return false;
  const uint64_t size = file.fileSize64();
  if (size == 0 || size > MAX_INDEX_RECORD_BYTES) {
    file.close();
    return false;
  }

  encoded.assign(static_cast<size_t>(size), '\0');
  const int bytesRead = file.read(encoded.data(), encoded.size());
  file.close();
  return bytesRead == static_cast<int>(encoded.size());
}

bool sourceMatchesRecord(const micromarkd::MarkdownIndexRecord& record) {
  if (!micromarkd::isVaultMarkdownPath(record.path) || !Storage.exists(record.path.c_str())) return false;

  auto source = Storage.open(record.path.c_str());
  if (!source || source.isDirectory()) {
    if (source) source.close();
    return false;
  }
  const uint64_t size = source.fileSize64();
  source.close();
  return size == record.sourceSize;
}

}  // namespace

bool listMarkdownIndexCacheFiles(std::vector<std::string>& cachePaths, const size_t maxFiles, bool& truncated) {
  cachePaths.clear();
  truncated = false;
  if (!Storage.exists(INDEX_ROOT)) return true;

  auto directory = Storage.open(INDEX_ROOT);
  if (!directory || !directory.isDirectory()) return false;

  std::array<char, NAME_BUFFER_SIZE> name{};
  directory.rewindDirectory();
  for (auto entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    entry.getName(name.data(), name.size());
    const bool isDirectory = entry.isDirectory();
    entry.close();
    if (isDirectory || name[0] == '.' || !hasIndexExtension(name.data())) continue;

    if (cachePaths.size() >= maxFiles) {
      truncated = true;
      break;
    }
    cachePaths.emplace_back(std::string(INDEX_PREFIX) + name.data());
  }
  directory.close();
  std::sort(cachePaths.begin(), cachePaths.end());
  return true;
}

bool loadValidatedMarkdownIndexCache(const std::string& cachePath, micromarkd::MarkdownIndexRecord& record) {
  if (cachePath.rfind(INDEX_PREFIX, 0) != 0 || cachePath.find("..") != std::string::npos ||
      !hasIndexExtension(cachePath)) {
    return false;
  }

  std::string encoded;
  if (!readIndexFile(cachePath, encoded)) return false;

  micromarkd::MarkdownIndexRecord decoded;
  if (!micromarkd::decodeMarkdownIndexRecord(encoded, decoded)) return false;
  if (!sourceMatchesRecord(decoded)) return false;
  if (micromarkd::markdownIndexCachePath(decoded.path) != cachePath) return false;

  record = std::move(decoded);
  return true;
}

bool loadMarkdownCatalogFromCache(micromarkd::MarkdownCatalog& catalog, MarkdownCatalogLoadReport& report,
                                  const size_t maxRecords) {
  report = {};
  std::vector<std::string> cachePaths;
  bool truncated = false;
  if (!listMarkdownIndexCacheFiles(cachePaths, maxRecords, truncated)) return false;
  report.cacheFilesFound = cachePaths.size();
  report.recordLimitReached = truncated;

  for (const auto& cachePath : cachePaths) {
    micromarkd::MarkdownIndexRecord record;
    if (!loadValidatedMarkdownIndexCache(cachePath, record)) {
      report.invalidRecords++;
      if (!Storage.remove(cachePath.c_str())) {
        LOG_ERR(MODULE, "Failed to remove invalid catalog record: %s", cachePath.c_str());
      }
      continue;
    }
    if (!catalog.addRecord(record)) {
      report.recordLimitReached = true;
      break;
    }
    report.recordsLoaded++;
  }
  catalog.finalize();
  return true;
}

#endif  // MICROMARKD_APP
