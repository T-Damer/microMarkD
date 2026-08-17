#include "MarkdownRecovery.h"

#ifdef MICROMARKD_APP

#include <HalStorage.h>
#include <Logging.h>
#include <MarkdownRecoveryPlan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "util/BookCacheUtils.h"

namespace {
constexpr char MODULE[] = "MDR";
constexpr size_t NAME_BUFFER_SIZE = 500;
constexpr size_t MAX_DIRECTORIES = 512;

std::string joinPath(const std::string& directory, const std::string& name) {
  return directory + (directory.back() == '/' ? "" : "/") + name;
}

bool removeSidecar(const std::string& path, MarkdownRecoveryReport& report) {
  if (!Storage.exists(path.c_str())) return true;
  if (Storage.remove(path.c_str())) {
    report.removedSidecars++;
    return true;
  }

  LOG_ERR(MODULE, "Failed to remove stale save sidecar: %s", path.c_str());
  report.failures++;
  return false;
}

enum class ReadyMarkerState : uint8_t { Missing, Valid, Invalid, Unreadable };

ReadyMarkerState readReadyMarker(const std::string& path, const bool exists) {
  if (!exists) return ReadyMarkerState::Missing;

  HalFile file;
  if (!Storage.openFileForRead(MODULE, path, file)) return ReadyMarkerState::Unreadable;
  if (file.fileSize64() != micromarkd::NOTE_READY_MAGIC_BYTES) {
    file.close();
    return ReadyMarkerState::Invalid;
  }

  std::array<char, micromarkd::NOTE_READY_MAGIC_BYTES> marker{};
  const bool valid = file.read(marker.data(), marker.size()) == static_cast<int>(marker.size()) &&
                     std::memcmp(marker.data(), micromarkd::NOTE_READY_MAGIC, marker.size()) == 0;
  file.close();
  return valid ? ReadyMarkerState::Valid : ReadyMarkerState::Invalid;
}

void recoverNote(const std::string& canonicalPath, MarkdownRecoveryReport& report) {
  const std::string temporaryPath = canonicalPath + micromarkd::NOTE_TEMPORARY_SUFFIX;
  const std::string backupPath = canonicalPath + micromarkd::NOTE_BACKUP_SUFFIX;
  const std::string readyPath = canonicalPath + micromarkd::NOTE_READY_SUFFIX;
  const bool canonicalExists = Storage.exists(canonicalPath.c_str());
  const bool temporaryExists = Storage.exists(temporaryPath.c_str());
  const bool backupExists = Storage.exists(backupPath.c_str());
  const bool readyExists = Storage.exists(readyPath.c_str());
  ReadyMarkerState markerState = ReadyMarkerState::Missing;
  if (!canonicalExists && temporaryExists) {
    markerState = readReadyMarker(readyPath, readyExists);
    if (markerState == ReadyMarkerState::Unreadable) {
      LOG_ERR(MODULE, "Failed to read temporary-note completion marker: %s", readyPath.c_str());
      report.failures++;
      return;
    }
  }
  const bool temporaryComplete = markerState == ReadyMarkerState::Valid;
  const auto plan =
      micromarkd::planNoteRecovery(canonicalExists, temporaryExists, temporaryComplete, backupExists);

  switch (plan.source) {
    case micromarkd::NoteRecoverySource::Canonical:
      if (plan.removeTemporary) removeSidecar(temporaryPath, report);
      if (plan.removeBackup) removeSidecar(backupPath, report);
      if (readyExists) removeSidecar(readyPath, report);
      return;

    case micromarkd::NoteRecoverySource::Temporary:
      if (!Storage.rename(temporaryPath.c_str(), canonicalPath.c_str())) {
        LOG_ERR(MODULE, "Failed to promote temporary note: %s", temporaryPath.c_str());
        report.failures++;
        if (backupExists && Storage.rename(backupPath.c_str(), canonicalPath.c_str())) {
          report.restoredBackup++;
          clearBookCache(canonicalPath);
          removeSidecar(temporaryPath, report);
          removeSidecar(readyPath, report);
          LOG_INF(MODULE, "Restored backup after temporary promotion failed: %s", canonicalPath.c_str());
        }
        return;
      }

      report.promotedTemporary++;
      clearBookCache(canonicalPath);
      if (plan.removeBackup) removeSidecar(backupPath, report);
      if (readyExists) removeSidecar(readyPath, report);
      LOG_INF(MODULE, "Recovered completed temporary note: %s", canonicalPath.c_str());
      return;

    case micromarkd::NoteRecoverySource::Backup:
      if (!Storage.rename(backupPath.c_str(), canonicalPath.c_str())) {
        LOG_ERR(MODULE, "Failed to restore note backup: %s", backupPath.c_str());
        report.failures++;
        return;
      }

      report.restoredBackup++;
      clearBookCache(canonicalPath);
      if (temporaryExists) {
        if (!temporaryComplete) report.discardedIncompleteTemporary++;
        removeSidecar(temporaryPath, report);
      }
      if (readyExists) removeSidecar(readyPath, report);
      LOG_INF(MODULE, "Restored note backup: %s", canonicalPath.c_str());
      return;

    case micromarkd::NoteRecoverySource::None:
      if (temporaryExists) {
        if (!temporaryComplete) report.discardedIncompleteTemporary++;
        removeSidecar(temporaryPath, report);
      }
      if (readyExists) removeSidecar(readyPath, report);
      return;
  }
}

}  // namespace

