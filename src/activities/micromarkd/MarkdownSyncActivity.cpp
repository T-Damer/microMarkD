#include "MarkdownSyncActivity.h"

#ifdef MICROMARKD_APP

#include <Arduino.h>
#include <HalStorage.h>
#include <I18n.h>
#include <MarkdownIndex.h>
#include <Memory.h>
#include <WiFi.h>

#ifndef SIMULATOR
#include <SecureHttpClient.h>
#include <esp32_git.h>

#include "activities/micromarkd/GitHubRootCa.h"
#endif

#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "MappedInputManager.h"
#include "activities/micromarkd/MarkdownCatalogStorage.h"
#include "activities/micromarkd/MarkdownIndexStorage.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
constexpr char VAULT_ROOT[] = "/vault";
constexpr char SYNC_ROOT[] = "/.micromarkd/sync";
constexpr char MANIFEST_TEMP[] = "/.micromarkd/sync/HEAD.tmp";
constexpr char MANIFEST_PATH[] = "/.micromarkd/sync/HEAD";
constexpr char MODULE[] = "MDS";
constexpr size_t MAX_MANIFEST_ENTRIES = micromarkd::MAX_CATALOG_NOTES;

#ifndef SIMULATOR
constexpr char GIT_MODULE[] = "MDG";
constexpr char DEFAULT_GIT_BRANCH[] = "main";
constexpr char GITHUB_TOKEN_USER[] = "x-access-token";
constexpr size_t INITIAL_HTTP_BODY_CAPACITY = 4096;
// ponytail: advertisements and push responses stay bounded; upload-pack fetch
// responses use the stream-to-SD transport below.
constexpr size_t MAX_HTTP_BODY_BYTES = 192 * 1024;

struct ResponseBuffer {
  ~ResponseBuffer() { delete[] data; }

  bool append(const uint8_t* chunk, const size_t length) {
    if (length > MAX_HTTP_BODY_BYTES - size) return false;
    const size_t required = size + length;
    if (required > capacity) {
      size_t nextCapacity = capacity == 0 ? INITIAL_HTTP_BODY_CAPACITY : capacity;
      while (nextCapacity < required) {
        if (nextCapacity > MAX_HTTP_BODY_BYTES / 2) {
          nextCapacity = MAX_HTTP_BODY_BYTES;
          break;
        }
        nextCapacity *= 2;
      }
      auto next = makeUniqueNoThrow<uint8_t[]>(nextCapacity);
      if (!next) return false;
      if (size > 0) memcpy(next.get(), data, size);
      delete[] data;
      data = next.release();
      capacity = nextCapacity;
    }
    if (length > 0) memcpy(data + size, chunk, length);
    size = required;
    return true;
  }

  uint8_t* release() {
    uint8_t* result = data;
    data = nullptr;
    size = 0;
    capacity = 0;
    return result;
  }

  uint8_t* data = nullptr;
  size_t size = 0;
  size_t capacity = 0;
};

int64_t gitFileSize(const char* path) {
  if (!path) return -1;
  HalFile file;
  if (!Storage.openFileForRead(GIT_MODULE, path, file)) return -1;
  return static_cast<int64_t>(file.fileSize64());
}

int gitReadFile(const char* path, uint8_t* buffer, const size_t capacity, size_t* outLength) {
  if (!path || !buffer || !outLength) return -1;
  *outLength = 0;

  HalFile file;
  if (!Storage.openFileForRead(GIT_MODULE, path, file)) return -1;
  const uint64_t fileSize = file.fileSize64();
  if (fileSize > capacity) return -1;
  if (fileSize == 0) return 0;
  const int read = file.read(buffer, static_cast<size_t>(fileSize));
  if (read != static_cast<int>(fileSize)) return -1;
  *outLength = static_cast<size_t>(fileSize);
  return 0;
}

