#pragma once

#include <HalStorage.h>

#include "activities/Activity.h"

class UsbDriveActivity final : public Activity {
 public:
  UsbDriveActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("UsbDrive", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == UsbDriveState::Connected || state == UsbDriveState::IoError; }
  bool requiresExclusiveStorageLoop() const override { return !restartRequested; }

 private:
  void restartToHome();

  UsbDriveState state = UsbDriveState::Unsupported;
  bool restartRequested = false;
  bool forcedDisconnectRequested = false;
  unsigned long hostWaitStartedAt = 0;
  unsigned long forcedDisconnectRequestedAt = 0;
};
