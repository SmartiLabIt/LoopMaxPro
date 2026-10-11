// LoopMaxPro SDK - lights: RGB strips on three PWM pins and lines of addressable LEDs.
// Each light has a colour or an effect, a brightness, a speed, and is on or off. On and off is
// a channel without a pin of a PinBank: names, "restore" and the schedules are those of the
// SDK, one channel for each light. The effects run on the device; the document changes only
// when a choice changes, not at every colour of an effect.
//
//   static const lmp::sdk::LightSpec kLights[] = {
//       {"Leaf", lmp::sdk::LightKind::Pwm, {2, 3, 4}, 1},
//       {"Ring", lmp::sdk::LightKind::Pixels, {10}, 12}};
//   lmp::sdk::PinBank<2> power_;
//   lmp::sdk::PinSchedule<2> schedules_{power_};
//   lmp::sdk::Lights<2> lights_{power_};
//   LMP_TRY(lights_.declare(c, kLights));   // declares the channels of power_ too
//   LMP_TRY(use(power_)); LMP_TRY(use(schedules_)); LMP_TRY(use(lights_));
//
// Commands, all with "channel", 0 when missing: color (hex, or r g b), effect (name),
// brightness and speed (1..100), power (value). docs/modules-sdk.md.
#pragma once
#include "sdk/pin_bank.h"

namespace lmp::sdk {

#ifndef LMP_LIGHT_PIXELS_MAX
#define LMP_LIGHT_PIXELS_MAX 64
#endif

enum class LightKind : uint8_t { Pwm, Pixels };

struct LightSpec {
  const char* name;       // the name its channel is born with
  LightKind kind;
  uint8_t pins[3];        // Pwm: red, green, blue. Pixels: the data pin in pins[0]
  uint16_t count;         // Pixels: how many; 1 for a strip
};

struct LightColor {
  uint8_t r = 0, g = 0, b = 0;
};

// The effects. The first five go on every light, the others only on lines of pixels.
enum class LightEffect : uint8_t {
  Christmas, Rainbow, Breath, Blink, Fade,   // every light
  Chase, Wheel, Comet, Sparkle,               // lines of pixels
  Count
};
constexpr uint8_t kStripEffects = 5;
const char* lightEffectName(LightEffect e);
int lightEffectOf(const char* name);

// The colour of pixel i of n, ms milliseconds after the effect began, speed 1..100.
// Sparkle keeps a level for each pixel: it is not a function of the time and is not drawn here.
LightColor lightFrame(LightEffect e, uint32_t ms, uint8_t speed, const LightColor& base,
                      uint16_t i, uint16_t n);

class LightsBase : public Component {
 public:
  // Takes the pins of every light and declares the channels of the bank, one each.
  Status declare(ModuleContext& c, const LightSpec* specs);
  // Frequency of the PWM of the strips: before declare.
  void setPwmHz(uint16_t hz) { pwmHz_ = hz; }

  uint8_t size() const { return count_; }
  bool on(uint8_t light) const { return light < count_ && state_[light].on; }

  void commands(CommandSet& set) override;
  void describe(JsonWriter& w) const override;
  void save(JsonWriter& w) const override;
  void load(const JsonValue& doc) override;
  void tick(uint32_t nowMs) override;
  void stop() override;
  // For a home system each light is a light, "light0", "light1": on and off, brightness,
  // colour. A colour asked from there ends the effect, as the command does.
  void things(ThingSink& sink) const override;
  Status thingSet(const char* id, const ThingAsk& ask) override;

 protected:
  struct Light {
    LightSpec spec{};
    bool on = false;
    LightColor color{255, 0, 0};
    int8_t effect = -1;          // -1: the fixed colour
    uint8_t brightness = 100;
    uint8_t speed = 50;
    uint32_t startMs = 0;        // when the effect began
    uint8_t bits = 10;           // PWM
    uint32_t duty[3] = {};       // PWM: what the pins have
    bool written = false;
    uint32_t shownMs = 0;        // pixels: when the frame was sent last
    uint8_t frame[3 * LMP_LIGHT_PIXELS_MAX] = {};   // pixels: green, red, blue
    uint8_t spark[LMP_LIGHT_PIXELS_MAX] = {};       // sparkle: level of each pixel
  };

  LightsBase(PinBankBase& bank, Light* lights, uint8_t count)
      : bank_(bank), state_(lights), count_(count) {}

 private:
  Status open(Light& l);
  void close(Light& l);
  void render(Light& l, uint32_t nowMs);
  uint32_t dutyOf(const Light& l, uint8_t value) const;
  uint8_t pixelOf(const Light& l, uint8_t value) const;
  uint32_t random();
  bool effectFits(const Light& l, int effect) const;
  Status turnOn(uint8_t light);
  bool lightOf(const Args& a, CmdContext& ctx, uint8_t& out) const;

  PinBankBase& bank_;
  Light* state_;
  uint8_t count_;
  bool declared_ = false;
  uint16_t pwmHz_ = 5000;
  uint32_t nowMs_ = 0;
  uint32_t seed_ = 0x2545F491u;
};

template <size_t N>
class Lights : public LightsBase {
 public:
  explicit Lights(PinBankBase& bank) : LightsBase(bank, lights_, static_cast<uint8_t>(N)) {}

 private:
  Light lights_[N];
};

}  // namespace lmp::sdk
