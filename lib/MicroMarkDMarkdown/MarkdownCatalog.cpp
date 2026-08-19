#include "MarkdownCatalog.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "MarkdownDocument.h"
#include "MarkdownSearch.h"

namespace micromarkd {
namespace {

bool isAsciiSpace(const unsigned char ch) {
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

bool endsWithAsciiCaseInsensitive(const std::string_view value, const std::string_view suffix) {
  if (value.size() < suffix.size()) return false;
  const size_t offset = value.size() - suffix.size();
  for (size_t index = 0; index < suffix.size(); index++) {
    const unsigned char left = static_cast<unsigned char>(value[offset + index]);
    const unsigned char right = static_cast<unsigned char>(suffix[index]);
    if (std::tolower(left) != std::tolower(right)) return false;
  }
  return true;
}

std::string stripMarkdownExtension(std::string value) {
  if (endsWithAsciiCaseInsensitive(value, ".markdown")) {
    value.resize(value.size() - 9);
  } else if (endsWithAsciiCaseInsensitive(value, ".md")) {
    value.resize(value.size() - 3);
  }
  return value;
}

std::string vaultRelativeStem(const std::string_view path) {
  constexpr std::string_view prefix = "/vault/";
  if (path.size() <= prefix.size() || path.substr(0, prefix.size()) != prefix) return {};
  return stripMarkdownExtension(std::string(path.substr(prefix.size())));
}

std::string sourceFolderRelative(const std::string_view sourcePath) {
  std::string relative = vaultRelativeStem(sourcePath);
  const size_t slash = relative.find_last_of('/');
  if (slash == std::string::npos) return {};
  relative.resize(slash + 1);
  return relative;
}

std::string normalizeRelativePath(const std::string_view base, const std::string_view raw) {
  std::vector<std::string> parts;
  parts.reserve(12);

  const auto append = [&parts](const std::string_view path) -> bool {
    size_t start = 0;
    while (start <= path.size()) {
      size_t end = path.find('/', start);
      if (end == std::string_view::npos) end = path.size();
      const std::string_view part = path.substr(start, end - start);
      if (!part.empty() && part != ".") {
        if (part == "..") {
          if (parts.empty()) return false;
          parts.pop_back();
        } else {
          parts.emplace_back(part);
        }

      }
      if (end == path.size()) break;
      start = end + 1;
    }
    return true;
  };

  if (!append(base) || !append(raw)) return {};
  std::string result;
  for (size_t index = 0; index < parts.size(); index++) {
    if (index > 0) result.push_back('/');
    result += parts[index];
  }
  return stripMarkdownExtension(result);
}

bool noteHasTag(const MarkdownCatalogNote& note, const std::string_view tag) {
  const std::string key = normalizeCatalogKey(tag);
  for (const auto& candidate : note.tags) {
    if (normalizeCatalogKey(candidate) == key) return true;
  }
  return false;
}

}  // namespace

std::string normalizeCatalogKey(const std::string_view value) {
  const std::string folded = foldSearchText(value);
  std::string normalized;
  normalized.reserve(folded.size());
  bool pendingSpace = false;
  for (const char raw : folded) {
    const unsigned char ch = static_cast<unsigned char>(raw);
    if (isAsciiSpace(ch)) {
      pendingSpace = !normalized.empty();
      continue;
    }
    if (pendingSpace) normalized.push_back(' ');
    pendingSpace = false;
    normalized.push_back(raw == '\\' ? '/' : raw);
  }
  while (!normalized.empty() && normalized.back() == ' ') normalized.pop_back();
  return normalized;
}

bool MarkdownCatalog::addRecord(const MarkdownIndexRecord& record) {
  if (!isVaultMarkdownPath(record.path)) return false;

  for (const auto& existing : notes_) {
    if (existing.path == record.path) return true;
  }
  if (notes_.size() >= MAX_CATALOG_NOTES) {
    truncated_ = true;
    return false;
  }

  MarkdownCatalogNote note;
  note.path = record.path;
  note.title = vaultNoteDisplayName(record.path);
  note.aliases = record.metadata.aliases;
  note.tags = record.metadata.tags;
  notes_.push_back(std::move(note));

  for (const auto& tag : record.metadata.tags) {
    const std::string key = normalizeCatalogKey(tag);
    if (key.empty()) continue;

    auto found = std::find_if(tags_.begin(), tags_.end(), [&key](const MarkdownCatalogTag& candidate) {
      return normalizeCatalogKey(candidate.name) == key;
    });
    if (found != tags_.end()) {
      found->noteCount++;
      continue;
    }
    if (tags_.size() >= MAX_CATALOG_TAGS) {
      truncated_ = true;
      continue;
    }
    tags_.push_back({tag, 1});
  }
  if (record.metadata.truncated) truncated_ = true;
  return true;
}

void MarkdownCatalog::finalize() {
  std::sort(notes_.begin(), notes_.end(), [](const MarkdownCatalogNote& left, const MarkdownCatalogNote& right) {
    return normalizeCatalogKey(left.path) < normalizeCatalogKey(right.path);
  });
  std::sort(tags_.begin(), tags_.end(), [](const MarkdownCatalogTag& left, const MarkdownCatalogTag& right) {
    const std::string leftKey = normalizeCatalogKey(left.name);
    const std::string rightKey = normalizeCatalogKey(right.name);
    if (leftKey != rightKey) return leftKey < rightKey;
    return left.name < right.name;
  });
}

std::string MarkdownCatalog::resolveTarget(const std::string_view sourcePath, const std::string_view rawTarget) const {
  std::string target = trimNoteTitle(rawTarget);
  if (target.empty()) return {};

  const size_t heading = target.find('#');
  if (heading != std::string::npos) target.resize(heading);
  target = trimNoteTitle(target);
  if (target.empty()) return {};
  std::replace(target.begin(), target.end(), '\\', '/');
  const bool rootOnly = !target.empty() && target.front() == '/';
  while (!target.empty() && target.front() == '/') target.erase(target.begin());
  target = stripMarkdownExtension(target);
  if (target.empty()) return {};

  const std::string sourceFolder = sourceFolderRelative(sourcePath);
  const std::string relativeCandidate = rootOnly ? std::string{} : normalizeRelativePath(sourceFolder, target);
  const std::string rootCandidate = normalizeRelativePath({}, target);

  const auto findPath = [this](const std::string& candidate) -> std::string {
    if (candidate.empty()) return {};
    const std::string key = normalizeCatalogKey(candidate);
    for (const auto& note : notes_) {
      if (normalizeCatalogKey(vaultRelativeStem(note.path)) == key) return note.path;
    }
    return {};
  };

  if (!relativeCandidate.empty()) {
    const std::string resolved = findPath(relativeCandidate);
    if (!resolved.empty()) return resolved;
  }
  if (!rootCandidate.empty() && normalizeCatalogKey(rootCandidate) != normalizeCatalogKey(relativeCandidate)) {
    const std::string resolved = findPath(rootCandidate);
    if (!resolved.empty()) return resolved;
  }

  if (rootOnly || target.find('/') != std::string::npos) return {};

  const std::string targetKey = normalizeCatalogKey(target);
  std::string resolved;
  for (const auto& note : notes_) {
    bool matches = normalizeCatalogKey(note.title) == targetKey;
    if (!matches) {
      for (const auto& alias : note.aliases) {
        if (normalizeCatalogKey(alias) == targetKey) {
          matches = true;
          break;
        }
      }
    }
    if (!matches) continue;
    if (!resolved.empty() && resolved != note.path) return {};
    resolved = note.path;
  }
  return resolved;
}

std::vector<std::string> MarkdownCatalog::notesForTag(const std::string_view tag, const size_t maxResults) const {
  std::vector<std::string> results;
  results.reserve(std::min(maxResults, notes_.size()));
  for (const auto& note : notes_) {
    if (!noteHasTag(note, tag)) continue;
    if (results.size() >= maxResults) break;
    results.push_back(note.path);
  }
  return results;
}

bool catalogLinkTargetsPath(const MarkdownCatalog& catalog, const std::string_view sourcePath,
                            const MarkdownIndexLink& link, const std::string_view targetPath) {
  if (link.target.empty()) return false;
  return catalog.resolveTarget(sourcePath, link.target) == targetPath;
}

}  // namespace micromarkd
