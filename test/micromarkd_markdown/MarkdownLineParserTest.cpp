#include <gtest/gtest.h>

#include "MicroMarkDMarkdown/MarkdownLineParser.h"

using micromarkd::BlockKind;
using micromarkd::parseMarkdownLine;

TEST(MarkdownLineParser, ParsesHeadingAndRemovesPrefix) {
  const auto parsed = parseMarkdownLine("## Project notes");
  EXPECT_EQ(parsed.block, BlockKind::Heading);
  EXPECT_EQ(parsed.headingLevel, 2);
  EXPECT_TRUE(parsed.bold);
  EXPECT_EQ(parsed.text, "Project notes");
}

TEST(MarkdownLineParser, ParsesQuoteAndBulletBlocks) {
  const auto quote = parseMarkdownLine("> quoted text");
  EXPECT_EQ(quote.block, BlockKind::Quote);
  EXPECT_TRUE(quote.italic);
  EXPECT_EQ(quote.text, "quoted text");

  const auto bullet = parseMarkdownLine("- list item");
  EXPECT_EQ(bullet.block, BlockKind::Bullet);
  EXPECT_EQ(bullet.text, "list item");
}

TEST(MarkdownLineParser, RendersWikiAliasAndRecordsTarget) {
  const auto parsed = parseMarkdownLine("See [[Projects/MiniMed|MiniMed]] next");
  ASSERT_EQ(parsed.linkCount, 1);
  EXPECT_EQ(parsed.text, "See MiniMed next");
  EXPECT_EQ(parsed.links[0].target, "Projects/MiniMed");
  EXPECT_EQ(parsed.text.substr(parsed.links[0].start, parsed.links[0].end - parsed.links[0].start), "MiniMed");
}

TEST(MarkdownLineParser, HandlesMultipleWikiLinks) {
  const auto parsed = parseMarkdownLine("[[One]] and [[Two#Section|second]]");
  ASSERT_EQ(parsed.linkCount, 2);
  EXPECT_EQ(parsed.text, "One and second");
  EXPECT_EQ(parsed.links[0].target, "One");
  EXPECT_EQ(parsed.links[1].target, "Two#Section");
}

TEST(MarkdownLineParser, RemovesSimpleInlineMarkers) {
  const auto parsed = parseMarkdownLine("**bold** and *italic* and `code`");
  EXPECT_TRUE(parsed.bold);
  EXPECT_TRUE(parsed.italic);
  EXPECT_EQ(parsed.text, "bold and italic and code");
}

TEST(MarkdownLineParser, ExtractsWikiPathWithoutAliasOrHeading) {
  EXPECT_EQ(micromarkd::wikiTargetPathPart(" Projects/Note#Details | label "), "Projects/Note");
  EXPECT_TRUE(micromarkd::wikiTargetPathPart("#Local heading").empty());
}
