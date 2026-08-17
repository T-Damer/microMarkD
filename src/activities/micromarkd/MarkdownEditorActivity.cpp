#include "MarkdownEditorActivity.h"

#ifdef MICROMARKD_APP

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Txt.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <variant>

#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr char VAULT_PREFIX[] = "/vault/";
constexpr char MODULE[] = "MDE";
}  // namespace

MarkdownEditorActivity::MarkdownEditorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               std::string path)
    : UiListActivity("MarkdownEditor", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      path_(std::move(path)) {}

MarkdownEditorActivity::MarkdownEditorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               std::string path, std::vector<std::string> initialLines,
                                               const bool trailingNewline)
    : UiListActivity("MarkdownEditor", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      path_(std::move(path)),
      lines_(std::move(initialLines)),
      loadFromDisk_(false),
      trailingNewline_(trailingNewline),
      dirty_(true) {}

void MarkdownEditorActivity::onEnter() {
  if (!normalisePath()) {
    loadFailed_ = true;
    errorMessage_ = "Could not load note";
  } else if (loadFromDisk_ && !loadDocument()) {
    loadFailed_ = true;
    if (errorMessage_.empty()) errorMessage_ = "Could not load note";
  }

  if (lines_.empty()) lines_.emplace_back();
  title_ = filenameTitle(path_);
  rebuildRows();
  UiListActivity::onEnter();
}

bool MarkdownEditorActivity::normalisePath() {
  const std::string normalised = FsHelpers::normalisePath(path_);
  if (normalised.empty()) return false;

  path_ = "/" + normalised;
  return path_.rfind(VAULT_PREFIX, 0) == 0 && FsHelpers::hasMarkdownExtension(path_);
}

bool MarkdownEditorActivity::loadDocument() {
  HalFile file;
  if (!Storage.openFileForRead(MODULE, path_, file)) {
    LOG_ERR(MODULE, "Failed to open note: %s", path_.c_str());
    return false;
  }

  const uint64_t fileSize = file.fileSize64();
  if (fileSize > MAX_FILE_BYTES) {
    LOG_ERR(MODULE, "Note is too large to edit: %llu bytes", static_cast<unsigned long long>(fileSize));
    errorMessage_ = "Note exceeds editor limits";
    return false;
  }

  lines_.clear();
  std::string currentLine;
  currentLine.reserve(128);
  bool swallowLineFeed = false;
  bool sawAnyByte = false;
  trailingNewline_ = false;

  uint8_t buffer[512];
  while (file.available()) {
    const int read = file.read(buffer, sizeof(buffer));
    if (read < 0) {
      LOG_ERR(MODULE, "Failed while reading note: %s", path_.c_str());
      return false;
    }
    if (read == 0) break;

    for (int i = 0; i < read; i++) {
      const char ch = static_cast<char>(buffer[i]);
      sawAnyByte = true;

      if (swallowLineFeed) {
        swallowLineFeed = false;
        if (ch == '\n') continue;
      }

      if (ch == '\r' || ch == '\n') {
        lines_.push_back(std::move(currentLine));
        currentLine.clear();
        currentLine.reserve(128);
        trailingNewline_ = true;
        swallowLineFeed = ch == '\r';
        if (lines_.size() > MAX_LINES) {
          errorMessage_ = "Note exceeds editor limits";
          return false;
        }
        continue;
      }

      trailingNewline_ = false;
      currentLine.push_back(ch);
      if (currentLine.size() > MAX_LINE_BYTES) {
        errorMessage_ = "Note exceeds editor limits";
        return false;
      }
    }
  }

  if (!trailingNewline_) lines_.push_back(std::move(currentLine));
  if (!sawAnyByte || lines_.empty()) lines_.emplace_back();
  dirty_ = false;
  return true;
}

size_t MarkdownEditorActivity::documentSize() const {
  size_t size = trailingNewline_ ? 1 : 0;
  for (const auto& line : lines_) size += line.size();
  if (lines_.size() > 1) size += lines_.size() - 1;
  return size;
}

bool MarkdownEditorActivity::writeTemporaryFile(const std::string& temporaryPath) const {
  HalFile file;
  if (!Storage.openFileForWrite(MODULE, temporaryPath, file)) {
    LOG_ERR(MODULE, "Failed to open temporary note: %s", temporaryPath.c_str());
    return false;
  }

  for (size_t i = 0; i < lines_.size(); i++) {
    const auto& line = lines_[i];
    if (!line.empty() && file.write(line.data(), line.size()) != line.size()) {
      LOG_ERR(MODULE, "Short write saving note: %s", temporaryPath.c_str());
      file.close();
      return false;
    }
    if (i + 1 < lines_.size() && file.write(static_cast<uint8_t>('\n')) != 1) {
      LOG_ERR(MODULE, "Short newline write saving note: %s", temporaryPath.c_str());
      file.close();
      return false;
    }
  }

  if (trailingNewline_ && file.write(static_cast<uint8_t>('\n')) != 1) {
    LOG_ERR(MODULE, "Short trailing-newline write saving note: %s", temporaryPath.c_str());
    file.close();
    return false;
  }

  file.flush();
  return file.close();
}

bool MarkdownEditorActivity::saveDocumentAtomic() {
  if (documentSize() > MAX_FILE_BYTES) {
    errorMessage_ = "Note exceeds editor limits";
    return false;
  }

  const std::string temporaryPath = path_ + ".tmp";
  const std::string backupPath = path_ + ".bak";
  Storage.remove(temporaryPath.c_str());

  if (!writeTemporaryFile(temporaryPath)) {
    Storage.remove(temporaryPath.c_str());
    return false;
  }

  Storage.remove(backupPath.c_str());
  const bool hadOriginal = Storage.exists(path_.c_str());
  if (hadOriginal && !Storage.rename(path_.c_str(), backupPath.c_str())) {
    LOG_ERR(MODULE, "Failed to move original note to backup: %s", path_.c_str());
    Storage.remove(temporaryPath.c_str());
    return false;
  }

  if (!Storage.rename(temporaryPath.c_str(), path_.c_str())) {
    LOG_ERR(MODULE, "Failed to move temporary note into place: %s", path_.c_str());
    Storage.remove(temporaryPath.c_str());
    if (hadOriginal && !Storage.rename(backupPath.c_str(), path_.c_str())) {
      LOG_ERR(MODULE, "Failed to restore note backup: %s", backupPath.c_str());
    }
    return false;
  }

  if (hadOriginal) Storage.remove(backupPath.c_str());

  Txt note(path_, "/.crosspoint");
  if (!note.clearCache()) {
    LOG_ERR(MODULE, "Saved note but failed to clear its reading cache: %s", path_.c_str());
  }
  return true;
}

int MarkdownEditorActivity::listCount() const {
  if (loadFailed_) return 0;
  return static_cast<int>(lines_.size()) + 2;
}

std::string MarkdownEditorActivity::linePreview(const std::string& line) {
  if (line.empty()) return "[Empty line]";
  if (line.size() <= PREVIEW_BYTES) return line;

  size_t end = PREVIEW_BYTES;
  while (end > 0 && end < line.size() && (static_cast<unsigned char>(line[end]) & 0xC0) == 0x80) end--;
  std::string preview = line.substr(0, end);
  preview += "...";
  return preview;
}

void MarkdownEditorActivity::rebuildRows() {
  if (loadFailed_) {
    rowLabels_.clear();
    rowValues_.clear();
    rowItems_.clear();
    return;
  }

  const size_t count = lines_.size() + 2;
  rowLabels_.resize(count);
  rowValues_.resize(count);
  rowItems_.resize(count);

  for (size_t i = 0; i < lines_.size(); i++) {
    rowLabels_[i] = linePreview(lines_[i]);
    rowValues_[i] = std::to_string(i + 1);
  }

  const size_t addIndex = lines_.size();
  const size_t saveIndex = addIndex + 1;
  rowLabels_[addIndex] = "Add line";
  rowLabels_[saveIndex] = "Save note";
  rowValues_[addIndex].clear();
  rowValues_[saveIndex] = dirty_ ? "*" : "";

  for (size_t i = 0; i < count; i++) {
    fui::ListItem item{};
    item.label = rowLabels_[i].c_str();
    if (!rowValues_[i].empty()) item.value = rowValues_[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems_[i] = item;
  }
}

void MarkdownEditorActivity::markChanged() {
  dirty_ = true;
  saveFailed_ = false;
  errorMessage_.clear();
  rebuildRows();
}

void MarkdownEditorActivity::editLine(const size_t index) {
  if (index >= lines_.size()) return;

  app.clearTapFlash();
  std::string prompt = "Edit line";
  prompt += " ";
  prompt += std::to_string(index + 1);

  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, std::move(prompt), lines_[index], MAX_LINE_BYTES,
                                              InputType::Text),
      [this, index](const ActivityResult& result) {
        if (result.isCancelled) return;
        const auto* keyboard = std::get_if<KeyboardResult>(&result.data);
        if (!keyboard || index >= lines_.size()) return;

        RenderLock lock(*this);
        lines_[index] = keyboard->text;
        markChanged();
        nav.selected = static_cast<int>(index);
        nav.follow(listCount());
      });
}

