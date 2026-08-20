#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace micromarkd {

constexpr size_t MAX_NOTE_FILENAME_BYTES = 100;
constexpr size_t MAX_VAULT_COMPONENT_BYTES = 100;

std::string trimNoteTitle(std::string_view title);
std::string safeVaultPathComponent(std::string_view value, size_t maxBytes = MAX_VAULT_COMPONENT_BYTES);
std::string safeNoteFilename(std::string_view title, size_t maxBytes = MAX_NOTE_FILENAME_BYTES);
bool isVaultMarkdownPath(std::string_view path);
std::string vaultNoteDisplayName(std::string_view path);
std::string vaultNoteFolderLabel(std::string_view path);

inline std::string rebaseVaultPath(const std::string_view path, const std::string_view oldPrefix,
                                   const std::string_view newPrefix) {
  if (path == oldPrefix) return std::string(newPrefix);
  if (path.size() <= oldPrefix.size() || !path.starts_with(oldPrefix) || path[oldPrefix.size()] != '/') return {};
  return std::string(newPrefix) + std::string(path.substr(oldPrefix.size()));
}

std::vector<std::string> splitMarkdownLines(std::string_view content, bool& trailingNewline);
std::string joinMarkdownLines(const std::vector<std::string>& lines, bool trailingNewline);

}  // namespace micromarkd
