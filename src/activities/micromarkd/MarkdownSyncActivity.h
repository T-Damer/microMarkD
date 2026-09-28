#pragma once

#ifdef MICROMARKD_APP

#include <Arduino.h>

#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "activities/micromarkd/MarkdownVaultIndexer.h"

class MarkdownSyncActivity final : public UiListActivity {
 public:
  MarkdownSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  static bool downloadBook(const std::string& path, std::string& status);
  // Stores the vault's Git remote and token as if entered on the sync screen.
  static bool saveSyncCredentials(const std::string& url, const std::string& token);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  // A sync can take many minutes; the grace period keeps its result on screen.
  bool preventAutoSleep() override {
    return phase_ == Phase::Connecting || phase_ == Phase::Syncing || phase_ == Phase::Indexing ||
           millis() - syncEndedAtMs_ < SYNC_RESULT_GRACE_MS;
  }

 private:
  enum class Phase : uint8_t { Indexing, Ready, EnteringUrl, EnteringToken, Connecting, Syncing, Complete, Failed };

  static constexpr int GIT_ACTION_INDEX = 0;
  static constexpr int COMPLETE_VAULT_INDEX = 1;
  static constexpr size_t MAX_REMOTE_URL_BYTES = 256;
  static constexpr size_t MAX_ACCESS_TOKEN_BYTES = 160;
  static constexpr unsigned long SYNC_RESULT_GRACE_MS = 5000;

  int listCount() const override { return static_cast<int>(rowItems_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  bool handleCustomInput() override;

  MarkdownVaultIndexer indexer_;
  std::vector<freeink::ui::ListItem> rowItems_;
  std::string status_;
  std::string remoteUrl_;
  std::string accessToken_;
  Phase phase_ = Phase::Indexing;
  bool manifestSaved_ = false;
  bool credentialsSaveFailed_ = false;
  unsigned long syncEndedAtMs_ = 0;

  void refreshActionRow();
  void completeVault();
  void promptRemoteUrl();
  void promptAccessToken();
  void connectAndSync();
  void syncRepository();
  bool directoryIsEmpty(const char* path) const;
  bool saveManifest();
};

#endif  // MICROMARKD_APP
