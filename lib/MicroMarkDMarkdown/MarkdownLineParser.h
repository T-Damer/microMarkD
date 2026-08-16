#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace micromarkd {

constexpr size_t MAX_WIKI_LINKS_PER_LINE = 4;

enum class BlockKind : uint8_t { Paragraph, Heading, Quote, Bullet, OrderedList, Separator, Code };

struct WikiLink {
  uint16_t start = 0;
  uint16_t end = 0;
  std::string target;
};

struct ParsedLine {
  std::string text;
  BlockKind block = BlockKind::Paragraph;
  uint8_t headingLevel = 0;
  bool bold = false;
  bool italic = false;
  std::array<WikiLink, MAX_WIKI_LINKS_PER_LINE> links{};
  uint8_t linkCount = 0;
};

ParsedLine parseMarkdownLine(std::string_view source);
std::string wikiTargetPathPart(std::string_view target);

}  // namespace micromarkd
