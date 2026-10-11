#include "kernel/log/last_words_sink.h"

#include <stdio.h>

namespace lmp {

void LastWordsSink::onLog(const LogEntry& entry, const char* sourceName) {
  // The details stay out: the room is for what tells the story.
  if (!sys_ || entry.level > LogLevel::Info) return;
  // The whole line here, cut by the board to the room it has.
  char line[LMP_LOG_MSG_LEN + 48];
  snprintf(line, sizeof(line), "%lu %c %s: %s", static_cast<unsigned long>(entry.ms),
           logLevelLetter(entry.level), sourceName, entry.msg);
  sys_->lastWordsKeep(line);
}

}  // namespace lmp
