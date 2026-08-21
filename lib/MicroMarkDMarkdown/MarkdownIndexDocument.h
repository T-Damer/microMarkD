#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "MarkdownIndex.h"

namespace micromarkd {

MarkdownIndexRecord buildMarkdownIndexRecord(std::string_view path, const std::vector<std::string>& lines,
                                             bool trailingNewline);

}  // namespace micromarkd
