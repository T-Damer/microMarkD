#include "MarkdownReaderMenuActivity.h"

#ifdef MICROMARKD_APP

#include <GfxRenderer.h>
#include <I18n.h>

#include <memory>
#include <utility>
#include <variant>

#include "activities/micromarkd/MarkdownBacklinksActivity.h"
#include "activities/micromarkd/MarkdownEditorActivity.h"
#include "activities/micromarkd/MarkdownGraphActivity.h"
#include "activities/settings/TextSettingsActivity.h"
#include "SdCardFontSystem.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

MarkdownReaderMenuActivity::MarkdownReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       std::string notePath)
    : UiListActivity("MarkdownReaderMenu", renderer, mappedInput), notePath_(std::move(notePath)) {
  const struct MenuRow {
    StrId label;
    StrId description;
    UIIcon icon;
  } rows[MENU_ITEM_COUNT] = {
      {StrId::STR_MICROMARKD_EDIT_NOTE, StrId::STR_MICROMARKD_EDIT_NOTE_DESC, UIIcon::Edit},
      {StrId::STR_TEXT_SETTINGS, StrId::STR_FONT_SIZE, UIIcon::Settings},
      {StrId::STR_MICROMARKD_BACKLINKS, StrId::STR_MICROMARKD_BACKLINKS_DESC, UIIcon::Links},
      {StrId::STR_MICROMARKD_GRAPH, StrId::STR_MICROMARKD_GRAPH_DESC, UIIcon::Graph},
  };

  for (int index = 0; index < MENU_ITEM_COUNT; index++) {
    items_[index].label = I18N.get(rows[index].label);
    items_[index].subtitle = I18N.get(rows[index].description);
    items_[index].icon = listIconFor(rows[index].icon, 32);
    items_[index].actionValue = static_cast<int16_t>(index);
  }
}

void MarkdownReaderMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props{};
  props.items = items_.data();
  props.count = MENU_ITEM_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  fui::TextStyle subtitle = screen.theme().smallText;
  subtitle.maxLines = 2;
  props.subtitleText = subtitle;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void MarkdownReaderMenuActivity::activateIndex(const int index) {
  if (index < 0 || index >= MENU_ITEM_COUNT) return;
  app.clearTapFlash();
  nav.selected = index;

  if (index == EDIT_NOTE) {
    startActivityForResult(std::make_unique<MarkdownEditorActivity>(renderer, mappedInput, notePath_),
                           [path = notePath_](const ActivityResult& result) {
                             if (result.isCancelled) return;
                             const auto* file = std::get_if<FilePathResult>(&result.data);
                             if (file && !file->path.empty()) activityManager.goToReader(path);
                           });
    return;
  }

  if (index == TEXT_SETTINGS) {
    startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry()),
                           [path = notePath_](const ActivityResult& result) {
                             if (!result.isCancelled) activityManager.goToReader(path);
                           });
    return;
  }

  if (index == BACKLINKS) {
    activityManager.pushActivity(std::make_unique<MarkdownBacklinksActivity>(renderer, mappedInput, notePath_));
    return;
  }

  activityManager.pushActivity(std::make_unique<MarkdownGraphActivity>(renderer, mappedInput, notePath_));
}

const char* MarkdownReaderMenuActivity::headerTitle() const { return tr(STR_MICROMARKD_NOTE_MENU); }

#endif  // MICROMARKD_APP
