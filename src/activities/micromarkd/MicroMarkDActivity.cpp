#include "MicroMarkDActivity.h"

#ifdef MICROMARKD_APP

#include <HalStorage.h>
#include <I18n.h>
#include <MarkdownDocument.h>
#include <MarkdownRecoveryPlan.h>

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "activities/micromarkd/MarkdownEditorActivity.h"
#include "activities/micromarkd/MarkdownRecentActivity.h"
#include "activities/micromarkd/MarkdownRecovery.h"
#include "activities/micromarkd/MarkdownSearchActivity.h"
#include "activities/micromarkd/MarkdownVaultActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
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

constexpr int VAULT_INDEX = 0;
constexpr int RECENT_INDEX = 1;
constexpr int SEARCH_INDEX = 2;
constexpr int NEW_NOTE_INDEX = 3;
constexpr char VAULT_ROOT[] = "/vault";
constexpr size_t MAX_NOTE_TITLE_BYTES = 96;
constexpr size_t MAX_SEARCH_QUERY_BYTES = 96;
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

void MicroMarkDActivity::onEnter() {
  recoverInterruptedSaves();
  UiListActivity::onEnter();
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

  if (index == NEW_NOTE_INDEX) {
    startNewNote();
    return;
  }

  rowItems_[index].subtitle = tr(STR_MICROMARKD_PLANNED);
  requestUpdate();
}

void MicroMarkDActivity::startSearch() {
  rowItems_[SEARCH_INDEX].subtitle = tr(STR_MICROMARKD_SEARCH_DESC);
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Search vault", "",
                                                                 MAX_SEARCH_QUERY_BYTES, InputType::Text),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto* keyboard = std::get_if<KeyboardResult>(&result.data);
                           if (!keyboard) return;

                           const std::string query = micromarkd::trimNoteTitle(keyboard->text);
                           if (query.empty()) return;
                           activityManager.pushActivity(
                               std::make_unique<MarkdownSearchActivity>(renderer, mappedInput, query));
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
