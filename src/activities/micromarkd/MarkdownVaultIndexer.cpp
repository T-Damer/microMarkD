#include "MarkdownVaultIndexer.h"

#ifdef MICROMARKD_APP

#include <HalStorage.h>
#include <Logging.h>
#include <MarkdownDocument.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include "activities/micromarkd/MarkdownIndexStorage.h"

namespace {
constexpr char MODULE[] = "MDX";
}

MarkdownVaultIndexer::~MarkdownVaultIndexer() { closeActiveFile(); }

std::string MarkdownVaultIndexer::joinPath(const std::string& directory, const std::string& name) {
  return directory + (directory.back() == '/' ? "" : "/") + name;
}

void MarkdownVaultIndexer::begin(std::string rootPath) {
  closeActiveFile();
  rootPath_ = std::move(rootPath);
  directories_.clear();
  notePaths_.clear();
  directoryIndex_ = 0;
  noteIndex_ = 0;
  report_ = {};
  activePath_.clear();
  activeBuilder_ = micromarkd::MarkdownIndexStreamBuilder{};
  activeBytes_ = 0;
  readyRecord_ = {};
  recordReady_ = false;

  if (!Storage.exists(rootPath_.c_str())) {
    phase_ = Phase::Complete;
    return;
  }

  directories_.reserve(32);
  notePaths_.reserve(64);
  directories_.push_back(rootPath_);
  phase_ = Phase::Enumerating;
}

void MarkdownVaultIndexer::step(const size_t byteBudget) {
  if (recordReady_ || phase_ == Phase::Idle || phase_ == Phase::Complete) return;

  if (phase_ == Phase::Enumerating) {
    enumerateNextDirectory();
  } else if (phase_ == Phase::Indexing) {
    indexStep(std::max<size_t>(1, byteBudget));
  }
}

micromarkd::MarkdownIndexRecord MarkdownVaultIndexer::takeRecord() {
  if (!recordReady_) return {};
  recordReady_ = false;
  return std::move(readyRecord_);
}

void MarkdownVaultIndexer::enumerateNextDirectory() {
  if (directoryIndex_ >= directories_.size() || report_.noteLimitReached) {
    phase_ = notePaths_.empty() ? Phase::Complete : Phase::Indexing;
    return;
  }

  const std::string directoryPath = directories_[directoryIndex_++];
  auto directory = Storage.open(directoryPath.c_str());
  if (!directory || !directory.isDirectory()) {
    LOG_ERR(MODULE, "Failed to scan vault directory: %s", directoryPath.c_str());
    report_.failures++;
    return;
  }

  std::array<char, NAME_BUFFER_SIZE> name{};
  report_.directoriesScanned++;
  directory.rewindDirectory();
  for (auto entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    entry.getName(name.data(), name.size());
    const bool isDirectory = entry.isDirectory();
    entry.close();

    if (name[0] == '.' || std::strcmp(name.data(), "System Volume Information") == 0) continue;

    if (isDirectory) {
      if (directories_.size() >= MAX_DIRECTORIES) {
        report_.directoriesSkipped++;
      } else {
        directories_.push_back(joinPath(directoryPath, name.data()));
      }
      continue;
    }

    const std::string path = joinPath(directoryPath, name.data());
    if (!micromarkd::isVaultMarkdownPath(path)) continue;
    if (notePaths_.size() >= MAX_NOTES) {
      report_.noteLimitReached = true;
      break;
    }
    notePaths_.push_back(path);
    report_.notesQueued++;
  }
  directory.close();

  if (directoryIndex_ >= directories_.size() || report_.noteLimitReached) {
    std::sort(notePaths_.begin(), notePaths_.end());
    notePaths_.erase(std::unique(notePaths_.begin(), notePaths_.end()), notePaths_.end());
    phase_ = notePaths_.empty() ? Phase::Complete : Phase::Indexing;
  }
}

