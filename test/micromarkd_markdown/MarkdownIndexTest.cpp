#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "MicroMarkDMarkdown/MarkdownIndex.h"
#include "MicroMarkDMarkdown/MarkdownIndexDocument.h"

TEST(MarkdownIndex, ExtractsFrontmatterAliasesAndTags) {
  micromarkd::MarkdownIndexBuilder builder;
  builder.addLine("---", 0);
  builder.addLine("aliases: [MiniMed, \"Medical notes\", 'Child''s notes']", 4);
  builder.addLine("tags:", 64);
  builder.addLine("  - medicine", 70);
  builder.addLine("  - #offline", 83);
  builder.addLine("tag: pediatrics # retained comment", 96);
  builder.addLine("---", 132);

  const auto& metadata = builder.metadata();
  EXPECT_EQ(metadata.aliases, (std::vector<std::string>{"MiniMed", "Medical notes", "Child's notes"}));
  EXPECT_EQ(metadata.tags, (std::vector<std::string>{"medicine", "offline", "pediatrics"}));
  EXPECT_FALSE(metadata.truncated);
}

TEST(MarkdownIndex, ExtractsHeadingsLinksAndInlineTags) {
  micromarkd::MarkdownIndexBuilder builder;
  builder.addLine("## **Plan** [[Overview|overview]] ###", 42);
  builder.addLine("See #pediatrics and [[Projects/Plan#Risks|risk list]] plus [[#Local heading]].", 90);

  const auto& metadata = builder.metadata();
  ASSERT_EQ(metadata.headings.size(), 1u);
  EXPECT_EQ(metadata.headings[0].level, 2);
  EXPECT_EQ(metadata.headings[0].sourceOffset, 42u);
  EXPECT_EQ(metadata.headings[0].text, "Plan overview");
  EXPECT_EQ(metadata.tags, (std::vector<std::string>{"pediatrics"}));

  ASSERT_EQ(metadata.links.size(), 3u);
  EXPECT_EQ(metadata.links[0].target, "Overview");
  EXPECT_TRUE(metadata.links[0].heading.empty());
  EXPECT_EQ(metadata.links[1].target, "Projects/Plan");
  EXPECT_EQ(metadata.links[1].heading, "Risks");
  EXPECT_TRUE(metadata.links[2].target.empty());
  EXPECT_EQ(metadata.links[2].heading, "Local heading");
}

TEST(MarkdownIndex, IgnoresFencedAndInlineCodeAndDeduplicatesReferences) {
  micromarkd::MarkdownIndexBuilder builder;
  builder.addLine("```markdown", 0);
  builder.addLine("# Hidden [[Hidden]] #hidden", 12);
  builder.addLine("```", 42);
  builder.addLine("# Visible", 46);
  builder.addLine("`[[Inline]] #inline` [[Visible]] [[Visible]] #real #real", 56);

  const auto& metadata = builder.metadata();
  ASSERT_EQ(metadata.headings.size(), 1u);
  EXPECT_EQ(metadata.headings[0].text, "Visible");
  ASSERT_EQ(metadata.links.size(), 1u);
  EXPECT_EQ(metadata.links[0].target, "Visible");
  EXPECT_EQ(metadata.tags, (std::vector<std::string>{"real"}));
}

TEST(MarkdownIndex, EnforcesBoundedMetadataCollections) {
  micromarkd::MarkdownIndexBuilder builder;
  builder.addLine("---");
  builder.addLine("aliases:");
  for (size_t index = 0; index <= micromarkd::MAX_INDEX_ALIASES; index++) {
    builder.addLine("  - alias-" + std::to_string(index));
  }
  builder.addLine("---");

  const auto& metadata = builder.metadata();
  EXPECT_EQ(metadata.aliases.size(), micromarkd::MAX_INDEX_ALIASES);
  EXPECT_TRUE(metadata.truncated);
}

TEST(MarkdownIndex, BuildsRecordUsingExactEditorByteLayout) {
  const std::vector<std::string> lines = {"# First", "Body [[Other]] #tag", "## Second"};
  const auto record = micromarkd::buildMarkdownIndexRecord("/vault/Test.md", lines, true);

  EXPECT_EQ(record.path, "/vault/Test.md");
  EXPECT_EQ(record.sourceSize, 38u);
  EXPECT_EQ(record.sourceFingerprint, 0x7019EDD40B611B5FULL);
  EXPECT_EQ(record.metadata.tags, (std::vector<std::string>{"tag"}));
  ASSERT_EQ(record.metadata.headings.size(), 2u);
  EXPECT_EQ(record.metadata.headings[0].sourceOffset, 0u);
  EXPECT_EQ(record.metadata.headings[0].text, "First");
  EXPECT_EQ(record.metadata.headings[1].sourceOffset, 28u);
  EXPECT_EQ(record.metadata.headings[1].text, "Second");
  ASSERT_EQ(record.metadata.links.size(), 1u);
  EXPECT_EQ(record.metadata.links[0].target, "Other");
}

