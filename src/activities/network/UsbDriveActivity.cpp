#include <BoardConfig.h>

#if FREEINK_CAP_USB_MSC
#include "UsbDriveActivity.h"

#include <Arduino.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "components/HeaderBackArrow.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long HOST_WAIT_TIMEOUT_MS = 5UL * 60UL * 1000UL;
constexpr unsigned long FORCED_DISCONNECT_TIMEOUT_MS = 1000UL;
}

void UsbDriveActivity::onEnter() {
  Activity::onEnter();
  state = UsbDriveState::WaitingForHost;
  // Finish all drawing before the filesystem is detached for raw USB access.
#ifdef SIMULATOR
  requestUpdate();  // The browser simulator has no raw SD handoff to wait for.
#else
  requestUpdateAndWait();
#endif
  if (!Storage.beginUsbDrive()) {
    restartToHome();
    return;
  }
  hostWaitStartedAt = millis();
}

void UsbDriveActivity::onExit() {
  if (!restartRequested) Storage.endUsbDrive();
  Activity::onExit();
}

void UsbDriveActivity::loop() {
  state = Storage.usbDriveState();

  if (state == UsbDriveState::WaitingForHost && millis() - hostWaitStartedAt >= HOST_WAIT_TIMEOUT_MS) {
    restartToHome();
    return;
  }
  if (state == UsbDriveState::IoError) {
    if (!forcedDisconnectRequested) {
      forcedDisconnectRequested = true;
      forcedDisconnectRequestedAt = millis();
      Storage.disconnectUsbDriveHost();
    } else if (millis() - forcedDisconnectRequestedAt >= FORCED_DISCONNECT_TIMEOUT_MS) {
      restartToHome();
    }
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int buttonSize = metrics.headerHeight - 8;
  if (state == UsbDriveState::WaitingForHost &&
      ((mappedInput.hasTouch() && mappedInput.wasTapInRect(4, metrics.topPadding + 4, buttonSize, buttonSize)) ||
       mappedInput.wasPressed(MappedInputManager::Button::Back) ||
       mappedInput.wasPressed(MappedInputManager::Button::Power) || mappedInput.wasHomeGesture())) {
    restartToHome();
    return;
  }

  if (state == UsbDriveState::Ejected || state == UsbDriveState::Disconnected || state == UsbDriveState::Unsupported) {
    restartToHome();
  }
}

void UsbDriveActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int buttonSize = metrics.headerHeight - 8;
  const int16_t leftReserve = mappedInput.hasTouch() ? metrics.headerHeight + metrics.headerSidePadding : 0;
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_USB_DRIVE), nullptr, leftReserve);
  if (mappedInput.hasTouch()) {
    HeaderBackArrow::draw(renderer, 4 + buttonSize / 2, metrics.topPadding + 4 + buttonSize / 2);
  }
  renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2 - 40, tr(STR_USB_DRIVE_WAITING));
  renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2 + 10, tr(STR_USB_DRIVE_EJECT_HINT));
  renderer.displayBuffer();
}

void UsbDriveActivity::restartToHome() {
  if (restartRequested) return;
  restartRequested = true;
  Storage.endUsbDrive();
#ifdef SIMULATOR
  onGoHome();
#else
  delay(20);
  restartToHomeAfterStorageHandoff();
#endif
}

#endif  // FREEINK_CAP_USB_MSC
