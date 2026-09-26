#include "MarkdownCatalogStorage.h"

#ifdef MICROMARKD_APP

#include <HalStorage.h>
#include <Logging.h>
#include <MarkdownDocument.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
constexpr char MODULE[] = "MDC";
constexpr char INDEX_ROOT[] = "/.micromarkd/index";
constexpr char INDEX_PREFIX[] = "/.micromarkd/index/";
constexpr char GRAPH_NODES_PATH[] = "/.micromarkd/index/.graph.nodes";
constexpr char GRAPH_EDGES_PATH[] = "/.micromarkd/index/.graph.edges";
constexpr char GRAPH_READY_PATH[] = "/.micromarkd/index/.graph.ready";
constexpr char GRAPH_NODES_TEMPORARY_PATH[] = "/.micromarkd/index/.graph.nodes.tmp";
constexpr char GRAPH_EDGES_TEMPORARY_PATH[] = "/.micromarkd/index/.graph.edges.tmp";
constexpr char GRAPH_READY_TEMPORARY_PATH[] = "/.micromarkd/index/.graph.ready.tmp";
constexpr std::string_view GRAPH_READY_PREFIX = "MMDGRAPH\t1\t";
constexpr size_t NAME_BUFFER_SIZE = 96;
constexpr size_t MAX_INDEX_RECORD_BYTES = 64 * 1024;
constexpr size_t MAX_GRAPH_LINE_BYTES = 512;
constexpr size_t MAX_GRAPH_PAGE_EDGES = 128;

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
  uint64_t fingerprint = micromarkd::MARKDOWN_FINGERPRINT_SEED;
  std::array<uint8_t, 256> buffer{};
  while (source.available()) {
    const int bytesRead = source.read(buffer.data(), buffer.size());
    if (bytesRead <= 0) {
      source.close();
      return false;
    }
    fingerprint = micromarkd::updateMarkdownFingerprint(
        fingerprint, std::string_view(reinterpret_cast<const char*>(buffer.data()), static_cast<size_t>(bytesRead)));
  }
  source.close();
  return size == record.sourceSize && fingerprint == record.sourceFingerprint;
}

bool ensureIndexRoot() {
  if (!Storage.exists(INDEX_ROOT)) return Storage.mkdir(INDEX_ROOT, true);

  auto directory = Storage.open(INDEX_ROOT);
  const bool valid = directory && directory.isDirectory();
  if (directory) directory.close();
  return valid;
}

bool removeIfPresent(const char* path) { return !Storage.exists(path) || Storage.remove(path); }

bool writeGraphLine(HalFile& file, const std::string_view line) {
  return file.write(line.data(), line.size()) == line.size();
}

bool readGraphLine(HalFile& file, std::string& line) {
  line.clear();
  line.reserve(96);
  bool readAny = false;
  while (file.available()) {
    const int value = file.read();
    if (value < 0) return false;
    readAny = true;
    if (value == '\n') return !line.empty();
    if (value == '\r') continue;
    if (line.size() >= MAX_GRAPH_LINE_BYTES) return false;
    line.push_back(static_cast<char>(value));
  }
  return readAny && !line.empty();
}

bool readGraphReadyCounts(size_t& nodeCount, size_t& edgeCount) {
  nodeCount = 0;
  edgeCount = 0;
  HalFile file;
  if (!Storage.openFileForRead(MODULE, GRAPH_READY_PATH, file)) return false;
  std::string line;
  const bool read = readGraphLine(file, line);
  file.close();
  if (!read || line.rfind(GRAPH_READY_PREFIX, 0) != 0) return false;

  unsigned nodes = 0;
  unsigned edges = 0;
  if (std::sscanf(line.c_str() + GRAPH_READY_PREFIX.size(), "%u\t%u", &nodes, &edges) != 2) return false;
  nodeCount = nodes;
  edgeCount = edges;
  return true;
}

