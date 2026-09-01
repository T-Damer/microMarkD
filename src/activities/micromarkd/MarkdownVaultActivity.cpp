#include "MarkdownVaultActivity.h"

#ifdef MICROMARKD_APP

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <MarkdownDocument.h>
#include <MarkdownRecoveryPlan.h>
#include <Txt.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>
#include <string_view>
#include <utility>
#include <variant>

#include "RecentBooksStore.h"
#include "activities/micromarkd/MarkdownBacklinksActivity.h"
#include "activities/micromarkd/MarkdownEditorActivity.h"
#include "activities/micromarkd/MarkdownIndexStorage.h"
#include "activities/micromarkd/MarkdownRecovery.h"
#include "activities/reader/ReaderActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/back32.h"
#include "util/BookCacheUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr char VAULT_ROOT[] = "/vault";
constexpr char VAULT_PREFIX[] = "/vault/";

bool notePathReserved(const std::string& path) {
  return Storage.exists(path.c_str()) || Storage.exists((path + micromarkd::NOTE_TEMPORARY_SUFFIX).c_str()) ||
         Storage.exists((path + micromarkd::NOTE_BACKUP_SUFFIX).c_str()) ||
         Storage.exists((path + micromarkd::NOTE_READY_SUFFIX).c_str());
}
}  // namespace

MarkdownVaultActivity::MarkdownVaultActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             std::string initialPath)
    : UiListActivity("MarkdownVault", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      path_(std::move(initialPath)) {}

void MarkdownVaultActivity::onEnter() {
  UiListActivity::onEnter();
  app.on(ACTION_HOME, &MarkdownVaultActivity::toolbarActionTrampoline, this);
  app.on(ACTION_NEW_NOTE, &MarkdownVaultActivity::toolbarActionTrampoline, this);
  app.on(ACTION_NEW_FOLDER, &MarkdownVaultActivity::toolbarActionTrampoline, this);
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
  status_.clear();
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
    const std::string_view filename{nameBuffer_.data()};

    if (isDirectory) {
      entries_.emplace_back(std::string(nameBuffer_.data()) + "/");
    } else if (FsHelpers::hasMarkdownExtension(filename) || FsHelpers::hasTxtExtension(filename) ||
               FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename)) {
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
  const bool showActionRows = !mappedInput.hasTouch();
  const size_t count = entries_.size() + (showActionRows ? ACTION_ROW_COUNT : 0);
  rowNames_.resize(count);
  rowExtensions_.resize(count);
  rowItems_.clear();
  rowItems_.reserve(count);

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

  if (!showActionRows) return;

  const size_t actionStart = entries_.size();
  const char* actionLabels[ACTION_ROW_COUNT] = {tr(STR_BACK), tr(STR_HOME), tr(STR_MICROMARKD_NEW_NOTE),
                                                tr(STR_MICROMARKD_NEW_FOLDER)};
  const fui::BitmapRef actionIcons[ACTION_ROW_COUNT] = {
      fui::bitmapFromIcon(icon_arrow_left_32), fui::bitmapFromIcon(icon_house_24), listIconFor(UIIcon::NewNote),
      fui::bitmapFromIcon(icon_folder_plus_24)};
  for (int action = 0; action < ACTION_ROW_COUNT; action++) {
    const size_t index = actionStart + static_cast<size_t>(action);
    rowNames_[index] = actionLabels[action];
    rowExtensions_[index].clear();

    fui::ListItem item{};
    item.label = rowNames_[index].c_str();
    item.icon = actionIcons[action];
    item.actionValue = static_cast<int16_t>(index);
    rowItems_.push_back(item);
  }
}

void MarkdownVaultActivity::toolbarActionTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<MarkdownVaultActivity*>(user);
  self->app.clearTapFlash();
  switch (event.action) {
    case ACTION_HOME:
      self->onGoHome();
      break;
    case ACTION_NEW_NOTE:
      self->startNewNoteHere();
      break;
    case ACTION_NEW_FOLDER:
      self->startNewFolder();
      break;
    default:
      break;
  }
}

int MarkdownVaultActivity::listCount() const { return static_cast<int>(rowItems_.size()); }

std::string MarkdownVaultActivity::fullPath(const std::string& entry) const {
  return path_ + (path_.back() == '/' ? "" : "/") + entry;
}

