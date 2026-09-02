#include "ReaderActivity.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderActivity.h"
#include "I18n.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "TxtReaderActivity.h"
#include "XtcReaderActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int TOUCH_NAV_BUTTON_WIDTH = 96;
constexpr int TOUCH_NAV_BUTTON_GAP = 8;
constexpr int TOUCH_NAV_BUTTON_INSET = 4;
}  // namespace

ReaderActivity::ReaderActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                               std::string bookPath, const bool allowFastInitialRefresh)
    : Activity(name, renderer, mappedInput), bookPath(std::move(bookPath)) {
  if (allowFastInitialRefresh) {
    const int refreshFrequency = SETTINGS.getRefreshFrequency();
    pagesUntilFullRefresh = refreshFrequency > 1 ? refreshFrequency : 2;
  }
}

std::unique_ptr<ReaderActivity> ReaderActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       std::string path, const bool allowFastInitialRefresh) {
  // ActivityManager requires heap ownership; each branch allocates exactly one screen-lifetime object.
  std::unique_ptr<ReaderActivity> activity;
  if (FsHelpers::hasXtcExtension(path)) {
    activity = makeUniqueNoThrow<XtcReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  } else if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    activity = makeUniqueNoThrow<TxtReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  } else {
    activity = makeUniqueNoThrow<EpubReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  }

  if (!activity) {
    LOG_ERR("READER", "OOM: reader activity");
  }
  return activity;
}

void ReaderActivity::applyInitialOrientation() { ReaderUtils::applyOrientation(renderer, SETTINGS.orientation); }

void ReaderActivity::disableFastInitialRefresh() { pagesUntilFullRefresh = 0; }

void ReaderActivity::onEnter() {
  Activity::onEnter();

  if (!Storage.exists(bookPath.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", bookPath.c_str());
    finish();
    return;
  }

  sdFontSystem.ensureLoaded(renderer);
  applyInitialOrientation();

  if (!loadBook()) {
    finish();
    return;
  }

  APP_STATE.openEpubPath = bookPath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(bookPath, getBookTitle(), getBookAuthor(), getBookThumbBmpPath());
  requestUpdate();
}

void ReaderActivity::onExit() {
  Activity::onExit();

  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();

  endOfBookOptions.reset();
  endOfBookOptionsReady.store(false, std::memory_order_release);
}

bool ReaderActivity::handleBackNavigation() {
  if (backReturnsToPreviousActivity) {
    if (mappedInput.wasBackGesture()) return false;
    if (!mappedInput.wasReleased(MappedInputManager::Button::Back)) return false;
    if (handleTouchBackNavigation()) return true;
    activityManager.popActivity();
    return true;
  }
  return ReaderUtils::handleBackNavigation(mappedInput, activityManager, bookPath.c_str(),
                                           {this, [](void* ctx) { static_cast<ReaderActivity*>(ctx)->onGoHome(); }});
}

int ReaderActivity::touchNavigationHeight() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return std::max(1, metrics.headerHeight - TOUCH_NAV_BUTTON_INSET) + metrics.verticalSpacing;
}

Rect ReaderActivity::touchNavigationButton(const bool home) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  int orientedMarginTop = 0;
  int orientedMarginRight = 0;
  int orientedMarginBottom = 0;
  int orientedMarginLeft = 0;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);

  const int buttonHeight = std::max(1, metrics.headerHeight - TOUCH_NAV_BUTTON_INSET);
  const int totalWidth = TOUCH_NAV_BUTTON_WIDTH * 2 + TOUCH_NAV_BUTTON_GAP;
  const int startX = (renderer.getScreenWidth() - totalWidth) / 2;
  const int bottom = renderer.getScreenHeight() - orientedMarginBottom - UITheme::getInstance().getStatusBarHeight() -
                     metrics.verticalSpacing;
  const int x = startX + (home ? TOUCH_NAV_BUTTON_WIDTH + TOUCH_NAV_BUTTON_GAP : 0);
  return Rect{x, bottom - buttonHeight, TOUCH_NAV_BUTTON_WIDTH, buttonHeight};
}

bool ReaderActivity::handleTouchNavigationTap(const int x, const int y) {
  if (!mappedInput.hasTouch()) return false;
  const auto contains = [x, y](const Rect& rect) {
    return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
  };

  if (contains(touchNavigationButton(false))) {
    if (!handleTouchBackNavigation()) {
      if (backReturnsToPreviousActivity) {
        activityManager.popActivity();
      } else {
        onGoHome();
      }
    }
    return true;
  }
  if (contains(touchNavigationButton(true))) {
    onGoHome();
    return true;
  }
  return false;
}

