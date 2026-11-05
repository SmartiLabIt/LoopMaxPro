#include "kernel/log/serial_sink.h"

#include <stdio.h>
#include <string.h>

namespace lmp {

void SerialLogSink::onLog(const LogEntry& entry, const char* sourceName) {
  if (!serial_ || entry.level > level_ || !serial_->ready()) return;

  // One buffer, one write: lines from different tasks do not mix.
  char line[LMP_LOG_MSG_LEN + 48];
  int n = snprintf(line, sizeof(line), "[%8lu] %c %-8s %s%s\r\n",
                   static_cast<unsigned long>(entry.ms), logLevelLetter(entry.level), sourceName,
                   entry.msg, entry.truncated ? "..." : "");
  if (n < 0) return;
  size_t len = static_cast<size_t>(n);
  if (len >= sizeof(line)) len = sizeof(line) - 1;
  serial_->write(reinterpret_cast<const uint8_t*>(line), len);
}

}  // namespace lmp
