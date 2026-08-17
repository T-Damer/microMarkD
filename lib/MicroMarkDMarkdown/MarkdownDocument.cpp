#include "MarkdownDocument.h"

#include <algorithm>
#include <cctype>

namespace micromarkd {
namespace {

bool isAsciiWhitespace(const unsigned char ch) {
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

bool isUnsafeFatCharacter(const unsigned char ch) {
  switch (ch) {
    case '<':
    case '>':
    case ':':
    case '"':
    case '/':
    case '\\':
    case '|':
    case '?':
    case '*':
      return true;
    default:
      return ch < 0x20 || ch == 0x7F;
  }
}

char asciiLower(const char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); }

bool endsWithAsciiCaseInsensitive(const std::string_view value, const std::string_view suffix) {
  if (value.size() < suffix.size()) return false;
  const size_t offset = value.size() - suffix.size();
  for (size_t i = 0; i < suffix.size(); i++) {
    if (asciiLower(value[offset + i]) != asciiLower(suffix[i])) return false;
  }
  return true;
}

size_t utf8SequenceLength(const unsigned char lead) {
  if (lead < 0x80) return 1;
  if (lead >= 0xC2 && lead <= 0xDF) return 2;
  if (lead >= 0xE0 && lead <= 0xEF) return 3;
  if (lead >= 0xF0 && lead <= 0xF4) return 4;
  return 0;
}

bool validUtf8Sequence(const std::string_view text, const size_t start, const size_t length) {
  if (length < 2 || start + length > text.size()) return false;
  for (size_t i = 1; i < length; i++) {
    if ((static_cast<unsigned char>(text[start + i]) & 0xC0) != 0x80) return false;
  }
  return true;
}

void trimUnsafeEdges(std::string& value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '.')) value.erase(value.begin());
  while (!value.empty() && (value.back() == ' ' || value.back() == '.')) value.pop_back();
}

std::string truncateUtf8(const std::string_view value, const size_t maxBytes) {
  if (value.size() <= maxBytes) return std::string(value);

  size_t end = 0;
  while (end < value.size()) {
    const unsigned char lead = static_cast<unsigned char>(value[end]);
    size_t length = utf8SequenceLength(lead);
    if (length == 0 || !validUtf8Sequence(value, end, length)) length = 1;
    if (end + length > maxBytes) break;
    end += length;
  }
  return std::string(value.substr(0, end));
}

bool isReservedFatStem(const std::string_view stem) {
  const size_t dot = stem.find('.');
  const std::string_view base = stem.substr(0, dot);
  std::string upper;
  upper.reserve(base.size());
  for (const char ch : base) upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));

  if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL") return true;
  if (upper.size() == 4 && (upper.rfind("COM", 0) == 0 || upper.rfind("LPT", 0) == 0) && upper[3] >= '1' &&
      upper[3] <= '9') {
    return true;
  }
  return false;
}

}  // namespace

std::string trimNoteTitle(const std::string_view title) {
  size_t start = 0;
  while (start < title.size() && isAsciiWhitespace(static_cast<unsigned char>(title[start]))) start++;

  size_t end = title.size();
  while (end > start && isAsciiWhitespace(static_cast<unsigned char>(title[end - 1]))) end--;
  return std::string(title.substr(start, end - start));
}

