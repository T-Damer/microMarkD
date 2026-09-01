#pragma once

#ifdef MICROMARKD_APP

#include <MarkdownCatalog.h>

#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "activities/micromarkd/MarkdownVaultIndexer.h"
#include "components/OptionPopup.h"

class MarkdownTagsActivity final : public UiListActivity {
 public:
  MarkdownTagsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class ViewMode : uint8_t { Indexing, Tags, Notes };

  micromarkd::MarkdownCatalog catalog_;
  bool cachedIndexPartial_ = false;
  MarkdownVaultIndexer indexer_;
  ViewMode mode_ = ViewMode::Indexing;
  std::string selectedTag_;
  std::string header_;
  std::string emptyMessage_;
  std::vector<std::string> notePaths_;
  std::vector<std::string> rowLabels_;
  std::vector<std::string> rowSubtitles_;
  std::vector<std::string> rowValues_;
  std::vector<freeink::ui::ListItem> rowItems_;
  OptionPopup popup_;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  void onBackButton() override;
  const char* headerTitle() const override;
  void drawFooter() override;

  void beginIndexing();
  void advanceIndexing();
  void finishIndexing();
  void showTags();
  void showNotesForTag(const std::string& tag);
  void rebuildTagRows();
  void rebuildNoteRows();
  void showNoteActions(int index);
  void editNote(const std::string& path);
  void updateIndexProgress();
};

#endif  // MICROMARKD_APP
