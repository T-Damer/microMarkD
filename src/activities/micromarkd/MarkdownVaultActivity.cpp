#include "MarkdownVaultActivity.h"

#ifdef MICROMARKD_APP

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>
#include <utility>
#include <variant>

#include "activities/micromarkd/MarkdownEditorActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/BookCacheUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr char VAULT_ROOT[] = "/vault";
constexpr char VAULT_PREFIX[] = "/vault/";
}  // namespace

MarkdownVaultActivity::MarkdownVaultActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             std::string initialPath)
    : UiListActivity("MarkdownVault", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      path_(std::move(initialPath)) {}

void MarkdownVaultActivity::onEnter() {
  UiListActivity::onEnter();
  loadFailed_ = !normalisePath();
  if (!loadFailed_) loadEntries();
}

void MarkdownVaultActivity::onExit() {
  UiListActivity::onExit();
  entries_.clear();
  rowNames_.clear();
  rowExtensions_.clear();
  rowItems_.clear();
}

bool MarkdownVaultActivity::normalisePath() {
  const std::string normalised = FsHelpers::normalisePath(path_);
  if (normalised.empty()) return false;

  path_ = "/" + normalised;
  if (path_ != VAULT_ROOT && path_.rfind(VAULT_PREFIX, 0) != 0) return false;

  if (!Storage.exists(VAULT_ROOT) && !Storage.mkdir(VAULT_ROOT, true)) return false;
  auto root = Storage.open(path_.c_str());
  if (!root || !root.isDirectory()) return false;
  root.close();
  return true;
}

void MarkdownVaultActivity::loadEntries() {
  entries_.clear();
  loadFailed_ = false;

  auto root = Storage.open(path_.c_str());
  if (!root || !root.isDirectory()) {
    loadFailed_ = true;
    rebuildRows();
    return;
  }

  root.rewindDirectory();
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(nameBuffer_.data(), nameBuffer_.size());
    const bool isDirectory = file.isDirectory();
    file.close();

    if (nameBuffer_[0] == '.' || std::strcmp(nameBuffer_.data(), "System Volume Information") == 0) continue;

    if (isDirectory) {
      entries_.emplace_back(std::string(nameBuffer_.data()) + "/");
    } else if (FsHelpers::hasMarkdownExtension(nameBuffer_.data())) {
      entries_.emplace_back(nameBuffer_.data());
    }
  }
  root.close();

  FsHelpers::sortFileList(entries_);
  rebuildRows();

  if (path_ == VAULT_ROOT) {
    header_ = tr(STR_MICROMARKD_VAULT);
  } else {
    header_ = path_.substr(path_.find_last_of('/') + 1);
  }
}

void MarkdownVaultActivity::rebuildRows() {
  rowNames_.resize(entries_.size());
  rowExtensions_.resize(entries_.size());
  rowItems_.clear();
  rowItems_.reserve(entries_.size());

  for (size_t i = 0; i < entries_.size(); i++) {
    rowNames_[i] = displayName(entries_[i]);
    rowExtensions_[i] = displayExtension(entries_[i]);

    fui::ListItem item{};
    item.label = rowNames_[i].c_str();
    if (!rowExtensions_[i].empty()) item.value = rowExtensions_[i].c_str();
    item.icon = listIconFor(UITheme::getFileIcon(entries_[i]));
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
}

int MarkdownVaultActivity::listCount() const { return static_cast<int>(entries_.size()); }

std::string MarkdownVaultActivity::fullPath(const std::string& entry) const {
  return path_ + (path_.back() == '/' ? "" : "/") + entry;
}

void MarkdownVaultActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();

  const std::string& entry = entries_[index];
  if (!entry.empty() && entry.back() == '/') {
    openDirectory(entry);
    return;
  }
  openNote(fullPath(entry));
}

void MarkdownVaultActivity::onRowLongPress(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;

  const std::string& entry = entries_[index];
  if (!entry.empty() && entry.back() != '/') showNoteActions(entry, fullPath(entry));
}

void MarkdownVaultActivity::openDirectory(const std::string& entry) {
  const std::string previousPath = path_;
  path_ = fullPath(entry.substr(0, entry.size() - 1));
  if (!normalisePath()) {
    path_ = previousPath;
    loadFailed_ = true;
    requestUpdate();
    return;
  }

  loadEntries();
  nav.reset();
  requestUpdate();
}

