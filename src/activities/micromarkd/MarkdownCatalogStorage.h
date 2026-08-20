#pragma once

#ifdef MICROMARKD_APP

#include <MarkdownCatalog.h>
#include <MarkdownIndex.h>

#include <cstddef>
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

bool listMarkdownIndexCacheFiles(std::vector<std::string>& cachePaths, size_t maxFiles, bool& truncated);
bool loadValidatedMarkdownIndexCache(const std::string& cachePath, micromarkd::MarkdownIndexRecord& record);
bool loadMarkdownCatalogFromCache(micromarkd::MarkdownCatalog& catalog, MarkdownCatalogLoadReport& report,
                                  size_t maxRecords = micromarkd::MAX_CATALOG_NOTES);

#endif  // MICROMARKD_APP
