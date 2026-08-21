#pragma once

#ifdef MICROMARKD_APP

#include <HalStorage.h>
#include <MarkdownCatalog.h>
#include <MarkdownIndex.h>
#include <MarkdownIndexStream.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class MarkdownVaultIndexer {
 public:
  struct Report {
    size_t directoriesScanned = 0;
    size_t directoriesSkipped = 0;
    size_t notesQueued = 0;
    size_t indexesReused = 0;
    size_t indexesBuilt = 0;
    size_t indexesWriteFailed = 0;
    size_t notesSkippedLarge = 0;
    size_t failures = 0;
    bool noteLimitReached = false;

    bool partial() const {
      return directoriesSkipped > 0 || indexesWriteFailed > 0 || notesSkippedLarge > 0 || failures > 0 ||
             noteLimitReached;
    }
  };

  enum class Phase : uint8_t { Idle, Enumerating, Indexing, Complete };

  ~MarkdownVaultIndexer();

  void begin(std::string rootPath = "/vault");
  void step(size_t byteBudget = 4 * 1024);

  Phase phase() const { return phase_; }
  bool complete() const { return phase_ == Phase::Complete; }
  bool hasRecord() const { return recordReady_; }
  micromarkd::MarkdownIndexRecord takeRecord();
  const Report& report() const { return report_; }
  size_t completedNotes() const { return noteIndex_; }
  size_t queuedNotes() const { return notePaths_.size(); }

 private:
  static constexpr size_t NAME_BUFFER_SIZE = 500;
  static constexpr size_t READ_CHUNK_BYTES = 1024;
  static constexpr size_t MAX_DIRECTORIES = 512;
  static constexpr size_t MAX_NOTES = micromarkd::MAX_CATALOG_NOTES;
  static constexpr size_t MAX_NOTE_BYTES = 4 * 1024 * 1024;

  std::string rootPath_;
  std::vector<std::string> directories_;
  size_t directoryIndex_ = 0;
  std::vector<std::string> notePaths_;
  size_t noteIndex_ = 0;
  Phase phase_ = Phase::Idle;
  Report report_;

  HalFile activeFile_;
  std::string activePath_;
  micromarkd::MarkdownIndexStreamBuilder activeBuilder_;
  size_t activeBytes_ = 0;
  bool activeFileOpen_ = false;

  micromarkd::MarkdownIndexRecord readyRecord_;
  bool recordReady_ = false;

  void enumerateNextDirectory();
  void indexStep(size_t byteBudget);
  bool beginNextNote();
  void finishActiveNote();
  void skipActiveNote(bool tooLarge);
  void closeActiveFile();
  void publishRecord(micromarkd::MarkdownIndexRecord record);

  static std::string joinPath(const std::string& directory, const std::string& name);
};

#endif  // MICROMARKD_APP
