#include "MarkdownIndex.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace micromarkd {
namespace {

bool isAsciiWhitespace(const unsigned char ch) {
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

std::string_view trim(std::string_view value) {
  while (!value.empty() && isAsciiWhitespace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
  while (!value.empty() && isAsciiWhitespace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
  return value;
}

char asciiLower(const char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); }

std::string asciiLowerCopy(const std::string_view value) {
  std::string lower;
  lower.reserve(value.size());
  for (const char ch : value) lower.push_back(asciiLower(ch));
  return lower;
}

std::string collapseWhitespace(const std::string_view value) {
  std::string result;
  result.reserve(value.size());
  bool pendingSpace = false;
  for (const char raw : value) {
    const auto ch = static_cast<unsigned char>(raw);
    if (isAsciiWhitespace(ch)) {
      pendingSpace = !result.empty();
      continue;
    }
    if (pendingSpace) result.push_back(' ');
    pendingSpace = false;
    result.push_back(raw);
  }
  return result;
}

std::string unquoteYamlScalar(std::string_view value) {
  value = trim(value);
  if (value.size() < 2 || (value.front() != '"' && value.front() != '\'') || value.back() != value.front()) {
    return collapseWhitespace(value);
  }

  const char quote = value.front();
  value.remove_prefix(1);
  value.remove_suffix(1);
  std::string result;
  result.reserve(value.size());
  for (size_t index = 0; index < value.size(); index++) {
    const char ch = value[index];
    if (quote == '\'' && ch == '\'' && index + 1 < value.size() && value[index + 1] == '\'') {
      result.push_back('\'');
      index++;
      continue;
    }
    if (quote == '"' && ch == '\\' && index + 1 < value.size()) {
      const char escaped = value[++index];
      switch (escaped) {
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 't':
          result.push_back('\t');
          break;
        case '\\':
        case '"':
          result.push_back(escaped);
          break;
        default:
          result.push_back(escaped);
          break;
      }
      continue;
    }
    result.push_back(ch);
  }
  return collapseWhitespace(result);
}

std::string_view stripYamlComment(std::string_view value) {
  char quote = 0;
  bool escaped = false;
  for (size_t index = 0; index < value.size(); index++) {
    const char ch = value[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (quote == '"' && ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"' || ch == '\'') {
      if (quote == 0) {
        quote = ch;
      } else if (quote == ch) {
        quote = 0;
      }
      continue;
    }
    const bool commentBoundary = index == 0 || isAsciiWhitespace(static_cast<unsigned char>(value[index - 1]));
    const bool commentText = index + 1 == value.size() || isAsciiWhitespace(static_cast<unsigned char>(value[index + 1]));
    if (ch == '#' && quote == 0 && commentBoundary && commentText) return trim(value.substr(0, index));
  }
  return trim(value);
}

template <typename Callback>
void forEachYamlValue(std::string_view value, Callback callback) {
  value = stripYamlComment(value);
  bool bracketed = value.size() >= 2 && value.front() == '[' && value.back() == ']';
  if (!bracketed) {
    const std::string scalar = unquoteYamlScalar(value);
    if (!scalar.empty()) callback(scalar);
    return;
  }

  value.remove_prefix(1);
  value.remove_suffix(1);
  size_t start = 0;
  char quote = 0;
  bool escaped = false;
  for (size_t index = 0; index <= value.size(); index++) {
    const bool atEnd = index == value.size();
    const char ch = atEnd ? ',' : value[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (quote == '"' && ch == '\\') {
      escaped = true;
      continue;
    }
    if (!atEnd && (ch == '"' || ch == '\'')) {
      if (quote == 0) {
        quote = ch;
      } else if (quote == ch) {
        quote = 0;
      }
      continue;
    }
    if (ch != ',' || quote != 0) continue;

    const std::string scalar = unquoteYamlScalar(value.substr(start, index - start));
    if (!scalar.empty()) callback(scalar);
    start = index + 1;
  }
}

template <typename Vector>
void addUniqueBounded(Vector& values, typename Vector::value_type value, const size_t limit, bool& truncated) {
  if (value.empty()) return;
  if (std::find(values.begin(), values.end(), value) != values.end()) return;
  if (values.size() >= limit) {
    truncated = true;
    return;
  }
  values.push_back(std::move(value));
}

std::string normaliseTag(std::string value) {
  value = std::string(trim(value));
  while (!value.empty() && value.front() == '#') value.erase(value.begin());
  while (!value.empty() && (value.back() == ',' || value.back() == ';')) value.pop_back();
  if (std::any_of(value.begin(), value.end(), [](const char ch) {
        return isAsciiWhitespace(static_cast<unsigned char>(ch));
      })) {
    return {};
  }
  return value;
}


}  // namespace

void MarkdownIndexBuilder::parseFrontmatterLine(const std::string_view rawLine) {
  const std::string_view line = trim(rawLine);
  if (line == "---" || line == "...") {
    frontmatterState_ = FrontmatterState::Complete;
    frontmatterList_ = FrontmatterList::None;
    return;
  }

  if (frontmatterList_ != FrontmatterList::None && line.starts_with("-") &&
      (line.size() == 1 || isAsciiWhitespace(static_cast<unsigned char>(line[1])))) {
    const std::string value = unquoteYamlScalar(stripYamlComment(line.substr(1)));
    if (frontmatterList_ == FrontmatterList::Aliases) {
      addUniqueBounded(metadata_.aliases, value, MAX_INDEX_ALIASES, metadata_.truncated);
    } else {
      addUniqueBounded(metadata_.tags, normaliseTag(value), MAX_INDEX_TAGS, metadata_.truncated);
    }
    return;
  }

  frontmatterList_ = FrontmatterList::None;
  const size_t colon = line.find(':');
  if (colon == std::string_view::npos) return;
  const std::string key = asciiLowerCopy(trim(line.substr(0, colon)));
  const std::string_view value = trim(line.substr(colon + 1));

  if (key == "alias" || key == "aliases") {
    if (value.empty()) {
      frontmatterList_ = FrontmatterList::Aliases;
      return;
    }
    forEachYamlValue(value, [&](const std::string& alias) {
      addUniqueBounded(metadata_.aliases, alias, MAX_INDEX_ALIASES, metadata_.truncated);
    });
  } else if (key == "tag" || key == "tags") {
    if (value.empty()) {
      frontmatterList_ = FrontmatterList::Tags;
      return;
    }
    forEachYamlValue(value, [&](const std::string& tag) {
      addUniqueBounded(metadata_.tags, normaliseTag(tag), MAX_INDEX_TAGS, metadata_.truncated);
    });
  }
}


}  // namespace micromarkd
