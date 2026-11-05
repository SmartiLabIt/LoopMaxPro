// LoopMaxPro - log sink that hands the entries to the last words of the board (hal/api/isystem.h):
// what a board keeps across a restart, to tell after it why it stopped.
#pragma once
#include "hal/api/isystem.h"
#include "kernel/log/logger.h"

namespace lmp {

class LastWordsSink : public ILogSink {
 public:
  void setSystem(hal::ISystem* sys) { sys_ = sys; }
  void onLog(const LogEntry& entry, const char* sourceName) override;

 private:
  hal::ISystem* sys_ = nullptr;
};

}  // namespace lmp