TEST(MarkdownIndex, PreservesMissingAndEmptyFinalNewlineLayouts) {
  const std::vector<std::string> lines = {"# One", "tail"};
  const auto noTrailing = micromarkd::buildMarkdownIndexRecord("/vault/NoTrailing.md", lines, false);
  const auto trailing = micromarkd::buildMarkdownIndexRecord("/vault/Trailing.md", lines, true);
  const auto emptyTrailing =
      micromarkd::buildMarkdownIndexRecord("/vault/Empty.md", std::vector<std::string>{}, true);

  EXPECT_EQ(noTrailing.sourceSize, 10u);
  EXPECT_EQ(noTrailing.sourceFingerprint,
            micromarkd::updateMarkdownFingerprint(micromarkd::MARKDOWN_FINGERPRINT_SEED, "# One\ntail"));
  EXPECT_EQ(trailing.sourceSize, 11u);
  EXPECT_EQ(trailing.sourceFingerprint,
            micromarkd::updateMarkdownFingerprint(micromarkd::MARKDOWN_FINGERPRINT_SEED, "# One\ntail\n"));
  EXPECT_EQ(emptyTrailing.sourceSize, 1u);
  EXPECT_EQ(emptyTrailing.sourceFingerprint,
            micromarkd::updateMarkdownFingerprint(micromarkd::MARKDOWN_FINGERPRINT_SEED, "\n"));
}

TEST(MarkdownIndex, RoundTripsVersionedDisposableRecord) {
  micromarkd::MarkdownIndexRecord record;
  record.path = "/vault/Projects/Plan\tA.md";
  record.sourceSize = 987654;
  record.sourceFingerprint = 0x0123456789ABCDEFULL;
  record.metadata.truncated = true;
  record.metadata.aliases = {"Plan", "Line one\nline two", "Back\\slash"};
  record.metadata.tags = {"medicine", "child-health"};
  record.metadata.headings.push_back({2, 123, "Risk\tregister"});
  record.metadata.links.push_back({"Projects/Other", "Details"});
  record.metadata.links.push_back({"", "Local"});

  const std::string encoded = micromarkd::encodeMarkdownIndexRecord(record);
  micromarkd::MarkdownIndexRecord decoded;
  ASSERT_TRUE(micromarkd::decodeMarkdownIndexRecord(encoded, decoded));

  EXPECT_EQ(decoded.path, record.path);
  EXPECT_EQ(decoded.sourceSize, record.sourceSize);
  EXPECT_EQ(decoded.sourceFingerprint, record.sourceFingerprint);
  EXPECT_EQ(decoded.metadata.truncated, record.metadata.truncated);
  EXPECT_EQ(decoded.metadata.aliases, record.metadata.aliases);
  EXPECT_EQ(decoded.metadata.tags, record.metadata.tags);
  ASSERT_EQ(decoded.metadata.headings.size(), 1u);
  EXPECT_EQ(decoded.metadata.headings[0].level, 2);
  EXPECT_EQ(decoded.metadata.headings[0].sourceOffset, 123u);
  EXPECT_EQ(decoded.metadata.headings[0].text, "Risk\tregister");
  ASSERT_EQ(decoded.metadata.links.size(), 2u);
  EXPECT_EQ(decoded.metadata.links[0].target, "Projects/Other");
  EXPECT_EQ(decoded.metadata.links[0].heading, "Details");
  EXPECT_TRUE(decoded.metadata.links[1].target.empty());
  EXPECT_EQ(decoded.metadata.links[1].heading, "Local");
}

TEST(MarkdownIndex, RejectsUnknownOrMalformedRecordVersions) {
  micromarkd::MarkdownIndexRecord record;
  EXPECT_FALSE(micromarkd::decodeMarkdownIndexRecord("MMDIDX\t2\nE\n", record));
  EXPECT_FALSE(micromarkd::decodeMarkdownIndexRecord(
      "MMDIDX\t1\nP\t/vault/Test.md\nS\tnot-a-number\nF\t0000000000000000\nX\t0\nE\n", record));
  EXPECT_FALSE(micromarkd::decodeMarkdownIndexRecord(
      "MMDIDX\t1\nP\t/vault/Test.md\nS\t1\nF\t0000000000000000\nX\t0\nA\tbad\\qescape\nE\n", record));
  EXPECT_FALSE(
      micromarkd::decodeMarkdownIndexRecord("MMDIDX\t1\nP\t/vault/Test.md\nS\t1\nF\t0000000000000000\nX\t0\n", record));
}

TEST(MarkdownIndex, UsesStableIncrementalFingerprintAndCacheKey) {
  uint64_t incremental = micromarkd::updateMarkdownFingerprint(micromarkd::MARKDOWN_FINGERPRINT_SEED, "ab");
  incremental = micromarkd::updateMarkdownFingerprint(incremental, "c");
  const uint64_t complete = micromarkd::updateMarkdownFingerprint(micromarkd::MARKDOWN_FINGERPRINT_SEED, "abc");

  EXPECT_EQ(incremental, complete);
  EXPECT_EQ(complete, 0xE71FA2190541574BULL);
  EXPECT_EQ(micromarkd::markdownIndexCacheKey("/vault/Plan.md"), "aa57d9ca23a4b993.midx");
  EXPECT_EQ(micromarkd::markdownIndexCachePath("/vault/Plan.md"), "/.micromarkd/index/aa57d9ca23a4b993.midx");
}
