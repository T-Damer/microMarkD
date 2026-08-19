#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "MarkdownIndex.h"

namespace micromarkd {

constexpr size_t MAX_STREAM_INDEX_LINE_BYTES = 8 * 1024;

class MarkdownIndexStreamBuilder {
 public:
  void addBytes(std::string_view bytes);
  MarkdownIndexRecord finish(std::string_view path);

  size_t sourceSize() const { return sourceSize_; }
  bool truncated() const { return truncated_; }

 private:
  MarkdownIndexBuilder builder_;
  std::string line_;
  size_t sourceSize_ = 0;
  size_t lineOffset_ = 0;
  uint64_t fingerprint_ = MARKDOWN_FINGERPRINT_SEED;
  bool pendingCarriageReturn_ = false;
  bool lineTruncated_ = false;
  bool finished_ = false;
  bool truncated_ = false;

  void finishLine();
};

}  // namespace micromarkd