std::string safeNoteFilename(const std::string_view title, const size_t maxBytes) {
  std::string stem = trimNoteTitle(title);
  if (endsWithAsciiCaseInsensitive(stem, ".markdown")) {
    stem.resize(stem.size() - 9);
  } else if (endsWithAsciiCaseInsensitive(stem, ".md")) {
    stem.resize(stem.size() - 3);
  }
  stem = trimNoteTitle(stem);

  std::string safe;
  safe.reserve(stem.size());
  bool pendingSpace = false;

  for (size_t i = 0; i < stem.size();) {
    const unsigned char ch = static_cast<unsigned char>(stem[i]);
    if (ch < 0x80) {
      if (isAsciiWhitespace(ch) || isUnsafeFatCharacter(ch)) {
        pendingSpace = !safe.empty();
        i++;
        continue;
      }

      if (pendingSpace && !safe.empty() && safe.back() != ' ') safe.push_back(' ');
      pendingSpace = false;
      safe.push_back(static_cast<char>(ch));
      i++;
      continue;
    }

    const size_t length = utf8SequenceLength(ch);
    if (length == 0 || !validUtf8Sequence(stem, i, length)) {
      pendingSpace = !safe.empty();
      i++;
      continue;
    }

    if (pendingSpace && !safe.empty() && safe.back() != ' ') safe.push_back(' ');
    pendingSpace = false;
    safe.append(stem, i, length);
    i += length;
  }

  trimUnsafeEdges(safe);
  if (safe.empty()) safe = "Untitled";
  if (isReservedFatStem(safe)) safe.insert(safe.begin(), '_');

  const size_t stemLimit = maxBytes > 3 ? maxBytes - 3 : 1;
  safe = truncateUtf8(safe, stemLimit);
  trimUnsafeEdges(safe);
  if (safe.empty()) safe = truncateUtf8("Untitled", stemLimit);
  if (safe.empty()) safe = "U";
  if (isReservedFatStem(safe)) safe.insert(safe.begin(), '_');

  return safe + ".md";
}

bool isVaultMarkdownPath(const std::string_view path) {
  constexpr std::string_view prefix = "/vault/";
  if (!path.starts_with(prefix) || path.size() <= prefix.size()) return false;
  if (!endsWithAsciiCaseInsensitive(path, ".md") && !endsWithAsciiCaseInsensitive(path, ".markdown")) return false;

  const std::string_view relative = path.substr(prefix.size());
  size_t componentStart = 0;
  while (componentStart < relative.size()) {
    const size_t slash = relative.find('/', componentStart);
    const size_t componentEnd = slash == std::string_view::npos ? relative.size() : slash;
    const std::string_view component = relative.substr(componentStart, componentEnd - componentStart);
    if (component.empty() || component == "." || component == ".." || component.find('\\') != std::string_view::npos) {
      return false;
    }
    if (slash == std::string_view::npos) break;
    componentStart = slash + 1;
  }
  return true;
}

std::string vaultNoteDisplayName(const std::string_view path) {
  if (!isVaultMarkdownPath(path)) return {};

  const size_t slash = path.find_last_of('/');
  std::string filename(path.substr(slash + 1));
  if (endsWithAsciiCaseInsensitive(filename, ".markdown")) {
    filename.resize(filename.size() - 9);
  } else {
    filename.resize(filename.size() - 3);
  }
  return filename;
}

std::string vaultNoteFolderLabel(const std::string_view path) {
  if (!isVaultMarkdownPath(path)) return {};

  constexpr std::string_view prefix = "/vault/";
  const std::string_view relative = path.substr(prefix.size());
  const size_t slash = relative.find_last_of('/');
  if (slash == std::string_view::npos) return "Vault";
  return std::string(relative.substr(0, slash));
}

std::vector<std::string> splitMarkdownLines(const std::string_view content, bool& trailingNewline) {
  std::vector<std::string> lines;
  trailingNewline = false;

  size_t start = 0;
  size_t i = 0;
  while (i < content.size()) {
    if (content[i] != '\n' && content[i] != '\r') {
      i++;
      continue;
    }

    lines.emplace_back(content.substr(start, i - start));
    if (content[i] == '\r' && i + 1 < content.size() && content[i + 1] == '\n') i++;
    i++;
    start = i;
    trailingNewline = start == content.size();
  }

  if (start < content.size()) {
    lines.emplace_back(content.substr(start));
    trailingNewline = false;
  }
  if (lines.empty()) lines.emplace_back();
  return lines;
}

std::string joinMarkdownLines(const std::vector<std::string>& lines, const bool trailingNewline) {
  size_t total = trailingNewline ? 1 : 0;
  for (const auto& line : lines) total += line.size();
  if (lines.size() > 1) total += lines.size() - 1;

  std::string result;
  result.reserve(total);
  for (size_t i = 0; i < lines.size(); i++) {
    if (i > 0) result.push_back('\n');
    result += lines[i];
  }
  if (trailingNewline) result.push_back('\n');
  return result;
}

}  // namespace micromarkd
