#include "MarkdownSearchActivity.h"

#ifdef MICROMARKD_APP

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <MarkdownDocument.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "activities/micromarkd/MarkdownEditorActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
constexpr char MODULE[] = "MDS";
constexpr char VAULT_ROOT[] = "/vault";
constexpr size_t NAME_BUFFER_SIZE = 500;

bool isUtf8Continuation(const unsigned char ch) { return (ch & 0xC0) == 0x80; }
}  // namespace

MarkdownSearchActivity::MarkdownSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               std::string query)
    : UiListActivity("MarkdownSearch", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      queryText_(std::move(query)),
      query_(micromarkd::compileSearchQuery(queryText_)),
      header_("Search: " + queryText_) {}

void MarkdownSearchActivity::onEnter() {
  UiListActivity::onEnter();
  beginSearch();
}

void MarkdownSearchActivity::onExit() {
  closeActiveFile();
  Activity::onExit();
  rowItems_.clear();
  results_.clear();
  notePaths_.clear();
  directories_.clear();
}

void MarkdownSearchActivity::loop() {
  UiListActivity::loop();
  if (phase_ == SearchPhase::Enumerating || phase_ == SearchPhase::Scanning) advanceSearch();
}

std::string MarkdownSearchActivity::joinPath(const std::string& directory, const std::string& name) {
  return directory + (directory.back() == '/' ? "" : "/") + name;
}

std::string MarkdownSearchActivity::overlapTail(const std::string& text, const size_t bytes) {
  if (text.size() <= bytes) return text;
  size_t start = text.size() - bytes;
  while (start > 0 && isUtf8Continuation(static_cast<unsigned char>(text[start]))) start--;
  return text.substr(start);
}

void MarkdownSearchActivity::beginSearch() {
  closeActiveFile();
  results_.clear();
  results_.reserve(MAX_RESULTS);
  rowItems_.clear();
  directories_.clear();
  notePaths_.clear();
  report_ = {};
  directoryIndex_ = 0;
  noteIndex_ = 0;
  phase_ = SearchPhase::Idle;

  if (query_.empty()) {
    header_ = "Search";
    emptyMessage_ = "Enter a search query";
    phase_ = SearchPhase::Complete;
    requestUpdate();
    return;
  }

  if (!Storage.exists(VAULT_ROOT)) {
    header_ = "Search: " + queryText_;
    emptyMessage_ = "No Markdown notes yet";
    phase_ = SearchPhase::Complete;
    requestUpdate();
    return;
  }

  directories_.reserve(32);
  notePaths_.reserve(64);
  directories_.emplace_back(VAULT_ROOT);
  phase_ = SearchPhase::Enumerating;
  updateProgress(true);
}

void MarkdownSearchActivity::advanceSearch() {
  if (phase_ == SearchPhase::Enumerating) {
    enumerateNextDirectory();
  } else if (phase_ == SearchPhase::Scanning) {
    scanActiveNoteStep();
  }
}

void MarkdownSearchActivity::enumerateNextDirectory() {
  if (directoryIndex_ >= directories_.size() || report_.fileLimitReached) {
    phase_ = SearchPhase::Scanning;
    updateProgress(true);
    if (notePaths_.empty()) finishSearch();
    return;
  }

  const std::string directoryPath = directories_[directoryIndex_++];
  auto directory = Storage.open(directoryPath.c_str());
  if (!directory || !directory.isDirectory()) {
    LOG_ERR(MODULE, "Failed to scan search directory: %s", directoryPath.c_str());
    report_.failures++;
    updateProgress();
    return;
  }

  std::array<char, NAME_BUFFER_SIZE> nameBuffer{};
  report_.directoriesScanned++;
  directory.rewindDirectory();
  for (auto entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    entry.getName(nameBuffer.data(), nameBuffer.size());
    const bool isDirectory = entry.isDirectory();
    entry.close();

    if (nameBuffer[0] == '.' || std::strcmp(nameBuffer.data(), "System Volume Information") == 0) continue;

    if (isDirectory) {
      if (directories_.size() >= MAX_DIRECTORIES) {
        report_.directoriesSkipped++;
      } else {
        directories_.push_back(joinPath(directoryPath, nameBuffer.data()));
      }
      continue;
    }

    const std::string path = joinPath(directoryPath, nameBuffer.data());
    if (!micromarkd::isVaultMarkdownPath(path)) continue;
    if (notePaths_.size() >= MAX_FILES) {
      report_.fileLimitReached = true;
      break;
    }

    notePaths_.push_back(path);
    report_.filesQueued++;
  }
  directory.close();

  if (directoryIndex_ >= directories_.size() || report_.fileLimitReached) phase_ = SearchPhase::Scanning;
  updateProgress();
  if (phase_ == SearchPhase::Scanning && notePaths_.empty()) finishSearch();
}

void MarkdownSearchActivity::beginNextNote() {
  if (noteIndex_ >= notePaths_.size() || report_.resultLimitReached || report_.byteLimitReached) {
    finishSearch();
    return;
  }

  activePath_ = notePaths_[noteIndex_];
  activeTitle_ = micromarkd::vaultNoteDisplayName(activePath_);
  activeFolder_ = micromarkd::vaultNoteFolderLabel(activePath_);
  activeSnippet_.clear();
  activeOverlap_.clear();
  activeMatches_.fill(false);
  activeBytes_ = 0;
  activeMetadataMatch_ = false;
  report_.filesScanned++;

  micromarkd::matchSearchTerms(query_, activeTitle_, activeMatches_);
  micromarkd::matchSearchTerms(query_, activeFolder_, activeMatches_);
  activeMetadataMatch_ = micromarkd::allSearchTermsMatched(query_, activeMatches_);
  if (activeMetadataMatch_) {
    activeSnippet_ = activeFolder_;
    finishActiveNote();
    return;
  }

  if (!Storage.openFileForRead(MODULE, activePath_, activeFile_)) {
    LOG_ERR(MODULE, "Failed to open note for search: %s", activePath_.c_str());
    report_.failures++;
    noteIndex_++;
    activePath_.clear();
    updateProgress();
    return;
  }
  activeFileOpen_ = true;
}

void MarkdownSearchActivity::scanActiveNoteStep() {
  if (!activeFileOpen_) {
    beginNextNote();
    return;
  }

  std::array<uint8_t, READ_CHUNK_BYTES> buffer{};
  size_t stepBytes = 0;
  while (activeFile_.available() && stepBytes < READ_BYTES_PER_STEP && activeBytes_ < MAX_BYTES_PER_NOTE &&
         report_.bytesScanned < MAX_TOTAL_BYTES) {
    const size_t readLimit = std::min({buffer.size(), READ_BYTES_PER_STEP - stepBytes,
                                       MAX_BYTES_PER_NOTE - activeBytes_, MAX_TOTAL_BYTES - report_.bytesScanned});
    if (readLimit == 0) break;

    const int bytesRead = activeFile_.read(buffer.data(), readLimit);
    if (bytesRead < 0) {
      LOG_ERR(MODULE, "Failed while searching note: %s", activePath_.c_str());
      report_.failures++;
      closeActiveFile();
      noteIndex_++;
      activePath_.clear();
      updateProgress();
      return;
    }
    if (bytesRead == 0) break;

    const size_t readSize = static_cast<size_t>(bytesRead);
    activeBytes_ += readSize;
    stepBytes += readSize;
    report_.bytesScanned += readSize;

    std::string text = activeOverlap_;
    text.append(reinterpret_cast<const char*>(buffer.data()), readSize);
    const auto before = activeMatches_;
    micromarkd::matchSearchTerms(query_, text, activeMatches_);
    if (activeSnippet_.empty() && before != activeMatches_ &&
        micromarkd::anySearchTermMatched(query_, activeMatches_)) {
      activeSnippet_ = micromarkd::makeSearchSnippet(text, query_, MAX_SNIPPET_BYTES);
    }
    if (micromarkd::allSearchTermsMatched(query_, activeMatches_)) break;

    activeOverlap_ = overlapTail(text, query_.longestTermBytes + 4);
  }

  const bool hasMore = activeFile_.available();
  const bool matched = micromarkd::allSearchTermsMatched(query_, activeMatches_);
  const bool noteLimitReached = hasMore && activeBytes_ >= MAX_BYTES_PER_NOTE;
  const bool totalLimitReached = hasMore && report_.bytesScanned >= MAX_TOTAL_BYTES;
  if (!hasMore || matched || noteLimitReached || totalLimitReached) {
    if (noteLimitReached || totalLimitReached) report_.filesTruncated++;
    if (totalLimitReached) report_.byteLimitReached = true;
    finishActiveNote();
  }
}

void MarkdownSearchActivity::finishActiveNote() {
  closeActiveFile();

  const bool matched = micromarkd::allSearchTermsMatched(query_, activeMatches_);
  bool resultAdded = false;
  if (matched) {
    if (results_.size() >= MAX_RESULTS) {
      report_.resultLimitReached = true;
    } else {
      SearchResult result;
      result.path = activePath_;
      result.title = activeTitle_;
      result.rank = activeMetadataMatch_ ? 0 : 1;
      if (activeSnippet_.empty()) activeSnippet_ = activeFolder_;
      if (!activeFolder_.empty() && activeSnippet_ != activeFolder_) {
        result.subtitle = activeFolder_ + ": " + activeSnippet_;
      } else {
        result.subtitle = activeSnippet_;
      }
      results_.push_back(std::move(result));
      resultAdded = true;
    }
  }

  noteIndex_++;
  activePath_.clear();
  activeTitle_.clear();
  activeFolder_.clear();
  activeSnippet_.clear();
  activeOverlap_.clear();
  activeMatches_.fill(false);
  activeBytes_ = 0;
  activeMetadataMatch_ = false;

  const bool forceUpdate =
      (resultAdded && results_.size() == 1) || report_.filesScanned % PROGRESS_UPDATE_INTERVAL == 0;
  updateProgress(forceUpdate);
  if (noteIndex_ >= notePaths_.size() || report_.resultLimitReached || report_.byteLimitReached) finishSearch();
}

void MarkdownSearchActivity::finishSearch() {
  closeActiveFile();
  phase_ = SearchPhase::Complete;

  RenderLock lock(*this);
  std::sort(results_.begin(), results_.end(), [](const SearchResult& left, const SearchResult& right) {
    if (left.rank != right.rank) return left.rank < right.rank;
    return FsHelpers::naturalLess(left.path, right.path);
  });
  header_ = report_.partial() ? "Search (partial): " + queryText_ : "Search: " + queryText_;
  rebuildRows();
  buildEmptyMessage();
  requestUpdate();
}

void MarkdownSearchActivity::updateProgress(const bool force) {
  const bool intervalReached = phase_ == SearchPhase::Enumerating
                                   ? report_.directoriesScanned % PROGRESS_UPDATE_INTERVAL == 0
                                   : report_.filesScanned % PROGRESS_UPDATE_INTERVAL == 0;
  if (!force && !intervalReached) return;

  RenderLock lock(*this);
  if (phase_ == SearchPhase::Enumerating) {
    header_ = "Indexing " + std::to_string(report_.filesQueued) + ": " + queryText_;
    emptyMessage_ = "Scanning vault...";
  } else if (phase_ == SearchPhase::Scanning) {
    header_ = "Searching " + std::to_string(std::min(noteIndex_ + (activeFileOpen_ ? 1 : 0), notePaths_.size())) + "/" +
              std::to_string(notePaths_.size()) + ": " + queryText_;
    emptyMessage_ = "Searching notes...";
  }
  rebuildRows();
  requestUpdate();
}

void MarkdownSearchActivity::closeActiveFile() {
  if (!activeFileOpen_) return;
  activeFile_.close();
  activeFileOpen_ = false;
}

void MarkdownSearchActivity::rebuildRows() {
  rowItems_.clear();
  rowItems_.reserve(results_.size());
  for (size_t index = 0; index < results_.size(); index++) {
    fui::ListItem item{};
    item.label = results_[index].title.c_str();
    if (!results_[index].subtitle.empty()) item.subtitle = results_[index].subtitle.c_str();
    item.icon = listIconFor(UITheme::getFileIcon(results_[index].path), 32);
    item.actionValue = static_cast<int16_t>(index);
    rowItems_.push_back(item);
  }
}

void MarkdownSearchActivity::buildEmptyMessage() {
  if (!rowItems_.empty()) return;
  emptyMessage_ = report_.partial() ? "No matches in the scanned part of the vault" : "No matches";
}

int MarkdownSearchActivity::listCount() const { return static_cast<int>(rowItems_.size()); }

const char* MarkdownSearchActivity::headerTitle() const { return header_.c_str(); }

void MarkdownSearchActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  onSelectBook(results_[index].path);
}

