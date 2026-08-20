#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace micromarkd {

constexpr size_t MAX_INDEX_ALIASES = 16;
constexpr size_t MAX_INDEX_TAGS = 64;
constexpr size_t MAX_INDEX_HEADINGS = 128;
constexpr size_t MAX_INDEX_LINKS = 128;
constexpr uint64_t MARKDOWN_FINGERPRINT_SEED = 14695981039346656037ULL;

struct MarkdownIndexHeading {
  uint8_t level = 0;
  size_t sourceOffset = 0;
  std::string text;
};

struct MarkdownIndexLink {
  std::string target;
  std::string heading;
};

struct MarkdownIndexMetadata {
  std::vector<std::string> aliases;
  std::vector<std::string> tags;
  std::vector<MarkdownIndexHeading> headings;
  std::vector<MarkdownIndexLink> links;
  bool truncated = false;
};

struct MarkdownIndexRecord {
  std::string path;
  uint64_t sourceSize = 0;
  uint64_t sourceFingerprint = MARKDOWN_FINGERPRINT_SEED;
  MarkdownIndexMetadata metadata;
};

class MarkdownIndexBuilder {
 public:
  void addLine(std::string_view line, size_t sourceOffset = 0);

  const MarkdownIndexMetadata& metadata() const { return metadata_; }

 private:
  enum class FrontmatterState : uint8_t { Undecided, Active, Complete };
  enum class FrontmatterList : uint8_t { None, Aliases, Tags };

  MarkdownIndexMetadata metadata_;
  FrontmatterState frontmatterState_ = FrontmatterState::Undecided;
  FrontmatterList frontmatterList_ = FrontmatterList::None;
  char fenceMarker_ = 0;
  size_t fenceLength_ = 0;

  void parseFrontmatterLine(std::string_view line);
  void parseContentLine(std::string_view line, size_t sourceOffset);
};

uint64_t updateMarkdownFingerprint(uint64_t fingerprint, std::string_view bytes);
std::string markdownIndexCacheKey(std::string_view vaultPath);
std::string markdownIndexCachePath(std::string_view vaultPath);
std::string encodeMarkdownIndexRecord(const MarkdownIndexRecord& record);
bool decodeMarkdownIndexRecord(std::string_view encoded, MarkdownIndexRecord& record);

}  // namespace micromarkd
