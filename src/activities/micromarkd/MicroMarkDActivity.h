#pragma once

#ifdef MICROMARKD_APP

#include <string>

#include "activities/UiListActivity.h"
#include "activities/micromarkd/WeatherWidget.h"
#include "components/OptionPopup.h"

class MicroMarkDActivity final : public UiListActivity {
 public:
  static constexpr int MENU_ITEM_COUNT = 8;

  explicit MicroMarkDActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  const char* headerTitle() const override;

  void startNewNote();
  void startSearch();
  void openNewNoteEditor(const std::string& title);
  std::string uniqueNotePath(const std::string& filename) const;
  void showCreateError();
  void recoverInterruptedSaves();
  void updateWidget();
  void showWidgetSettings();
  void promptManualLocation();
  static void widgetSettingsTrampoline(const freeink::ui::ActionEvent& event, void* user);

  freeink::ui::ListItem rowItems_[MENU_ITEM_COUNT]{};
  std::string vaultStatus_;
  std::string widgetLabel_;
  std::string widgetSubtitle_;
  std::string recentBookPath_;
  WeatherWidget weather_;
  OptionPopup popup_;
  bool showWeather_ = true;
};

#endif  // MICROMARKD_APP
