#pragma once

#ifdef MICROMARKD_APP

#include <MarkdownCatalog.h>
#include <MarkdownIndex.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct MarkdownCatalogLoadReport {
  size_t cacheFilesFound = 0;
  size_t recordsLoaded = 0;
  size_t invalidRecords = 0;
  size_t sourceFilesMissing = 0;
  bool recordLimitReached = false;

  bool partial() const { return invalidRecords > 0 || sourceFilesMissing > 0 || recordLimitReached; }
};

struct MarkdownGraphCacheEdge {
  uint8_t from = 0;
  uint8_t to = 0;
};

struct MarkdownGraphCachePage {
  size_t totalNodes = 0;
  std::vector<std::string> paths;
  std::vector<uint32_t> globalIndices;
  std::vector<uint16_t> degrees;
  std::vector<MarkdownGraphCacheEdge> edges;
};

bool listMarkdownIndexCacheFiles(std::vector<std::string>& cachePaths, size_t maxFiles, bool& truncated);
bool loadMarkdownIndexCacheRecord(const std::string& cachePath, micromarkd::MarkdownIndexRecord& record);
bool loadValidatedMarkdownIndexCache(const std::string& cachePath, micromarkd::MarkdownIndexRecord& record);
bool loadMarkdownCatalogFromCache(micromarkd::MarkdownCatalog& catalog, MarkdownCatalogLoadReport& report,
                                  size_t maxRecords = micromarkd::MAX_CATALOG_NOTES);
bool markdownGraphCacheReady();
bool rebuildMarkdownGraphCache();
void invalidateMarkdownGraphCache();
bool loadMarkdownGraphCachePage(size_t page, size_t pageSize, size_t maxNodes, MarkdownGraphCachePage& result);

#endif  // MICROMARKD_APP
