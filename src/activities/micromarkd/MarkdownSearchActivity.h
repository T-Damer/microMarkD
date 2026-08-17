#pragma once

#ifdef MICROMARKD_APP

#include <cstddef>
#include <string>
#include <vector>

#include <MarkdownSearch.h>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class MarkdownSearchActivity final : public UiListActivity {
 public:
  MarkdownSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string query);

  void onEnter() override;
  void onExit() override;
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

  static constexpr size_t READ_CHUNK_BYTES = 1024;
  static constexpr size_t MAX_DIRECTORIES = 512;
  static constexpr size_t MAX_FILES = 256;
  static constexpr size_t MAX_RESULTS = 100;
  static constexpr size_t MAX_BYTES_PER_NOTE = 256 * 1024;
  static constexpr size_t MAX_TOTAL_BYTES = 4 * 1024 * 1024;
  static constexpr size_t MAX_SNIPPET_BYTES = 170;

  std::string queryText_;
  micromarkd::SearchQuery query_;
  std::string header_;
  std::string emptyMessage_;
  std::vector<SearchResult> results_;
  std::vector<freeink::ui::ListItem> rowItems_;
  OptionPopup popup_;
  SearchReport report_;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  const char* headerTitle() const override;
  void drawFooter() override;

  void runSearch();
  bool scanNote(const std::string& path, SearchResult& result);
  void rebuildRows();
  void showResultActions(int index);
  void editNote(const std::string& path);
  void buildEmptyMessage();

  static std::string joinPath(const std::string& directory, const std::string& name);
  static std::string overlapTail(const std::string& text, size_t bytes);
};

#endif  // MICROMARKD_APP
