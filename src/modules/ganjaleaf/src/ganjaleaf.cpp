// LoopMaxPro - GanjaLeaf, free edition: the lamp in the shape of a leaf. An analog RGB strip in
// the leaf, on three PWM pins, and a ring of twelve addressable LEDs (WS2812) in the base.
// Two lights of the SDK (sdk/lights.h), each with its colour or effect, brightness, speed, on
// and off and its own schedules: channel 0 "Leaf", channel 1 "Ring".
//
// The console draws it with the interface of the lights, shared with RGB Controller
// (src/sdk/ui/lights.js).
#include "sdk/lights.h"
#include "sdk/module.h"
#include "sdk/pin_bank.h"
#include "sdk/pin_schedule.h"
#include "sdk/state.h"

// Parameters of the build: custom_lmp_modules = ganjaleaf:leaf=2|3|4;ring=10;leds=12.
// The pins of each board are in module.json ("boardPins"): the build puts them here.
#ifndef LMP_GANJALEAF_LEAF
#define LMP_GANJALEAF_LEAF {-1, -1, -1}
#endif
#ifndef LMP_GANJALEAF_RING
#define LMP_GANJALEAF_RING -1
#endif
#ifndef LMP_GANJALEAF_LEDS
#define LMP_GANJALEAF_LEDS 12
#endif

namespace {

struct LeafCfg {
  uint16_t pwmHz;
};

}  // namespace

LMP_FIELDS(LeafCfg,
  LMP_F(pwmHz, "pwmHz", U16(100, 20000, 5000).unit("Hz").technical()))

namespace {

class GanjaLeaf final : public lmp::sdk::Module {
  LMP_MODULE("ganjaleaf", "GanjaLeaf", "\xF0\x9F\x8C\xBF", "1.0.2", lmp::sdk::ModuleKind::Hardware, 20)

 public:
  lmp::Status onInit(lmp::sdk::ModuleContext& c) override {
    static const int leaf[3] = LMP_GANJALEAF_LEAF;
    static lmp::sdk::LightSpec specs[2] = {
        {"Leaf", lmp::sdk::LightKind::Pwm, {0, 0, 0}, 1},
        {"Ring", lmp::sdk::LightKind::Pixels, {0, 0, 0}, LMP_GANJALEAF_LEDS},
    };
    for (uint8_t i = 0; i < 3; ++i) {
      if (leaf[i] < 0 || LMP_GANJALEAF_RING < 0) {
        LMP_LOGE(c.log, "no pins on this board: give them in the build, ganjaleaf:leaf=r|g|b;ring=n");
        return lmp::Status::Invalid;
      }
      specs[0].pins[i] = static_cast<uint8_t>(leaf[i]);
    }
    specs[1].pins[0] = static_cast<uint8_t>(LMP_GANJALEAF_RING);
    LMP_TRY(useSettings(cfg_));
    lights_.setPwmHz(cfg_->pwmHz);
    LMP_TRY(lights_.declare(c, specs));
    LMP_TRY(use(power_));
    LMP_TRY(use(schedules_));
    return use(lights_);
  }

 private:
  lmp::sdk::ModuleConfig<LeafCfg> cfg_;
  lmp::sdk::PinBank<2> power_;
  lmp::sdk::PinSchedule<2> schedules_{power_};
  lmp::sdk::Lights<2> lights_{power_};
};

}  // namespace

LMP_REGISTER_MODULE(ganjaleaf, GanjaLeaf)