bool removeMarkdownRecoverySidecars(const std::string& canonicalPath) {
  MarkdownRecoveryReport report;
  removeSidecar(canonicalPath + micromarkd::NOTE_TEMPORARY_SUFFIX, report);
  removeSidecar(canonicalPath + micromarkd::NOTE_BACKUP_SUFFIX, report);
  removeSidecar(canonicalPath + micromarkd::NOTE_READY_SUFFIX, report);
  return report.failures == 0;
}

MarkdownRecoveryReport recoverMarkdownVault(const std::string& rootPath) {
  MarkdownRecoveryReport report;
  if (!Storage.exists(rootPath.c_str())) {
    if (!Storage.mkdir(rootPath.c_str(), true)) {
      LOG_ERR(MODULE, "Failed to create vault before recovery: %s", rootPath.c_str());
      report.failures++;
    }
    return report;
  }

  auto root = Storage.open(rootPath.c_str());
  if (!root || !root.isDirectory()) {
    LOG_ERR(MODULE, "Vault recovery root is not a directory: %s", rootPath.c_str());
    report.failures++;
    return report;
  }
  root.close();

  std::vector<std::string> directories;
  directories.reserve(32);
  directories.push_back(rootPath);
  std::array<char, NAME_BUFFER_SIZE> nameBuffer{};

  for (size_t directoryIndex = 0; directoryIndex < directories.size(); directoryIndex++) {
    const std::string directoryPath = directories[directoryIndex];
    auto directory = Storage.open(directoryPath.c_str());
    if (!directory || !directory.isDirectory()) {
      LOG_ERR(MODULE, "Failed to scan vault directory: %s", directoryPath.c_str());
      report.failures++;
      continue;
    }

    report.directoriesScanned++;
    std::vector<std::string> recoveryCandidates;
    directory.rewindDirectory();

    for (auto entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
      entry.getName(nameBuffer.data(), nameBuffer.size());
      const bool isDirectory = entry.isDirectory();
      entry.close();

      if (nameBuffer[0] == '.' || std::strcmp(nameBuffer.data(), "System Volume Information") == 0) continue;

      if (isDirectory) {
        if (directories.size() >= MAX_DIRECTORIES) {
          report.directoriesSkipped++;
          continue;
        }
        directories.push_back(joinPath(directoryPath, nameBuffer.data()));
        continue;
      }

      const std::string canonicalName = micromarkd::recoveryCanonicalPath(nameBuffer.data());
      if (!canonicalName.empty()) recoveryCandidates.push_back(joinPath(directoryPath, canonicalName));
    }
    directory.close();

    std::sort(recoveryCandidates.begin(), recoveryCandidates.end());
    recoveryCandidates.erase(std::unique(recoveryCandidates.begin(), recoveryCandidates.end()),
                             recoveryCandidates.end());
    for (const auto& canonicalPath : recoveryCandidates) recoverNote(canonicalPath, report);
  }

  if (report.directoriesSkipped > 0) {
    LOG_ERR(MODULE, "Vault recovery skipped %u directories after reaching its scan limit",
            static_cast<unsigned>(report.directoriesSkipped));
  }
  return report;
}

#endif  // MICROMARKD_APP
