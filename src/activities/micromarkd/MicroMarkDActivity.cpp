#include "MicroMarkDActivity.h"

#ifdef MICROMARKD_APP

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <MarkdownDocument.h>
#include <MarkdownRecoveryPlan.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/micromarkd/MarkdownEditorActivity.h"
#include "activities/micromarkd/MarkdownGraphActivity.h"
#include "activities/micromarkd/MarkdownRecentActivity.h"
#include "activities/micromarkd/MarkdownRecovery.h"
#include "activities/micromarkd/MarkdownSearchActivity.h"
#include "activities/micromarkd/MarkdownSyncActivity.h"
#include "activities/micromarkd/MarkdownTagsActivity.h"
#include "activities/micromarkd/MarkdownVaultActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/customListIcons.h"

namespace fui = freeink::ui;

namespace {
constexpr int WIDGET_INDEX = 0;
constexpr int VAULT_INDEX = 1;
constexpr int RECENT_INDEX = 2;
constexpr int SEARCH_INDEX = 3;
constexpr int TAGS_INDEX = 4;
constexpr int GRAPH_INDEX = 5;
constexpr int NEW_NOTE_INDEX = 6;
constexpr int SYNC_INDEX = 7;
constexpr freeink::ui::ActionId ACTION_WIDGET_SETTINGS = 3;
constexpr char VAULT_ROOT[] = "/vault";
constexpr size_t MAX_NOTE_TITLE_BYTES = 96;
constexpr size_t MAX_SEARCH_QUERY_BYTES = 96;
}  // namespace

MicroMarkDActivity::MicroMarkDActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("MicroMarkD", renderer, mappedInput, /*wantsTouchLongPress=*/true) {
  const auto setTranslatedRow = [this](const int index, const StrId label, const StrId description, const UIIcon icon) {
    fui::ListItem item{};
    item.label = I18N.get(label);
    item.subtitle = I18N.get(description);
    item.icon = listIconFor(icon, 32);
    item.actionValue = static_cast<int16_t>(index);
    rowItems_[index] = item;
  };

  setTranslatedRow(VAULT_INDEX, StrId::STR_MICROMARKD_VAULT, StrId::STR_MICROMARKD_VAULT_DESC, UIIcon::Folder);
  setTranslatedRow(RECENT_INDEX, StrId::STR_MICROMARKD_RECENT, StrId::STR_MICROMARKD_RECENT_DESC, UIIcon::Recent);
  setTranslatedRow(SEARCH_INDEX, StrId::STR_MICROMARKD_SEARCH, StrId::STR_MICROMARKD_SEARCH_DESC, UIIcon::Search);

  fui::ListItem tags{};
  tags.label = I18N.get(StrId::STR_MICROMARKD_TAGS);
  tags.subtitle = I18N.get(StrId::STR_MICROMARKD_TAGS_DESC);
  tags.icon = listIconFor(UIIcon::Tag, 32);
  tags.actionValue = TAGS_INDEX;
  rowItems_[TAGS_INDEX] = tags;

  setTranslatedRow(GRAPH_INDEX, StrId::STR_MICROMARKD_GRAPH, StrId::STR_MICROMARKD_GRAPH_DESC, UIIcon::Graph);
  setTranslatedRow(NEW_NOTE_INDEX, StrId::STR_MICROMARKD_NEW_NOTE, StrId::STR_MICROMARKD_NEW_NOTE_DESC,
                   UIIcon::NewNote);
  setTranslatedRow(SYNC_INDEX, StrId::STR_MICROMARKD_SYNC, StrId::STR_MICROMARKD_SYNC_DESC, UIIcon::Git);
  rowItems_[WIDGET_INDEX].actionValue = WIDGET_INDEX;
}

void MicroMarkDActivity::onEnter() {
  recoverInterruptedSaves();
  UiListActivity::onEnter();
  app.on(ACTION_WIDGET_SETTINGS, &MicroMarkDActivity::widgetSettingsTrampoline, this);
  weather_.load();
  weather_.refresh();
  updateWidget();
  if (weather_.mode() == WeatherWidget::LocationMode::Unset) showWidgetSettings();
}

