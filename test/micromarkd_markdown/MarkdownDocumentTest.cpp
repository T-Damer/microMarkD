#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "MicroMarkDMarkdown/MarkdownDocument.h"

TEST(MarkdownDocument, BuildsSafeFatFilename) {
  EXPECT_EQ(micromarkd::safeNoteFilename("  Project plan.md  "), "Project plan.md");
  EXPECT_EQ(micromarkd::safeNoteFilename("Research.MarkDown"), "Research.md");
  EXPECT_EQ(micromarkd::safeNoteFilename("../Secrets\\today:*?"), "Secrets today.md");
  EXPECT_EQ(micromarkd::safeNoteFilename("..."), "Untitled.md");
  EXPECT_EQ(micromarkd::safeNoteFilename("CON"), "_CON.md");
  EXPECT_EQ(micromarkd::safeNoteFilename("com1"), "_com1.md");
}

TEST(MarkdownDocument, TruncatesFilenameAtUtf8Boundary) {
  const std::string filename = micromarkd::safeNoteFilename("Очень длинное название заметки", 24);
  EXPECT_EQ(filename, "Очень длинн.md");
  EXPECT_EQ(filename.size(), 24u);
}

TEST(MarkdownDocument, RecognisesOnlyCanonicalMarkdownPathsInsideVault) {
  EXPECT_TRUE(micromarkd::isVaultMarkdownPath("/vault/Inbox.md"));
  EXPECT_TRUE(micromarkd::isVaultMarkdownPath("/vault/Projects/MiniMed.MarkDown"));

  EXPECT_FALSE(micromarkd::isVaultMarkdownPath("/vault"));
  EXPECT_FALSE(micromarkd::isVaultMarkdownPath("/vault/"));
  EXPECT_FALSE(micromarkd::isVaultMarkdownPath("/vault/Inbox.txt"));
  EXPECT_FALSE(micromarkd::isVaultMarkdownPath("/other/Inbox.md"));
  EXPECT_FALSE(micromarkd::isVaultMarkdownPath("/vault/../Inbox.md"));
  EXPECT_FALSE(micromarkd::isVaultMarkdownPath("/vault/Projects/./Inbox.md"));
  EXPECT_FALSE(micromarkd::isVaultMarkdownPath("/vault/Projects//Inbox.md"));
  EXPECT_FALSE(micromarkd::isVaultMarkdownPath("/vault/Projects\\Inbox.md"));
}

TEST(MarkdownDocument, DerivesRecentNoteLabelsFromVaultPath) {
  EXPECT_EQ(micromarkd::vaultNoteDisplayName("/vault/Inbox.md"), "Inbox");
  EXPECT_EQ(micromarkd::vaultNoteFolderLabel("/vault/Inbox.md"), "Vault");
  EXPECT_EQ(micromarkd::vaultNoteDisplayName("/vault/Projects/MiniMed.markdown"), "MiniMed");
  EXPECT_EQ(micromarkd::vaultNoteFolderLabel("/vault/Projects/MiniMed.markdown"), "Projects");
  EXPECT_EQ(micromarkd::vaultNoteFolderLabel("/vault/Projects/Active/MiniMed.md"), "Projects/Active");
  EXPECT_TRUE(micromarkd::vaultNoteDisplayName("/outside/Note.md").empty());
  EXPECT_TRUE(micromarkd::vaultNoteFolderLabel("/outside/Note.md").empty());
}

TEST(MarkdownDocument, NormalisesLineEndingsAndPreservesTrailingNewline) {
  bool trailingNewline = false;
  const auto lines = micromarkd::splitMarkdownLines("one\r\ntwo\rthree\n\n", trailingNewline);

  EXPECT_EQ(lines, (std::vector<std::string>{"one", "two", "three", ""}));
  EXPECT_TRUE(trailingNewline);
  EXPECT_EQ(micromarkd::joinMarkdownLines(lines, trailingNewline), "one\ntwo\nthree\n\n");
}

TEST(MarkdownDocument, KeepsDocumentsWithoutFinalNewlineUnchanged) {
  bool trailingNewline = true;
  const auto lines = micromarkd::splitMarkdownLines("first\nsecond", trailingNewline);

  EXPECT_EQ(lines, (std::vector<std::string>{"first", "second"}));
  EXPECT_FALSE(trailingNewline);
  EXPECT_EQ(micromarkd::joinMarkdownLines(lines, trailingNewline), "first\nsecond");
}

TEST(MarkdownDocument, RepresentsEmptyDocumentAsOneEditableLine) {
  bool trailingNewline = true;
  const auto lines = micromarkd::splitMarkdownLines("", trailingNewline);

  EXPECT_EQ(lines, (std::vector<std::string>{""}));
  EXPECT_FALSE(trailingNewline);
  EXPECT_TRUE(micromarkd::joinMarkdownLines(lines, trailingNewline).empty());
}
