#pragma once
#include <string>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// Reader status bar configuration activity
class StatusBarSettingsActivity final : public UiListActivity {
 public:
  explicit StatusBarSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  OptionPopup optionPopup;

  // Decided in onEnter() based on halClock.isAvailable() so clock entries are hidden on X4.
  int visibleItemCount = 0;

  int listCount() const override { return visibleItemCount; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;

  std::string rowValueText(int index);

  void handleSelection();
};
