// LoopMaxPro - serial service: log output and command line.
// The command line has no commands of its own: it reads documents and sends commands
// through the same bus used by every other interface.
#pragma once
#include "kernel/kernel.h"
#include "kernel/log/serial_sink.h"

namespace lmp {

class SerialService : public Service {
 public:
  const ServiceInfo& info() const override;
  Status start(Kernel& kernel) override;
  void stop() override;
  void tick(uint32_t nowMs) override;
  void describe(JsonWriter& w) const override;

  // Runs one line as if it had been typed. Used by the tests.
  void execute(const char* line);

 private:
  void println(const char* text);
  void prompt();
  void handleLine(char* line);
  void printHelp();
  void printLog(uint16_t count);

  Kernel* kernel_ = nullptr;
  hal::ISerial* serial_ = nullptr;
  SerialLogSink sink_;
  char line_[LMP_CLI_LINE];
  size_t length_ = 0;
  bool overflow_ = false;
  bool lastWasCr_ = false;
};

}  // namespace lmp
