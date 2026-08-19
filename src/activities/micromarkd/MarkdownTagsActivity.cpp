#include "MarkdownTagsActivity.h"

#ifdef MICROMARKD_APP

#include <GfxRenderer.h>
#include <I18n.h>
#include <MarkdownDocument.h>

#include <memory>
#include <string>
#include <utility>
#include <variant>

#include "activities/micromarkd/MarkdownEditorActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

MarkdownTagsActivity::MarkdownTagsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("MarkdownTags", renderer, mappedInput, /*wantsTouchLongPress=*/true) {}

void MarkdownTagsActivity::onEnter() {
  UiListActivity::onEnter();
  reloadCatalog();
  showTags();
}

void MarkdownTagsActivity::onExit() {
  UiListActivity::onExit();
  notePaths_.clear();
  rowLabels_.clear();
  rowSubtitles_.clear();
  rowValues_.clear();
  rowItems_.clear();
}

void MarkdownTagsActivity::reloadCatalog() {
  catalog_ = micromarkd::MarkdownCatalog{};
  report_ = {};
  if (!loadMarkdownCatalogFromCache(catalog_, report_)) report_.invalidRecords++;
}

void MarkdownTagsActivity::showTags() {
  mode_ = ViewMode::Tags;
  selectedTag_.clear();
  notePaths_.clear();
  nav.reset();
  header_ = report_.partial() || catalog_.truncated() ? "Tags (partial)" : "Tags";
  emptyMessage_ = catalog_.tags().empty() ? "No indexed tags yet" : "";
  rebuildTagRows();
  requestUpdate();
}

void MarkdownTagsActivity::showNotesForTag(const std::string& tag) {
  mode_ = ViewMode::Notes;
  selectedTag_ = tag;
  notePaths_ = catalog_.notesForTag(tag);
  nav.reset();
  header_ = "#" + tag;
  emptyMessage_ = notePaths_.empty() ? "No notes with this tag" : "";
  rebuildNoteRows();
  requestUpdate();
}

void MarkdownTagsActivity::rebuildTagRows() {
  const auto& tags = catalog_.tags();
  rowLabels_.resize(tags.size());
  rowSubtitles_.assign(tags.size(), {});
  rowValues_.resize(tags.size());
  rowItems_.resize(tags.size());

  for (size_t index = 0; index < tags.size(); index++) {
    rowLabels_[index] = "#" + tags[index].name;
    rowValues_[index] = std::to_string(tags[index].noteCount);

    fui::ListItem item{};
    item.label = rowLabels_[index].c_str();
    item.value = rowValues_[index].c_str();
    item.actionValue = static_cast<int16_t>(index);
    rowItems_[index] = item;
  }
}

void MarkdownTagsActivity::rebuildNoteRows() {
  rowLabels_.resize(notePaths_.size());
  rowSubtitles_.resize(notePaths_.size());
  rowValues_.assign(notePaths_.size(), {});
  rowItems_.resize(notePaths_.size());

  for (size_t index = 0; index < notePaths_.size(); index++) {
    rowLabels_[index] = micromarkd::vaultNoteDisplayName(notePaths_[index]);
    rowSubtitles_[index] = micromarkd::vaultNoteFolderLabel(notePaths_[index]);

    fui::ListItem item{};
    item.label = rowLabels_[index].c_str();
    if (!rowSubtitles_[index].empty()) item.subtitle = rowSubtitles_[index].c_str();
    item.icon = listIconFor(UITheme::getFileIcon(notePaths_[index]), 32);
    item.actionValue = static_cast<int16_t>(index);
    rowItems_[index] = item;
  }
}

int MarkdownTagsActivity::listCount() const { return static_cast<int>(rowItems_.size()); }

void MarkdownTagsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;

  if (mode_ == ViewMode::Tags) {
    showNotesForTag(catalog_.tags()[static_cast<size_t>(index)].name);
    return;
  }
  onSelectBook(notePaths_[static_cast<size_t>(index)]);
}

void MarkdownTagsActivity::onRowLongPress(const int index) {
  if (mode_ != ViewMode::Notes || index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  showNoteActions(index);
}

void MarkdownTagsActivity::showNoteActions(const int index) {
  if (index < 0 || static_cast<size_t>(index) >= notePaths_.size()) return;
  const std::string path = notePaths_[static_cast<size_t>(index)];
  const char* options[] = {tr(STR_OPEN), "Edit note"};
  popup_.show("Tag result", options, 2, 0, [this, path](const int option) {
    if (option == 0) {
      onSelectBook(path);
    } else if (option == 1) {
      editNote(path);
    }
  });
  requestUpdate();
}

void MarkdownTagsActivity::editNote(const std::string& path) {
  startActivityForResult(std::make_unique<MarkdownEditorActivity>(renderer, mappedInput, path),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto* file = std::get_if<FilePathResult>(&result.data);
                           if (!file || file->path.empty()) return;
                           reloadCatalog();
                           activityManager.goToReader(file->path);
                         });
}

bool MarkdownTagsActivity::handleCustomInput() {
  if (!popup_.isActive()) return false;
  return popup_.handleInput(mappedInput, [this] { requestUpdate(); });
}

void MarkdownTagsActivity::onBackButton() {
  if (mode_ == ViewMode::Notes) {
    showTags();
    return;
  }
  finish();
}

const char* MarkdownTagsActivity::headerTitle() const { return header_.c_str(); }

void MarkdownTagsActivity::buildScreen(UiScreen& screen) {
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
  props.inputMask = mode_ == ViewMode::Notes ? fui::InputTouch | fui::InputLongPress : fui::InputTouch;
  props.valueInset = 8;
  fui::TextStyle subtitle = screen.theme().smallText;
  subtitle.maxLines = 2;
  props.subtitleText = subtitle;
  syncListViewport(screen, props, mode_ == ViewMode::Notes);
  screen.list(props);
}

void MarkdownTagsActivity::drawFooter() {
  const bool hasRows = !rowItems_.empty();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), hasRows ? tr(STR_OPEN) : "", hasRows ? tr(STR_DIR_UP) : "",
                                            hasRows ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void MarkdownTagsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();
  drawFooter();
  if (popup_.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer();
}

#endif  // MICROMARKD_APP
