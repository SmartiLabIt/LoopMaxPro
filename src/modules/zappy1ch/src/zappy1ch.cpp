// LoopMaxPro - Zappy 1: one relay with a name and schedules.
#include "sdk/module.h"
#include "sdk/pin_bank.h"
#include "sdk/pin_schedule.h"

// Parameters of the build: custom_lmp_modules = zappy1ch:pins=22.
#ifndef LMP_ZAPPY1CH_PINS
#define LMP_ZAPPY1CH_PINS {-1}
#endif

namespace {

constexpr uint8_t kChannels = 1;

class Zappy1 final : public lmp::sdk::Module {
  LMP_MODULE("zappy1ch", "Zappy 1", "\xF0\x9F\x94\x8C", "1.0.0", lmp::sdk::ModuleKind::Hardware, 50)

 public:
  lmp::Status onInit(lmp::sdk::ModuleContext& c) override {
    static const int given[kChannels] = LMP_ZAPPY1CH_PINS;
    static uint8_t map[kChannels];
    for (uint8_t i = 0; i < kChannels; ++i) {
      if (given[i] < 0) {
        LMP_LOGE(c.log, "no pin for line %u on this board: give it in the build, zappy1ch:pins=pin", i + 1);
        return lmp::Status::Invalid;
      }
      map[i] = static_cast<uint8_t>(given[i]);
    }
    LMP_TRY(pins_.declare(c, map, lmp::hal::PinMode::Output, "Line %u"));
    pins_.kind("outlet");
    LMP_TRY(use(pins_));
    return use(schedules_);
  }

 private:
  lmp::sdk::PinBank<kChannels> pins_;
  lmp::sdk::PinSchedule<kChannels> schedules_{pins_};
};

}  // namespace

LMP_REGISTER_MODULE(zappy1ch, Zappy1)
