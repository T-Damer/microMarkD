#pragma once

#ifdef MICROMARKD_APP

#include <cstdint>
#include <string>
#include <vector>

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
  static constexpr int COMPLETE_VAULT_INDEX = 1;
  static constexpr size_t MAX_REMOTE_URL_BYTES = 256;
  static constexpr size_t MAX_ACCESS_TOKEN_BYTES = 160;

  int listCount() const override { return static_cast<int>(rowItems_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  bool handleCustomInput() override;

  MarkdownVaultIndexer indexer_;
  std::vector<freeink::ui::ListItem> rowItems_;
  std::vector<std::string> bookPaths_;
  std::vector<std::string> bookLabels_;
  std::vector<std::string> bookSubtitles_;
  std::string status_;
  std::string remoteUrl_;
  std::string accessToken_;
  Phase phase_ = Phase::Indexing;
  bool manifestSaved_ = false;

  void refreshActionRow();
  void loadBookIndex();
  void openBook(size_t index);
  void completeVault();
  void promptRemoteUrl();
  void promptAccessToken();
  void connectAndSync();
  void syncRepository();
  bool directoryIsEmpty(const char* path) const;
  bool saveManifest();
};

#endif  // MICROMARKD_APP
