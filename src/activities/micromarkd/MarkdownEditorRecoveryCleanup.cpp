#include "MarkdownEditorActivity.h"

#ifdef MICROMARKD_APP

#include <HalStorage.h>
#include <MarkdownRecoveryPlan.h>

MarkdownEditorActivity::~MarkdownEditorActivity() {
  if (loadFromDisk_ || !dirty_ || path_.empty()) return;

  Storage.remove((path_ + micromarkd::NOTE_TEMPORARY_SUFFIX).c_str());
  Storage.remove((path_ + micromarkd::NOTE_BACKUP_SUFFIX).c_str());
  Storage.remove((path_ + micromarkd::NOTE_READY_SUFFIX).c_str());
}

#endif  // MICROMARKD_APP