void MarkdownVaultActivity::selectEntry(const std::string& entry) {
  const auto found = std::find(entries_.begin(), entries_.end(), entry);
  if (found == entries_.end()) return;
  nav.selected = static_cast<int>(std::distance(entries_.begin(), found));
  nav.follow(listCount());
}

void MarkdownVaultActivity::setStatus(std::string message) {
  status_ = std::move(message);
  requestUpdate();
}

void MarkdownVaultActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();

  if (!mappedInput.hasTouch() && static_cast<size_t>(index) >= entries_.size()) {
    switch (static_cast<size_t>(index) - entries_.size()) {
      case 0:
        navigateToParent();
        break;
      case 1:
        onGoHome();
        break;
      case 2:
        startNewNoteHere();
        break;
      case 3:
        startNewFolder();
        break;
      default:
        break;
    }
    return;
  }

  const std::string& entry = entries_[index];
  if (!entry.empty() && entry.back() == '/') {
    openDirectory(entry);
    return;
  }
  openNote(fullPath(entry));
}

void MarkdownVaultActivity::onRowLongPress(const int index) {
  if (index < 0 || static_cast<size_t>(index) >= entries_.size()) return;
  app.clearTapFlash();
  nav.selected = index;

  const std::string& entry = entries_[index];
  if (!entry.empty() && entry.back() == '/') {
    showFolderActions(entry, fullPath(entry.substr(0, entry.size() - 1)));
  } else if (FsHelpers::hasMarkdownExtension(entry)) {
    showNoteActions(entry, fullPath(entry));
  }
}

void MarkdownVaultActivity::startNewNoteHere() {
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "New note title", "",
                                                                 MAX_ENTRY_NAME_BYTES, InputType::Text),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto* keyboard = std::get_if<KeyboardResult>(&result.data);
                           if (!keyboard) return;
                           openNewNoteEditor(keyboard->text);
                         });
}

std::string MarkdownVaultActivity::uniqueNotePath(const std::string& filename) const {
  std::string candidate = fullPath(filename);
  if (!notePathReserved(candidate)) return candidate;

  const size_t extension = filename.size() >= 3 ? filename.size() - 3 : filename.size();
  const std::string stem = filename.substr(0, extension);
  for (int suffix = 2; suffix < 10000; suffix++) {
    candidate = fullPath(stem + " " + std::to_string(suffix) + ".md");
    if (!notePathReserved(candidate)) return candidate;
  }
  return {};
}

void MarkdownVaultActivity::openNewNoteEditor(const std::string& rawTitle) {
  auto directory = Storage.open(path_.c_str());
  if (!directory || !directory.isDirectory()) {
    setStatus("Could not create note");
    return;
  }
  directory.close();

  std::string title = micromarkd::trimNoteTitle(rawTitle);
  if (title.empty()) title = "Untitled";
  const std::string notePath = uniqueNotePath(micromarkd::safeNoteFilename(title));
  if (notePath.empty()) {
    setStatus("Could not create note");
    return;
  }

  std::vector<std::string> lines;
  lines.reserve(2);
  lines.push_back("# " + title);
  lines.emplace_back();

  startActivityForResult(std::make_unique<MarkdownEditorActivity>(renderer, mappedInput, notePath, std::move(lines),
                                                                  /*trailingNewline=*/true),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto* file = std::get_if<FilePathResult>(&result.data);
                           if (!file || file->path.empty()) return;

                           loadEntries();
                           openReader(file->path);
                         });
}

void MarkdownVaultActivity::startNewFolder() {
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "New folder", "",
                                                                 MAX_ENTRY_NAME_BYTES, InputType::Text),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto* keyboard = std::get_if<KeyboardResult>(&result.data);
                           if (!keyboard) return;
                           createFolder(keyboard->text);
                         });
}

std::string MarkdownVaultActivity::uniqueFolderPath(const std::string& component) const {
  std::string candidate = fullPath(component);
  if (!Storage.exists(candidate.c_str())) return candidate;

  for (int suffix = 2; suffix < 10000; suffix++) {
    candidate = fullPath(component + " " + std::to_string(suffix));
    if (!Storage.exists(candidate.c_str())) return candidate;
  }
  return {};
}

