#include "GitRemoteStore.h"

#include <CredentialIntegrity.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

namespace {
constexpr size_t MAX_TOKEN_LENGTH = 128;
}  // namespace

void GitRemoteStore::toJson(JsonDocument& doc) const {
  doc["url"] = url;
  doc["user"] = user;
  if (!token.empty()) {
    // extractPassword() reads the shared "password_obf" key.
    doc["password_obf"] = obfuscation::obfuscateToBase64(token);
    doc["token_crc32"] = credential_integrity::crc32(token);
  }
}

bool GitRemoteStore::fromJson(JsonVariantConst doc) {
  url = doc["url"] | "";
  user = doc["user"] | "";
  token.clear();

  bool needsResave = false;
  bool tokenValid = false;
  token = extractPassword(doc, needsResave, MAX_TOKEN_LENGTH, tokenValid);
  if (!tokenValid) {
    LOG_ERR("GIT", "Discarding corrupted stored token");
    token.clear();
    url.clear();
    user.clear();
    requestResave();
    return true;
  }

  LOG_DBG("GIT", "Loaded git remote config (url %s)", url.empty() ? "<none>" : url.c_str());
  if (needsResave) requestResave();
  return true;
}

void GitRemoteStore::setRemote(const std::string& newUrl, const std::string& newUser, const std::string& newToken) {
  url = newUrl;
  user = newUser;
  token = newToken;
  saveToFile();
}

void GitRemoteStore::clearRemote() {
  url.clear();
  user.clear();
  token.clear();
  saveToFile();
}
