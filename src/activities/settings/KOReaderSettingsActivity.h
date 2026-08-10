#pragma once

#include "activities/UiListActivity.h"

/**
 * Submenu for KOReader Sync settings.
 * Shows username, password, and authenticate options.
 */
class KOReaderSettingsActivity final : public UiListActivity {
 public:
  explicit KOReaderSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
};