void MarkdownVaultActivity::createFolder(const std::string& rawName) {
  const std::string component = micromarkd::safeVaultPathComponent(rawName);
  const std::string folderPath = uniqueFolderPath(component);
  if (folderPath.empty() || !Storage.mkdir(folderPath.c_str(), true)) {
    setStatus("Could not create folder");
    return;
  }

  path_ = folderPath;
  loadEntries();
  nav.reset();
  requestUpdate();
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

void MarkdownVaultActivity::openNote(const std::string& notePath) { openReader(notePath); }

void MarkdownVaultActivity::openReader(const std::string& notePath) {
  auto reader = ReaderActivity::create(renderer, mappedInput, notePath, false);
  if (!reader) return;
  reader->setBackReturnsToPreviousActivity();
  std::unique_ptr<Activity> activity = std::move(reader);
  activityManager.pushActivity(std::move(activity));
}

void MarkdownVaultActivity::editNote(const std::string& notePath) {
  startActivityForResult(std::make_unique<MarkdownEditorActivity>(renderer, mappedInput, notePath),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto* file = std::get_if<FilePathResult>(&result.data);
                           if (!file || file->path.empty()) return;

                           loadEntries();
                           openReader(file->path);
                         });
}

void MarkdownVaultActivity::showNoteActions(const std::string& entry, const std::string& notePath) {
  const char* options[] = {tr(STR_OPEN), "Edit note", "Rename note", "Backlinks", tr(STR_DELETE)};
  popup_.show("Note actions", options, 5, 1, [this, entry, notePath](const int option) {
    switch (option) {
      case 0:
        openNote(notePath);
        break;
      case 1:
        editNote(notePath);
        break;
      case 2:
        startRenameNote(entry, notePath);
        break;
      case 3:
        activityManager.pushActivity(std::make_unique<MarkdownBacklinksActivity>(renderer, mappedInput, notePath));
        break;
      case 4:
        confirmDelete(entry, notePath);
        break;
      default:
        break;
    }
  });
  requestUpdate();
}

void MarkdownVaultActivity::showFolderActions(const std::string& entry, const std::string& folderPath) {
  const char* options[] = {tr(STR_OPEN), "Rename folder"};
  popup_.show("Folder actions", options, 2, 0, [this, entry, folderPath](const int option) {
    if (option == 0) {
      openDirectory(entry);
    } else if (option == 1) {
      startRenameFolder(entry, folderPath);
    }
  });
  requestUpdate();
}

void MarkdownVaultActivity::startRenameNote(const std::string& entry, const std::string& notePath) {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Rename note", displayName(entry),
                                              MAX_ENTRY_NAME_BYTES, InputType::Text),
      [this, notePath](const ActivityResult& result) {
        if (result.isCancelled) return;
        const auto* keyboard = std::get_if<KeyboardResult>(&result.data);
        if (!keyboard) return;
        renameNote(notePath, keyboard->text);
      });
}

void MarkdownVaultActivity::migrateNoteState(const std::string& oldPath, const std::string& newPath) {
  Txt oldNote(oldPath, "/.crosspoint");
  Txt newNote(newPath, "/.crosspoint");
  const std::string oldCachePath = oldNote.getCachePath();
  const std::string newCachePath = newNote.getCachePath();

  clearBookCache(oldPath);
  clearBookCache(newPath);
  if (!removeMarkdownIndexRecord(oldPath)) {
    LOG_ERR("MDV", "Failed to remove old metadata index after rename: %s", oldPath.c_str());
  }
  if (!removeMarkdownIndexRecord(newPath)) {
    LOG_ERR("MDV", "Failed to remove target metadata index after rename: %s", newPath.c_str());
  }
  RECENT_BOOKS.updatePath(oldPath, newPath, oldCachePath, newCachePath);
}

void MarkdownVaultActivity::renameNote(const std::string& oldPath, const std::string& rawName) {
  std::string filename = micromarkd::safeNoteFilename(rawName);
  if (FsHelpers::checkFileExtension(oldPath, ".markdown")) {
    filename.resize(filename.size() - 3);
    filename += ".markdown";
  }
  const std::string newPath = fullPath(filename);
  if (newPath == oldPath) return;
  if (notePathReserved(newPath)) {
    setStatus("Name already exists");
    return;
  }
  if (!removeMarkdownRecoverySidecars(oldPath)) {
    setStatus("Could not prepare note rename");
    return;
  }
  if (!Storage.rename(oldPath.c_str(), newPath.c_str())) {
    setStatus("Could not rename note");
    return;
  }

  migrateNoteState(oldPath, newPath);
  loadEntries();
  selectEntry(filename);
  requestUpdate(true);
}

