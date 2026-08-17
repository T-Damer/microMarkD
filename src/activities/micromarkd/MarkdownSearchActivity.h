#pragma once

#ifdef MICROMARKD_APP

#include <HalStorage.h>
#include <MarkdownSearch.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class MarkdownSearchActivity final : public UiListActivity {
 public:
  MarkdownSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string query);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct SearchResult {
    std::string path;
    std::string title;
    std::string subtitle;
    uint8_t rank = 1;
  };

  struct SearchReport {
    size_t directoriesScanned = 0;
    size_t directoriesSkipped = 0;
    size_t filesQueued = 0;
    size_t filesScanned = 0;
    size_t filesTruncated = 0;
    size_t bytesScanned = 0;
    size_t failures = 0;
    bool fileLimitReached = false;
    bool byteLimitReached = false;
    bool resultLimitReached = false;

    bool partial() const {
      return directoriesSkipped > 0 || filesTruncated > 0 || failures > 0 || fileLimitReached || byteLimitReached ||
             resultLimitReached;
    }
  };

  enum class SearchPhase : uint8_t { Idle, Enumerating, Scanning, Complete };

  static constexpr size_t READ_CHUNK_BYTES = 1024;
  static constexpr size_t READ_BYTES_PER_STEP = 4 * 1024;
  static constexpr size_t MAX_DIRECTORIES = 512;
  static constexpr size_t MAX_FILES = 256;
  static constexpr size_t MAX_RESULTS = 100;
  static constexpr size_t MAX_BYTES_PER_NOTE = 256 * 1024;
  static constexpr size_t MAX_TOTAL_BYTES = 4 * 1024 * 1024;
  static constexpr size_t MAX_SNIPPET_BYTES = 170;
  static constexpr size_t PROGRESS_UPDATE_INTERVAL = 8;

  std::string queryText_;
  micromarkd::SearchQuery query_;
  std::string header_;
  std::string emptyMessage_;
  std::vector<SearchResult> results_;
  std::vector<freeink::ui::ListItem> rowItems_;
  OptionPopup popup_;
  SearchReport report_;
  SearchPhase phase_ = SearchPhase::Idle;

  std::vector<std::string> directories_;
  size_t directoryIndex_ = 0;
  std::vector<std::string> notePaths_;
  size_t noteIndex_ = 0;

  HalFile activeFile_;
  std::string activePath_;
  std::string activeTitle_;
  std::string activeFolder_;
  std::string activeSnippet_;
  std::string activeOverlap_;
  micromarkd::SearchTermMatches activeMatches_{};
  size_t activeBytes_ = 0;
  bool activeMetadataMatch_ = false;
  bool activeFileOpen_ = false;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  const char* headerTitle() const override;
  void drawFooter() override;

  void beginSearch();
  void advanceSearch();
  void enumerateNextDirectory();
  void beginNextNote();
  void scanActiveNoteStep();
  void finishActiveNote();
  void finishSearch();
  void updateProgress(bool force = false);
  void rebuildRows();
  void showResultActions(int index);
  void editNote(const std::string& path);
  void buildEmptyMessage();
  void closeActiveFile();

  static std::string joinPath(const std::string& directory, const std::string& name);
  static std::string overlapTail(const std::string& text, size_t bytes);
};

#endif  // MICROMARKD_APP
