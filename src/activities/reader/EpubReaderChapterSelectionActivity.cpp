#include "EpubReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

EpubReaderChapterSelectionActivity::EpubReaderChapterSelectionActivity(GfxRenderer& renderer,
                                                                       MappedInputManager& mappedInput,
                                                                       const std::shared_ptr<Epub>& epub,
                                                                       const int currentSpineIndex)
    : UiListActivity("EpubReaderChapterSelection", renderer, mappedInput),
      epub(epub),
      currentSpineIndex(currentSpineIndex) {}

void EpubReaderChapterSelectionActivity::onEnter() {
  UiListActivity::onEnter();

  if (!epub) {
    return;
  }

  // Start with the current chapter at the top of the viewport; the first
  // screen build pulls the viewport to it (ListNav follow-on-build).
  int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (tocIndex == -1) {
    tocIndex = 0;
  }
  nav.selected = tocIndex;
}

void EpubReaderChapterSelectionActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) {
    return;
  }
  // The activated row leaves this screen (finish); a lingering flash would gray
  // an unrelated element on the next render.
  app.clearTapFlash();
  nav.selected = index;
  const auto tocItem = epub->getTocItem(index);
  if (tocItem.spineIndex == -1) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  } else {
    setResult(ChapterResult{tocItem.spineIndex, tocItem.anchor});
    finish();
  }
}

bool EpubReaderChapterSelectionActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return true;
  }

  if (!epub) {
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }

  return false;
}

void EpubReaderChapterSelectionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band drawChrome paints the title in.
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (!epub) {
    return;
  }
  const int totalItems = listCount();
  if (totalItems == 0) {
    screen.centeredText(tr(STR_NO_CHAPTERS), screen.theme().bodyText);
    return;
  }

  // Per-render composed labels (indent + title from the TOC cache): the
  // strings must stay alive through screen.list(), so they live in a local
  // vector the ListItems point into.
  std::vector<std::string> labels;
  labels.reserve(totalItems);
  std::vector<fui::ListItem> items;
  items.reserve(totalItems);
  for (int i = 0; i < totalItems; i++) {
    const auto tocItem = epub->getTocItem(i);
    std::string indent(tocItem.level > 0 ? (tocItem.level - 1) * 2 : 0, ' ');
    labels.push_back(indent + tocItem.title);
    fui::ListItem item;
    item.label = labels.back().c_str();
    item.actionValue = static_cast<int16_t>(i);
    items.push_back(item);
  }

  fui::ListProps props;
  props.items = items.data();
  props.count = static_cast<uint16_t>(items.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

void EpubReaderChapterSelectionActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight},
                 tr(STR_SELECT_CHAPTER));
}
