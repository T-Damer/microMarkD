#pragma once

#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

class FileBrowserActivity final : public UiListActivity {
 public:
  // Books = standard reader browser; PickFirmware = filter to .bin only and return path via ActivityResult.
  enum class Mode { Books, PickFirmware };

 private:
  // Deletion
  bool removeDirFile(const std::string& fullPath);

  bool lockLongPressBack = false;
  // True when this activity was entered while Confirm was already held; we must swallow the next
  // release so we don't immediately auto-open the first entry.
  bool lockNextConfirmRelease = false;

  Mode mode = Mode::Books;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;
  std::unique_ptr<char[]> fileNameBuffer;

  int listCount() const override { return static_cast<int>(files.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  // Long-press BACK goes to root; short Back goes up a directory (home/cancel at
  // root), and Confirm activates on RELEASE (a hold is "delete").
  bool handleCustomInput() override;
  bool handleButtons() override;
  // Header shows the current folder name (battery indicator via GUI.drawHeader);
  // footer labels depend on path depth and picker mode.
  void drawChrome() override;
  void drawFooter() override;
  // forceDelete routes the touch long-press to the delete branch; button
  // navigation leaves it false and relies on getHeldTime() instead.
  // fromButton: only the physical Confirm path may consume the
  // lockNextConfirmRelease swallow — a touch tap has no pending release.
  void activateSelected(bool forceDelete = false, bool fromButton = false);

  // Data loading
  void loadFiles();
  size_t findEntry(const std::string& name) const;

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               Mode mode = Mode::Books);
  void onEnter() override;
  void onExit() override;
};