void MicroMarkDActivity::updateWidget() {
  if (showWeather_) {
    widgetLabel_ = std::string(tr(STR_MICROMARKD_WIDGET_WEATHER)) + " · " + weather_.place();
    widgetSubtitle_ = weather_.hasWeather() ? std::to_string(weather_.temperature()) + " °C · " +
                                                  std::to_string(weather_.forecastDays()) + " d"
                                            : tr(STR_MICROMARKD_WIDGET_UNAVAILABLE);
  } else {
    widgetLabel_ = tr(STR_MICROMARKD_WIDGET_LAST_BOOK);
    widgetSubtitle_ = tr(STR_MICROMARKD_WIDGET_NO_BOOK);
    recentBookPath_.clear();
    const auto& recent = RECENT_BOOKS.getBooks();
    const auto bookIt = std::find_if(recent.begin(), recent.end(), [](const RecentBook& book) {
      return FsHelpers::hasEpubExtension(book.path) || FsHelpers::hasXtcExtension(book.path);
    });
    if (bookIt != recent.end()) {
      const auto& book = *bookIt;
      recentBookPath_ = book.path;
      widgetLabel_ = book.title.empty() ? book.path.substr(book.path.find_last_of('/') + 1) : book.title;
      widgetSubtitle_ = tr(STR_MICROMARKD_WIDGET_LAST_BOOK);
      if (FsHelpers::hasEpubExtension(book.path)) {
        Epub epub(book.path, "/.crosspoint");
        if (epub.load(false, true)) {
          HalFile file;
          if (Storage.openFileForRead("MDW", epub.getCachePath() + "/progress.bin", file)) {
            uint8_t data[6];
            if (file.read(data, sizeof(data)) == sizeof(data)) {
              const int spine = data[0] | (data[1] << 8);
              const int page = data[2] | (data[3] << 8);
              const int total = data[4] | (data[5] << 8);
              if (total > 0 && page <= total) {
                const int percent = std::clamp(
                    static_cast<int>(epub.calculateProgress(spine, static_cast<float>(page) / total) * 100), 0, 100);
                widgetSubtitle_ = std::to_string(percent) + "% · " + std::to_string(page) + "/" + std::to_string(total);
              }
            }
          }
        }
      }
    }
  }
  rowItems_[WIDGET_INDEX].label = widgetLabel_.c_str();
  rowItems_[WIDGET_INDEX].subtitle = widgetSubtitle_.c_str();
  rowItems_[WIDGET_INDEX].value = mappedInput.hasTouch() ? nullptr : "...";
  requestUpdate();
}

void MicroMarkDActivity::showWidgetSettings() {
  const char* options[] = {tr(STR_MICROMARKD_WIDGET_AUTO), tr(STR_MICROMARKD_WIDGET_MANUAL),
                           tr(STR_MICROMARKD_WIDGET_REFRESH)};
  popup_.show(tr(STR_MICROMARKD_WIDGET_SETUP), options, weather_.mode() == WeatherWidget::LocationMode::Unset ? 2 : 3,
              0, [this](const int option) {
                if (option == 0) {
                  weather_.useAutomatic();
                  updateWidget();
                } else if (option == 1) {
                  promptManualLocation();
                } else if (option == 2) {
                  weather_.refresh();
                  updateWidget();
                }
              });
  requestUpdate();
}

void MicroMarkDActivity::promptManualLocation() {
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_MICROMARKD_WIDGET_PLACE),
                                                                 "Miami", 96, InputType::Text),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto* keyboard = std::get_if<KeyboardResult>(&result.data);
                           if (!keyboard) return;
                           weather_.setManual(keyboard->text);
                           updateWidget();
                         });
}

void MicroMarkDActivity::widgetSettingsTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<MicroMarkDActivity*>(user)->showWidgetSettings();
}

void MicroMarkDActivity::onRowLongPress(const int index) {
  if (index == WIDGET_INDEX) showWidgetSettings();
}

