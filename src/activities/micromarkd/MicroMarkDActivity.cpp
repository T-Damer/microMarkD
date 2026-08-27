#include "MicroMarkDActivity.h"

#ifdef MICROMARKD_APP

#include <HalStorage.h>
#include <I18n.h>
#include <MarkdownDocument.h>
#include <MarkdownRecoveryPlan.h>
#include <WiFi.h>

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "activities/micromarkd/MarkdownEditorActivity.h"
#include "activities/micromarkd/MarkdownRecentActivity.h"
#include "activities/micromarkd/MarkdownRecovery.h"
#include "activities/micromarkd/MarkdownSearchActivity.h"
#include "activities/micromarkd/MarkdownTagsActivity.h"
#include "activities/micromarkd/MarkdownVaultActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/Esp32GitHttp.h"
#include "util/Esp32GitStorage.h"
#include "util/GitRemoteStore.h"

#include <esp32_git.h>

namespace fui = freeink::ui;

namespace {
constexpr const char* GIT_REMOTE_DIR = "/vault-backup.git";
constexpr const char* GIT_ID_NAME = "microMarkD";
constexpr const char* GIT_ID_EMAIL = "vault@xteink.device";
constexpr size_t MAX_GIT_FILES = 256;
constexpr int VAULT_INDEX = 0;
constexpr int RECENT_INDEX = 1;
constexpr int SEARCH_INDEX = 2;
constexpr int TAGS_INDEX = 3;
constexpr int NEW_NOTE_INDEX = 4;
constexpr int SYNC_INDEX = 5;
constexpr int GIT_REMOTE_INDEX = 6;
constexpr char VAULT_ROOT[] = "/vault";
constexpr size_t MAX_NOTE_TITLE_BYTES = 96;
constexpr size_t MAX_SEARCH_QUERY_BYTES = 96;
constexpr size_t MAX_GIT_URL_BYTES = 160;
constexpr size_t MAX_GIT_USER_BYTES = 64;
constexpr size_t MAX_GIT_TOKEN_BYTES = 128;
}  // namespace

MicroMarkDActivity::MicroMarkDActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("MicroMarkD", renderer, mappedInput) {
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
  setTranslatedRow(SEARCH_INDEX, StrId::STR_MICROMARKD_SEARCH, StrId::STR_MICROMARKD_SEARCH_DESC, UIIcon::Text);

  fui::ListItem tags{};
  tags.label = "Tags";
  tags.subtitle = "Browse indexed tags and tagged notes";
  tags.icon = listIconFor(UIIcon::Text, 32);
  tags.actionValue = TAGS_INDEX;
  rowItems_[TAGS_INDEX] = tags;

  setTranslatedRow(NEW_NOTE_INDEX, StrId::STR_MICROMARKD_NEW_NOTE, StrId::STR_MICROMARKD_NEW_NOTE_DESC, UIIcon::File);
  setTranslatedRow(SYNC_INDEX, StrId::STR_MICROMARKD_SYNC, StrId::STR_MICROMARKD_SYNC_DESC, UIIcon::Transfer);
  setTranslatedRow(GIT_REMOTE_INDEX, StrId::STR_MICROMARKD_GIT_REMOTE, StrId::STR_MICROMARKD_GIT_REMOTE_DESC,
                   UIIcon::Wifi);
}

void MicroMarkDActivity::onEnter() {
  recoverInterruptedSaves();
  updateGitRemoteRow();
  UiListActivity::onEnter();
}

void MicroMarkDActivity::updateGitRemoteRow() {
  const std::string& url = GIT_STORE.getUrl();
  if (url.empty()) {
    rowItems_[GIT_REMOTE_INDEX].subtitle = tr(STR_MICROMARKD_GIT_REMOTE_DESC);
    return;
  }
  gitRemoteDisplay_ = url;
  rowItems_[GIT_REMOTE_INDEX].subtitle = gitRemoteDisplay_.c_str();
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

  if (index == TAGS_INDEX) {
    activityManager.pushActivity(std::make_unique<MarkdownTagsActivity>(renderer, mappedInput));
    return;
  }

  if (index == NEW_NOTE_INDEX) {
    startNewNote();
    return;
  }

  if (index == SYNC_INDEX) {
    runGitSync();
    return;
  }

  if (index == GIT_REMOTE_INDEX) {
    startGitRemoteConfig();
    return;
  }

  rowItems_[index].subtitle = tr(STR_MICROMARKD_PLANNED);
  requestUpdate();
}

