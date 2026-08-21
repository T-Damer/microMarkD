#include <gtest/gtest.h>

#include <array>
#include <string>

#include "MicroMarkDMarkdown/MarkdownSearch.h"

TEST(MarkdownSearch, CompilesDistinctWhitespaceSeparatedTerms) {
  const auto query = micromarkd::compileSearchQuery("  MiniMed   ДЕТСКИЙ  minimed ");
  ASSERT_EQ(query.termCount, 2);
  EXPECT_EQ(query.terms[0], "minimed");
  EXPECT_EQ(query.terms[1], "детский");
  EXPECT_EQ(query.longestTermBytes, std::string("детский").size());
}

TEST(MarkdownSearch, FoldsAsciiAndCommonCyrillicCase) {
  EXPECT_EQ(micromarkd::foldSearchText("ABC ЁЖИК ПрИвЕт ІЇЄ"), "abc ёжик привет іїє");
}

TEST(MarkdownSearch, AccumulatesTermsAcrossSeparateTextWindows) {
  const auto query = micromarkd::compileSearchQuery("alpha beta");
  micromarkd::SearchTermMatches matches{};

  micromarkd::matchSearchTerms(query, "Alpha appears in the title", matches);
  EXPECT_TRUE(micromarkd::anySearchTermMatched(query, matches));
  EXPECT_FALSE(micromarkd::allSearchTermsMatched(query, matches));

  micromarkd::matchSearchTerms(query, "BETA appears later in the note", matches);
  EXPECT_TRUE(micromarkd::allSearchTermsMatched(query, matches));
}

TEST(MarkdownSearch, CapsTheNumberOfTerms) {
  const auto query = micromarkd::compileSearchQuery("one two three four five six seven eight nine ten");
  EXPECT_EQ(query.termCount, micromarkd::MAX_SEARCH_TERMS);
  EXPECT_EQ(query.terms[7], "eight");
}

TEST(MarkdownSearch, BuildsBoundedSnippetAroundFirstMatch) {
  const auto query = micromarkd::compileSearchQuery("ВАЖНЫЙ");
  const std::string snippet = micromarkd::makeSearchSnippet(
      "prefix prefix prefix prefix важный результат и длинный хвост хвост хвост", query, 38);

  EXPECT_LE(snippet.size(), 38u);
  EXPECT_NE(snippet.find("важный"), std::string::npos);
  EXPECT_EQ(snippet.rfind("...", 0), 0u);
}

TEST(MarkdownSearch, CollapsesWhitespaceInSnippet) {
  const auto query = micromarkd::compileSearchQuery("result");
  EXPECT_EQ(micromarkd::makeSearchSnippet("  line one\n\tresult   here  ", query, 80), "line one result here");
}

TEST(MarkdownSearch, HandlesTinySnippetBudgetWithoutUnderflow) {
  const auto query = micromarkd::compileSearchQuery("target");
  const std::string snippet = micromarkd::makeSearchSnippet("prefix target suffix", query, 2);
  EXPECT_LE(snippet.size(), 2u);
}