void MarkdownSearchActivity::onRowLongPress(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  showResultActions(index);
}

void MarkdownSearchActivity::showResultActions(const int index) {
  if (index < 0 || index >= listCount()) return;
  const std::string path = results_[index].path;
  const char* options[] = {tr(STR_OPEN), "Edit note"};
  popup_.show("Search result", options, 2, 0, [this, path](const int option) {
    if (option == 0) {
      onSelectBook(path);
    } else if (option == 1) {
      editNote(path);
    }
  });
  requestUpdate();
}

void MarkdownSearchActivity::editNote(const std::string& path) {
  startActivityForResult(std::make_unique<MarkdownEditorActivity>(renderer, mappedInput, path),
                         [](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto* file = std::get_if<FilePathResult>(&result.data);
                           if (!file || file->path.empty()) return;
                           activityManager.goToReader(file->path);
                         });
}

bool MarkdownSearchActivity::handleCustomInput() {
  if (!popup_.isActive()) return false;
  return popup_.handleInput(mappedInput, [this] { requestUpdate(); });
}

void MarkdownSearchActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (rowItems_.empty()) {
    screen.centeredText(emptyMessage_.c_str(), screen.theme().bodyText);
    return;
  }

  fui::ListProps props{};
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  fui::TextStyle label = screen.theme().smallText;
  label.bold = true;
  props.labelText = label;
  fui::TextStyle subtitle = screen.theme().smallText;
  subtitle.maxLines = 2;
  props.subtitleText = subtitle;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void MarkdownSearchActivity::drawFooter() {
  const bool empty = rowItems_.empty();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), empty ? "" : tr(STR_OPEN), empty ? "" : tr(STR_DIR_UP),
                                            empty ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void MarkdownSearchActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();
  drawFooter();
  if (popup_.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer();
}

#endif  // MICROMARKD_APP
