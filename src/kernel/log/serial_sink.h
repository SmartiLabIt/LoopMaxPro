// LoopMaxPro - log sink that prints on the console serial port.
// Line format:  [   12345] I net      message
#pragma once
#include "hal/api/iserial.h"
#include "kernel/log/logger.h"

namespace lmp {

class SerialLogSink : public ILogSink {
 public:
  SerialLogSink() = default;
  explicit SerialLogSink(hal::ISerial& serial) : serial_(&serial) {}

  void attach(hal::ISerial& serial) { serial_ = &serial; }

  // Entries above this level are not printed, whatever the logger accepts.
  void setLevel(LogLevel level) { level_ = level; }
  LogLevel level() const { return level_; }

  void onLog(const LogEntry& entry, const char* sourceName) override;

 private:
  hal::ISerial* serial_ = nullptr;
  LogLevel level_ = LogLevel::Trace;
};

}  // namespace lmp
