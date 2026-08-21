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

std::string_view trimLeft(std::string_view value) {
  while (!value.empty() && isAsciiWhitespace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
  return value;
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

size_t fenceRunLength(const std::string_view line, const char marker) {
  size_t length = 0;
  while (length < line.size() && line[length] == marker) length++;
  return length;
}

std::string plainInlineText(const std::string_view source) {
  std::string result;
  result.reserve(source.size());
  for (size_t index = 0; index < source.size();) {
    if (source[index] == '\\' && index + 1 < source.size()) {
      result.push_back(source[index + 1]);
      index += 2;
      continue;
    }

    if (index + 1 < source.size() && source[index] == '[' && source[index + 1] == '[') {
      const size_t close = source.find("]]", index + 2);
      if (close != std::string_view::npos) {
        const std::string_view body = trim(source.substr(index + 2, close - index - 2));
        const size_t alias = body.find('|');
        std::string_view label = alias == std::string_view::npos ? body : body.substr(alias + 1);
        label = trim(label);
        if (!label.empty()) result.append(label.data(), label.size());
        index = close + 2;
        continue;
      }
    }

    if (source[index] == '[') {
      const size_t labelEnd = source.find(']', index + 1);
      if (labelEnd != std::string_view::npos && labelEnd + 1 < source.size() && source[labelEnd + 1] == '(') {
        const size_t targetEnd = source.find(')', labelEnd + 2);
        if (targetEnd != std::string_view::npos) {
          result.append(source.data() + index + 1, labelEnd - index - 1);
          index = targetEnd + 1;
          continue;
        }
      }
    }

    if (source[index] == '*' || source[index] == '_' || source[index] == '~' || source[index] == '`') {
      index++;
      continue;
    }

    result.push_back(source[index++]);
  }
  return collapseWhitespace(trim(result));
}

bool parseAtxHeading(const std::string_view rawLine, uint8_t& level, std::string& text) {
  size_t indent = 0;
  while (indent < rawLine.size() && rawLine[indent] == ' ') indent++;
  if (indent > 3) return false;

  std::string_view line = rawLine.substr(indent);
  size_t count = 0;
  while (count < line.size() && line[count] == '#') count++;
  if (count == 0 || count > 6) return false;
  if (count < line.size() && line[count] != ' ' && line[count] != '\t') return false;

  std::string_view body = trim(line.substr(count));
  size_t closingStart = body.size();
  while (closingStart > 0 && body[closingStart - 1] == '#') closingStart--;
  if (closingStart < body.size() && closingStart > 0 &&
      isAsciiWhitespace(static_cast<unsigned char>(body[closingStart - 1]))) {
    body = trim(body.substr(0, closingStart));
  }

  level = static_cast<uint8_t>(count);
  text = plainInlineText(body);
  return !text.empty();
}

std::string_view wikiReferencePart(std::string_view body) {
  body = trim(body);
  const size_t alias = body.find('|');
  if (alias != std::string_view::npos) body = body.substr(0, alias);
  return trim(body);
}

void addWikiReference(MarkdownIndexMetadata& metadata, const std::string_view body) {
  std::string_view reference = wikiReferencePart(body);
  if (reference.empty()) return;

  const size_t headingSeparator = reference.find('#');
  MarkdownIndexLink link;
  if (headingSeparator == std::string_view::npos) {
    link.target.assign(reference.data(), reference.size());
  } else {
    const std::string_view target = trim(reference.substr(0, headingSeparator));
    const std::string_view heading = trim(reference.substr(headingSeparator + 1));
    link.target.assign(target.data(), target.size());
    link.heading.assign(heading.data(), heading.size());
  }
  if (link.target.empty() && link.heading.empty()) return;

  const auto duplicate = std::find_if(
      metadata.links.begin(), metadata.links.end(),
      [&](const MarkdownIndexLink& value) { return value.target == link.target && value.heading == link.heading; });
  if (duplicate != metadata.links.end()) return;
  if (metadata.links.size() >= MAX_INDEX_LINKS) {
    metadata.truncated = true;
    return;
  }
  metadata.links.push_back(std::move(link));
}

bool validTagStart(const unsigned char ch) { return ch >= 0x80 || std::isalnum(ch) != 0 || ch == '_'; }

bool validTagContinuation(const unsigned char ch) { return validTagStart(ch) || ch == '-' || ch == '/'; }

void parseLinksAndTags(MarkdownIndexMetadata& metadata, const std::string_view line) {
  bool inlineCode = false;
  for (size_t index = 0; index < line.size();) {
    if (line[index] == '\\' && index + 1 < line.size()) {
      index += 2;
      continue;
    }
    if (line[index] == '`') {
      inlineCode = !inlineCode;
      index++;
      continue;
    }
    if (inlineCode) {
      index++;
      continue;
    }

    if (index + 1 < line.size() && line[index] == '[' && line[index + 1] == '[') {
      const size_t close = line.find("]]", index + 2);
      if (close != std::string_view::npos) {
        addWikiReference(metadata, line.substr(index + 2, close - index - 2));
        index = close + 2;
        continue;
      }
    }

    if (line[index] != '#') {
      index++;
      continue;
    }

    const bool boundary = index == 0 || isAsciiWhitespace(static_cast<unsigned char>(line[index - 1])) ||
                          line[index - 1] == '(' || line[index - 1] == '[' || line[index - 1] == '{' ||
                          line[index - 1] == '"' || line[index - 1] == '\'';
    if (!boundary || index + 1 >= line.size() || !validTagStart(static_cast<unsigned char>(line[index + 1]))) {
      index++;
      continue;
    }

    size_t end = index + 2;
    while (end < line.size() && validTagContinuation(static_cast<unsigned char>(line[end]))) end++;
    addUniqueBounded(metadata.tags, std::string(line.substr(index + 1, end - index - 1)), MAX_INDEX_TAGS,
                     metadata.truncated);
    index = end;
  }
}

}  // namespace

void MarkdownIndexBuilder::addLine(std::string_view line, const size_t sourceOffset) {
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  if (frontmatterState_ == FrontmatterState::Undecided) {
    if (line.starts_with("\xEF\xBB\xBF")) line.remove_prefix(3);
    if (trim(line) == "---") {
      frontmatterState_ = FrontmatterState::Active;
      return;
    }
    frontmatterState_ = FrontmatterState::Complete;
  }

  if (frontmatterState_ == FrontmatterState::Active) {
    parseFrontmatterLine(line);
    return;
  }
  parseContentLine(line, sourceOffset);
}

void MarkdownIndexBuilder::parseContentLine(const std::string_view line, const size_t sourceOffset) {
  const std::string_view left = trimLeft(line);
  if (!left.empty() && (left.front() == '`' || left.front() == '~')) {
    const size_t run = fenceRunLength(left, left.front());
    if (run >= 3) {
      if (fenceMarker_ == 0) {
        fenceMarker_ = left.front();
        fenceLength_ = run;
        return;
      }
      if (left.front() == fenceMarker_ && run >= fenceLength_ && trim(left.substr(run)).empty()) {
        fenceMarker_ = 0;
        fenceLength_ = 0;
        return;
      }
    }
  }
  if (fenceMarker_ != 0) return;

  uint8_t level = 0;
  std::string heading;
  if (parseAtxHeading(line, level, heading)) {
    if (metadata_.headings.size() >= MAX_INDEX_HEADINGS) {
      metadata_.truncated = true;
    } else {
      metadata_.headings.push_back(MarkdownIndexHeading{level, sourceOffset, std::move(heading)});
    }
  }
  parseLinksAndTags(metadata_, line);
}

}  // namespace micromarkd