std::string stripGraphExtension(std::string value) {
  const auto endsWith = [&value](const std::string_view suffix) {
    if (value.size() < suffix.size()) return false;
    const size_t offset = value.size() - suffix.size();
    for (size_t index = 0; index < suffix.size(); index++) {
      if (std::tolower(static_cast<unsigned char>(value[offset + index])) !=
          std::tolower(static_cast<unsigned char>(suffix[index]))) {
        return false;
      }
    }
    return true;
  };
  if (endsWith(".markdown")) {
    value.resize(value.size() - 9);
  } else if (endsWith(".md")) {
    value.resize(value.size() - 3);
  }
  return value;
}

std::string graphRelativeStem(const std::string_view path) {
  constexpr std::string_view prefix = "/vault/";
  if (path.size() <= prefix.size() || path.substr(0, prefix.size()) != prefix) return {};
  return stripGraphExtension(std::string(path.substr(prefix.size())));
}

std::string graphSourceFolder(const std::string_view sourcePath) {
  std::string relative = graphRelativeStem(sourcePath);
  const size_t slash = relative.find_last_of('/');
  if (slash == std::string::npos) return {};
  relative.resize(slash + 1);
  return relative;
}

std::string normalizeGraphRelativePath(const std::string_view base, const std::string_view raw) {
  std::vector<std::string> parts;
  parts.reserve(12);
  const auto append = [&parts](const std::string_view path) {
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
  return stripGraphExtension(result);
}

struct GraphResolverNote {
  std::string path;
  std::string relativeKey;
  std::string titleKey;
  std::vector<std::string> aliasKeys;
};

std::string resolveGraphTarget(const std::string_view sourcePath, const std::string_view rawTarget,
                               const std::vector<GraphResolverNote>& notes) {
  std::string target = micromarkd::trimNoteTitle(rawTarget);
  if (target.empty()) return {};
  const size_t heading = target.find('#');
  if (heading != std::string::npos) target.resize(heading);
  target = micromarkd::trimNoteTitle(target);
  if (target.empty()) return {};
  std::replace(target.begin(), target.end(), '\\', '/');
  const bool rootOnly = !target.empty() && target.front() == '/';
  while (!target.empty() && target.front() == '/') target.erase(target.begin());
  target = stripGraphExtension(target);
  if (target.empty()) return {};

  const std::string sourceFolder = graphSourceFolder(sourcePath);
  const std::string relativeCandidate = rootOnly ? std::string{} : normalizeGraphRelativePath(sourceFolder, target);
  const std::string rootCandidate = normalizeGraphRelativePath({}, target);
  const auto findByRelativeKey = [&notes](const std::string& candidate) -> std::string {
    if (candidate.empty()) return {};
    const std::string key = micromarkd::normalizeCatalogKey(candidate);
    for (const auto& note : notes) {
      if (note.relativeKey == key) return note.path;
    }
    return {};
  };

  if (const std::string resolved = findByRelativeKey(relativeCandidate); !resolved.empty()) return resolved;
  if (!rootCandidate.empty() &&
      micromarkd::normalizeCatalogKey(rootCandidate) != micromarkd::normalizeCatalogKey(relativeCandidate)) {
    if (const std::string resolved = findByRelativeKey(rootCandidate); !resolved.empty()) return resolved;
  }
  if (rootOnly || target.find('/') != std::string::npos) return {};

  const std::string targetKey = micromarkd::normalizeCatalogKey(target);
  std::string resolved;
  for (const auto& note : notes) {
    bool matches = note.titleKey == targetKey;
    if (!matches) {
      matches = std::find(note.aliasKeys.begin(), note.aliasKeys.end(), targetKey) != note.aliasKeys.end();
    }
    if (!matches) continue;
    if (!resolved.empty() && resolved != note.path) return {};
    resolved = note.path;
  }
  return resolved;
}

size_t findGraphNote(const std::vector<GraphResolverNote>& notes, const std::string_view path) {
  for (size_t index = 0; index < notes.size(); index++) {
    if (notes[index].path == path) return index;
  }
  return notes.size();
}

bool readGraphNodeAt(const uint32_t index, std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead(MODULE, GRAPH_NODES_PATH, file)) return false;
  std::string line;
  for (uint32_t current = 0; current <= index; current++) {
    if (!readGraphLine(file, line)) {
      file.close();
      return false;
    }
  }
  file.close();
  path = std::move(line);
  return true;
}