void MarkdownVaultActivity::startRenameFolder(const std::string& entry, const std::string& folderPath) {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Rename folder", displayName(entry),
                                              MAX_ENTRY_NAME_BYTES, InputType::Text),
      [this, folderPath](const ActivityResult& result) {
        if (result.isCancelled) return;
        const auto* keyboard = std::get_if<KeyboardResult>(&result.data);
        if (!keyboard) return;
        renameFolder(folderPath, keyboard->text);
      });
}

bool MarkdownVaultActivity::collectMarkdownFiles(const std::string& rootPath, std::vector<std::string>& files) {
  files.clear();
  std::vector<std::string> directories;
  directories.reserve(16);
  directories.push_back(rootPath);
  bool complete = true;

  for (size_t directoryIndex = 0; directoryIndex < directories.size(); directoryIndex++) {
    auto directory = Storage.open(directories[directoryIndex].c_str());
    if (!directory || !directory.isDirectory()) {
      complete = false;
      continue;
    }

    directory.rewindDirectory();
    for (auto entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
      entry.getName(nameBuffer_.data(), nameBuffer_.size());
      const bool isDirectory = entry.isDirectory();
      entry.close();

      if (nameBuffer_[0] == '.' || std::strcmp(nameBuffer_.data(), "System Volume Information") == 0) continue;
      const std::string entryPath = directories[directoryIndex] + "/" + nameBuffer_.data();

      if (isDirectory) {
        if (directories.size() >= MAX_RENAME_DIRECTORIES) {
          complete = false;
        } else {
          directories.push_back(entryPath);
        }
        continue;
      }
      if (!FsHelpers::hasMarkdownExtension(nameBuffer_.data())) continue;
      if (files.size() >= MAX_RENAME_NOTES) {
        complete = false;
        continue;
      }
      files.push_back(entryPath);
    }
    directory.close();
  }
  return complete;
}

void MarkdownVaultActivity::renameFolder(const std::string& oldPath, const std::string& rawName) {
  const std::string component = micromarkd::safeVaultPathComponent(rawName);
  const std::string newPath = fullPath(component);
  if (newPath == oldPath) return;
  if (Storage.exists(newPath.c_str())) {
    setStatus("Name already exists");
    return;
  }
  if (!Storage.rename(oldPath.c_str(), newPath.c_str())) {
    setStatus("Could not rename folder");
    return;
  }

  std::vector<std::string> renamedNotes;
  const bool complete = collectMarkdownFiles(newPath, renamedNotes);
  for (const auto& newNotePath : renamedNotes) {
    const std::string oldNotePath = micromarkd::rebaseVaultPath(newNotePath, newPath, oldPath);
    if (!oldNotePath.empty()) migrateNoteState(oldNotePath, newNotePath);
  }

  std::vector<std::pair<std::string, std::string>> recentMappings;
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    const std::string newRecentPath = micromarkd::rebaseVaultPath(book.path, oldPath, newPath);
    if (!newRecentPath.empty()) recentMappings.push_back({book.path, newRecentPath});
  }
  for (const auto& [oldRecentPath, newRecentPath] : recentMappings) {
    migrateNoteState(oldRecentPath, newRecentPath);
  }

  loadEntries();
  selectEntry(component + "/");
  if (!complete) setStatus("Folder renamed; remaining caches will rebuild lazily");
  requestUpdate(true);
}

