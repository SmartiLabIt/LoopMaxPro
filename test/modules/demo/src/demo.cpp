// LoopMaxPro - demo module: two outputs with names and schedules, a button, a few settings.
// It is the module of the simulator and of the tests, and what a small product looks like:
// the code below is all there is to write.
#include "sdk/module.h"
#include "sdk/pin_bank.h"
#include "sdk/pin_schedule.h"
#include "sdk/state.h"
#include "sdk/tools.h"

// Parameters of the build: custom_lmp_modules = demo:pins=22|23;button=0
#ifndef LMP_DEMO_PINS
#define LMP_DEMO_PINS {22, 23}
#endif
#ifndef LMP_DEMO_BUTTON
#define LMP_DEMO_BUTTON -1
#endif

namespace {

constexpr uint8_t kChannels = 2;

struct DemoCfg {
  lmp::FixedString<32> label;
  uint16_t pulseMs;
  bool buttonToggles;
};

struct DemoState {
  uint32_t presses;
  uint32_t pulses;
};

constexpr lmp::ArgSpec kPulseArgs[] = {
    lmp::Arg::u8("channel", 0, kChannels - 1),
};

}  // namespace

LMP_FIELDS(DemoCfg,
  LMP_F(label,         "label",         Str(0, 32, "Demo")),
  LMP_F(pulseMs,       "pulseMs",       U16(50, 60000, 1000)),
  LMP_F(buttonToggles, "buttonToggles", Bool(true)))

LMP_FIELDS(DemoState,
  LMP_F(presses, "presses", U32()),
  LMP_F(pulses,  "pulses",  U32()))

namespace {

class Demo final : public lmp::sdk::Module {
  LMP_MODULE("demo", "Demo", "\xF0\x9F\xA7\xAA", "1.0.0", lmp::sdk::ModuleKind::Hardware, 20)

 public:
  lmp::Status onInit(lmp::sdk::ModuleContext& c) override {
    static const uint8_t map[kChannels] = LMP_DEMO_PINS;
    LMP_TRY(pins_.declare(c, map, lmp::hal::PinMode::Output, "Line %u"));
    LMP_TRY(useSettings(cfg_));
    LMP_TRY(use(state_));
    LMP_TRY(use(pins_));
    LMP_TRY(use(schedules_));

    c.commands->add("pulse",
                    LMP_HELP("Turns a channel on for the time of the settings"), kPulseArgs,
                    [this](const lmp::Args& a, lmp::CmdContext&) -> lmp::Status {
                      return pulse(a.u8("channel"));
                    });

    if (LMP_DEMO_BUTTON >= 0 && c.gpio) {
      LMP_TRY(button_.begin(*c.gpio, static_cast<uint8_t>(LMP_DEMO_BUTTON), true,
                            manifest().name));
    }
    return lmp::Status::Ok;
  }

  void onTick(uint32_t nowMs) override {
    nowMs_ = nowMs;
    for (uint8_t i = 0; i < kChannels; ++i) {
      if (pulseEnd_[i].due(nowMs)) pins_.set(i, false);
    }
    if (button_.poll(nowMs) == lmp::sdk::ButtonEvent::Pressed) {
      ++state_->presses;
      state_.touch();
      if (cfg_->buttonToggles) pins_.toggle(0);
    }
  }

  void onStop() override { button_.end(); }

 private:
  lmp::Status pulse(uint8_t channel) {
    LMP_TRY(pins_.set(channel, true));
    pulseEnd_[channel].arm(nowMs_, cfg_->pulseMs);
    ++state_->pulses;
    state_.touch();
    return lmp::Status::Ok;
  }

  lmp::sdk::ModuleConfig<DemoCfg> cfg_;
  lmp::sdk::State<DemoState> state_;
  lmp::sdk::PinBank<kChannels> pins_;
  lmp::sdk::PinSchedule<kChannels> schedules_{pins_};
  lmp::sdk::Button button_;
  lmp::sdk::Debounced pulseEnd_[kChannels];
  uint32_t nowMs_ = 0;
};

}  // namespace

LMP_REGISTER_MODULE(demo, Demo)
