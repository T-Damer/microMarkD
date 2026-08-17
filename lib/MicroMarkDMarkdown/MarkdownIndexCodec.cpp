#include <algorithm>
#include <charconv>
#include <string>
#include <utility>
#include <vector>

#include "MarkdownIndex.h"

namespace micromarkd {
namespace {

constexpr std::string_view INDEX_MAGIC = "MMDIDX\t1";
constexpr std::string_view INDEX_ROOT = "/.micromarkd/index/";

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

std::string escapeField(const std::string_view value) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string escaped;
  escaped.reserve(value.size());
  for (const char raw : value) {
    const auto ch = static_cast<unsigned char>(raw);
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '\t':
        escaped += "\\t";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      default:
        if (ch < 0x20 || ch == 0x7F) {
          escaped += "\\x";
          escaped.push_back(hex[ch >> 4]);
          escaped.push_back(hex[ch & 0x0F]);
        } else {
          escaped.push_back(raw);
        }
        break;
    }
  }
  return escaped;
}

int hexDigit(const char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

bool unescapeField(const std::string_view value, std::string& result) {
  result.clear();
  result.reserve(value.size());
  for (size_t index = 0; index < value.size(); index++) {
    if (value[index] != '\\') {
      result.push_back(value[index]);
      continue;
    }
    if (++index >= value.size()) return false;
    const char escaped = value[index];
    switch (escaped) {
      case '\\':
        result.push_back('\\');
        break;
      case 't':
        result.push_back('\t');
        break;
      case 'n':
        result.push_back('\n');
        break;
      case 'r':
        result.push_back('\r');
        break;
      case 'x': {
        if (index + 2 >= value.size()) return false;
        const int high = hexDigit(value[index + 1]);
        const int low = hexDigit(value[index + 2]);
        if (high < 0 || low < 0) return false;
        result.push_back(static_cast<char>((high << 4) | low));
        index += 2;
        break;
      }
      default:
        return false;
    }
  }
  return true;
}

std::vector<std::string_view> splitFields(const std::string_view line) {
  std::vector<std::string_view> fields;
  size_t start = 0;
  while (true) {
    const size_t tab = line.find('\t', start);
    if (tab == std::string_view::npos) {
      fields.push_back(line.substr(start));
      break;
    }
    fields.push_back(line.substr(start, tab - start));
    start = tab + 1;
  }
  return fields;
}

template <typename Integer>
bool parseDecimal(const std::string_view value, Integer& result) {
  if (value.empty()) return false;
  Integer parsed{};
  const char* first = value.data();
  const char* last = value.data() + value.size();
  const auto conversion = std::from_chars(first, last, parsed, 10);
  if (conversion.ec != std::errc{} || conversion.ptr != last) return false;
  result = parsed;
  return true;
}

std::string hex64(const uint64_t value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result(16, '0');
  for (size_t index = 0; index < result.size(); index++) {
    const size_t shift = (result.size() - index - 1) * 4;
    result[index] = digits[(value >> shift) & 0x0F];
  }
  return result;
}

bool parseHex64(const std::string_view value, uint64_t& result) {
  if (value.size() != 16) return false;
  uint64_t parsed = 0;
  for (const char ch : value) {
    const int digit = hexDigit(ch);
    if (digit < 0) return false;
    parsed = (parsed << 4) | static_cast<uint64_t>(digit);
  }
  result = parsed;
  return true;
}

void appendRecordLine(std::string& encoded, const char kind, const std::string_view value) {
  encoded.push_back(kind);
  encoded.push_back('\t');
  encoded += escapeField(value);
  encoded.push_back('\n');
}

bool consumeRecordLine(const std::vector<std::string_view>& fields, MarkdownIndexRecord& record, bool& hasPath,
                       bool& hasSize, bool& hasFingerprint, bool& hasTruncated) {
  if (fields.empty() || fields[0].size() != 1) return false;
  switch (fields[0][0]) {
    case 'P':
      if (fields.size() != 2 || hasPath || !unescapeField(fields[1], record.path) || record.path.empty()) return false;
      hasPath = true;
      return true;
    case 'S':
      if (fields.size() != 2 || hasSize || !parseDecimal(fields[1], record.sourceSize)) return false;
      hasSize = true;
      return true;
    case 'F':
      if (fields.size() != 2 || hasFingerprint || !parseHex64(fields[1], record.sourceFingerprint)) return false;
      hasFingerprint = true;
      return true;
    case 'X':
      if (fields.size() != 2 || hasTruncated || (fields[1] != "0" && fields[1] != "1")) return false;
      record.metadata.truncated = fields[1] == "1";
      hasTruncated = true;
      return true;
    case 'A': {
      std::string alias;
      if (fields.size() != 2 || !unescapeField(fields[1], alias)) return false;
      addUniqueBounded(record.metadata.aliases, std::move(alias), MAX_INDEX_ALIASES, record.metadata.truncated);
      return true;
    }
    case 'T': {
      std::string tag;
      if (fields.size() != 2 || !unescapeField(fields[1], tag)) return false;
      addUniqueBounded(record.metadata.tags, std::move(tag), MAX_INDEX_TAGS, record.metadata.truncated);
      return true;
    }
    case 'H': {
      if (fields.size() != 4 || record.metadata.headings.size() >= MAX_INDEX_HEADINGS) return false;
      unsigned int level = 0;
      size_t offset = 0;
      std::string text;
      if (!parseDecimal(fields[1], level) || level == 0 || level > 6 || !parseDecimal(fields[2], offset) ||
          !unescapeField(fields[3], text) || text.empty()) {
        return false;
      }
      record.metadata.headings.push_back(MarkdownIndexHeading{static_cast<uint8_t>(level), offset, std::move(text)});
      return true;
    }
    case 'L': {
      if (fields.size() != 3 || record.metadata.links.size() >= MAX_INDEX_LINKS) return false;
      MarkdownIndexLink link;
      if (!unescapeField(fields[1], link.target) || !unescapeField(fields[2], link.heading) ||
          (link.target.empty() && link.heading.empty())) {
        return false;
      }
      record.metadata.links.push_back(std::move(link));
      return true;
    }
    default:
      return false;
  }
}
}  // namespace

