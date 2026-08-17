#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace micromarkd {

constexpr size_t MAX_NOTE_FILENAME_BYTES = 100;

std::string trimNoteTitle(std::string_view title);
std::string safeNoteFilename(std::string_view title, size_t maxBytes = MAX_NOTE_FILENAME_BYTES);
bool isVaultMarkdownPath(std::string_view path);
std::string vaultNoteDisplayName(std::string_view path);
std::string vaultNoteFolderLabel(std::string_view path);
std::vector<std::string> splitMarkdownLines(std::string_view content, bool& trailingNewline);
std::string joinMarkdownLines(const std::vector<std::string>& lines, bool trailingNewline);

}  // namespace micromarkd
