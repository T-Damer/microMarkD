#include <gtest/gtest.h>

#include <array>

#include "MicroMarkDMarkdown/MarkdownRecoveryPlan.h"

using micromarkd::NoteRecoverySource;
using micromarkd::planNoteRecovery;
using micromarkd::recoveryCanonicalPath;

TEST(MarkdownRecoveryPlan, CoversCanonicalTemporaryMarkerAndBackupStates) {
  struct RecoveryCase {
    bool canonical;
    bool temporary;
    bool temporaryComplete;
    bool backup;
    NoteRecoverySource source;
    bool removeTemporary;
    bool removeBackup;
  };

  constexpr std::array<RecoveryCase, 12> cases{{
      {false, false, false, false, NoteRecoverySource::None, false, false},
      {false, false, false, true, NoteRecoverySource::Backup, false, false},
      {false, true, false, false, NoteRecoverySource::None, true, false},
      {false, true, false, true, NoteRecoverySource::Backup, true, false},
      {false, true, true, false, NoteRecoverySource::Temporary, false, false},
      {false, true, true, true, NoteRecoverySource::Temporary, false, true},
      {true, false, false, false, NoteRecoverySource::Canonical, false, false},
      {true, false, false, true, NoteRecoverySource::Canonical, false, true},
      {true, true, false, false, NoteRecoverySource::Canonical, true, false},
      {true, true, false, true, NoteRecoverySource::Canonical, true, true},
      {true, true, true, false, NoteRecoverySource::Canonical, true, false},
      {true, true, true, true, NoteRecoverySource::Canonical, true, true},
  }};

  for (const auto& recoveryCase : cases) {
    const auto plan = planNoteRecovery(recoveryCase.canonical, recoveryCase.temporary,
                                       recoveryCase.temporaryComplete, recoveryCase.backup);
    EXPECT_EQ(plan.source, recoveryCase.source);
    EXPECT_EQ(plan.removeTemporary, recoveryCase.removeTemporary);
    EXPECT_EQ(plan.removeBackup, recoveryCase.removeBackup);
  }
}

TEST(MarkdownRecoveryPlan, IgnoresCompletionMarkerWithoutTemporaryFile) {
  const auto plan = planNoteRecovery(false, false, true, false);
  EXPECT_EQ(plan.source, NoteRecoverySource::None);
}

TEST(MarkdownRecoveryPlan, ExtractsCanonicalMarkdownPathFromEverySidecar) {
  EXPECT_EQ(recoveryCanonicalPath("/vault/Inbox.md.tmp"), "/vault/Inbox.md");
  EXPECT_EQ(recoveryCanonicalPath("/vault/Inbox.md.tmp.ready"), "/vault/Inbox.md");
  EXPECT_EQ(recoveryCanonicalPath("/vault/Notes.MarkDown.BAK"), "/vault/Notes.MarkDown");
  EXPECT_TRUE(recoveryCanonicalPath("/vault/not-markdown.txt.tmp.ready").empty());
  EXPECT_TRUE(recoveryCanonicalPath("/vault/Inbox.md").empty());
}