void MarkdownEditorActivity::insertLine(const size_t index, const bool editImmediately) {
  if (lines_.size() >= MAX_LINES) {
    saveFailed_ = true;
    errorMessage_ = "Note exceeds editor limits";
    requestUpdate();
    return;
  }

  const size_t insertAt = std::min(index, lines_.size());
  {
    RenderLock lock(*this);
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(insertAt), std::string{});
    markChanged();
    nav.selected = static_cast<int>(insertAt);
    nav.follow(listCount());
  }

  requestUpdate();
  if (editImmediately) editLine(insertAt);
}

void MarkdownEditorActivity::deleteLine(const size_t index) {
  if (index >= lines_.size()) return;

  RenderLock lock(*this);
  if (lines_.size() == 1) {
    lines_[0].clear();
    nav.selected = 0;
  } else {
    lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(index));
    nav.selected = std::min(static_cast<int>(index), static_cast<int>(lines_.size()) - 1);
  }
  markChanged();
  nav.follow(listCount());
}

void MarkdownEditorActivity::showLineActions(const size_t index) {
  if (index >= lines_.size()) return;

  const char* options[] = {"Edit line", "Insert above", "Insert below", tr(STR_DELETE)};
  popup_.show("Line actions", options, 4, 0, [this, index](const int option) {
    switch (option) {
      case 0:
        editLine(index);
        break;
      case 1:
        insertLine(index, true);
        break;
      case 2:
        insertLine(index + 1, true);
        break;
      case 3:
        deleteLine(index);
        requestUpdate();
        break;
      default:
        break;
    }
  });
  requestUpdate();
}

