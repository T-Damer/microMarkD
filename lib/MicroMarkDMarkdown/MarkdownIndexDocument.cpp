#include "MarkdownIndexDocument.h"

namespace micromarkd {

MarkdownIndexRecord buildMarkdownIndexRecord(const std::string_view path, const std::vector<std::string>& lines,
                                             const bool trailingNewline) {
  MarkdownIndexRecord record;
  record.path.assign(path.data(), path.size());

  MarkdownIndexBuilder builder;
  size_t sourceOffset = 0;
  uint64_t fingerprint = MARKDOWN_FINGERPRINT_SEED;
  for (size_t index = 0; index < lines.size(); index++) {
    const std::string& line = lines[index];
    builder.addLine(line, sourceOffset);
    fingerprint = updateMarkdownFingerprint(fingerprint, line);
    sourceOffset += line.size();

    if (index + 1 < lines.size() || trailingNewline) {
      fingerprint = updateMarkdownFingerprint(fingerprint, "\n");
      sourceOffset++;
    }
  }

  if (lines.empty() && trailingNewline) {
    fingerprint = updateMarkdownFingerprint(fingerprint, "\n");
    sourceOffset = 1;
  }

  record.sourceSize = sourceOffset;
  record.sourceFingerprint = fingerprint;
  record.metadata = builder.metadata();
  return record;
}

}  // namespace micromarkd
