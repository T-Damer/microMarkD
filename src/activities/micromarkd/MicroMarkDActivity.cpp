#include "MicroMarkDActivity.h"

#ifdef MICROMARKD_APP

#include <I18n.h>

#include <memory>

#include "activities/home/FileBrowserActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
constexpr StrId menuItems[MicroMarkDActivity::MENU_ITEM_COUNT] = {
    StrId::STR_MICROMARKD_VAULT, StrId::STR_MICROMARKD_RECENT, StrId::STR_MICROMARKD_SEARCH,
    StrId::STR_MICROMARKD_NEW_NOTE, StrId::STR_MICROMARKD_SYNC};

constexpr StrId menuDescriptions[MicroMarkDActivity::MENU_ITEM_COUNT] = {
    StrId::STR_MICROMARKD_VAULT_DESC, StrId::STR_MICROMARKD_RECENT_DESC, StrId::STR_MICROMARKD_SEARCH_DESC,
    StrId::STR_MICROMARKD_NEW_NOTE_DESC, StrId::STR_MICROMARKD_SYNC_DESC};

constexpr UIIcon menuIcons[MicroMarkDActivity::MENU_ITEM_COUNT] = {UIIcon::Folder, UIIcon::Recent, UIIcon::Text,
                                                                   UIIcon::File, UIIcon::Transfer};

constexpr char VAULT_ROOT[] = "/vault";
}  // namespace

MicroMarkDActivity::MicroMarkDActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("MicroMarkD", renderer, mappedInput) {
  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    fui::ListItem item{};
    item.label = I18N.get(menuItems[i]);
    item.subtitle = I18N.get(menuDescriptions[i]);
    item.icon = listIconFor(menuIcons[i], 32);
    item.actionValue = static_cast<int16_t>(i);
    rowItems_[i] = item;
  }
}

int MicroMarkDActivity::listCount() const { return MENU_ITEM_COUNT; }

const char* MicroMarkDActivity::headerTitle() const { return tr(STR_MICROMARKD); }

void MicroMarkDActivity::activateIndex(const int index) {
  if (index < 0 || index >= MENU_ITEM_COUNT) return;

  app.clearTapFlash();
  nav.selected = index;

  if (index == 0) {
    activityManager.pushActivity(std::make_unique<FileBrowserActivity>(renderer, mappedInput, VAULT_ROOT));
    return;
  }

  rowItems_[index].subtitle = tr(STR_MICROMARKD_PLANNED);
  requestUpdate();
}

void MicroMarkDActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props{};
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(MENU_ITEM_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

#endif  // MICROMARKD_APP
