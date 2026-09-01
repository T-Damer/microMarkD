#pragma once

#ifdef MICROMARKD_APP

#include <string>

#include "activities/UiListActivity.h"

class MicroMarkDActivity final : public UiListActivity {
 public:
  static constexpr int MENU_ITEM_COUNT = 7;

  explicit MicroMarkDActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  bool isHomeActivity() const override { return true; }

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  void startNewNote();
  void startSearch();
  void openNewNoteEditor(const std::string& title);
  std::string uniqueNotePath(const std::string& filename) const;
  void showCreateError();
  void recoverInterruptedSaves();

  freeink::ui::ListItem rowItems_[MENU_ITEM_COUNT]{};
  std::string vaultStatus_;
};

#endif  // MICROMARKD_APP