bool MicroMarkDActivity::handleCustomInput() {
  if (popup_.isActive()) return popup_.handleInput(mappedInput, [this] { requestUpdate(); });
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Right) {
    showWeather_ = !showWeather_;
    updateWidget();
    return true;
  }
  return false;
}

void MicroMarkDActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();
  drawFooter();
  if (popup_.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer();
}

void MicroMarkDActivity::recoverInterruptedSaves() {
  static bool recoveryComplete = false;
  if (recoveryComplete) return;

  const MarkdownRecoveryReport report = recoverMarkdownVault(VAULT_ROOT);
  recoveryComplete = report.complete();

  if (report.recoveredNotes() > 0) {
    vaultStatus_ = "Recovered ";
    vaultStatus_ += std::to_string(report.recoveredNotes());
    vaultStatus_ += report.recoveredNotes() == 1 ? " interrupted save" : " interrupted saves";
    if (!report.complete()) vaultStatus_ += "; some files need attention";
    rowItems_[VAULT_INDEX].subtitle = vaultStatus_.c_str();
  } else if (report.discardedIncompleteTemporary > 0) {
    vaultStatus_ = "Discarded ";
    vaultStatus_ += std::to_string(report.discardedIncompleteTemporary);
    vaultStatus_ += report.discardedIncompleteTemporary == 1 ? " incomplete save" : " incomplete saves";
    if (!report.complete()) vaultStatus_ += "; some files need attention";
    rowItems_[VAULT_INDEX].subtitle = vaultStatus_.c_str();
  } else if (!report.complete()) {
    vaultStatus_ = "Interrupted-save recovery needs attention";
    rowItems_[VAULT_INDEX].subtitle = vaultStatus_.c_str();
  } else if (!vaultStatus_.empty()) {
    vaultStatus_.clear();
    rowItems_[VAULT_INDEX].subtitle = tr(STR_MICROMARKD_VAULT_DESC);
  }
}

int MicroMarkDActivity::listCount() const { return MENU_ITEM_COUNT; }

const char* MicroMarkDActivity::headerTitle() const { return tr(STR_MICROMARKD); }

void MicroMarkDActivity::activateIndex(const int index) {
  if (index < 0 || index >= MENU_ITEM_COUNT) return;

  app.clearTapFlash();
  nav.selected = index;

  if (index == WIDGET_INDEX) {
    if (showWeather_ || recentBookPath_.empty()) {
      showWeather_ = !showWeather_;
      updateWidget();
    } else {
      activityManager.goToReader(recentBookPath_);
    }
    return;
  }

  if (index == VAULT_INDEX) {
    activityManager.pushActivity(std::make_unique<MarkdownVaultActivity>(renderer, mappedInput, VAULT_ROOT));
    return;
  }

  if (index == RECENT_INDEX) {
    activityManager.pushActivity(std::make_unique<MarkdownRecentActivity>(renderer, mappedInput));
    return;
  }

  if (index == SEARCH_INDEX) {
    startSearch();
    return;
  }

  if (index == TAGS_INDEX) {
    activityManager.pushActivity(std::make_unique<MarkdownTagsActivity>(renderer, mappedInput));
    return;
  }

  if (index == GRAPH_INDEX) {
    activityManager.pushActivity(std::make_unique<MarkdownGraphActivity>(renderer, mappedInput, std::string{}));
    return;
  }

  if (index == NEW_NOTE_INDEX) {
    startNewNote();
    return;
  }

  if (index == SYNC_INDEX) {
    activityManager.pushActivity(std::make_unique<MarkdownSyncActivity>(renderer, mappedInput));
  }
}

void MicroMarkDActivity::startSearch() {
  rowItems_[SEARCH_INDEX].subtitle = tr(STR_MICROMARKD_SEARCH_DESC);
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Search vault", "", MAX_SEARCH_QUERY_BYTES,
                                              InputType::Text),
      [this](const ActivityResult& result) {
        if (result.isCancelled) return;
        const auto* keyboard = std::get_if<KeyboardResult>(&result.data);
        if (!keyboard) return;

        const std::string query = micromarkd::trimNoteTitle(keyboard->text);
        if (query.empty()) return;
        activityManager.pushActivity(std::make_unique<MarkdownSearchActivity>(renderer, mappedInput, query));
      });
}

