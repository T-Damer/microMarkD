#pragma once

#ifdef MICROMARKD_APP

#include <MarkdownCatalog.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "activities/micromarkd/MarkdownVaultIndexer.h"
#include "components/OptionPopup.h"

class MarkdownBacklinksActivity final : public UiListActivity {
 public:
  MarkdownBacklinksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string targetPath);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Phase : uint8_t { IndexingVault, ScanningLinks, Complete };

  static constexpr size_t MAX_CACHE_RECORDS = micromarkd::MAX_CATALOG_NOTES;
  static constexpr size_t RECORDS_PER_STEP = 4;
  static constexpr size_t MAX_RESULTS = 256;

  std::string targetPath_;
  std::string header_;
  std::string emptyMessage_;
  MarkdownVaultIndexer indexer_;
  micromarkd::MarkdownCatalog catalog_;
  bool cachedIndexPartial_ = false;
  std::vector<std::string> cachePaths_;
  size_t cacheIndex_ = 0;
  Phase phase_ = Phase::Complete;
  bool partial_ = false;

  std::vector<std::string> resultPaths_;
  std::vector<std::string> rowLabels_;
  std::vector<std::string> rowSubtitles_;
  std::vector<freeink::ui::ListItem> rowItems_;
  OptionPopup popup_;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  const char* headerTitle() const override;
  void drawFooter() override;

  void beginScan();
  void advanceScan();
  void indexVaultStep();
  void beginLinkScan();
  void scanBacklinksStep();
  void finishScan();
  void rebuildRows();
  void updateProgress();
  void showNoteActions(int index);
  void editNote(const std::string& path);
};

#endif  // MICROMARKD_APP
