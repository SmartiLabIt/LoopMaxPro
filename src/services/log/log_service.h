// LoopMaxPro - log service: document, commands and paged reading of the kernel logger.
#pragma once
#include "kernel/kernel.h"

namespace lmp {

// Interface offered to other services.
class ILog {
 public:
  static constexpr ServiceId kId = ServiceId::Log;
  virtual ~ILog() = default;

  struct Query {
    uint32_t since = 0;                     // entries with a greater sequence number
    uint16_t max = 50;
    LogLevel level = LogLevel::Trace;       // entries up to this level
    const char* source = nullptr;           // only this source, null for all
  };

  // Writes {"seqLast":..,"bootDoneMs":..,"dropped":..,"items":[..]}.
  virtual void writeEntries(JsonWriter& w, const Query& query) const = 0;
};

class LogService : public Service, public ILog {
 public:
  const ServiceInfo& info() const override;
  Status start(Kernel& kernel) override;
  void describe(JsonWriter& w) const override;
  void commands(CommandSet& set) override;
  void* api() override { return static_cast<ILog*>(this); }

  void writeEntries(JsonWriter& w, const Query& query) const override;

 private:
  Kernel* kernel_ = nullptr;
};

}  // namespace lmp
