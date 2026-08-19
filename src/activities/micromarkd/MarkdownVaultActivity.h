#pragma once

#ifdef MICROMARKD_APP

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class MarkdownVaultActivity final : public UiListActivity {
 public:
  explicit MarkdownVaultActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 std::string initialPath = "/vault");

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;

 private:
  static constexpr size_t NAME_BUFFER_SIZE = 500;
  static constexpr int ACTION_ROW_COUNT = 2;

  std::string path_;
  std::string header_;
  std::vector<std::string> entries_;
  std::vector<std::string> rowNames_;
  std::vector<std::string> rowExtensions_;
  std::vector<freeink::ui::ListItem> rowItems_;
  std::array<char, NAME_BUFFER_SIZE> nameBuffer_{};
  OptionPopup popup_;
  bool loadFailed_ = false;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  void onBackButton() override;
  const char* headerTitle() const override;
  void drawFooter() override;

  bool normalisePath();
  void loadEntries();
  void rebuildRows();
  void startNewNoteHere();
  void openNewNoteEditor(const std::string& rawTitle);
  void startNewFolder();
  void createFolder(const std::string& rawName);
  std::string uniqueNotePath(const std::string& filename) const;
  std::string uniqueFolderPath(const std::string& component) const;
  void openDirectory(const std::string& entry);
  void openNote(const std::string& fullPath);
  void editNote(const std::string& fullPath);
  void showNoteActions(const std::string& entry, const std::string& fullPath);
  void confirmDelete(const std::string& entry, const std::string& fullPath);
  std::string fullPath(const std::string& entry) const;
  static std::string displayName(const std::string& entry);
  static std::string displayExtension(const std::string& entry);
};

#endif  // MICROMARKD_APP
