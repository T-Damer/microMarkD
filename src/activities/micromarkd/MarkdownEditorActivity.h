#pragma once

#ifdef MICROMARKD_APP

#include <cstddef>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class MarkdownEditorActivity final : public UiListActivity {
 public:
  explicit MarkdownEditorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path);
  MarkdownEditorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path,
                         std::vector<std::string> initialLines, bool trailingNewline);
  ~MarkdownEditorActivity() override;

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  static constexpr size_t MAX_FILE_BYTES = 128 * 1024;
  static constexpr size_t MAX_LINE_BYTES = 1024;
  static constexpr size_t MAX_LINES = 1024;
  static constexpr size_t PREVIEW_BYTES = 180;

  std::string path_;
  std::string title_;
  std::vector<std::string> lines_;
  std::vector<std::string> rowLabels_;
  std::vector<std::string> rowValues_;
  std::vector<freeink::ui::ListItem> rowItems_;
  OptionPopup popup_;

  bool loadFromDisk_ = true;
  bool trailingNewline_ = false;
  bool dirty_ = false;
  bool loadFailed_ = false;
  bool saveFailed_ = false;
  std::string errorMessage_;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  void onBackButton() override;
  const char* headerTitle() const override;

  bool normalisePath();
  bool loadDocument();
  bool saveDocumentAtomic();
  bool writeTemporaryFile(const std::string& temporaryPath) const;
  bool writeReadyMarker(const std::string& readyPath) const;
  size_t documentSize() const;

  void rebuildRows();
  void editDocument();
  void editLine(size_t index);
  void insertLine(size_t index, bool editImmediately);
  void deleteLine(size_t index);
  void showLineActions(size_t index);
  void showUnsavedActions();
  void saveAndFinish();
  void discardAndFinish();
  void markChanged();

  static std::string filenameTitle(const std::string& path);
  static std::string linePreview(const std::string& line);
};

#endif  // MICROMARKD_APP
