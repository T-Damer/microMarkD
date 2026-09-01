#pragma once

#ifdef MICROMARKD_APP

#include <cstdint>
#include <string>

#include "activities/UiListActivity.h"
#include "activities/micromarkd/MarkdownVaultIndexer.h"

class MarkdownSyncActivity final : public UiListActivity {
 public:
  MarkdownSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  enum class Phase : uint8_t { Indexing, Ready, EnteringUrl, EnteringToken, Connecting, Syncing, Complete, Failed };

  static constexpr int GIT_ACTION_INDEX = 0;
  static constexpr size_t MAX_REMOTE_URL_BYTES = 256;
  static constexpr size_t MAX_ACCESS_TOKEN_BYTES = 160;

  int listCount() const override { return 1; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  bool handleCustomInput() override;

  MarkdownVaultIndexer indexer_;
  freeink::ui::ListItem rowItems_[1]{};
  std::string status_;
  std::string remoteUrl_;
  std::string accessToken_;
  Phase phase_ = Phase::Indexing;
  bool manifestSaved_ = false;

  void refreshActionRow();
  void promptRemoteUrl();
  void promptAccessToken();
  void connectAndSync();
  void syncRepository();
  bool vaultIsEmpty() const;
  bool saveManifest();
};

#endif  // MICROMARKD_APP