bool parseGraphEdge(const std::string& line, uint32_t& from, uint32_t& to) {
  unsigned parsedFrom = 0;
  unsigned parsedTo = 0;
  if (std::sscanf(line.c_str(), "%u\t%u", &parsedFrom, &parsedTo) != 2) return false;
  from = parsedFrom;
  to = parsedTo;
  return true;
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

bool loadMarkdownIndexCacheRecord(const std::string& cachePath, micromarkd::MarkdownIndexRecord& record) {
  if (cachePath.rfind(INDEX_PREFIX, 0) != 0 || cachePath.find("..") != std::string::npos ||
      !hasIndexExtension(cachePath)) {
    return false;
  }

  std::string encoded;
  if (!readIndexFile(cachePath, encoded)) return false;

  micromarkd::MarkdownIndexRecord decoded;
  if (!micromarkd::decodeMarkdownIndexRecord(encoded, decoded)) return false;
  if (micromarkd::markdownIndexCachePath(decoded.path) != cachePath) return false;

  record = std::move(decoded);
  return true;
}

bool loadValidatedMarkdownIndexCache(const std::string& cachePath, micromarkd::MarkdownIndexRecord& record) {
  if (!loadMarkdownIndexCacheRecord(cachePath, record)) return false;
  return sourceMatchesRecord(record);
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

bool markdownGraphCacheReady() {
  size_t nodeCount = 0;
  size_t edgeCount = 0;
  return readGraphReadyCounts(nodeCount, edgeCount) && Storage.exists(GRAPH_NODES_PATH) &&
         Storage.exists(GRAPH_EDGES_PATH);
}

void invalidateMarkdownGraphCache() {
  removeIfPresent(GRAPH_NODES_PATH);
  removeIfPresent(GRAPH_EDGES_PATH);
  removeIfPresent(GRAPH_READY_PATH);
  removeIfPresent(GRAPH_NODES_TEMPORARY_PATH);
  removeIfPresent(GRAPH_EDGES_TEMPORARY_PATH);
  removeIfPresent(GRAPH_READY_TEMPORARY_PATH);
}

bool rebuildMarkdownGraphCache() {
  if (!ensureIndexRoot()) return false;
  invalidateMarkdownGraphCache();

  std::vector<std::string> cachePaths;
  bool truncated = false;
  if (!listMarkdownIndexCacheFiles(cachePaths, micromarkd::MAX_CATALOG_NOTES, truncated)) return false;

  // ponytail: retain only paths and aliases during the one-time SD cache build;
  // browsing afterwards loads one bounded page instead of the catalog.
  std::vector<GraphResolverNote> notes;
  notes.reserve(cachePaths.size());
  for (const auto& cachePath : cachePaths) {
    micromarkd::MarkdownIndexRecord record;
    if (!loadValidatedMarkdownIndexCache(cachePath, record)) continue;
    GraphResolverNote note;
    note.path = record.path;
    note.relativeKey = micromarkd::normalizeCatalogKey(graphRelativeStem(record.path));
    note.titleKey = micromarkd::normalizeCatalogKey(micromarkd::vaultNoteDisplayName(record.path));
    note.aliasKeys.reserve(record.metadata.aliases.size());
    for (const auto& alias : record.metadata.aliases) note.aliasKeys.push_back(micromarkd::normalizeCatalogKey(alias));
    notes.push_back(std::move(note));
  }
  std::sort(notes.begin(), notes.end(), [](const GraphResolverNote& left, const GraphResolverNote& right) {
    if (left.relativeKey != right.relativeKey) return left.relativeKey < right.relativeKey;
    return left.path < right.path;
  });

  HalFile nodesFile;
  if (!Storage.openFileForWrite(MODULE, GRAPH_NODES_TEMPORARY_PATH, nodesFile)) return false;
  bool success = true;
  for (const auto& note : notes) {
    if (!writeGraphLine(nodesFile, note.path) || !writeGraphLine(nodesFile, "\n")) {
      success = false;
      break;
    }
  }
  nodesFile.close();
  if (!success) {
    removeIfPresent(GRAPH_NODES_TEMPORARY_PATH);
    return false;
  }

  HalFile edgesFile;
  if (!Storage.openFileForWrite(MODULE, GRAPH_EDGES_TEMPORARY_PATH, edgesFile)) {
    removeIfPresent(GRAPH_NODES_TEMPORARY_PATH);
    return false;
  }
  size_t edgeCount = 0;
  std::vector<size_t> targets;
  targets.reserve(micromarkd::MAX_INDEX_LINKS);
  for (const auto& cachePath : cachePaths) {
    micromarkd::MarkdownIndexRecord record;
    if (!loadValidatedMarkdownIndexCache(cachePath, record)) continue;
    const size_t source = findGraphNote(notes, record.path);
    if (source >= notes.size()) continue;
    targets.clear();
    for (const auto& link : record.metadata.links) {
      const std::string targetPath = resolveGraphTarget(record.path, link.target, notes);
      if (targetPath.empty()) continue;
      const size_t target = findGraphNote(notes, targetPath);
      if (target >= notes.size() || target == source ||
          std::find(targets.begin(), targets.end(), target) != targets.end()) {
        continue;
      }
      targets.push_back(target);
    }
    for (const size_t target : targets) {
      char edgeLine[48];
      const int length = snprintf(edgeLine, sizeof(edgeLine), "%u\t%u\n", static_cast<unsigned>(source),
                                  static_cast<unsigned>(target));
      if (length <= 0 || !writeGraphLine(edgesFile, std::string_view(edgeLine, static_cast<size_t>(length)))) {
        success = false;
        break;
      }
      edgeCount++;
    }
    if (!success) break;
  }
  edgesFile.close();
  if (!success) {
    removeIfPresent(GRAPH_NODES_TEMPORARY_PATH);
    removeIfPresent(GRAPH_EDGES_TEMPORARY_PATH);
    return false;
  }

  HalFile readyFile;
  if (!Storage.openFileForWrite(MODULE, GRAPH_READY_TEMPORARY_PATH, readyFile)) {
    removeIfPresent(GRAPH_NODES_TEMPORARY_PATH);
    removeIfPresent(GRAPH_EDGES_TEMPORARY_PATH);
    return false;
  }
  char marker[64];
  const int markerLength = snprintf(marker, sizeof(marker), "MMDGRAPH\t1\t%u\t%u\n",
                                    static_cast<unsigned>(notes.size()), static_cast<unsigned>(edgeCount));
  success = markerLength > 0 && writeGraphLine(readyFile, std::string_view(marker, static_cast<size_t>(markerLength)));
  readyFile.close();
  if (!success || !Storage.rename(GRAPH_NODES_TEMPORARY_PATH, GRAPH_NODES_PATH) ||
      !Storage.rename(GRAPH_EDGES_TEMPORARY_PATH, GRAPH_EDGES_PATH) ||
      !Storage.rename(GRAPH_READY_TEMPORARY_PATH, GRAPH_READY_PATH)) {
    removeIfPresent(GRAPH_NODES_TEMPORARY_PATH);
    removeIfPresent(GRAPH_EDGES_TEMPORARY_PATH);
    removeIfPresent(GRAPH_READY_TEMPORARY_PATH);
    removeIfPresent(GRAPH_NODES_PATH);
    removeIfPresent(GRAPH_EDGES_PATH);
    return false;
  }
  return true;
}

bool loadMarkdownGraphCachePage(const size_t page, const size_t pageSize, const size_t maxNodes,
                                MarkdownGraphCachePage& result) {
  result = {};
  if (pageSize == 0 || maxNodes == 0) return false;

  size_t totalNodes = 0;
  size_t ignoredEdgeCount = 0;
  if (!readGraphReadyCounts(totalNodes, ignoredEdgeCount)) return false;
  result.totalNodes = totalNodes;
  const size_t first = page * pageSize;
  if (first >= totalNodes) return true;
  const size_t baseCount = std::min(pageSize, std::min(maxNodes, totalNodes - first));
  result.paths.reserve(maxNodes);
  result.globalIndices.reserve(maxNodes);
  result.degrees.reserve(maxNodes);
  result.edges.reserve(MAX_GRAPH_PAGE_EDGES);

  HalFile nodesFile;
  if (!Storage.openFileForRead(MODULE, GRAPH_NODES_PATH, nodesFile)) return false;
  std::string line;
  for (size_t index = 0; index < first; index++) {
    if (!readGraphLine(nodesFile, line)) {
      nodesFile.close();
      return false;
    }
  }
  for (size_t index = 0; index < baseCount; index++) {
    if (!readGraphLine(nodesFile, line) || !micromarkd::isVaultMarkdownPath(line)) {
      nodesFile.close();
      return false;
    }
    result.paths.push_back(line);
    result.globalIndices.push_back(static_cast<uint32_t>(first + index));
  }
  nodesFile.close();

  const auto localIndex = [&result](const uint32_t globalIndex) -> int {
    for (size_t index = 0; index < result.globalIndices.size(); index++) {
      if (result.globalIndices[index] == globalIndex) return static_cast<int>(index);
    }
    return -1;
  };
  const auto addNeighbor = [&result, &localIndex, maxNodes](const uint32_t globalIndex) {
    if (globalIndex >= result.totalNodes || result.globalIndices.size() >= maxNodes || localIndex(globalIndex) >= 0) {
      return;
    }
    result.globalIndices.push_back(globalIndex);
  };

  HalFile edgesFile;
  if (!Storage.openFileForRead(MODULE, GRAPH_EDGES_PATH, edgesFile)) return false;
  while (readGraphLine(edgesFile, line)) {
    uint32_t from = 0;
    uint32_t to = 0;
    if (!parseGraphEdge(line, from, to)) continue;
    const bool fromInPage = from >= first && from < first + baseCount;
    const bool toInPage = to >= first && to < first + baseCount;
    if (fromInPage) addNeighbor(to);
    if (toInPage) addNeighbor(from);
  }
  edgesFile.close();

  for (size_t index = baseCount; index < result.globalIndices.size(); index++) {
    std::string path;
    if (!readGraphNodeAt(result.globalIndices[index], path) || !micromarkd::isVaultMarkdownPath(path)) return false;
    result.paths.push_back(std::move(path));
  }
  result.degrees.assign(result.globalIndices.size(), 0);

  if (!Storage.openFileForRead(MODULE, GRAPH_EDGES_PATH, edgesFile)) return false;
  while (readGraphLine(edgesFile, line)) {
    uint32_t from = 0;
    uint32_t to = 0;
    if (!parseGraphEdge(line, from, to)) continue;
    const int localFrom = localIndex(from);
    const int localTo = localIndex(to);
    if (localFrom >= 0 && result.degrees[static_cast<size_t>(localFrom)] < UINT16_MAX) {
      result.degrees[static_cast<size_t>(localFrom)]++;
    }
    if (localTo >= 0 && localTo != localFrom && result.degrees[static_cast<size_t>(localTo)] < UINT16_MAX) {
      result.degrees[static_cast<size_t>(localTo)]++;
    }
    if (localFrom < 0 || localTo < 0 || localFrom == localTo || result.edges.size() >= MAX_GRAPH_PAGE_EDGES) continue;
    const uint8_t edgeFrom = static_cast<uint8_t>(localFrom);
    const uint8_t edgeTo = static_cast<uint8_t>(localTo);
    const bool duplicate = std::any_of(result.edges.begin(), result.edges.end(), [edgeFrom, edgeTo](const auto& edge) {
      return (edge.from == edgeFrom && edge.to == edgeTo) || (edge.from == edgeTo && edge.to == edgeFrom);
    });
    if (!duplicate) result.edges.push_back({edgeFrom, edgeTo});
  }
  edgesFile.close();
  return result.paths.size() == result.globalIndices.size();
}

#endif  // MICROMARKD_APP
