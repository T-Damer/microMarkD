#include "Esp32GitHttp.h"

#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>
#include <base64.h>

#include <esp32_git.h>

#include <cstdint>
#include <string>

namespace {
// Response bodies (packfiles, pkt-line streams) are buffered whole. Vault-sized
// markdown syncs stay in the tens of KB; a fresh clone of a large repository
// exceeds this cap and fails with a transport error.
// ponytail: full-body buffering, switch to a streamed pack parser if real
// clones larger than a few MB are needed.
constexpr size_t MAX_RESPONSE_BYTES = 4 * 1024 * 1024;

// Local base64: Arduino's base64::encode returns String, which the native
// simulator/WASM builds do not provide.
std::string base64Encode(const std::string& in) {
  static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((in.size() + 2) / 3) * 4);
  size_t i = 0;
  while (i + 3 <= in.size()) {
    const uint32_t v = (static_cast<uint8_t>(in[i]) << 16) | (static_cast<uint8_t>(in[i + 1]) << 8) |
                       static_cast<uint8_t>(in[i + 2]);
    out += kAlphabet[(v >> 18) & 0x3F];
    out += kAlphabet[(v >> 12) & 0x3F];
    out += kAlphabet[(v >> 6) & 0x3F];
    out += kAlphabet[v & 0x3F];
    i += 3;
  }
  const size_t rest = in.size() - i;
  if (rest == 1) {
    const uint32_t v = static_cast<uint8_t>(in[i]) << 16;
    out += kAlphabet[(v >> 18) & 0x3F];
    out += kAlphabet[(v >> 12) & 0x3F];
    out += "==";
  } else if (rest == 2) {
    const uint32_t v = (static_cast<uint8_t>(in[i]) << 16) | (static_cast<uint8_t>(in[i + 1]) << 8);
    out += kAlphabet[(v >> 18) & 0x3F];
    out += kAlphabet[(v >> 12) & 0x3F];
    out += kAlphabet[(v >> 6) & 0x3F];
    out += '=';
  }
  return out;
}

int request(const char* url, int isPost, const char* user, const char* token, const char* contentType,
            const uint8_t* body, size_t bodyLen, uint8_t** outBody, size_t* outLen) {
  *outBody = nullptr;
  *outLen = 0;
  if (url == nullptr) return -1;

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("GIT", "HTTP begin failed: %s", url);
    return -1;
  }

  if (contentType != nullptr) http.addHeader("Content-Type", contentType);
  http.addHeader("User-Agent", "microMarkD-esp32git");
  if (token != nullptr && token[0] != '\0') {
    const std::string credentials = std::string(user == nullptr ? "" : user) + ":" + token;
    http.addHeader("Authorization", "Basic " + base64Encode(credentials));
  }

  const std::string payload(reinterpret_cast<const char*>(body), bodyLen);
  const int status = isPost ? http.POST(payload) : http.GET();
  // Keep embedded NULs (pkt-lines and packs are binary): never route through c_str().
  const auto bodyText = http.getString();
  const std::string response(bodyText.c_str(), bodyText.length());
  LOG_DBG("GIT", "HTTP %s %s -> %d (%zu bytes)", isPost ? "POST" : "GET", url, status, response.size());
  http.end();
  if (status <= 0) return status < 0 ? status : -1;
  if (response.size() > MAX_RESPONSE_BYTES) {
    LOG_ERR("GIT", "HTTP response too large: %zu bytes", response.size());
    return -1;
  }
  if (response.empty()) return status;

  auto out = makeUniqueNoThrow<uint8_t[]>(response.size());
  if (!out) {
    LOG_ERR("GIT", "OOM: HTTP response buffer (%zu bytes)", response.size());
    return -1;
  }
  memcpy(out.get(), response.data(), response.size());
  *outBody = out.release();
  *outLen = response.size();
  return status;
}
}  // namespace

void registerEsp32GitHttp() {
  static const esp32git_http_port port = {request};
  esp32git_http_register(&port);
}