int gitWriteFile(const char* path, const uint8_t* data, const size_t length) {
  if (!path || (length > 0 && !data)) return -1;
  HalFile file;
  if (!Storage.openFileForWrite(GIT_MODULE, path, file)) return -1;
  if (length > 0 && file.write(data, length) != length) return -1;
  file.flush();
  return file.close() ? 0 : -1;
}

int gitFileExists(const char* path) {
  if (!path) return 0;
  HalFile file = Storage.open(path);
  return file && !file.isDirectory() ? 1 : 0;
}

int gitMakeDirectories(const char* path) {
  return path && Storage.ensureDirectoryExists(path) ? 0 : -1;
}

void* gitFileOpen(const char* path, const int write) {
  if (!path) return nullptr;
  auto file = makeUniqueNoThrow<HalFile>();
  if (!file) return nullptr;
  const bool ok = write ? Storage.openFileForWrite(GIT_MODULE, path, *file)
                        : Storage.openFileForRead(GIT_MODULE, path, *file);
  return ok ? file.release() : nullptr;
}

int gitFileRead(void* handle, uint8_t* buffer, const size_t capacity,
                size_t* outLength) {
  if (!handle || !outLength || (capacity > 0 && !buffer)) return -1;
  *outLength = 0;
  if (capacity == 0) return 0;
  const int read = static_cast<HalFile*>(handle)->read(buffer, capacity);
  if (read < 0 || static_cast<size_t>(read) > capacity) return -1;
  *outLength = static_cast<size_t>(read);
  return 0;
}

int gitFileWrite(void* handle, const uint8_t* data, const size_t length) {
  if (!handle || (length > 0 && !data)) return -1;
  if (length == 0) return 0;
  return static_cast<HalFile*>(handle)->write(data, length) == length ? 0 : -1;
}

int gitFileSeek(void* handle, const uint64_t offset) {
  return handle && static_cast<HalFile*>(handle)->seek64(offset) ? 0 : -1;
}

int gitFileClose(void* handle) {
  if (!handle) return 0;
  auto* file = static_cast<HalFile*>(handle);
  const bool ok = file->close();
  delete file;
  return ok ? 0 : -1;
}

int gitFileRemove(const char* path) {
  return path && Storage.remove(path) ? 0 : -1;
}

const esp32git_fs_port GIT_FS_PORT = {
    gitFileSize,
    gitReadFile,
    gitWriteFile,
    gitFileExists,
    gitMakeDirectories,
    {gitFileOpen, gitFileRead, gitFileWrite, gitFileSeek, gitFileClose},
    gitFileRemove};

int gitHttpRequest(const char* url, const int isPost, const char* user, const char* token,
                   const char* contentType, const uint8_t* body, const size_t bodyLength,
                   uint8_t** outBody, size_t* outLength) {
  if (!url || !outBody || !outLength || strncmp(url, "https://", 8) != 0) return -1;
  *outBody = nullptr;
  *outLength = 0;

  freeink::SecureHttpClient http;
  http.setTimeout(60000);
  http.setCACert(micromarkd::GITHUB_ROOT_CA);
  // Do not forward the Git token to another host through a redirect.
  http.setFollowRedirects(0);
  http.setUserAgent("CrossPoint-ESP32-microMarkD");
  if (user && *user) http.setBasicAuth(user, token ? token : "");
  if (contentType && *contentType) http.addHeader("Content-Type", contentType);
  http.addHeader("Accept", isPost ? "application/x-git-upload-pack-result"
                                  : "application/x-git-upload-pack-advertisement");
  if (!http.begin(url)) return -1;

  ResponseBuffer response;
  const auto onData = [&response](const uint8_t* chunk, const size_t length) {
    return response.append(chunk, length);
  };
  const int status = isPost ? http.sendRequest("POST", body, bodyLength, onData)
                            : http.GET(onData);
  if (status < 0 || !http.responseComplete() || http.callbackAborted()) return -1;
  const size_t responseLength = response.size;
  *outBody = response.release();
  *outLength = responseLength;
  return status;
}

