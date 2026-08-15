#pragma once

#ifdef MICROMARKD_APP

#include "activities/UiListActivity.h"

class MicroMarkDActivity final : public UiListActivity {
 public:
  static constexpr int MENU_ITEM_COUNT = 5;

  explicit MicroMarkDActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  bool isHomeActivity() const override { return true; }

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  freeink::ui::ListItem rowItems_[MENU_ITEM_COUNT]{};
};

#endif  // MICROMARKD_APP
