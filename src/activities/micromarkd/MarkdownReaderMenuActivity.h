#pragma once

#ifdef MICROMARKD_APP

#include <array>
#include <string>

#include "activities/UiListActivity.h"

class MarkdownReaderMenuActivity final : public UiListActivity {
 public:
  MarkdownReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string notePath);

 private:
  enum MenuIndex { EDIT_NOTE, TEXT_SETTINGS, BACKLINKS, GRAPH, MENU_ITEM_COUNT };

  std::string notePath_;
  std::array<freeink::ui::ListItem, MENU_ITEM_COUNT> items_{};

  int listCount() const override { return MENU_ITEM_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
};

#endif  // MICROMARKD_APP
