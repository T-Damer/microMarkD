#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace micromarkd {

constexpr size_t MAX_SEARCH_TERMS = 8;

struct SearchQuery {
  std::array<std::string, MAX_SEARCH_TERMS> terms{};
  uint8_t termCount = 0;
  size_t longestTermBytes = 0;

  bool empty() const { return termCount == 0; }
};

using SearchTermMatches = std::array<bool, MAX_SEARCH_TERMS>;

SearchQuery compileSearchQuery(std::string_view input);
std::string foldSearchText(std::string_view input);
void matchSearchTerms(const SearchQuery& query, std::string_view text, SearchTermMatches& matches);
bool allSearchTermsMatched(const SearchQuery& query, const SearchTermMatches& matches);
bool anySearchTermMatched(const SearchQuery& query, const SearchTermMatches& matches);
std::string makeSearchSnippet(std::string_view text, const SearchQuery& query, size_t maxBytes = 180);

}  // namespace micromarkd
