#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>

/**
 * Git sync remote configuration for microMarkD, persisted on the SD card.
 * Empty URL selects the SD-card bare-repo transport (/vault-backup.git);
 * an https URL selects smart-HTTP sync with optional basic-auth credentials.
 */
class GitRemoteStore : public PersistableStore<GitRemoteStore> {
 private:
  std::string url;
  std::string user;
  std::string token;

  GitRemoteStore() = default;
  ~GitRemoteStore() = default;

  friend class PersistableStore<GitRemoteStore>;

 public:
  static const char* getFilePath() { return "/.micromarkd/sync/remote.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  void setRemote(const std::string& newUrl, const std::string& newUser, const std::string& newToken);
  void clearRemote();

  const std::string& getUrl() const { return url; }
  const std::string& getUser() const { return user; }
  const std::string& getToken() const { return token; }
  bool hasRemote() const { return !url.empty(); }
};

#define GIT_STORE GitRemoteStore::getInstance()