uint64_t updateMarkdownFingerprint(uint64_t fingerprint, const std::string_view bytes) {
  constexpr uint64_t prime = 1099511628211ULL;
  for (const char raw : bytes) {
    fingerprint ^= static_cast<unsigned char>(raw);
    fingerprint *= prime;
  }
  return fingerprint;
}

std::string markdownIndexCacheKey(const std::string_view vaultPath) {
  return hex64(updateMarkdownFingerprint(MARKDOWN_FINGERPRINT_SEED, vaultPath)) + ".midx";
}

std::string markdownIndexCachePath(const std::string_view vaultPath) {
  return std::string(INDEX_ROOT) + markdownIndexCacheKey(vaultPath);
}

std::string encodeMarkdownIndexRecord(const MarkdownIndexRecord& record) {
  std::string encoded;
  encoded.reserve(128 + record.path.size() + record.metadata.aliases.size() * 24 + record.metadata.tags.size() * 16 +
                  record.metadata.headings.size() * 32 + record.metadata.links.size() * 32);
  encoded += INDEX_MAGIC;
  encoded.push_back('\n');
  appendRecordLine(encoded, 'P', record.path);
  encoded += "S\t" + std::to_string(record.sourceSize) + "\n";
  encoded += "F\t" + hex64(record.sourceFingerprint) + "\n";
  encoded += record.metadata.truncated ? "X\t1\n" : "X\t0\n";
  for (const auto& alias : record.metadata.aliases) appendRecordLine(encoded, 'A', alias);
  for (const auto& tag : record.metadata.tags) appendRecordLine(encoded, 'T', tag);
  for (const auto& heading : record.metadata.headings) {
    encoded += "H\t" + std::to_string(heading.level) + "\t" + std::to_string(heading.sourceOffset) + "\t" +
               escapeField(heading.text) + "\n";
  }
  for (const auto& link : record.metadata.links) {
    encoded += "L\t" + escapeField(link.target) + "\t" + escapeField(link.heading) + "\n";
  }
  encoded += "E\n";
  return encoded;
}

bool decodeMarkdownIndexRecord(const std::string_view encoded, MarkdownIndexRecord& record) {
  MarkdownIndexRecord parsed;
  size_t lineStart = 0;
  size_t lineNumber = 0;
  bool hasPath = false;
  bool hasSize = false;
  bool hasFingerprint = false;
  bool hasTruncated = false;
  bool ended = false;

  while (lineStart <= encoded.size()) {
    const size_t newline = encoded.find('\n', lineStart);
    const size_t lineEnd = newline == std::string_view::npos ? encoded.size() : newline;
    std::string_view line = encoded.substr(lineStart, lineEnd - lineStart);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

    if (lineNumber == 0) {
      if (line != INDEX_MAGIC) return false;
    } else if (!ended) {
      if (line == "E") {
        ended = true;
      } else if (line.empty() ||
                 !consumeRecordLine(splitFields(line), parsed, hasPath, hasSize, hasFingerprint, hasTruncated)) {
        return false;
      }
    } else if (!line.empty()) {
      return false;
    }

    lineNumber++;
    if (newline == std::string_view::npos) break;
    lineStart = newline + 1;
  }

  if (!ended || !hasPath || !hasSize || !hasFingerprint || !hasTruncated) return false;
  record = std::move(parsed);
  return true;
}
}  // namespace micromarkd
