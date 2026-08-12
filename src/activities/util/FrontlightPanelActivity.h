#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Top-anchored frontlight overlay opened by a top-edge down-swipe. It drives
// brightness and warmth live, and includes only a sun on/off control; Night
// Mode deliberately lives in the reader menu instead.
class FrontlightPanelActivity final : public Activity {
  using UiApp = freeink::ui::FreeInkApp<10, 5>;

  ButtonNavigator buttonNavigator;
  freeink::ui::GfxRendererTarget uiTarget;
  UiApp app;
  std::atomic<bool> uiReady{false};

  uint8_t brightness = 60;
  uint8_t warmth = 50;
  bool lightOn = false;
  bool draggingSlider = false;
  int panelBottom = 0;

  static void panelScreen(UiApp::ScreenType& screen, void* user);
  static void onBrightnessEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onWarmthEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onToggleEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onBrightnessStepEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onWarmthStepEvent(const freeink::ui::ActionEvent& event, void* user);

  void buildPanelScreen(UiApp::ScreenType& screen);
  void addStepSlider(UiApp::ScreenType& screen, const freeink::ui::Rect& row, uint8_t value,
                     freeink::ui::ActionId sliderAction, freeink::ui::ActionId stepAction);
  int computePanelBottom() const;
  void adjustBrightness(int delta);
  void adjustWarmth(int delta);
  void toggleLight();
  void close();

 public:
  explicit FrontlightPanelActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool handleHomeGesture() override;
};