int gitHttpRequestStream(const char* url, const int isPost, const char* user,
                         const char* token, const char* contentType,
                         const uint8_t* body, const size_t bodyLength,
                         const esp32git_http_write_callback write,
                         void* context, size_t* outLength) {
  if (!url || !write || !outLength || strncmp(url, "https://", 8) != 0) {
    return -1;
  }
  *outLength = 0;

  freeink::SecureHttpClient http;
  http.setTimeout(60000);
  http.setCACert(micromarkd::GITHUB_ROOT_CA);
  http.setFollowRedirects(0);
  http.setUserAgent("CrossPoint-ESP32-microMarkD");
  if (user && *user) http.setBasicAuth(user, token ? token : "");
  if (contentType && *contentType) http.addHeader("Content-Type", contentType);
  http.addHeader("Accept", isPost ? "application/x-git-upload-pack-result"
                                  : "application/x-git-upload-pack-advertisement");
  if (!http.begin(url)) return -1;

  size_t received = 0;
  const auto onData = [&](const uint8_t* chunk, const size_t length) {
    if (write(context, chunk, length) != 0) return false;
    received += length;
    return true;
  };
  const int status = isPost ? http.sendRequest("POST", body, bodyLength, onData)
                            : http.GET(onData);
  *outLength = received;
  if (status < 0 || !http.responseComplete() || http.callbackAborted()) return -1;
  return status;
}

const esp32git_http_port GIT_HTTP_PORT = {gitHttpRequest, gitHttpRequestStream};

void registerGitPorts() {
  esp32git_fs_register(&GIT_FS_PORT);
  esp32git_http_register(&GIT_HTTP_PORT);
}

void unregisterGitPorts() {
  esp32git_http_register(nullptr);
  esp32git_fs_register(nullptr);
}

const char* gitStatusText(const esp32git_status status) {
  switch (status) {
    case ESP32GIT_AUTH_FAILED:
      return tr(STR_MICROMARKD_GIT_AUTH_FAILED);
    case ESP32GIT_UP_TO_DATE:
      return tr(STR_MICROMARKD_GIT_UP_TO_DATE);
    case ESP32GIT_OK:
      return tr(STR_MICROMARKD_GIT_DONE);
    default:
      return tr(STR_MICROMARKD_GIT_FAILED);
  }
}
#endif

bool isSupportedRepositoryUrl(const std::string& url) {
  constexpr std::string_view prefix = "https://github.com/";
  return url.size() > prefix.size() && url.compare(0, prefix.size(), prefix) == 0;
}
}

MarkdownSyncActivity::MarkdownSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("MarkdownSync", renderer, mappedInput) {}

void MarkdownSyncActivity::onEnter() {
  UiListActivity::onEnter();
  phase_ = Phase::Indexing;
  manifestSaved_ = false;
  remoteUrl_.clear();
  accessToken_.clear();
  status_ = tr(STR_MICROMARKD_SYNC_DESC);
  refreshActionRow();
  invalidateMarkdownIndexCatalog();
  indexer_.begin(VAULT_ROOT);
  if (indexer_.complete()) {
    manifestSaved_ = saveManifest();
    status_ = manifestSaved_ ? tr(STR_MICROMARKD_SYNC_SAVED) : tr(STR_MICROMARKD_SYNC_FAILED);
    phase_ = Phase::Ready;
  }
  requestUpdate();
}

void MarkdownSyncActivity::onExit() {
#ifndef SIMULATOR
  unregisterGitPorts();
#endif
  UiListActivity::onExit();
}

