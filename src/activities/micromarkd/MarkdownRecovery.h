#pragma once

#ifdef MICROMARKD_APP

#include <cstddef>
#include <string>

struct MarkdownRecoveryReport {
  size_t promotedTemporary = 0;
  size_t restoredBackup = 0;
  size_t discardedIncompleteTemporary = 0;
  size_t removedSidecars = 0;
  size_t failures = 0;
  size_t directoriesScanned = 0;
  size_t directoriesSkipped = 0;

  size_t recoveredNotes() const { return promotedTemporary + restoredBackup; }
  bool complete() const { return failures == 0 && directoriesSkipped == 0; }
};

MarkdownRecoveryReport recoverMarkdownVault(const std::string& rootPath);
bool removeMarkdownRecoverySidecars(const std::string& canonicalPath);

#endif  // MICROMARKD_APP
