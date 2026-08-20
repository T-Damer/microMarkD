#include "MarkdownBacklinksActivity.h"

#ifdef MICROMARKD_APP

#include <GfxRenderer.h>
#include <I18n.h>
#include <MarkdownDocument.h>
#include <MarkdownIndex.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <variant>

#include "activities/micromarkd/MarkdownCatalogStorage.h"
#include "activities/micromarkd/MarkdownEditorActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

MarkdownBacklinksActivity::MarkdownBacklinksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     std::string targetPath)
    : UiListActivity("MarkdownBacklinks", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      targetPath_(std::move(targetPath)) {}

void MarkdownBacklinksActivity::onEnter() {
  UiListActivity::onEnter();
  beginScan();
}

void MarkdownBacklinksActivity::onExit() {
  UiListActivity::onExit();
  cachePaths_.clear();
  resultPaths_.clear();
  rowLabels_.clear();
  rowSubtitles_.clear();
  rowItems_.clear();
}

void MarkdownBacklinksActivity::loop() {
  UiListActivity::loop();
  if (phase_ != Phase::Complete) advanceScan();
}

void MarkdownBacklinksActivity::beginScan() {
  catalog_ = micromarkd::MarkdownCatalog{};
  cachePaths_.clear();
  resultPaths_.clear();
  rowItems_.clear();
  rowLabels_.clear();
  rowSubtitles_.clear();
  cacheIndex_ = 0;
  partial_ = false;

  if (!micromarkd::isVaultMarkdownPath(targetPath_)) {
    phase_ = Phase::Complete;
    header_ = "Backlinks";
    emptyMessage_ = "Invalid note path";
    requestUpdate();
    return;
  }

  phase_ = Phase::IndexingVault;
  header_ = "Backlinks: indexing";
  emptyMessage_ = "Building vault metadata index...";
  indexer_.begin();
  requestUpdate();
}

void MarkdownBacklinksActivity::advanceScan() {
  if (phase_ == Phase::IndexingVault) {
    indexVaultStep();
  } else if (phase_ == Phase::ScanningLinks) {
    scanBacklinksStep();
  }
}

void MarkdownBacklinksActivity::indexVaultStep() {
  if (indexer_.hasRecord()) {
    catalog_.addRecord(indexer_.takeRecord());
  } else if (!indexer_.complete()) {
    indexer_.step();
    if (indexer_.hasRecord()) catalog_.addRecord(indexer_.takeRecord());
  }

  if (indexer_.complete() && !indexer_.hasRecord()) {
    beginLinkScan();
  } else {
    updateProgress();
  }
}

void MarkdownBacklinksActivity::beginLinkScan() {
  micromarkd::MarkdownIndexRecord targetRecord;
  targetRecord.path = targetPath_;
  catalog_.addRecord(targetRecord);
  catalog_.finalize();
  partial_ = indexer_.report().partial() || catalog_.truncated();

  bool truncated = false;
  if (!listMarkdownIndexCacheFiles(cachePaths_, MAX_CACHE_RECORDS, truncated)) {
    partial_ = true;
    finishScan();
    return;
  }
  partial_ = partial_ || truncated;
  cacheIndex_ = 0;
  phase_ = Phase::ScanningLinks;
  updateProgress();
  if (cachePaths_.empty()) finishScan();
}

void MarkdownBacklinksActivity::scanBacklinksStep() {
  size_t processed = 0;
  while (cacheIndex_ < cachePaths_.size() && processed < RECORDS_PER_STEP && resultPaths_.size() < MAX_RESULTS) {
    micromarkd::MarkdownIndexRecord record;
    const bool valid = loadValidatedMarkdownIndexCache(cachePaths_[cacheIndex_], record);
    if (!valid) {
      partial_ = true;
    } else if (record.path != targetPath_) {
      for (const auto& link : record.metadata.links) {
        if (micromarkd::catalogLinkTargetsPath(catalog_, record.path, link, targetPath_)) {
          resultPaths_.push_back(record.path);
          break;
        }
      }
    }
    cacheIndex_++;
    processed++;
  }

  if (resultPaths_.size() >= MAX_RESULTS && cacheIndex_ < cachePaths_.size()) partial_ = true;
  if (cacheIndex_ >= cachePaths_.size() || resultPaths_.size() >= MAX_RESULTS) {
    finishScan();
  } else {
    updateProgress();
  }
}

void MarkdownBacklinksActivity::finishScan() {
  phase_ = Phase::Complete;
  std::sort(resultPaths_.begin(), resultPaths_.end());
  resultPaths_.erase(std::unique(resultPaths_.begin(), resultPaths_.end()), resultPaths_.end());
  header_ = partial_ ? "Backlinks (partial): " + micromarkd::vaultNoteDisplayName(targetPath_)
                     : "Backlinks: " + micromarkd::vaultNoteDisplayName(targetPath_);
  emptyMessage_ = resultPaths_.empty() ? "No backlinks found" : "";
  rebuildRows();
  requestUpdate();
}

void MarkdownBacklinksActivity::updateProgress() {
  if (phase_ == Phase::Complete) return;
  RenderLock lock(*this);
  if (phase_ == Phase::IndexingVault) {
    if (indexer_.phase() == MarkdownVaultIndexer::Phase::Enumerating) {
      header_ = "Backlinks: finding notes";
      emptyMessage_ = "Scanning vault folders...";
    } else {
      header_ = "Backlinks: indexing " + std::to_string(indexer_.completedNotes()) + "/" +
                std::to_string(indexer_.queuedNotes());
      emptyMessage_ = "Reading Markdown metadata...";
    }
  } else {
    header_ = "Backlinks: scanning " + std::to_string(cacheIndex_) + "/" + std::to_string(cachePaths_.size());
    emptyMessage_ = "Resolving wikilinks...";
  }
  requestUpdate();
}

void MarkdownBacklinksActivity::rebuildRows() {
  rowLabels_.resize(resultPaths_.size());
  rowSubtitles_.resize(resultPaths_.size());
  rowItems_.resize(resultPaths_.size());

  for (size_t index = 0; index < resultPaths_.size(); index++) {
    rowLabels_[index] = micromarkd::vaultNoteDisplayName(resultPaths_[index]);
    rowSubtitles_[index] = micromarkd::vaultNoteFolderLabel(resultPaths_[index]);

    fui::ListItem item{};
    item.label = rowLabels_[index].c_str();
    if (!rowSubtitles_[index].empty()) item.subtitle = rowSubtitles_[index].c_str();
    item.icon = listIconFor(UITheme::getFileIcon(resultPaths_[index]), 32);
    item.actionValue = static_cast<int16_t>(index);
    rowItems_[index] = item;
  }
}

int MarkdownBacklinksActivity::listCount() const { return static_cast<int>(rowItems_.size()); }

void MarkdownBacklinksActivity::activateIndex(const int index) {
  if (phase_ != Phase::Complete || index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  onSelectBook(resultPaths_[static_cast<size_t>(index)]);
}

void MarkdownBacklinksActivity::onRowLongPress(const int index) {
  if (phase_ != Phase::Complete || index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  showNoteActions(index);
}

void MarkdownBacklinksActivity::showNoteActions(const int index) {
  if (index < 0 || static_cast<size_t>(index) >= resultPaths_.size()) return;
  const std::string path = resultPaths_[static_cast<size_t>(index)];
  const char* options[] = {tr(STR_OPEN), "Edit note"};
  popup_.show("Backlink", options, 2, 0, [this, path](const int option) {
    if (option == 0) {
      onSelectBook(path);
    } else if (option == 1) {
      editNote(path);
    }
  });
  requestUpdate();
}

void MarkdownBacklinksActivity::editNote(const std::string& path) {
  startActivityForResult(std::make_unique<MarkdownEditorActivity>(renderer, mappedInput, path),
                         [](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto* file = std::get_if<FilePathResult>(&result.data);
                           if (!file || file->path.empty()) return;
                           activityManager.goToReader(file->path);
                         });
}

bool MarkdownBacklinksActivity::handleCustomInput() {
  if (!popup_.isActive()) return false;
  return popup_.handleInput(mappedInput, [this] { requestUpdate(); });
}

const char* MarkdownBacklinksActivity::headerTitle() const { return header_.c_str(); }

void MarkdownBacklinksActivity::buildScreen(UiScreen& screen) {
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
  fui::TextStyle subtitle = screen.theme().smallText;
  subtitle.maxLines = 2;
  props.subtitleText = subtitle;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void MarkdownBacklinksActivity::drawFooter() {
  const bool ready = phase_ == Phase::Complete && !rowItems_.empty();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), ready ? tr(STR_OPEN) : "", ready ? tr(STR_DIR_UP) : "",
                                            ready ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void MarkdownBacklinksActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();
  drawFooter();
  if (popup_.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer();
}

#endif  // MICROMARKD_APP
