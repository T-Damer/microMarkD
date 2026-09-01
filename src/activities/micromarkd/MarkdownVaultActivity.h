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
  static constexpr size_t MAX_ENTRY_NAME_BYTES = 96;
  static constexpr size_t MAX_RENAME_DIRECTORIES = 512;
  static constexpr size_t MAX_RENAME_NOTES = 1024;
  static constexpr int ACTION_ROW_COUNT = 4;
  static constexpr freeink::ui::ActionId ACTION_HOME = ACTION_USER;
  static constexpr freeink::ui::ActionId ACTION_NEW_NOTE = ACTION_USER + 1;
  static constexpr freeink::ui::ActionId ACTION_NEW_FOLDER = ACTION_USER + 2;

  std::string path_;
  std::string header_;
  std::string status_;
  std::vector<std::string> entries_;
  std::vector<std::string> rowNames_;
  std::vector<std::string> rowExtensions_;
  std::vector<freeink::ui::ListItem> rowItems_;
  std::array<char, NAME_BUFFER_SIZE> nameBuffer_{};
  OptionPopup popup_;
  bool loadFailed_ = false;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  static void toolbarActionTrampoline(const freeink::ui::ActionEvent& event, void* user);
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  void navigateButtons() override;
  void onBackButton() override;
  const char* headerTitle() const override;
  void drawFooter() override;

  bool normalisePath();
  void loadEntries();
  void rebuildRows();
  void selectEntry(const std::string& entry);
  void setStatus(std::string message);

  void startNewNoteHere();
  void openNewNoteEditor(const std::string& rawTitle);
  void startNewFolder();
  void createFolder(const std::string& rawName);
  std::string uniqueNotePath(const std::string& filename) const;
  std::string uniqueFolderPath(const std::string& component) const;

  void openDirectory(const std::string& entry);
  void openNote(const std::string& fullPath);
  void openReader(const std::string& fullPath);
  void editNote(const std::string& fullPath);
  void showNoteActions(const std::string& entry, const std::string& fullPath);
  void showFolderActions(const std::string& entry, const std::string& fullPath);
  void startRenameNote(const std::string& entry, const std::string& fullPath);
  void renameNote(const std::string& oldPath, const std::string& rawName);
  void startRenameFolder(const std::string& entry, const std::string& fullPath);
  void renameFolder(const std::string& oldPath, const std::string& rawName);
  void migrateNoteState(const std::string& oldPath, const std::string& newPath);
  bool collectMarkdownFiles(const std::string& rootPath, std::vector<std::string>& files);
  void confirmDelete(const std::string& entry, const std::string& fullPath);
  void navigateToParent();

  std::string fullPath(const std::string& entry) const;
  static std::string displayName(const std::string& entry);
  static std::string displayExtension(const std::string& entry);
};

#endif  // MICROMARKD_APP