void MarkdownVaultActivity::confirmDelete(const std::string& entry, const std::string& notePath) {
  std::string heading = tr(STR_DELETE) + std::string("? ");
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, entry),
                         [this, notePath](const ActivityResult& result) {
                           if (result.isCancelled) return;

                           if (!removeMarkdownRecoverySidecars(notePath)) {
                             LOG_ERR("MDV", "Failed to remove note recovery sidecars: %s", notePath.c_str());
                             return;
                           }
                           clearBookCache(notePath);
                           if (!Storage.remove(notePath.c_str())) {
                             LOG_ERR("MDV", "Failed to delete note: %s", notePath.c_str());
                             return;
                           }
                           RECENT_BOOKS.removeByPath(notePath);
                           if (!removeMarkdownIndexRecord(notePath)) {
                             LOG_ERR("MDV", "Deleted note but failed to remove its metadata index: %s",
                                     notePath.c_str());
                           }

                           loadEntries();
                           if (nav.selected >= listCount()) nav.selected = std::max(0, listCount() - 1);
                           nav.follow(listCount());
                           requestUpdate(true);
                         });
}

bool MarkdownVaultActivity::handleCustomInput() {
  if (!popup_.isActive()) return false;
  return popup_.handleInput(mappedInput, [this] { requestUpdate(); });
}

void MarkdownVaultActivity::navigateButtons() {
  // Keep vault navigation identical to every other button-only list: a short
  // directional press moves the focus, while a hold advances by one viewport.
  UiListActivity::navigateButtons();
}

void MarkdownVaultActivity::onBackButton() {
  if (!mappedInput.hasTouch()) {
    const bool actionFocused = nav.selected >= static_cast<int>(entries_.size());
    moveSelectionTo(actionFocused ? 0 : static_cast<int>(entries_.size()));
    return;
  }

  navigateToParent();
}

void MarkdownVaultActivity::navigateToParent() {
  if (path_ == VAULT_ROOT) {
    finish();
    return;
  }

  const std::string previous = path_;
  const size_t slash = path_.find_last_of('/');
  path_.resize(slash == std::string::npos || slash < sizeof(VAULT_ROOT) - 1 ? sizeof(VAULT_ROOT) - 1 : slash);
  loadEntries();

  const std::string directory = previous.substr(previous.find_last_of('/') + 1) + "/";
  selectEntry(directory);
  nav.top = 0;
  nav.follow(listCount());
  requestUpdate();
}

const char* MarkdownVaultActivity::headerTitle() const {
  if (!status_.empty()) return status_.c_str();
  return header_.empty() ? tr(STR_MICROMARKD_VAULT) : header_.c_str();
}

std::string MarkdownVaultActivity::displayName(const std::string& entry) {
  if (entry.empty()) return {};
  if (entry.back() == '/') return entry.substr(0, entry.size() - 1);
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

  if (mappedInput.hasTouch()) {
    const fui::Rect toolbar = screen.takeBottom(screen.theme().rowHeight, screen.theme().spaceSm);
    const int16_t buttonSize = static_cast<int16_t>(toolbar.height - 4);
    const int16_t gap = screen.theme().spaceSm;
    const int16_t totalWidth = static_cast<int16_t>(buttonSize * 4 + gap * 3);
    const int16_t startX = static_cast<int16_t>(toolbar.x + (toolbar.width - totalWidth) / 2);

    auto addToolbarButton = [&](const int16_t index, const fui::ActionId action, const fui::BitmapRef icon) {
      fui::ButtonProps props;
      props.icon = icon;
      props.iconSize = 24;
      props.action = action;
      props.inputMask = fui::InputTouch;
      props.styles = fui::plainStyles();
      props.radius = 8;
      screen.button(props, fui::Rect{static_cast<int16_t>(startX + index * (buttonSize + gap)),
                                     static_cast<int16_t>(toolbar.y + 2), buttonSize, buttonSize});
    };

    addToolbarButton(0, ACTION_BACK, fui::bitmapFromIcon(icon_arrow_left_32));
    addToolbarButton(1, ACTION_HOME, fui::bitmapFromIcon(icon_house_24));
    addToolbarButton(2, ACTION_NEW_NOTE, listIconFor(UIIcon::NewNote));
    addToolbarButton(3, ACTION_NEW_FOLDER, fui::bitmapFromIcon(icon_folder_plus_24));
  }

  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (loadFailed_) {
    screen.centeredText("Could not open vault", screen.theme().bodyText);
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
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), listCount() == 0 ? "" : tr(STR_OPEN), listCount() == 0 ? "" : tr(STR_DIR_LEFT),
                            listCount() == 0 ? "" : tr(STR_DIR_RIGHT));
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