bool MicroMarkDActivity::stageVaultForCommit() {
  std::vector<std::string> directories{VAULT_ROOT};
  size_t stagedOrChanged = 0;
  std::array<char, 128> name{};

  while (!directories.empty() && stagedOrChanged < MAX_GIT_FILES) {
    const std::string dirPath = directories.back();
    directories.pop_back();
    auto dir = Storage.open(dirPath.c_str());
    if (!dir || !dir.isDirectory()) continue;
    dir.rewindDirectory();
    for (auto entry = dir.openNextFile(); entry && stagedOrChanged < MAX_GIT_FILES;
         entry = dir.openNextFile()) {
      entry.getName(name.data(), name.size());
      const bool isDirectory = entry.isDirectory();
      entry.close();
      if (name[0] == '.' || std::strcmp(name.data(), "System Volume Information") == 0) {
        continue;
      }
      const std::string child = dirPath == VAULT_ROOT
                                    ? std::string(VAULT_ROOT) + "/" + name.data()
                                    : dirPath + "/" + name.data();
      if (isDirectory) {
        directories.push_back(child);
        continue;
      }
      const bool isNote = child.size() > 3 && (child.compare(child.size() - 3, 3, ".md") == 0 ||
                                               child.compare(child.size() - 9, 9, ".markdown") == 0);
      if (!isNote) continue;
      const std::string rel =
          child.substr(strlen(VAULT_ROOT) + 1);  // repo-relative path
      if (esp32git_add(VAULT_ROOT, rel.c_str()) == ESP32GIT_OK) stagedOrChanged++;
    }
  }
  return stagedOrChanged > 0;
}

void MicroMarkDActivity::runGitSync() {
  registerEsp32GitStorage();

  // Configured https remote wins; otherwise sync against the SD bare repo.
  if (GIT_STORE.hasRemote()) {
    if (WiFi.status() != WL_CONNECTED) {
      launchWifiThenSync();
      return;
    }
    runGitSyncUrl(GIT_STORE.getUrl(), GIT_STORE.getUser(), GIT_STORE.getToken());
    return;
  }

  // One-time repo bootstrap; init() is not idempotent (resets the index).
  if (!Storage.exists("/vault/.git/HEAD")) {
    if (esp32git_init(VAULT_ROOT) != ESP32GIT_OK) {
      rowItems_[SYNC_INDEX].subtitle = tr(STR_MICROMARKD_SYNC_ERROR);
      requestUpdate();
      return;
    }
  }

  // Fast-forward pull; divergence is reported, never merged.
  const esp32git_status pull = esp32git_fetch(GIT_REMOTE_DIR, "main", VAULT_ROOT);
  if (pull == ESP32GIT_REMOTE_DIVERGED) {
    rowItems_[SYNC_INDEX].subtitle = tr(STR_MICROMARKD_SYNC_DIVERGED);
    requestUpdate();
    return;
  }
  if (pull != ESP32GIT_OK && pull != ESP32GIT_UP_TO_DATE) {
    showSyncFailure(pull);
    return;
  }

  if (!stageVaultForCommit()) {
    rowItems_[SYNC_INDEX].subtitle = pull == ESP32GIT_UP_TO_DATE
                                         ? tr(STR_MICROMARKD_SYNC_NOTHING)
                                         : tr(STR_MICROMARKD_SYNC_OK);
    requestUpdate();
    return;
  }

  const esp32git_identity id = {GIT_ID_NAME, GIT_ID_EMAIL};
  char commitSha[41];
  const esp32git_status commit = esp32git_commit(VAULT_ROOT, &id, "vault sync", commitSha);
  if (commit != ESP32GIT_OK && commit != ESP32GIT_UP_TO_DATE) {
    showSyncFailure(commit);
    return;
  }
  if (commit == ESP32GIT_UP_TO_DATE) {
    // Nothing new locally; only report progress when the pull moved us.
    rowItems_[SYNC_INDEX].subtitle = pull == ESP32GIT_UP_TO_DATE
                                         ? tr(STR_MICROMARKD_SYNC_NOTHING)
                                         : tr(STR_MICROMARKD_SYNC_OK);
    requestUpdate();
    return;
  }

  const esp32git_status push = esp32git_push(GIT_REMOTE_DIR, "main", VAULT_ROOT);
  if (push != ESP32GIT_OK && push != ESP32GIT_UP_TO_DATE) {
    showSyncFailure(push);
    return;
  }
  rowItems_[SYNC_INDEX].subtitle = tr(STR_MICROMARKD_SYNC_OK);
  requestUpdate();
}

void MicroMarkDActivity::launchWifiThenSync() {
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                             rowItems_[SYNC_INDEX].subtitle = tr(STR_MICROMARKD_SYNC_ERROR);
                             requestUpdate();
                             return;
                           }
                           runGitSyncUrl(GIT_STORE.getUrl(), GIT_STORE.getUser(), GIT_STORE.getToken());
                         });
}