void MicroMarkDActivity::startNewNote() {
  rowItems_[NEW_NOTE_INDEX].subtitle = tr(STR_MICROMARKD_NEW_NOTE_DESC);
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "New note title", "",
                                                                 MAX_NOTE_TITLE_BYTES, InputType::Text),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto* keyboard = std::get_if<KeyboardResult>(&result.data);
                           if (!keyboard) return;
                           openNewNoteEditor(keyboard->text);
                         });
}

std::string MicroMarkDActivity::uniqueNotePath(const std::string& filename) const {
  const auto isAvailable = [](const std::string& path) {
    return !Storage.exists(path.c_str()) && !Storage.exists((path + micromarkd::NOTE_TEMPORARY_SUFFIX).c_str()) &&
           !Storage.exists((path + micromarkd::NOTE_BACKUP_SUFFIX).c_str()) &&
           !Storage.exists((path + micromarkd::NOTE_READY_SUFFIX).c_str());
  };

  std::string candidate = std::string(VAULT_ROOT) + "/" + filename;
  if (isAvailable(candidate)) return candidate;

  const size_t extension = filename.size() >= 3 ? filename.size() - 3 : filename.size();
  const std::string stem = filename.substr(0, extension);
  for (int suffix = 2; suffix < 10000; suffix++) {
    candidate = std::string(VAULT_ROOT) + "/" + stem + " " + std::to_string(suffix) + ".md";
    if (isAvailable(candidate)) return candidate;
  }
  return {};
}

void MicroMarkDActivity::openNewNoteEditor(const std::string& rawTitle) {
  if (Storage.exists(VAULT_ROOT)) {
    auto vault = Storage.open(VAULT_ROOT);
    if (!vault || !vault.isDirectory()) {
      showCreateError();
      return;
    }
  } else if (!Storage.mkdir(VAULT_ROOT, true)) {
    showCreateError();
    return;
  }

  std::string title = micromarkd::trimNoteTitle(rawTitle);
  if (title.empty()) title = "Untitled";
  const std::string path = uniqueNotePath(micromarkd::safeNoteFilename(title));
  if (path.empty()) {
    showCreateError();
    return;
  }

  std::vector<std::string> lines;
  lines.reserve(2);
  lines.push_back("# " + title);
  lines.emplace_back();

  startActivityForResult(std::make_unique<MarkdownEditorActivity>(renderer, mappedInput, path, std::move(lines),
                                                                  /*trailingNewline=*/true),
                         [](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto* file = std::get_if<FilePathResult>(&result.data);
                           if (!file || file->path.empty()) return;
                           activityManager.goToReader(file->path);
                         });
}

void MicroMarkDActivity::showCreateError() {
  rowItems_[NEW_NOTE_INDEX].subtitle = "Could not create note";
  requestUpdate();
}

void MicroMarkDActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (mappedInput.hasTouch()) {
    screen.target().bitmap(
        fui::Rect{4, static_cast<int16_t>(metrics.topPadding + 4), static_cast<int16_t>(metrics.headerHeight - 8),
                  static_cast<int16_t>(metrics.headerHeight - 8)},
        fui::bitmapFromIcon(icon_micromarkd_32), fui::BitmapMode::Center);
  }
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props{};
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(MENU_ITEM_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  const fui::Rect widgetBand = screen.body();
  screen.list(props);
  if (mappedInput.hasTouch()) {
    fui::ButtonProps settings{};
    settings.label = "...";
    settings.action = ACTION_WIDGET_SETTINGS;
    settings.inputMask = fui::InputTouch;
    settings.styles = fui::plainStyles();
    screen.button(settings, fui::Rect{static_cast<int16_t>(widgetBand.right() - 48), widgetBand.y, 44, 44});
  }
}

#endif  // MICROMARKD_APP