bool ReaderActivity::handleTouchNavigation() {
  if (!mappedInput.hasTouch()) return false;
  int x = 0;
  int y = 0;
  return mappedInput.wasScreenTapped(x, y) && handleTouchNavigationTap(x, y);
}

void ReaderActivity::drawTouchNavigation() const {
  if (!mappedInput.hasTouch()) return;

  const char* labels[] = {tr(STR_BACK), tr(STR_HOME)};
  for (int index = 0; index < 2; index++) {
    const Rect button = touchNavigationButton(index == 1);
    renderer.drawRoundedRect(button.x, button.y, button.width, button.height, 1, 5, true);
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, labels[index]);
    const int textHeight = renderer.getTextHeight(UI_10_FONT_ID);
    renderer.drawText(UI_10_FONT_ID, button.x + (button.width - textWidth) / 2,
                      button.y + (button.height - textHeight) / 2, labels[index]);
  }
}

void ReaderActivity::clearEndOfBookOptionsIfNeeded() {
  if (isAtEndOfBook() || !endOfBookOptionsReady.load(std::memory_order_acquire)) return;

  RenderLock lock(*this);
  endOfBookOptionsReady.store(false, std::memory_order_release);
  endOfBookOptions.reset();
}

bool ReaderActivity::handleEndOfBookMenu(const bool suppressConfirmRelease) {
  if (!isAtEndOfBook() || !endOfBookOptionsReady.load(std::memory_order_acquire) ||
      (!endOfBookOptions->menuActive() && !mappedInput.hasTouch()) || suppressConfirmRelease) {
    return false;
  }

  std::string openPath;
  switch (endOfBookOptions->handleMenuInput(mappedInput, &openPath)) {
    case EndOfBookOptions::Action::OpenBook:
      activityManager.goToReader(openPath);
      return true;
    case EndOfBookOptions::Action::GoHome:
      onGoHome();
      return true;
    case EndOfBookOptions::Action::LastPage:
      onReturnFromEndOfBook();
      requestUpdate();
      return true;
    case EndOfBookOptions::Action::Redraw:
      requestUpdate();
      return true;
    case EndOfBookOptions::Action::None:
      return false;
  }

  return false;
}

bool ReaderActivity::handleEndOfBookPageTurn(const bool prevTriggered, const bool nextTriggered) {
  if (!isAtEndOfBook()) return false;

  if (endOfBookOptionsReady.load(std::memory_order_acquire) && endOfBookOptions->menuActive()) {
    return true;
  }
  if (nextTriggered) {
    onGoHome();
  } else if (prevTriggered) {
    onReturnFromEndOfBook();
    requestUpdate();
  }
  return true;
}

void ReaderActivity::loop() {
  clearEndOfBookOptionsIfNeeded();
  if (handleEndOfBookMenu()) return;
  if (handleFormatInput()) return;
  if (handleTouchNavigation()) return;
  if (handleBackNavigation()) return;

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) return;
  if (handleEndOfBookPageTurn(prevTriggered, nextTriggered)) return;

  const unsigned long heldMs = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
  const bool skip =
      !fromTilt && SETTINGS.longPressButtonBehavior == SETTINGS.CHAPTER_SKIP && heldMs > ReaderUtils::SKIP_HOLD_MS;

  if (prevTriggered) {
    if (skip) {
      skipPages(-10);
    } else {
      pageTurn(false);
    }
  } else {
    if (skip) {
      skipPages(10);
    } else {
      pageTurn(true);
    }
  }
  requestUpdate();
}

void ReaderActivity::render(RenderLock&&) {
  if (isAtEndOfBook()) {
    if (!endOfBookOptions) {
      endOfBookOptions = makeUniqueNoThrow<EndOfBookOptions>(renderer);
      if (!endOfBookOptions) LOG_ERR("READER", "OOM: EndOfBookOptions");
    }
    renderer.clearScreen();
    if (endOfBookOptions) {
      endOfBookOptions->loadOnce(bookPath);
      // Release-publish AFTER loadOnce() so the main task's acquire load can't
      // observe an object whose names/selector are still being populated.
      endOfBookOptionsReady.store(true, std::memory_order_release);
      endOfBookOptions->render(renderer, mappedInput);
    }
    renderer.displayBuffer();
    onEndOfBookRendered();
    return;
  }

  renderBook();
}

bool ReaderActivity::handleForcedRefresh() {
  {
    RenderLock lock(*this);
    pagesUntilFullRefresh = 1;
    forcedRefreshPending = true;
  }
  requestUpdate();
  return true;
}
