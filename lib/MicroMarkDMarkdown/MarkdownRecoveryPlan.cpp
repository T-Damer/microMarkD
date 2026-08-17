#include "MarkdownRecoveryPlan.h"

#include <cctype>

namespace micromarkd {
namespace {

char asciiLower(const char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); }

bool endsWithAsciiCaseInsensitive(const std::string_view value, const std::string_view suffix) {
  if (value.size() < suffix.size()) return false;
  const size_t offset = value.size() - suffix.size();
  for (size_t i = 0; i < suffix.size(); i++) {
    if (asciiLower(value[offset + i]) != asciiLower(suffix[i])) return false;
  }
  return true;
}

}  // namespace

NoteRecoveryPlan planNoteRecovery(const bool canonicalExists, const bool temporaryExists, const bool temporaryComplete,
                                  const bool backupExists) {
  NoteRecoveryPlan plan;
  if (canonicalExists) {
    plan.source = NoteRecoverySource::Canonical;
  } else if (temporaryExists && temporaryComplete) {
    plan.source = NoteRecoverySource::Temporary;
  } else if (backupExists) {
    plan.source = NoteRecoverySource::Backup;
  }

  plan.removeTemporary = temporaryExists && plan.source != NoteRecoverySource::Temporary;
  plan.removeBackup = backupExists && plan.source != NoteRecoverySource::Backup;
  return plan;
}

std::string recoveryCanonicalPath(const std::string_view sidecarPath) {
  size_t sidecarLength = 0;
  if (endsWithAsciiCaseInsensitive(sidecarPath, NOTE_READY_SUFFIX)) {
    sidecarLength = sizeof(NOTE_READY_SUFFIX) - 1;
  } else if (endsWithAsciiCaseInsensitive(sidecarPath, NOTE_TEMPORARY_SUFFIX)) {
    sidecarLength = sizeof(NOTE_TEMPORARY_SUFFIX) - 1;
  } else if (endsWithAsciiCaseInsensitive(sidecarPath, NOTE_BACKUP_SUFFIX)) {
    sidecarLength = sizeof(NOTE_BACKUP_SUFFIX) - 1;
  } else {
    return {};
  }

  const std::string_view canonical = sidecarPath.substr(0, sidecarPath.size() - sidecarLength);
  if (!endsWithAsciiCaseInsensitive(canonical, ".md") && !endsWithAsciiCaseInsensitive(canonical, ".markdown")) {
    return {};
  }
  return std::string(canonical);
}

}  // namespace micromarkd