void MarkdownVaultActivity::openNote(const std::string& notePath) { onSelectBook(notePath); }

void MarkdownVaultActivity::editNote(const std::string& notePath) {
  startActivityForResult(
      std::make_unique<MarkdownEditorActivity>(renderer, mappedInput, notePath),
      [this](const ActivityResult& result) {
        if (result.isCancelled) return;
        const auto* file = std::get_if<FilePathResult>(&result.data);
        if (!file || file->path.empty()) return;

        loadEntries();
        activityManager.goToReader(file->path);
      });
}

void MarkdownVaultActivity::showNoteActions(const std::string& entry, const std::string& notePath) {
  const char* options[] = {tr(STR_OPEN), "Edit note", tr(STR_DELETE)};
  popup_.show("Note actions", options, 3, 1, [this, entry, notePath](const int option) {
    switch (option) {
      case 0:
        openNote(notePath);
        break;
      case 1:
        editNote(notePath);
        break;
      case 2:
        confirmDelete(entry, notePath);
        break;
      default:
        break;
    }
  });
  requestUpdate();
}

void MarkdownVaultActivity::confirmDelete(const std::string& entry, const std::string& notePath) {
  std::string heading = tr(STR_DELETE) + std::string("? ");
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, entry),
      [this, notePath](const ActivityResult& result) {
        if (result.isCancelled) return;

        clearBookCache(notePath);
        if (!Storage.remove(notePath.c_str())) {
          LOG_ERR("MDV", "Failed to delete note: %s", notePath.c_str());
          return;
        }

        loadEntries();
        if (entries_.empty()) {
          nav.selected = 0;
        } else if (nav.selected >= listCount()) {
          nav.selected = listCount() - 1;
        }
        nav.follow(listCount());
        requestUpdate(true);
      });
}

bool MarkdownVaultActivity::handleCustomInput() {
  if (!popup_.isActive()) return false;
  return popup_.handleInput(mappedInput, [this] { requestUpdate(); });
}

void MarkdownVaultActivity::onBackButton() {
  if (path_ == VAULT_ROOT) {
    finish();
    return;
  }

  const std::string previous = path_;
  const size_t slash = path_.find_last_of('/');
  path_.resize(slash == std::string::npos || slash < sizeof(VAULT_ROOT) - 1 ? sizeof(VAULT_ROOT) - 1 : slash);
  loadEntries();

  const std::string directory = previous.substr(previous.find_last_of('/') + 1) + "/";
  const auto found = std::find(entries_.begin(), entries_.end(), directory);
  nav.selected = found == entries_.end() ? 0 : static_cast<int>(std::distance(entries_.begin(), found));
  nav.top = 0;
  nav.follow(listCount());
  requestUpdate();
}

const char* MarkdownVaultActivity::headerTitle() const {
  return header_.empty() ? tr(STR_MICROMARKD_VAULT) : header_.c_str();
}

std::string MarkdownVaultActivity::displayName(const std::string& entry) {
  if (!entry.empty() && entry.back() == '/') return entry.substr(0, entry.size() - 1);
  const size_t dot = entry.find_last_of('.');
  return dot == std::string::npos ? entry : entry.substr(0, dot);
}

std::string MarkdownVaultActivity::displayExtension(const std::string& entry) {
  if (entry.empty() || entry.back() == '/') return {};
  const size_t dot = entry.find_last_of('.');
  return dot == std::string::npos ? std::string{} : entry.substr(dot);
}

void MarkdownVaultActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (loadFailed_) {
    screen.centeredText("Could not open vault", screen.theme().bodyText);
    return;
  }
  if (entries_.empty()) {
    screen.centeredText("No Markdown notes yet", screen.theme().bodyText);
    return;
  }

  fui::ListProps props{};
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.valueInset = 8;
  fui::TextStyle label = screen.theme().smallText;
  label.maxLines = 2;
  props.labelText = label;
  props.balanceWrappedLabelWithValue = false;
  syncListViewport(screen, props);
  screen.list(props);
}

void MarkdownVaultActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), entries_.empty() ? "" : tr(STR_OPEN),
                                            entries_.empty() ? "" : tr(STR_DIR_UP),
                                            entries_.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void MarkdownVaultActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();
  drawFooter();
  if (popup_.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer();
}

#endif  // MICROMARKD_APP
