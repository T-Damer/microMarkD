#pragma once

#ifdef MICROMARKD_APP

#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class MarkdownRecentActivity final : public UiListActivity {
 public:
  explicit MarkdownRecentActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;

 private:
  std::vector<RecentBook> notes_;
  std::vector<std::string> titles_;
  std::vector<std::string> folders_;
  std::vector<freeink::ui::ListItem> rowItems_;
  OptionPopup popup_;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  const char* headerTitle() const override;
  void drawFooter() override;

  void loadNotes();
  void rebuildRows();
  void showNoteActions(int index);
  void editNote(const std::string& path);
  void promptRemove(const std::string& path, const std::string& title);
};

#endif  // MICROMARKD_APP
