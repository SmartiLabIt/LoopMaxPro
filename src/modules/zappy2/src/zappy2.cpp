// LoopMaxPro - Zappy 2, free edition: two relays with names and schedules.
// The public one of the Zappy family: it works on its own, without cloud. In the span of a
// schedule the relay follows the schedule, as in LoopMax v1; "flip" leaves the relays that
// have schedules. The console draws it from its document: no interface of its own.
#include "sdk/module.h"
#include "sdk/pin_bank.h"
#include "sdk/pin_schedule.h"

// Parameters of the build: custom_lmp_modules = zappy2:pins=22|23. The pins of each board
// are in module.json ("boardPins"): the build puts them here.
#ifndef LMP_ZAPPY2_PINS
#define LMP_ZAPPY2_PINS {-1, -1}
#endif

namespace {

constexpr uint8_t kChannels = 2;

class Zappy2 final : public lmp::sdk::Module {
  LMP_MODULE("zappy2", "Zappy 2", "\xF0\x9F\x94\x8C", "1.0.1", lmp::sdk::ModuleKind::Hardware, 50)

 public:
  lmp::Status onInit(lmp::sdk::ModuleContext& c) override {
    static const int given[kChannels] = LMP_ZAPPY2_PINS;
    static uint8_t map[kChannels];
    for (uint8_t i = 0; i < kChannels; ++i) {
      if (given[i] < 0) {
        LMP_LOGE(c.log, "no pin for line %u on this board: give them in the build, zappy2:pins=a|b", i + 1);
        return lmp::Status::Invalid;
      }
      map[i] = static_cast<uint8_t>(given[i]);
    }
    LMP_TRY(pins_.declare(c, map, lmp::hal::PinMode::Output, "Line %u"));
    // For a home system, and the voice assistants behind it, the lines are sockets.
    pins_.kind("outlet");
    LMP_TRY(use(pins_));
    return use(schedules_);
  }

 private:
  lmp::sdk::PinBank<kChannels> pins_;
  lmp::sdk::PinSchedule<kChannels> schedules_{pins_};
};

}  // namespace

LMP_REGISTER_MODULE(zappy2, Zappy2)