void MarkdownSyncActivity::loop() {
  UiListActivity::loop();
  if (phase_ != Phase::Indexing) return;
  if (manifestSaved_) return;

  if (indexer_.hasRecord()) indexer_.takeRecord();
  if (!indexer_.complete()) indexer_.step();
  if (indexer_.complete() && !indexer_.hasRecord() && !manifestSaved_) {
    manifestSaved_ = saveManifest();
    status_ = manifestSaved_ ? tr(STR_MICROMARKD_SYNC_SAVED) : tr(STR_MICROMARKD_SYNC_FAILED);
    phase_ = Phase::Ready;
    refreshActionRow();
  } else if (indexer_.phase() == MarkdownVaultIndexer::Phase::Enumerating) {
    status_ = tr(STR_MICROMARKD_SYNC_SCANNING);
  } else {
    status_ = tr(STR_MICROMARKD_SYNC_INDEXING);
  }
  requestUpdate();
}

bool MarkdownSyncActivity::handleCustomInput() {
  return phase_ == Phase::Connecting || phase_ == Phase::Syncing;
}

void MarkdownSyncActivity::activateIndex(const int index) {
  if (index != GIT_ACTION_INDEX ||
      (phase_ != Phase::Ready && phase_ != Phase::Complete && phase_ != Phase::Failed)) {
    return;
  }
  promptRemoteUrl();
}

void MarkdownSyncActivity::refreshActionRow() {
  rowItems_[GIT_ACTION_INDEX].label = I18N.get(StrId::STR_MICROMARKD_GIT_CLONE);
  rowItems_[GIT_ACTION_INDEX].subtitle = I18N.get(StrId::STR_MICROMARKD_GIT_CLONE_DESC);
  rowItems_[GIT_ACTION_INDEX].icon = listIconFor(UIIcon::Git, 32);
  rowItems_[GIT_ACTION_INDEX].actionValue = GIT_ACTION_INDEX;
}

void MarkdownSyncActivity::promptRemoteUrl() {
  phase_ = Phase::EnteringUrl;
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_MICROMARKD_GIT_URL), "https://",
                                              MAX_REMOTE_URL_BYTES, InputType::Url),
      [this](const ActivityResult& result) {
        if (result.isCancelled) {
          phase_ = Phase::Ready;
          return;
        }
        const auto* keyboard = std::get_if<KeyboardResult>(&result.data);
        if (!keyboard) return;
        remoteUrl_ = keyboard->text;
        while (!remoteUrl_.empty() && (remoteUrl_.back() == '/' || remoteUrl_.back() == ' ')) remoteUrl_.pop_back();
        if (!isSupportedRepositoryUrl(remoteUrl_)) {
          status_ = tr(STR_MICROMARKD_GIT_FAILED);
          phase_ = Phase::Failed;
          requestUpdate();
          return;
        }
        promptAccessToken();
      });
}

void MarkdownSyncActivity::promptAccessToken() {
  phase_ = Phase::EnteringToken;
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_MICROMARKD_GIT_TOKEN), "",
                                              MAX_ACCESS_TOKEN_BYTES, InputType::Password),
      [this](const ActivityResult& result) {
        if (result.isCancelled) {
          phase_ = Phase::Ready;
          return;
        }
        const auto* keyboard = std::get_if<KeyboardResult>(&result.data);
        if (!keyboard) return;
        accessToken_ = keyboard->text;
        connectAndSync();
      });
}

void MarkdownSyncActivity::connectAndSync() {
  if (WiFi.status() == WL_CONNECTED) {
    syncRepository();
    return;
  }

  phase_ = Phase::Connecting;
  status_ = tr(STR_MICROMARKD_GIT_CONNECTING);
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                             phase_ = Phase::Ready;
                             status_ = tr(STR_MICROMARKD_GIT_FAILED);
                             requestUpdate();
                             return;
                           }
                           syncRepository();
                         });
}

bool MarkdownSyncActivity::vaultIsEmpty() const {
  if (!Storage.exists(VAULT_ROOT)) return true;
  HalFile directory = Storage.open(VAULT_ROOT);
  if (!directory || !directory.isDirectory()) return false;
  HalFile entry = directory.openNextFile();
  return !entry;
}

