#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "MicroMarkDMarkdown/MarkdownCatalog.h"

namespace {

micromarkd::MarkdownIndexRecord record(std::string path, std::vector<std::string> aliases = {},
                                       std::vector<std::string> tags = {}) {
  micromarkd::MarkdownIndexRecord value;
  value.path = std::move(path);
  value.metadata.aliases = std::move(aliases);
  value.metadata.tags = std::move(tags);
  return value;
}

}  // namespace

TEST(MarkdownCatalog, AggregatesTagsCaseInsensitively) {
  micromarkd::MarkdownCatalog catalog;
  EXPECT_TRUE(catalog.addRecord(record("/vault/A.md", {}, {"Medicine", "offline"})));
  EXPECT_TRUE(catalog.addRecord(record("/vault/B.md", {}, {"medicine", "Pediatrics"})));
  catalog.finalize();

  ASSERT_EQ(catalog.tags().size(), 3u);
  EXPECT_EQ(catalog.tags()[0].name, "Medicine");
  EXPECT_EQ(catalog.tags()[0].noteCount, 2u);
  EXPECT_EQ(catalog.notesForTag("MEDICINE"), (std::vector<std::string>{"/vault/A.md", "/vault/B.md"}));
}

TEST(MarkdownCatalog, ResolvesCurrentFolderBeforeVaultRoot) {
  micromarkd::MarkdownCatalog catalog;
  catalog.addRecord(record("/vault/Plan.md"));
  catalog.addRecord(record("/vault/Projects/Plan.md"));
  catalog.addRecord(record("/vault/Projects/Source.md"));
  catalog.finalize();

  EXPECT_EQ(catalog.resolveTarget("/vault/Projects/Source.md", "Plan"), "/vault/Projects/Plan.md");
  EXPECT_EQ(catalog.resolveTarget("/vault/Projects/Source.md", "/Plan"), "/vault/Projects/Plan.md");
  EXPECT_EQ(catalog.resolveTarget("/vault/Projects/Source.md", "../Plan"), "/vault/Plan.md");
}

TEST(MarkdownCatalog, ResolvesUniqueBasenameAndAlias) {
  micromarkd::MarkdownCatalog catalog;
  catalog.addRecord(record("/vault/Medicine/Diabetes.md", {"DM", "Сахарный диабет"}));
  catalog.addRecord(record("/vault/Projects/Overview.md"));
  catalog.finalize();

  EXPECT_EQ(catalog.resolveTarget("/vault/Home.md", "Diabetes"), "/vault/Medicine/Diabetes.md");
  EXPECT_EQ(catalog.resolveTarget("/vault/Home.md", "dm"), "/vault/Medicine/Diabetes.md");
  EXPECT_EQ(catalog.resolveTarget("/vault/Home.md", "САХАРНЫЙ ДИАБЕТ"), "/vault/Medicine/Diabetes.md");
}

TEST(MarkdownCatalog, RefusesAmbiguousAliases) {
  micromarkd::MarkdownCatalog catalog;
  catalog.addRecord(record("/vault/A.md", {"Shared"}));
  catalog.addRecord(record("/vault/B.md", {"shared"}));
  catalog.finalize();

  EXPECT_TRUE(catalog.resolveTarget("/vault/Home.md", "Shared").empty());
}

TEST(MarkdownCatalog, ResolvesExplicitPathsEvenWhenBasenamesCollide) {
  micromarkd::MarkdownCatalog catalog;
  catalog.addRecord(record("/vault/One/Note.md"));
  catalog.addRecord(record("/vault/Two/Note.markdown"));
  catalog.finalize();

  EXPECT_EQ(catalog.resolveTarget("/vault/Home.md", "One/Note.md"), "/vault/One/Note.md");
  EXPECT_EQ(catalog.resolveTarget("/vault/Home.md", "Two/Note"), "/vault/Two/Note.markdown");
  EXPECT_TRUE(catalog.resolveTarget("/vault/Home.md", "Note").empty());
}

TEST(MarkdownCatalog, RejectsTraversalOutsideVault) {
  micromarkd::MarkdownCatalog catalog;
  catalog.addRecord(record("/vault/Safe.md"));
  catalog.finalize();

  EXPECT_TRUE(catalog.resolveTarget("/vault/Folder/Source.md", "../../Safe").empty());
}

TEST(MarkdownCatalog, BacklinkHelperUsesResolvedAlias) {
  micromarkd::MarkdownCatalog catalog;
  catalog.addRecord(record("/vault/Medicine/Diabetes.md", {"DM"}));
  catalog.addRecord(record("/vault/Notes/Source.md"));
  catalog.finalize();

  micromarkd::MarkdownIndexLink aliasLink{"DM", "Treatment"};
  micromarkd::MarkdownIndexLink localHeading{"", "Local"};
  EXPECT_TRUE(micromarkd::catalogLinkTargetsPath(catalog, "/vault/Notes/Source.md", aliasLink,
                                                  "/vault/Medicine/Diabetes.md"));
  EXPECT_FALSE(micromarkd::catalogLinkTargetsPath(catalog, "/vault/Notes/Source.md", localHeading,
                                                   "/vault/Notes/Source.md"));
}

TEST(MarkdownCatalog, MarksCatalogTruncatedWhenMetadataIsIncomplete) {
  micromarkd::MarkdownCatalog catalog;
  auto value = record("/vault/A.md");
  value.metadata.truncated = true;
  EXPECT_TRUE(catalog.addRecord(value));
  EXPECT_TRUE(catalog.truncated());
}
