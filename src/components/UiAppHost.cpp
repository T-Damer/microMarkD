#include "UiAppHost.h"

#include "UiAppHelpers.h"

namespace fui = freeink::ui;

UiAppHost::UiAppHost(const GfxRenderer& renderer)
    : uiTarget(makeUiTarget(renderer)), app(uiTarget, uiTarget.deviceContext()) {}

void UiAppHost::resetUi() {
  uiReady = false;
  applySharedUiTheme(app, uiTarget);
}

void UiAppHost::renderUi() {
  uiReady = false;
  app.setDevice(uiTarget.deviceContext());
  app.render();
  uiReady = true;
}

UiAppHost::TouchRoute UiAppHost::routeTouch(const MappedInputManager& input, const bool withLongPress,
                                            const bool routeHeld) {
  TouchRoute route;
  if (!uiReady) return route;
  route.snap = touchSnapshotFrom(input, withLongPress);
  if (!route.snap.touchPressed && !route.snap.touchReleased && !(routeHeld && route.snap.touchHeld)) {
    return route;
  }
  route.routed = true;
  route.event = app.route(route.snap);
  return route;
}

fui::ActionEvent UiAppHost::route(const fui::InputSnapshot& snap) {
  if (!uiReady) return {};
  return app.route(snap);
}
