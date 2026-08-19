#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "MicroMarkDMarkdown/MarkdownIndex.h"
#include "MicroMarkDMarkdown/MarkdownIndexStream.h"

TEST(MarkdownIndexStream, PreservesOffsetsAcrossSplitCrLfChunks) {
  micromarkd::MarkdownIndexStreamBuilder builder;
  builder.addBytes("# One\r");
  builder.addBytes("\nbody\r");
  builder.addBytes("## Two\n[[Other]] #tag");
  const auto record = builder.finish("/vault/Test.md");

  EXPECT_EQ(record.sourceSize, 33u);
  EXPECT_EQ(record.sourceFingerprint, micromarkd::updateMarkdownFingerprint(micromarkd::MARKDOWN_FINGERPRINT_SEED,
                                                                            "# One\r\nbody\r## Two\n[[Other]] #tag"));
  ASSERT_EQ(record.metadata.headings.size(), 2u);
  EXPECT_EQ(record.metadata.headings[0].sourceOffset, 0u);
  EXPECT_EQ(record.metadata.headings[0].text, "One");
  EXPECT_EQ(record.metadata.headings[1].sourceOffset, 12u);
  EXPECT_EQ(record.metadata.headings[1].text, "Two");
  EXPECT_EQ(record.metadata.tags, (std::vector<std::string>{"tag"}));
  ASSERT_EQ(record.metadata.links.size(), 1u);
  EXPECT_EQ(record.metadata.links[0].target, "Other");
}

TEST(MarkdownIndexStream, DoesNotInventTrailingEmptyLine) {
  micromarkd::MarkdownIndexStreamBuilder builder;
  builder.addBytes("# One\n");
  const auto record = builder.finish("/vault/Test.md");

  ASSERT_EQ(record.metadata.headings.size(), 1u);
  EXPECT_EQ(record.metadata.headings[0].sourceOffset, 0u);
  EXPECT_EQ(record.sourceSize, 6u);
}

TEST(MarkdownIndexStream, SkipsOverlongPhysicalLineAndContinues) {
  micromarkd::MarkdownIndexStreamBuilder builder;
  std::string longLine(micromarkd::MAX_STREAM_INDEX_LINE_BYTES + 8, 'x');
  longLine += " [[Hidden]] #hidden\n# Visible\n";
  builder.addBytes(longLine.substr(0, 4000));
  builder.addBytes(longLine.substr(4000));
  const auto record = builder.finish("/vault/Test.md");

  EXPECT_TRUE(record.metadata.truncated);
  EXPECT_TRUE(record.metadata.links.empty());
  EXPECT_TRUE(record.metadata.tags.empty());
  ASSERT_EQ(record.metadata.headings.size(), 1u);
  EXPECT_EQ(record.metadata.headings[0].text, "Visible");
  EXPECT_EQ(record.metadata.headings[0].sourceOffset, longLine.find("# Visible"));
}

TEST(MarkdownIndexStream, IgnoresAdditionalBytesAfterFinish) {
  micromarkd::MarkdownIndexStreamBuilder builder;
  builder.addBytes("# One");
  const auto first = builder.finish("/vault/Test.md");
  builder.addBytes("\n# Two");
  const auto second = builder.finish("/vault/Test.md");

  EXPECT_EQ(second.sourceSize, first.sourceSize);
  EXPECT_EQ(second.sourceFingerprint, first.sourceFingerprint);
  ASSERT_EQ(second.metadata.headings.size(), 1u);
  EXPECT_EQ(second.metadata.headings[0].text, "One");
}
