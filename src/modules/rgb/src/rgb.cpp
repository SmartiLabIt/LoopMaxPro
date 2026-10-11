// LoopMaxPro - RGB Controller, free edition: an analog RGB strip on three PWM pins.
// One light of the SDK (sdk/lights.h): colour or effect, brightness, speed, on and off, the
// schedules of the channel "LED". What the strip does is all in the SDK; here are the pins.
//
// The console draws it with the interface of the lights, shared with GanjaLeaf
// (src/sdk/ui/lights.js). Schedules, settings and info are the standard view.
#include "sdk/lights.h"
#include "sdk/module.h"
#include "sdk/pin_bank.h"
#include "sdk/pin_schedule.h"
#include "sdk/state.h"

// Parameters of the build: custom_lmp_modules = rgb:pins=2|3|4. The pins of each board are in
// module.json ("boardPins"): the build puts them here.
#ifndef LMP_RGB_PINS
#define LMP_RGB_PINS {-1, -1, -1}
#endif

namespace {

struct RgbCfg {
  uint16_t pwmHz;
};

}  // namespace

LMP_FIELDS(RgbCfg,
  LMP_F(pwmHz, "pwmHz", U16(100, 20000, 5000)))

namespace {

class Rgb final : public lmp::sdk::Module {
  LMP_MODULE("rgb", "RGB Controller", "\xF0\x9F\x8C\x88", "1.0.1", lmp::sdk::ModuleKind::Hardware, 20)

 public:
  lmp::Status onInit(lmp::sdk::ModuleContext& c) override {
    static const int given[3] = LMP_RGB_PINS;
    static lmp::sdk::LightSpec spec[1] = {{"LED", lmp::sdk::LightKind::Pwm, {0, 0, 0}, 1}};
    for (uint8_t i = 0; i < 3; ++i) {
      if (given[i] < 0) {
        LMP_LOGE(c.log, "no pins on this board: give them in the build, rgb:pins=r|g|b");
        return lmp::Status::Invalid;
      }
      spec[0].pins[i] = static_cast<uint8_t>(given[i]);
    }
    LMP_TRY(useSettings(cfg_));
    lights_.setPwmHz(cfg_->pwmHz);
    LMP_TRY(lights_.declare(c, spec));
    LMP_TRY(use(power_));
    LMP_TRY(use(schedules_));
    return use(lights_);
  }

 private:
  lmp::sdk::ModuleConfig<RgbCfg> cfg_;
  lmp::sdk::PinBank<1> power_;
  lmp::sdk::PinSchedule<1> schedules_{power_};
  lmp::sdk::Lights<1> lights_{power_};
};

}  // namespace

LMP_REGISTER_MODULE(rgb, Rgb)