void MarkdownEditorActivity::showUnsavedActions() {
  const char* options[] = {"Save note", "Discard changes", "Continue editing"};
  popup_.show("Unsaved changes", options, 3, 2, [this](const int option) {
    if (option == 0) {
      saveAndFinish();
    } else if (option == 1) {
      discardAndFinish();
    }
  });
  requestUpdate();
}

void MarkdownEditorActivity::activateIndex(const int index) {
  if (loadFailed_ || index < 0 || index >= listCount()) return;
  app.clearTapFlash();

  const size_t lineCount = lines_.size();
  if (static_cast<size_t>(index) < lineCount) {
    editLine(static_cast<size_t>(index));
  } else if (static_cast<size_t>(index) == lineCount) {
    insertLine(lineCount, true);
  } else {
    saveAndFinish();
  }
}

void MarkdownEditorActivity::onRowLongPress(const int index) {
  if (loadFailed_ || index < 0 || static_cast<size_t>(index) >= lines_.size()) return;
  app.clearTapFlash();
  nav.selected = index;
  showLineActions(static_cast<size_t>(index));
}

bool MarkdownEditorActivity::handleCustomInput() {
  if (!popup_.isActive()) return false;
  return popup_.handleInput(mappedInput, [this] { requestUpdate(); });
}

void MarkdownEditorActivity::saveAndFinish() {
  if (loadFailed_) return;
  if (!saveDocumentAtomic()) {
    saveFailed_ = true;
    if (errorMessage_.empty()) errorMessage_ = "Could not save note";
    requestUpdate();
    return;
  }

  dirty_ = false;
  ActivityResult result{FilePathResult{path_}};
  result.isCancelled = false;
  setResult(std::move(result));
  finish();
}

void MarkdownEditorActivity::discardAndFinish() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void MarkdownEditorActivity::onBackButton() {
  if (loadFailed_ || !dirty_) {
    discardAndFinish();
    return;
  }
  showUnsavedActions();
}

const char* MarkdownEditorActivity::headerTitle() const {
  if (loadFailed_) return errorMessage_.c_str();
  if (saveFailed_) return errorMessage_.empty() ? "Could not save note" : errorMessage_.c_str();
  return title_.c_str();
}

std::string MarkdownEditorActivity::filenameTitle(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  std::string title = slash == std::string::npos ? path : path.substr(slash + 1);
  if (FsHelpers::checkFileExtension(title, ".markdown")) {
    title.resize(title.size() - 9);
  } else if (FsHelpers::checkFileExtension(title, ".md")) {
    title.resize(title.size() - 3);
  }
  return title;
}

void MarkdownEditorActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (loadFailed_) {
    screen.centeredText(errorMessage_.c_str(), screen.theme().bodyText);
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

void MarkdownEditorActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();
  drawFooter();
  if (popup_.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer();
}

#endif  // MICROMARKD_APP
