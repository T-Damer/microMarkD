#include "MarkdownSearchActivity.h"

#ifdef MICROMARKD_APP

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <MarkdownDocument.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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
  runSearch();
}

void MarkdownSearchActivity::onExit() {
  Activity::onExit();
  rowItems_.clear();
  results_.clear();
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

void MarkdownSearchActivity::runSearch() {
  results_.clear();
  report_ = {};
  if (query_.empty()) {
    emptyMessage_ = "Enter a search query";
    rebuildRows();
    return;
  }

  if (!Storage.exists(VAULT_ROOT)) {
    emptyMessage_ = "No Markdown notes yet";
    rebuildRows();
    return;
  }

  std::vector<std::string> directories;
  directories.reserve(32);
  directories.emplace_back(VAULT_ROOT);
  std::array<char, NAME_BUFFER_SIZE> nameBuffer{};
  bool stop = false;

  for (size_t directoryIndex = 0; directoryIndex < directories.size() && !stop; directoryIndex++) {
    const std::string directoryPath = directories[directoryIndex];
    auto directory = Storage.open(directoryPath.c_str());
    if (!directory || !directory.isDirectory()) {
      LOG_ERR(MODULE, "Failed to scan search directory: %s", directoryPath.c_str());
      report_.failures++;
      continue;
    }

    report_.directoriesScanned++;
    directory.rewindDirectory();
    for (auto entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
      entry.getName(nameBuffer.data(), nameBuffer.size());
      const bool isDirectory = entry.isDirectory();
      entry.close();

      if (nameBuffer[0] == '.' || std::strcmp(nameBuffer.data(), "System Volume Information") == 0) continue;

      if (isDirectory) {
        if (directories.size() >= MAX_DIRECTORIES) {
          report_.directoriesSkipped++;
        } else {
          directories.push_back(joinPath(directoryPath, nameBuffer.data()));
        }
        continue;
      }

      const std::string path = joinPath(directoryPath, nameBuffer.data());
      if (!micromarkd::isVaultMarkdownPath(path)) continue;
      if (report_.filesScanned >= MAX_FILES) {
        report_.fileLimitReached = true;
        stop = true;
        break;
      }

      SearchResult result;
      report_.filesScanned++;
      if (scanNote(path, result)) results_.push_back(std::move(result));
      if (results_.size() >= MAX_RESULTS) {
        report_.resultLimitReached = true;
        stop = true;
        break;
      }
      if (report_.bytesScanned >= MAX_TOTAL_BYTES) {
        report_.byteLimitReached = true;
        stop = true;
        break;
      }
      if (report_.filesScanned % 8 == 0) vTaskDelay(1);
    }
    directory.close();
  }

  std::sort(results_.begin(), results_.end(), [](const SearchResult& left, const SearchResult& right) {
    if (left.rank != right.rank) return left.rank < right.rank;
    return FsHelpers::naturalLess(left.path, right.path);
  });
  header_ = report_.partial() ? "Search (partial): " + queryText_ : "Search: " + queryText_;
  rebuildRows();
  buildEmptyMessage();
}

bool MarkdownSearchActivity::scanNote(const std::string& path, SearchResult& result) {
  micromarkd::SearchTermMatches matches{};
  const std::string title = micromarkd::vaultNoteDisplayName(path);
  const std::string folder = micromarkd::vaultNoteFolderLabel(path);
  micromarkd::matchSearchTerms(query_, title, matches);
  micromarkd::matchSearchTerms(query_, folder, matches);

  const bool metadataMatch = micromarkd::allSearchTermsMatched(query_, matches);
  std::string snippet;
  if (metadataMatch) {
    snippet = folder;
  } else {
    HalFile file;
    if (!Storage.openFileForRead(MODULE, path, file)) {
      LOG_ERR(MODULE, "Failed to open note for search: %s", path.c_str());
      report_.failures++;
      return false;
    }

    std::array<uint8_t, READ_CHUNK_BYTES> buffer{};
    std::string overlap;
    size_t fileBytes = 0;
    while (file.available() && fileBytes < MAX_BYTES_PER_NOTE && report_.bytesScanned < MAX_TOTAL_BYTES) {
      const size_t readLimit =
          std::min({buffer.size(), MAX_BYTES_PER_NOTE - fileBytes, MAX_TOTAL_BYTES - report_.bytesScanned});
      if (readLimit == 0) break;
      const int bytesRead = file.read(buffer.data(), readLimit);
      if (bytesRead < 0) {
        LOG_ERR(MODULE, "Failed while searching note: %s", path.c_str());
        report_.failures++;
        file.close();
        return false;
      }
      if (bytesRead == 0) break;

      fileBytes += static_cast<size_t>(bytesRead);
      report_.bytesScanned += static_cast<size_t>(bytesRead);
      std::string text = overlap;
      text.append(reinterpret_cast<const char*>(buffer.data()), static_cast<size_t>(bytesRead));

      const auto before = matches;
      micromarkd::matchSearchTerms(query_, text, matches);
      if (snippet.empty() && before != matches && micromarkd::anySearchTermMatched(query_, matches)) {
        snippet = micromarkd::makeSearchSnippet(text, query_, MAX_SNIPPET_BYTES);
      }
      if (micromarkd::allSearchTermsMatched(query_, matches)) break;

      overlap = overlapTail(text, query_.longestTermBytes + 4);
    }

    if (file.available()) report_.filesTruncated++;
    file.close();
  }

  if (!micromarkd::allSearchTermsMatched(query_, matches)) return false;

  result.path = path;
  result.title = title;
  result.rank = metadataMatch ? 0 : 1;
  if (snippet.empty()) snippet = folder;
  if (!folder.empty() && snippet != folder) {
    result.subtitle = folder + ": " + snippet;
  } else {
    result.subtitle = snippet;
  }
  return true;
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
  if (!results_.empty()) return;
  emptyMessage_ = report_.partial() ? "No matches in the scanned part of the vault" : "No matches";
}

int MarkdownSearchActivity::listCount() const { return static_cast<int>(results_.size()); }

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

  if (results_.empty()) {
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
  const bool empty = results_.empty();
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
