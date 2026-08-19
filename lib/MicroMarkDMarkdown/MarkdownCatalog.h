#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "MarkdownIndex.h"

namespace micromarkd {

constexpr size_t MAX_CATALOG_NOTES = 1024;
constexpr size_t MAX_CATALOG_TAGS = 512;

struct MarkdownCatalogNote {
  std::string path;
  std::string title;
  std::vector<std::string> aliases;
  std::vector<std::string> tags;
};

struct MarkdownCatalogTag {
  std::string name;
  size_t noteCount = 0;
};

class MarkdownCatalog {
 public:
  bool addRecord(const MarkdownIndexRecord& record);
  void finalize();

  const std::vector<MarkdownCatalogNote>& notes() const { return notes_; }
  const std::vector<MarkdownCatalogTag>& tags() const { return tags_; }
  bool truncated() const { return truncated_; }

  std::string resolveTarget(std::string_view sourcePath, std::string_view target) const;
  std::vector<std::string> notesForTag(std::string_view tag, size_t maxResults = MAX_CATALOG_NOTES) const;

 private:
  std::vector<MarkdownCatalogNote> notes_;
  std::vector<MarkdownCatalogTag> tags_;
  bool truncated_ = false;
};

std::string normalizeCatalogKey(std::string_view value);
bool catalogLinkTargetsPath(const MarkdownCatalog& catalog, std::string_view sourcePath,
                            const MarkdownIndexLink& link, std::string_view targetPath);

}  // namespace micromarkd
