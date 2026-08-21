#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace micromarkd {

inline constexpr char NOTE_TEMPORARY_SUFFIX[] = ".tmp";
inline constexpr char NOTE_BACKUP_SUFFIX[] = ".bak";
inline constexpr char NOTE_READY_SUFFIX[] = ".tmp.ready";
inline constexpr char NOTE_READY_MAGIC[] = "MDR1";
inline constexpr size_t NOTE_READY_MAGIC_BYTES = sizeof(NOTE_READY_MAGIC) - 1;

enum class NoteRecoverySource : uint8_t { None, Canonical, Temporary, Backup };

struct NoteRecoveryPlan {
  NoteRecoverySource source = NoteRecoverySource::None;
  bool removeTemporary = false;
  bool removeBackup = false;
};

NoteRecoveryPlan planNoteRecovery(bool canonicalExists, bool temporaryExists, bool temporaryComplete,
                                  bool backupExists);
std::string recoveryCanonicalPath(std::string_view sidecarPath);

}  // namespace micromarkd
