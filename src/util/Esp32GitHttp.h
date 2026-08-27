#pragma once

// Bridges esp32-git's smart-HTTP transport onto freeink::SecureHttpClient
// (wolfSSL). Register once before any esp32git_*_url call; pairs with
// registerEsp32GitStorage().

void registerEsp32GitHttp();