bool MarkdownVaultIndexer::beginNextNote() {
  if (noteIndex_ >= notePaths_.size()) {
    phase_ = Phase::Complete;
    return false;
  }

  activePath_ = notePaths_[noteIndex_];
  auto source = Storage.open(activePath_.c_str());
  if (!source || source.isDirectory()) {
    if (source) source.close();
    LOG_ERR(MODULE, "Failed to inspect note for indexing: %s", activePath_.c_str());
    report_.failures++;
    noteIndex_++;
    activePath_.clear();
    return false;
  }
  const uint64_t sourceSize = source.fileSize64();
  source.close();

  if (sourceSize > MAX_NOTE_BYTES) {
    report_.notesSkippedLarge++;
    noteIndex_++;
    activePath_.clear();
    return false;
  }

  micromarkd::MarkdownIndexRecord cached;
  if (loadMarkdownIndexRecord(activePath_, sourceSize, cached)) {
    report_.indexesReused++;
    noteIndex_++;
    activePath_.clear();
    publishRecord(std::move(cached));
    return false;
  }

  if (!Storage.openFileForRead(MODULE, activePath_, activeFile_)) {
    LOG_ERR(MODULE, "Failed to open note for metadata indexing: %s", activePath_.c_str());
    report_.failures++;
    noteIndex_++;
    activePath_.clear();
    return false;
  }

  activeFileOpen_ = true;
  activeBuilder_ = micromarkd::MarkdownIndexStreamBuilder{};
  activeBytes_ = 0;
  return true;
}

void MarkdownVaultIndexer::indexStep(const size_t byteBudget) {
  if (!activeFileOpen_) {
    while (!recordReady_ && noteIndex_ < notePaths_.size() && !activeFileOpen_) beginNextNote();
    if (recordReady_ || phase_ == Phase::Complete) return;
    if (!activeFileOpen_ && noteIndex_ >= notePaths_.size()) {
      phase_ = Phase::Complete;
      return;
    }
  }

  std::array<uint8_t, READ_CHUNK_BYTES> buffer{};
  size_t stepBytes = 0;
  while (activeFileOpen_ && activeFile_.available() && stepBytes < byteBudget) {
    const size_t readLimit = std::min(buffer.size(), byteBudget - stepBytes);
    const int bytesRead = activeFile_.read(buffer.data(), readLimit);
    if (bytesRead < 0) {
      LOG_ERR(MODULE, "Failed while indexing note: %s", activePath_.c_str());
      skipActiveNote(false);
      return;
    }
    if (bytesRead == 0) break;

    const size_t readSize = static_cast<size_t>(bytesRead);
    activeBuilder_.addBytes(
        std::string_view(reinterpret_cast<const char*>(buffer.data()), static_cast<size_t>(bytesRead)));
    activeBytes_ += readSize;
    stepBytes += readSize;
    if (activeBytes_ > MAX_NOTE_BYTES) {
      skipActiveNote(true);
      return;
    }
  }

  if (activeFileOpen_ && !activeFile_.available()) finishActiveNote();
}

void MarkdownVaultIndexer::finishActiveNote() {
  closeActiveFile();
  auto record = activeBuilder_.finish(activePath_);
  if (!writeMarkdownIndexRecord(record)) {
    report_.indexesWriteFailed++;
  } else {
    report_.indexesBuilt++;
  }
  noteIndex_++;
  activePath_.clear();
  activeBytes_ = 0;
  publishRecord(std::move(record));
  if (noteIndex_ >= notePaths_.size()) phase_ = Phase::Complete;
}

void MarkdownVaultIndexer::skipActiveNote(const bool tooLarge) {
  closeActiveFile();
  if (tooLarge) {
    report_.notesSkippedLarge++;
  } else {
    report_.failures++;
  }
  noteIndex_++;
  activePath_.clear();
  activeBytes_ = 0;
  if (noteIndex_ >= notePaths_.size()) phase_ = Phase::Complete;
}

void MarkdownVaultIndexer::closeActiveFile() {
  if (!activeFileOpen_) return;
  activeFile_.close();
  activeFileOpen_ = false;
}

void MarkdownVaultIndexer::publishRecord(micromarkd::MarkdownIndexRecord record) {
  readyRecord_ = std::move(record);
  recordReady_ = !readyRecord_.path.empty();
}

#endif  // MICROMARKD_APP