void MicroMarkDActivity::runGitSyncUrl(const std::string& url, const std::string& user, const std::string& token) {
  registerEsp32GitStorage();
  registerEsp32GitHttp();

  const esp32git_remote auth = {url.c_str(), user.c_str(), token.c_str()};

  if (!Storage.exists("/vault/.git/HEAD")) {
    rowItems_[SYNC_INDEX].subtitle = tr(STR_MICROMARKD_SYNC_RUNNING);
    requestUpdate();
    const esp32git_status clone = esp32git_clone_url(url.c_str(), "main", VAULT_ROOT, &auth);
    if (clone != ESP32GIT_OK) {
      showSyncFailure(clone);
      return;
    }
    rowItems_[SYNC_INDEX].subtitle = tr(STR_MICROMARKD_SYNC_CLONED);
    requestUpdate();
    return;
  }

  // Fast-forward pull; divergence is reported, never merged.
  rowItems_[SYNC_INDEX].subtitle = tr(STR_MICROMARKD_SYNC_RUNNING);
  requestUpdate();
  const esp32git_status pull = esp32git_fetch_url_auth(url.c_str(), "main", VAULT_ROOT, &auth);
  if (pull == ESP32GIT_REMOTE_DIVERGED) {
    rowItems_[SYNC_INDEX].subtitle = tr(STR_MICROMARKD_SYNC_DIVERGED);
    requestUpdate();
    return;
  }
  if (pull != ESP32GIT_OK && pull != ESP32GIT_UP_TO_DATE) {
    showSyncFailure(pull);
    return;
  }

  if (!stageVaultForCommit()) {
    rowItems_[SYNC_INDEX].subtitle = pull == ESP32GIT_UP_TO_DATE ? tr(STR_MICROMARKD_SYNC_NOTHING)
                                                                 : tr(STR_MICROMARKD_SYNC_OK);
    requestUpdate();
    return;
  }

  const esp32git_identity id = {GIT_ID_NAME, GIT_ID_EMAIL};
  char commitSha[41];
  const esp32git_status commit = esp32git_commit(VAULT_ROOT, &id, "vault sync", commitSha);
  if (commit != ESP32GIT_OK && commit != ESP32GIT_UP_TO_DATE) {
    showSyncFailure(commit);
    return;
  }
  if (commit == ESP32GIT_UP_TO_DATE) {
    rowItems_[SYNC_INDEX].subtitle = pull == ESP32GIT_UP_TO_DATE ? tr(STR_MICROMARKD_SYNC_NOTHING)
                                                                 : tr(STR_MICROMARKD_SYNC_OK);
    requestUpdate();
    return;
  }

  const esp32git_status push = esp32git_push_url_auth(url.c_str(), "main", VAULT_ROOT, &auth);
  if (push != ESP32GIT_OK && push != ESP32GIT_UP_TO_DATE) {
    showSyncFailure(push);
    return;
  }
  rowItems_[SYNC_INDEX].subtitle = tr(STR_MICROMARKD_SYNC_OK);
  requestUpdate();
}

void MicroMarkDActivity::showSyncFailure(const int status) {
  LOG_ERR("MMD", "Git sync failed: status %d", status);
  rowItems_[SYNC_INDEX].subtitle = status == ESP32GIT_AUTH_FAILED ? tr(STR_MICROMARKD_SYNC_AUTH)
                                                                  : tr(STR_MICROMARKD_SYNC_ERROR);
  requestUpdate();
}

void MicroMarkDActivity::startGitRemoteConfig() {
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Git remote URL",
                                                                 GIT_STORE.getUrl(), MAX_GIT_URL_BYTES,
                                                                 InputType::Url),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto* keyboard = std::get_if<KeyboardResult>(&result.data);
                           if (!keyboard) return;
                           const std::string url = micromarkd::trimNoteTitle(keyboard->text);
                           if (url.empty()) {
                             GIT_STORE.clearRemote();
                             updateGitRemoteRow();
                             requestUpdate();
                             return;
                           }
                           configureGitUser(url);
                         });
}

void MicroMarkDActivity::configureGitUser(const std::string& url) {
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Git username",
                                                                 GIT_STORE.getUser(), MAX_GIT_USER_BYTES,
                                                                 InputType::Text),
                         [this, url](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto* keyboard = std::get_if<KeyboardResult>(&result.data);
                           if (!keyboard) return;
                           configureGitToken(url, micromarkd::trimNoteTitle(keyboard->text));
                         });
}

void MicroMarkDActivity::configureGitToken(const std::string& url, const std::string& user) {
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Git access token",
                                                                 GIT_STORE.getToken(), MAX_GIT_TOKEN_BYTES,
                                                                 InputType::Password),
                         [this, url, user](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto* keyboard = std::get_if<KeyboardResult>(&result.data);
                           if (!keyboard) return;
                           GIT_STORE.setRemote(url, user, micromarkd::trimNoteTitle(keyboard->text));
                           updateGitRemoteRow();
                           requestUpdate();
                         });
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
