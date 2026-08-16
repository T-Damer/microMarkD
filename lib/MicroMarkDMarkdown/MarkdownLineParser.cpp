#include "MarkdownLineParser.h"

#include <algorithm>
#include <cctype>
#include <limits>

namespace micromarkd {
namespace {

std::string_view trim(std::string_view value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.remove_prefix(1);
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.remove_suffix(1);
  }
  return value;
}

bool isSeparator(std::string_view source) {
  const auto value = trim(source);
  if (value.size() < 3) return false;

  char marker = 0;
  size_t count = 0;
  for (const char c : value) {
    if (c == ' ') continue;
    if (c != '-' && c != '*' && c != '_') return false;
    if (marker == 0) marker = c;
    if (c != marker) return false;
    count++;
  }
  return count >= 3;
}

bool orderedListPrefix(std::string_view source, size_t& prefixLength) {
  size_t pos = 0;
  while (pos < source.size() && std::isdigit(static_cast<unsigned char>(source[pos])) != 0) pos++;
  if (pos == 0 || pos + 1 >= source.size()) return false;
  if ((source[pos] != '.' && source[pos] != ')') || source[pos + 1] != ' ') return false;
  prefixLength = pos + 2;
  return true;
}

void appendWikiLink(ParsedLine& parsed, std::string_view target, std::string_view label) {
  const size_t start = parsed.text.size();
  parsed.text.append(label.data(), label.size());
  const size_t end = parsed.text.size();

  if (parsed.linkCount >= parsed.links.size() || end <= start || start > std::numeric_limits<uint16_t>::max()) return;

  auto& link = parsed.links[parsed.linkCount++];
  link.start = static_cast<uint16_t>(start);
  link.end = static_cast<uint16_t>(std::min(end, static_cast<size_t>(std::numeric_limits<uint16_t>::max())));
  link.target.assign(target.data(), target.size());
}

}  // namespace

std::string wikiTargetPathPart(std::string_view target) {
  target = trim(target);
  const size_t alias = target.find('|');
  if (alias != std::string_view::npos) target = target.substr(0, alias);
  target = trim(target);

  const size_t heading = target.find('#');
  if (heading != std::string_view::npos) target = target.substr(0, heading);
  target = trim(target);
  return std::string(target);
}

ParsedLine parseMarkdownLine(std::string_view source) {
  ParsedLine parsed;
  if (!source.empty() && source.back() == '\r') source.remove_suffix(1);

  if (isSeparator(source)) {
    parsed.block = BlockKind::Separator;
    return parsed;
  }

  size_t prefixLength = 0;
  size_t headingLevel = 0;
  while (headingLevel < source.size() && headingLevel < 6 && source[headingLevel] == '#') headingLevel++;
  if (headingLevel > 0 && headingLevel < source.size() && source[headingLevel] == ' ') {
    parsed.block = BlockKind::Heading;
    parsed.headingLevel = static_cast<uint8_t>(headingLevel);
    parsed.bold = true;
    source.remove_prefix(headingLevel + 1);
  } else if (source.starts_with("> ")) {
    parsed.block = BlockKind::Quote;
    parsed.italic = true;
    source.remove_prefix(2);
  } else if (source.starts_with("- ") || source.starts_with("* ") || source.starts_with("+ ")) {
    parsed.block = BlockKind::Bullet;
    source.remove_prefix(2);
  } else if (orderedListPrefix(source, prefixLength)) {
    parsed.block = BlockKind::OrderedList;
  } else if (source.starts_with("    ")) {
    parsed.block = BlockKind::Code;
    source.remove_prefix(4);
  } else if (source.starts_with("\t")) {
    parsed.block = BlockKind::Code;
    source.remove_prefix(1);
  }

  if (parsed.block == BlockKind::Code) {
    parsed.text.assign(source.data(), source.size());
    return parsed;
  }

  parsed.text.reserve(source.size());
  for (size_t i = 0; i < source.size();) {
    if (source[i] == '\\' && i + 1 < source.size()) {
      parsed.text.push_back(source[i + 1]);
      i += 2;
      continue;
    }

    if (i + 1 < source.size() && source[i] == '[' && source[i + 1] == '[') {
      const size_t close = source.find("]]", i + 2);
      if (close != std::string_view::npos) {
        std::string_view body = trim(source.substr(i + 2, close - i - 2));
        const size_t alias = body.find('|');
        std::string_view target = trim(alias == std::string_view::npos ? body : body.substr(0, alias));
        std::string_view label = trim(alias == std::string_view::npos ? body : body.substr(alias + 1));
        if (!target.empty() && !label.empty()) {
          appendWikiLink(parsed, target, label);
          i = close + 2;
          continue;
        }
      }
    }

    if (i + 1 < source.size() &&
        ((source[i] == '*' && source[i + 1] == '*') || (source[i] == '_' && source[i + 1] == '_'))) {
      parsed.bold = true;
      i += 2;
      continue;
    }
    if (i + 1 < source.size() && source[i] == '~' && source[i + 1] == '~') {
      i += 2;
      continue;
    }
    if (source[i] == '*' || source[i] == '_') {
      parsed.italic = true;
      i++;
      continue;
    }
    if (source[i] == '`') {
      i++;
      continue;
    }

    parsed.text.push_back(source[i]);
    i++;
  }

  return parsed;
}

}  // namespace micromarkd
