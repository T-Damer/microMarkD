#include "MarkdownSearch.h"

#include <algorithm>
#include <cctype>
#include <limits>

namespace micromarkd {
namespace {

bool isAsciiWhitespace(const unsigned char ch) {
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

bool isContinuation(const unsigned char ch) { return (ch & 0xC0) == 0x80; }

size_t decodeUtf8(const std::string_view input, const size_t offset, uint32_t& codepoint) {
  const unsigned char lead = static_cast<unsigned char>(input[offset]);
  if (lead < 0x80) {
    codepoint = lead;
    return 1;
  }

  size_t length = 0;
  uint32_t value = 0;
  uint32_t minimum = 0;
  if (lead >= 0xC2 && lead <= 0xDF) {
    length = 2;
    value = lead & 0x1F;
    minimum = 0x80;
  } else if (lead >= 0xE0 && lead <= 0xEF) {
    length = 3;
    value = lead & 0x0F;
    minimum = 0x800;
  } else if (lead >= 0xF0 && lead <= 0xF4) {
    length = 4;
    value = lead & 0x07;
    minimum = 0x10000;
  } else {
    codepoint = lead;
    return 1;
  }

  if (offset + length > input.size()) {
    codepoint = lead;
    return 1;
  }
  for (size_t index = 1; index < length; index++) {
    const unsigned char continuation = static_cast<unsigned char>(input[offset + index]);
    if (!isContinuation(continuation)) {
      codepoint = lead;
      return 1;
    }
    value = (value << 6) | (continuation & 0x3F);
  }

  if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
    codepoint = lead;
    return 1;
  }
  codepoint = value;
  return length;
}

uint32_t foldCodepoint(const uint32_t codepoint) {
  if (codepoint >= 'A' && codepoint <= 'Z') return codepoint + ('a' - 'A');
  if (codepoint >= 0x0410 && codepoint <= 0x042F) return codepoint + 0x20;
  if (codepoint >= 0x0400 && codepoint <= 0x040F) return codepoint + 0x50;
  return codepoint;
}

void appendUtf8(std::string& output, const uint32_t codepoint) {
  if (codepoint <= 0x7F) {
    output.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

std::string collapseWhitespace(const std::string_view input) {
  std::string output;
  output.reserve(input.size());
  bool pendingSpace = false;
  for (const char raw : input) {
    const unsigned char ch = static_cast<unsigned char>(raw);
    if (isAsciiWhitespace(ch)) {
      pendingSpace = !output.empty();
      continue;
    }
    if (pendingSpace) output.push_back(' ');
    pendingSpace = false;
    output.push_back(raw);
  }
  return output;
}

size_t utf8StartAtOrAfter(const std::string_view text, size_t offset) {
  offset = std::min(offset, text.size());
  while (offset < text.size() && isContinuation(static_cast<unsigned char>(text[offset]))) offset++;
  return offset;
}

size_t utf8EndAtOrBefore(const std::string_view text, size_t offset) {
  offset = std::min(offset, text.size());
  while (offset > 0 && offset < text.size() && isContinuation(static_cast<unsigned char>(text[offset]))) offset--;
  return offset;
}

size_t firstMatchOffset(const SearchQuery& query, const std::string& foldedText) {
  size_t earliest = std::string::npos;
  for (uint8_t index = 0; index < query.termCount; index++) {
    const size_t found = foldedText.find(query.terms[index]);
    if (found != std::string::npos) earliest = std::min(earliest, found);
  }
  return earliest;
}

}  // namespace

std::string foldSearchText(const std::string_view input) {
  std::string folded;
  folded.reserve(input.size());
  for (size_t offset = 0; offset < input.size();) {
    uint32_t codepoint = 0;
    const size_t length = decodeUtf8(input, offset, codepoint);
    if (length == 1 && static_cast<unsigned char>(input[offset]) >= 0x80) {
      folded.push_back(input[offset]);
    } else {
      appendUtf8(folded, foldCodepoint(codepoint));
    }
    offset += length;
  }
  return folded;
}

SearchQuery compileSearchQuery(const std::string_view input) {
  SearchQuery query;
  size_t start = 0;
  while (start < input.size() && query.termCount < query.terms.size()) {
    while (start < input.size() && isAsciiWhitespace(static_cast<unsigned char>(input[start]))) start++;
    if (start >= input.size()) break;

    size_t end = start;
    while (end < input.size() && !isAsciiWhitespace(static_cast<unsigned char>(input[end]))) end++;
    std::string term = foldSearchText(input.substr(start, end - start));
    if (!term.empty()) {
      bool duplicate = false;
      for (uint8_t index = 0; index < query.termCount; index++) {
        if (query.terms[index] == term) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) {
        query.longestTermBytes = std::max(query.longestTermBytes, term.size());
        query.terms[query.termCount++] = std::move(term);
      }
    }
    start = end;
  }
  return query;
}

void matchSearchTerms(const SearchQuery& query, const std::string_view text, SearchTermMatches& matches) {
  if (query.empty() || text.empty()) return;
  const std::string folded = foldSearchText(text);
  for (uint8_t index = 0; index < query.termCount; index++) {
    if (!matches[index] && folded.find(query.terms[index]) != std::string::npos) matches[index] = true;
  }
}

bool allSearchTermsMatched(const SearchQuery& query, const SearchTermMatches& matches) {
  if (query.empty()) return false;
  for (uint8_t index = 0; index < query.termCount; index++)
    if (!matches[index]) return false;
  return true;
}

bool anySearchTermMatched(const SearchQuery& query, const SearchTermMatches& matches) {
  for (uint8_t index = 0; index < query.termCount; index++)
    if (matches[index]) return true;
  return false;
}

std::string makeSearchSnippet(const std::string_view text, const SearchQuery& query, const size_t maxBytes) {
  if (maxBytes == 0) return {};
  const std::string collapsed = collapseWhitespace(text);
  if (collapsed.size() <= maxBytes) return collapsed;

  const std::string folded = foldSearchText(collapsed);
  size_t match = firstMatchOffset(query, folded);
  if (match == std::string::npos) match = 0;

  const bool hasPrefix = match > maxBytes / 3 && maxBytes >= 3;
  const size_t prefixBytes = hasPrefix ? 3 : 0;
  size_t start = hasPrefix ? match - maxBytes / 3 : 0;
  start = utf8StartAtOrAfter(collapsed, start);

  size_t contentBudget = maxBytes - prefixBytes;
  size_t end = utf8EndAtOrBefore(collapsed, std::min(collapsed.size(), start + contentBudget));
  bool suffix = end < collapsed.size();
  if (suffix && contentBudget >= 3) {
    contentBudget -= 3;
    end = utf8EndAtOrBefore(collapsed, std::min(collapsed.size(), start + contentBudget));
  } else {
    suffix = false;
  }

  std::string snippet;
  snippet.reserve(maxBytes);
  if (hasPrefix) snippet += "...";
  snippet.append(collapsed, start, end - start);
  if (suffix) snippet += "...";
  return snippet;
}

}  // namespace micromarkd
