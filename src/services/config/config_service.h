// LoopMaxPro - config service: loads the settings and exposes them.
#pragma once
#include "kernel/kernel.h"

namespace lmp {

// The settings of a target, as the console reads them from the device (GET /api/cfg/<target>)
// and from the server (cloud request "cfg"): the section of the system that belongs to that
// service, or the settings of a module. Secrets never: "<key>Set" in their place. An object,
// empty for a target without settings; NotFound, and nothing written, for an unknown target.
Status describeTargetConfig(Kernel& kernel, StringView target, JsonWriter& w);

class ConfigService : public Service {
 public:
  const ServiceInfo& info() const override;
  Status start(Kernel& kernel) override;
  void stop() override;
  void tick(uint32_t nowMs) override;
  void describe(JsonWriter& w) const override;
  void commands(CommandSet& set) override;

 private:
  Status applySys(const Args& args, CmdContext& ctx);

  Kernel* kernel_ = nullptr;
  LogSource src_ = kLogSys;
};

}  // namespace lmp
