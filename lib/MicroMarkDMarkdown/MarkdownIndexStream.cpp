#include "MarkdownIndexStream.h"

#include <utility>

namespace micromarkd {

void MarkdownIndexStreamBuilder::finishLine() {
  if (lineTruncated_) {
    truncated_ = true;
  } else {
    builder_.addLine(line_, lineOffset_);
  }
  line_.clear();
  lineTruncated_ = false;
}

void MarkdownIndexStreamBuilder::addBytes(const std::string_view bytes) {
  if (finished_ || bytes.empty()) return;
  fingerprint_ = updateMarkdownFingerprint(fingerprint_, bytes);

  for (const char ch : bytes) {
    sourceSize_++;

    if (pendingCarriageReturn_) {
      pendingCarriageReturn_ = false;
      if (ch == '\n') {
        lineOffset_ = sourceSize_;
        continue;
      }
      lineOffset_ = sourceSize_ - 1;
    }

    if (ch == '\r') {
      finishLine();
      pendingCarriageReturn_ = true;
      continue;
    }
    if (ch == '\n') {
      finishLine();
      lineOffset_ = sourceSize_;
      continue;
    }

    if (lineTruncated_) continue;
    if (line_.size() >= MAX_STREAM_INDEX_LINE_BYTES) {
      line_.clear();
      lineTruncated_ = true;
      truncated_ = true;
      continue;
    }
    line_.push_back(ch);
  }
}

MarkdownIndexRecord MarkdownIndexStreamBuilder::finish(const std::string_view path) {
  if (!finished_) {
    if (!line_.empty() || lineTruncated_) finishLine();
    finished_ = true;
  }

  MarkdownIndexRecord record;
  record.path.assign(path.data(), path.size());
  record.sourceSize = sourceSize_;
  record.sourceFingerprint = fingerprint_;
  record.metadata = builder_.metadata();
  record.metadata.truncated = record.metadata.truncated || truncated_;
  return record;
}

}  // namespace micromarkd