void MarkdownSyncActivity::syncRepository() {
  phase_ = Phase::Syncing;
  status_ = Storage.exists("/vault/.git/HEAD") ? tr(STR_MICROMARKD_GIT_PULLING)
                                                : tr(STR_MICROMARKD_GIT_CLONING);
  requestUpdateAndWait();

#ifdef SIMULATOR
  status_ = tr(STR_MICROMARKD_GIT_SIMULATED);
  phase_ = Phase::Complete;
  requestUpdate();
  return;
#else
  registerGitPorts();
  const esp32git_remote auth = accessToken_.empty() ? esp32git_remote{nullptr, nullptr, nullptr}
                                                    : esp32git_remote{remoteUrl_.c_str(), GITHUB_TOKEN_USER,
                                                                      accessToken_.c_str()};
  esp32git_status result = ESP32GIT_IO_ERROR;
  if (Storage.exists("/vault/.git/HEAD")) {
    result = esp32git_fetch_url_auth(remoteUrl_.c_str(), DEFAULT_GIT_BRANCH, VAULT_ROOT,
                                     accessToken_.empty() ? nullptr : &auth);
  } else if (vaultIsEmpty()) {
    result = esp32git_clone_url(remoteUrl_.c_str(), DEFAULT_GIT_BRANCH, VAULT_ROOT,
                                accessToken_.empty() ? nullptr : &auth);
  } else {
    status_ = tr(STR_MICROMARKD_GIT_NOT_EMPTY);
    phase_ = Phase::Failed;
    requestUpdate();
    return;
  }

  status_ = gitStatusText(result);
  phase_ = result == ESP32GIT_OK || result == ESP32GIT_UP_TO_DATE ? Phase::Complete : Phase::Failed;
  requestUpdate();
#endif
}

bool MarkdownSyncActivity::saveManifest() {
  if (!Storage.ensureDirectoryExists(SYNC_ROOT)) return false;

  std::vector<std::string> cachePaths;
  cachePaths.reserve(MAX_MANIFEST_ENTRIES);
  bool truncated = false;
  if (!listMarkdownIndexCacheFiles(cachePaths, MAX_MANIFEST_ENTRIES, truncated)) return false;

  HalFile manifest;
  if (!Storage.openFileForWrite(MODULE, MANIFEST_TEMP, manifest)) return false;
  const auto writeText = [&manifest](const std::string_view text) {
    return manifest.write(text.data(), text.size()) == text.size();
  };
  if (!writeText("microMarkD local sync v1\n")) {
    manifest.close();
    Storage.remove(MANIFEST_TEMP);
    return false;
  }

  for (const auto& cachePath : cachePaths) {
    micromarkd::MarkdownIndexRecord record;
    if (!loadValidatedMarkdownIndexCache(cachePath, record)) continue;
    const std::string size = std::to_string(record.sourceSize);
    const std::string fingerprint = std::to_string(record.sourceFingerprint);
    if (!writeText(record.path) || !writeText("\t") || !writeText(size) || !writeText("\t") ||
        !writeText(fingerprint) || !writeText("\n")) {
      manifest.close();
      Storage.remove(MANIFEST_TEMP);
      return false;
    }
  }
  if (truncated && !writeText("# truncated\n")) {
    manifest.close();
    Storage.remove(MANIFEST_TEMP);
    return false;
  }

  manifest.flush();
  manifest.close();
  Storage.remove(MANIFEST_PATH);
  return Storage.rename(MANIFEST_TEMP, MANIFEST_PATH);
}

const char* MarkdownSyncActivity::headerTitle() const { return tr(STR_MICROMARKD_SYNC); }

void MarkdownSyncActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  fui::TextStyle statusStyle = screen.theme().bodyText;
  statusStyle.align = fui::TextAlign::Center;
  const int16_t lineHeight = screen.target().lineHeight(statusStyle.font);
  screen.target().text(screen.takeTop(lineHeight), status_.c_str(), statusStyle);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props{};
  props.items = rowItems_;
  props.count = 1;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props, true);
  screen.list(props);
}

#endif  // MICROMARKD_APP
