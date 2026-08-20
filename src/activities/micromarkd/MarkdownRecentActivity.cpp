#include "MarkdownRecentActivity.h"

#ifdef MICROMARKD_APP

#include <GfxRenderer.h>
#include <I18n.h>
#include <MarkdownDocument.h>

#include <memory>
#include <string>
#include <utility>
#include <variant>

#include "activities/micromarkd/MarkdownEditorActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

MarkdownRecentActivity::MarkdownRecentActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("MarkdownRecent", renderer, mappedInput, /*wantsTouchLongPress=*/true) {}

void MarkdownRecentActivity::onEnter() {
  UiListActivity::onEnter();
  if (RECENT_BOOKS.pruneMissing()) RECENT_BOOKS.saveToFile();
  loadNotes();
}

void MarkdownRecentActivity::onExit() {
  Activity::onExit();
  rowItems_.clear();
  folders_.clear();
  titles_.clear();
  notes_.clear();
}

void MarkdownRecentActivity::loadNotes() {
  notes_.clear();
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    if (micromarkd::isVaultMarkdownPath(book.path)) notes_.push_back(book);
  }
  rebuildRows();
}

void MarkdownRecentActivity::rebuildRows() {
  titles_.resize(notes_.size());
  folders_.resize(notes_.size());
  rowItems_.clear();
  rowItems_.reserve(notes_.size());

  for (size_t i = 0; i < notes_.size(); i++) {
    titles_[i] = micromarkd::vaultNoteDisplayName(notes_[i].path);
    folders_[i] = micromarkd::vaultNoteFolderLabel(notes_[i].path);

    fui::ListItem item{};
    item.label = titles_[i].c_str();
    item.subtitle = folders_[i].c_str();
    item.icon = listIconFor(UITheme::getFileIcon(notes_[i].path), 32);
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
}

int MarkdownRecentActivity::listCount() const { return static_cast<int>(notes_.size()); }

const char* MarkdownRecentActivity::headerTitle() const { return tr(STR_MICROMARKD_RECENT); }

void MarkdownRecentActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  onSelectBook(notes_[index].path);
}

void MarkdownRecentActivity::onRowLongPress(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  showNoteActions(index);
}

void MarkdownRecentActivity::showNoteActions(const int index) {
  if (index < 0 || index >= listCount()) return;

  const std::string path = notes_[index].path;
  const std::string title = titles_[index];
  const char* options[] = {tr(STR_OPEN), "Edit note", tr(STR_REMOVE_FROM_RECENTS)};
  popup_.show("Recent note actions", options, 3, 0, [this, path, title](const int option) {
    switch (option) {
      case 0:
        onSelectBook(path);
        break;
      case 1:
        editNote(path);
        break;
      case 2:
        promptRemove(path, title);
        break;
      default:
        break;
    }
  });
  requestUpdate();
}

void MarkdownRecentActivity::editNote(const std::string& path) {
  startActivityForResult(std::make_unique<MarkdownEditorActivity>(renderer, mappedInput, path),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto* file = std::get_if<FilePathResult>(&result.data);
                           if (!file || file->path.empty()) return;

                           loadNotes();
                           activityManager.goToReader(file->path);
                         });
}

void MarkdownRecentActivity::promptRemove(const std::string& path, const std::string& title) {
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_REMOVE_FROM_RECENTS), title),
      [this, path](const ActivityResult& result) {
        if (result.isCancelled || !RECENT_BOOKS.removeByPath(path)) return;

        loadNotes();
        if (notes_.empty()) {
          nav.selected = 0;
        } else if (nav.selected >= listCount()) {
          nav.selected = listCount() - 1;
        }
        nav.follow(listCount());
        requestUpdate(true);
      });
}

bool MarkdownRecentActivity::handleCustomInput() {
  if (!popup_.isActive()) return false;
  return popup_.handleInput(mappedInput, [this] { requestUpdate(); });
}

void MarkdownRecentActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (notes_.empty()) {
    screen.centeredText("No recent Markdown notes", screen.theme().bodyText);
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
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void MarkdownRecentActivity::drawFooter() {
  const bool empty = notes_.empty();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), empty ? "" : tr(STR_OPEN), empty ? "" : tr(STR_DIR_UP),
                                            empty ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void MarkdownRecentActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();
  drawFooter();
  if (popup_.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer();
}

#endif  // MICROMARKD_APP
