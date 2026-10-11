#include "sdk/lights.h"

#include <stdio.h>
#include <string.h>

#include "services/gpio/gpio_service.h"

namespace lmp::sdk {
namespace {

constexpr const char* kEffectNames[] = {"christmas", "rainbow", "breath", "blink", "fade",
                                        "chase", "wheel", "comet", "sparkle"};
static_assert(sizeof(kEffectNames) / sizeof(kEffectNames[0]) ==
              static_cast<size_t>(LightEffect::Count), "a name for every effect");

// How often a frame of pixels that does not change is sent again.
constexpr uint32_t kPixelsAgainMs = 2000;

constexpr ArgSpec kColorArgs[] = {
    Arg::u8("channel", 0, 7).optional(),
    Arg::str("hex", 6, 7).optional(),
    Arg::u8("r", 0, 255).optional(),
    Arg::u8("g", 0, 255).optional(),
    Arg::u8("b", 0, 255).optional(),
};
constexpr ArgSpec kEffectArgs[] = {
    Arg::u8("channel", 0, 7).optional(),
    Arg::str("name", 3, 12),
};
constexpr ArgSpec kPercentArgs[] = {
    Arg::u8("channel", 0, 7).optional(),
    Arg::u8("value", 1, 100),
};
constexpr ArgSpec kPowerArgs[] = {
    Arg::u8("channel", 0, 7).optional(),
    Arg::boolean("value"),
};

// "#ff8000" or "ff8000".
bool parseHex(const char* text, LightColor& out) {
  if (*text == '#') ++text;
  if (strlen(text) != 6) return false;
  uint8_t v[3];
  for (int i = 0; i < 3; ++i) {
    int n = 0;
    for (int k = 0; k < 2; ++k) {
      const char c = text[i * 2 + k];
      const int d = c >= '0' && c <= '9' ? c - '0'
                  : c >= 'a' && c <= 'f' ? c - 'a' + 10
                  : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (d < 0) return false;
      n = n * 16 + d;
    }
    v[i] = static_cast<uint8_t>(n);
  }
  out.r = v[0];
  out.g = v[1];
  out.b = v[2];
  return true;
}

void hexOf(const LightColor& c, char* out) { snprintf(out, 8, "#%02x%02x%02x", c.r, c.g, c.b); }

// Hue 0..1535 in six ramps of 256, full saturation and value.
LightColor wheel(uint32_t hue) {
  hue %= 1536;
  const uint8_t x = static_cast<uint8_t>(hue & 0xFF);
  switch (hue >> 8) {
    case 0:  return {255, x, 0};
    case 1:  return {static_cast<uint8_t>(255 - x), 255, 0};
    case 2:  return {0, 255, x};
    case 3:  return {0, static_cast<uint8_t>(255 - x), 255};
    case 4:  return {x, 0, 255};
    default: return {255, 0, static_cast<uint8_t>(255 - x)};
  }
}

LightColor scaled(const LightColor& c, uint32_t num, uint32_t den) {
  return {static_cast<uint8_t>(c.r * num / den), static_cast<uint8_t>(c.g * num / den),
          static_cast<uint8_t>(c.b * num / den)};
}

LightColor mix(const LightColor& a, const LightColor& b, uint32_t t, uint32_t span) {
  auto one = [&](uint8_t x, uint8_t y) {
    return static_cast<uint8_t>((x * (span - t) + y * t) / span);
  };
  return {one(a.r, b.r), one(a.g, b.g), one(a.b, b.b)};
}

}  // namespace

const char* lightEffectName(LightEffect e) {
  const uint8_t i = static_cast<uint8_t>(e);
  return i < static_cast<uint8_t>(LightEffect::Count) ? kEffectNames[i] : "";
}

int lightEffectOf(const char* name) {
  for (uint8_t i = 0; i < static_cast<uint8_t>(LightEffect::Count); ++i) {
    if (strcmp(name, kEffectNames[i]) == 0) return i;
  }
  return -1;
}

// At speed 50 the Christmas steps last about a second, as in the first firmwares of the
// controllers; the chase moves a pixel every 100 ms, as the ring of GanjaLeaf.
LightColor lightFrame(LightEffect e, uint32_t ms, uint8_t speed, const LightColor& base,
                      uint16_t i, uint16_t n) {
  const uint32_t s = speed ? speed : 1;
  if (n == 0) n = 1;
  switch (e) {
    case LightEffect::Christmas: {
      static const LightColor steps[] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0}};
      const uint32_t stepMs = 2100 - 20 * s;
      return steps[(ms / stepMs) % 5];
    }
    case LightEffect::Rainbow: {
      const uint32_t cycleMs = 600000 / s;   // 10 minutes at 1, 12 s at 50, 6 s at 100
      return wheel(static_cast<uint32_t>((static_cast<uint64_t>(ms % cycleMs) * 1536) / cycleMs));
    }
    case LightEffect::Breath: {
      const uint32_t periodMs = 40000 / s + 1500;
      const uint32_t t = ms % periodMs;
      const uint32_t half = periodMs / 2;
      // Up and down in a triangle, squared: slow at the bottom, as a breath.
      const uint64_t up = t < half ? t : periodMs - t;
      const uint32_t level = static_cast<uint32_t>(up * up * 1000 / (static_cast<uint64_t>(half) * half));
      return scaled(base, 50 + level * 950 / 1000, 1000);
    }
    case LightEffect::Blink: {
      const uint32_t periodMs = 20000 / s + 100;
      return (ms % periodMs) < periodMs / 2 ? base : LightColor{};
    }
    case LightEffect::Fade: {
      static const LightColor stops[] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
      const uint32_t stepMs = 30000 / s + 300;
      const uint32_t k = (ms / stepMs) % 3;
      return mix(stops[k], stops[(k + 1) % 3], ms % stepMs, stepMs);
    }
    case LightEffect::Chase: {
      // Every pixel on but one, the dark one going round; a new colour at every round.
      const uint32_t stepMs = 5000 / s;
      const uint32_t step = ms / stepMs;
      const uint32_t dark = (step + n - 1) % n;
      if (i == dark) return {};
      return wheel(static_cast<uint32_t>((step / n) * 192));
    }
    case LightEffect::Wheel: {
      // A rainbow round the ring, turning.
      const uint32_t cycleMs = 200000 / s;   // 4 s at 50
      const uint32_t turn = static_cast<uint32_t>((static_cast<uint64_t>(ms % cycleMs) * 1536) / cycleMs);
      return wheel(i * 1536u / n + turn);
    }
    case LightEffect::Comet: {
      // A head of the colour, and a tail that fades behind it, half the ring long.
      const uint32_t stepMs = 4000 / s;   // 80 ms a pixel at 50
      const uint32_t head = (ms / stepMs) % n;
      const uint32_t behind = (head + n - i) % n;
      const uint32_t tail = n / 2 ? n / 2 : 1;
      if (behind >= tail) return {};
      // Straight down the tail: the square of the brightness makes it fade as the eye sees.
      return scaled(base, tail - behind, tail);
    }
    default:
      return base;
  }
}

// ---------------------------------------------------------------- pins

Status LightsBase::declare(ModuleContext& c, const LightSpec* specs) {
  if (!c.gpio || !c.manifest || !specs) return Status::Unsupported;
  if (declared_) return Status::Invalid;
  bind(c);
  const char* names[8] = {};
  for (uint8_t i = 0; i < count_ && i < 8; ++i) {
    state_[i].spec = specs[i];
    if (specs[i].kind == LightKind::Pixels) {
      if (state_[i].spec.count > LMP_LIGHT_PIXELS_MAX) state_[i].spec.count = LMP_LIGHT_PIXELS_MAX;
      // A ring of LEDs at full white asks more current than a USB port gives.
      state_[i].brightness = 50;
    } else {
      state_[i].spec.count = 1;
    }
    names[i] = specs[i].name;
  }
  for (uint8_t i = 0; i < count_; ++i) {
    const Status s = open(state_[i]);
    if (s != Status::Ok) {
      for (uint8_t k = 0; k < i; ++k) close(state_[k]);
      return s;
    }
  }
  LMP_TRY(bank_.declareVirtual(c, "Light %u", [this](uint8_t channel, bool on) {
    if (channel >= count_) return;
    Light& l = state_[channel];
    if (on && !l.on) l.startMs = nowMs_;
    l.on = on;
  }, names));
  declared_ = true;
  return Status::Ok;
}

Status LightsBase::open(Light& l) {
  IGpio* gpio = ctx()->gpio;
  const char* owner = ctx()->manifest->name;
  if (l.spec.kind == LightKind::Pixels) {
    const uint8_t pin = l.spec.pins[0];
    LMP_TRY(gpio->claim(pin, hal::PinMode::Output, owner, l.spec.name));
    const Status s = gpio->pixels(pin, l.spec.count, owner);
    if (s != Status::Ok) {
      LMP_LOGE(ctx()->log, "%s on pin %u: %s", l.spec.name, pin, statusName(s));
      gpio->release(pin, owner);
    }
    return s;
  }
  static const char* const kLabels[3] = {"R", "G", "B"};
  for (uint8_t k = 0; k < 3; ++k) {
    const uint8_t pin = l.spec.pins[k];
    Status s = gpio->claim(pin, hal::PinMode::Output, owner, kLabels[k]);
    // 10 bits where the chip has them, for a smooth dimming; 8 otherwise.
    if (s == Status::Ok) {
      s = gpio->pwm(pin, pwmHz_, 10, owner);
      l.bits = 10;
      if (s != Status::Ok) {
        s = gpio->pwm(pin, pwmHz_, 8, owner);
        l.bits = 8;
      }
    }
    if (s != Status::Ok) {
      LMP_LOGE(ctx()->log, "%s, pin %u of %s: %s", l.spec.name, pin, kLabels[k], statusName(s));
      for (uint8_t j = 0; j <= k; ++j) gpio->release(l.spec.pins[j], owner);
      return s;
    }
  }
  return Status::Ok;
}

void LightsBase::close(Light& l) {
  if (!ctx() || !ctx()->gpio) return;
  const char* owner = ctx()->manifest->name;
  const uint8_t pins = l.spec.kind == LightKind::Pixels ? 1 : 3;
  for (uint8_t k = 0; k < pins; ++k) ctx()->gpio->release(l.spec.pins[k], owner);
  l.written = false;
}

void LightsBase::stop() {
  if (!declared_) return;
  for (uint8_t i = 0; i < count_; ++i) close(state_[i]);
  declared_ = false;
}

// ---------------------------------------------------------------- drawing

// The value of a colour, 0..255, with the brightness, as a duty. Squared: the eye sees the
// light in steps that look even.
uint32_t LightsBase::dutyOf(const Light& l, uint8_t value) const {
  const uint64_t level = static_cast<uint64_t>(value) * l.brightness;   // 0..25500
  const uint64_t full = (1u << l.bits) - 1;
  return static_cast<uint32_t>(level * level * full / (25500ull * 25500ull));
}

uint8_t LightsBase::pixelOf(const Light& l, uint8_t value) const {
  const uint64_t level = static_cast<uint64_t>(value) * l.brightness;
  return static_cast<uint8_t>(level * level * 255 / (25500ull * 25500ull));
}

uint32_t LightsBase::random() {
  seed_ ^= seed_ << 13;
  seed_ ^= seed_ >> 17;
  seed_ ^= seed_ << 5;
  return seed_;
}

void LightsBase::render(Light& l, uint32_t nowMs) {
  const uint32_t ms = nowMs - l.startMs;
  const bool fx = l.on && l.effect >= 0;
  const LightEffect effect = static_cast<LightEffect>(l.effect);
  IGpio* gpio = ctx()->gpio;
  const char* owner = ctx()->manifest->name;

  if (l.spec.kind == LightKind::Pwm) {
    LightColor out;
    if (l.on) out = fx ? lightFrame(effect, ms, l.speed, l.color, 0, 1) : l.color;
    const uint8_t v[3] = {out.r, out.g, out.b};
    for (uint8_t k = 0; k < 3; ++k) {
      const uint32_t d = dutyOf(l, v[k]);
      if (d == l.duty[k] && l.written) continue;
      gpio->duty(l.spec.pins[k], d, owner);
      l.duty[k] = d;
    }
    l.written = true;
    return;
  }

  const uint16_t n = l.spec.count;
  uint8_t next[3 * LMP_LIGHT_PIXELS_MAX];
  if (fx && effect == LightEffect::Sparkle) {
    // Pixels light up at random and fade: faster and more of them with the speed.
    const uint32_t fade = 2 + l.speed / 8;
    for (uint16_t i = 0; i < n; ++i) l.spark[i] = l.spark[i] > fade ? l.spark[i] - fade : 0;
    if (random() % 100 < 10u + l.speed / 2u) l.spark[random() % n] = 255;
  }
  for (uint16_t i = 0; i < n; ++i) {
    LightColor c;
    if (l.on) {
      if (!fx) {
        c = l.color;
      } else if (effect == LightEffect::Sparkle) {
        c = scaled(l.color, l.spark[i], 255);
      } else {
        c = lightFrame(effect, ms, l.speed, l.color, i, n);
      }
    }
    next[i * 3] = pixelOf(l, c.g);
    next[i * 3 + 1] = pixelOf(l, c.r);
    next[i * 3 + 2] = pixelOf(l, c.b);
  }
  // The pixels keep the last thing they heard, right or wrong: a spike on the wire of the data
  // or a dip of their supply leaves them with other colours. A frame that does not change is
  // sent again every so often, so that it does not last.
  if (l.written && memcmp(next, l.frame, n * 3u) == 0 && nowMs - l.shownMs < kPixelsAgainMs) return;
  memcpy(l.frame, next, n * 3u);
  gpio->show(l.spec.pins[0], l.frame, n * 3u, owner);
  l.written = true;
  l.shownMs = nowMs;
}

void LightsBase::tick(uint32_t nowMs) {
  nowMs_ = nowMs;
  if (!declared_) return;
  for (uint8_t i = 0; i < count_; ++i) render(state_[i], nowMs);
}

// ---------------------------------------------------------------- commands

bool LightsBase::effectFits(const Light& l, int effect) const {
  if (effect < 0 || effect >= static_cast<int>(LightEffect::Count)) return false;
  return l.spec.kind == LightKind::Pixels || effect < kStripEffects;
}

void LightsBase::things(ThingSink& sink) const {
  if (!declared_) return;
  char id[12];
  for (uint8_t i = 0; i < count_; ++i) {
    const Light& l = state_[i];
    const PinEntry* channel = bank_.entry(i);
    snprintf(id, sizeof(id), "light%u", static_cast<unsigned>(i));
    sink.light(id, channel ? channel->name.c_str() : l.spec.name, l.on, l.brightness, l.color.r,
               l.color.g, l.color.b, true);
  }
}

Status LightsBase::thingSet(const char* id, const ThingAsk& ask) {
  if (!declared_ || strncmp(id, "light", 5) != 0 || id[5] < '0' || id[5] > '9') {
    return Status::NotFound;
  }
  unsigned light = 0;
  for (const char* c = id + 5; *c; ++c) {
    if (*c < '0' || *c > '9' || light > 99) return Status::NotFound;
    light = light * 10 + static_cast<unsigned>(*c - '0');
  }
  if (light >= count_) return Status::NotFound;
  Light& l = state_[light];
  if (ask.hasBrightness) {
    l.brightness = ask.brightness < 1 ? 1 : (ask.brightness > 100 ? 100 : ask.brightness);
    unsaved();
  }
  if (ask.hasColour) {
    l.color.r = ask.r;
    l.color.g = ask.g;
    l.color.b = ask.b;
    l.effect = -1;
    unsaved();
  }
  if (!ask.hasOn) return Status::Ok;
  // In the span of a schedule the schedule decides, as for the commands.
  if (bank_.held(static_cast<uint8_t>(light))) return Status::Denied;
  return bank_.set(static_cast<uint8_t>(light), ask.on);
}

Status LightsBase::turnOn(uint8_t light) {
  unsaved();
  if (state_[light].on) return Status::Ok;
  return bank_.set(light, true);
}

// The light of a command: "channel", 0 when missing. False, with the field, when there is none.
bool LightsBase::lightOf(const Args& a, CmdContext& ctx, uint8_t& out) const {
  out = a.has("channel") ? a.u8("channel") : 0;
  if (out < count_) return true;
  ctx.errorField = "channel";
  return false;
}

void LightsBase::commands(CommandSet& set) {
  set.add("color", LMP_HELP("A fixed colour: hex \"#ff8000\", or r, g and b. Turns the light on"),
          kColorArgs, [this](const Args& a, CmdContext& ctx) -> Status {
            uint8_t i = 0;
            if (!lightOf(a, ctx, i)) return Status::Invalid;
            Light& l = state_[i];
            LightColor next = l.color;
            if (a.has("hex")) {
              if (!parseHex(a.str("hex"), next)) {
                ctx.errorField = "hex";
                return Status::Invalid;
              }
            } else if (a.has("r") || a.has("g") || a.has("b")) {
              if (a.has("r")) next.r = a.u8("r");
              if (a.has("g")) next.g = a.u8("g");
              if (a.has("b")) next.b = a.u8("b");
            } else {
              ctx.errorField = "hex";
              return Status::Invalid;
            }
            l.color = next;
            l.effect = -1;
            return turnOn(i);
          });
  set.add("effect", LMP_HELP("An effect: christmas, rainbow, breath, blink, fade; on the LEDs also chase, wheel, comet, sparkle. Turns the light on"),
          kEffectArgs, [this](const Args& a, CmdContext& ctx) -> Status {
            uint8_t i = 0;
            if (!lightOf(a, ctx, i)) return Status::Invalid;
            Light& l = state_[i];
            const int e = lightEffectOf(a.str("name"));
            if (!effectFits(l, e)) {
              ctx.errorField = "name";
              return Status::Invalid;
            }
            l.effect = static_cast<int8_t>(e);
            l.startMs = nowMs_;
            memset(l.spark, 0, sizeof(l.spark));
            return turnOn(i);
          });
  set.add("brightness", LMP_HELP("Brightness in percent, 1 to 100"), kPercentArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            uint8_t i = 0;
            if (!lightOf(a, ctx, i)) return Status::Invalid;
            state_[i].brightness = a.u8("value");
            unsaved();
            return Status::Ok;
          });
  set.add("speed", LMP_HELP("Speed of the effects, 1 to 100"), kPercentArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            uint8_t i = 0;
            if (!lightOf(a, ctx, i)) return Status::Invalid;
            state_[i].speed = a.u8("value");
            unsaved();
            return Status::Ok;
          });
  set.add("power", LMP_HELP("Turns a light on or off, with its last colour or effect"), kPowerArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            uint8_t i = 0;
            if (!lightOf(a, ctx, i)) return Status::Invalid;
            // In the span of a schedule the schedule decides, as for the relays.
            if (bank_.held(i)) {
              ctx.errorField = "value";
              return Status::Denied;
            }
            return bank_.set(i, a.boolean("value"));
          });
}

// ---------------------------------------------------------------- document and file

void LightsBase::describe(JsonWriter& w) const {
  char hex[8];
  w.beginArray("lights");
  for (uint8_t i = 0; i < count_; ++i) {
    const Light& l = state_[i];
    hexOf(l.color, hex);
    w.beginObject();
    w.u32("i", i);
    w.str("kind", l.spec.kind == LightKind::Pixels ? "pixels" : "pwm");
    if (l.spec.kind == LightKind::Pixels) w.u32("count", l.spec.count);
    w.boolean("on", l.on);
    w.str("mode", l.effect < 0 ? "color" : "effect");
    w.str("color", hex);
    w.str("effect", l.effect < 0 ? "" : kEffectNames[l.effect]);
    w.u32("brightness", l.brightness);
    w.u32("speed", l.speed);
    w.beginArray("effects");
    const uint8_t n = l.spec.kind == LightKind::Pixels ? static_cast<uint8_t>(LightEffect::Count)
                                                        : kStripEffects;
    for (uint8_t k = 0; k < n; ++k) w.str(StringView(kEffectNames[k]));
    w.endArray();
    w.beginArray("pins");
    const uint8_t pins = l.spec.kind == LightKind::Pixels ? 1 : 3;
    for (uint8_t k = 0; k < pins; ++k) w.u64(l.spec.pins[k]);
    w.endArray();
    w.endObject();
  }
  w.endArray();
}

// Colour, effect, brightness and speed come back at every start; whether a light is on comes
// back only with "restore", with the channels of the bank.
void LightsBase::save(JsonWriter& w) const {
  char hex[8];
  w.beginArray("lights");
  for (uint8_t i = 0; i < count_; ++i) {
    const Light& l = state_[i];
    hexOf(l.color, hex);
    w.beginObject();
    w.u32("i", i);
    w.str("color", hex);
    w.str("effect", l.effect < 0 ? "" : kEffectNames[l.effect]);
    w.u32("brightness", l.brightness);
    w.u32("speed", l.speed);
    w.endObject();
  }
  w.endArray();
}

void LightsBase::load(const JsonValue& doc) {
  auto one = [this](Light& l, const JsonValue& v) {
    char text[16] = "";
    if (v.get("color").getString(text, sizeof(text))) parseHex(text, l.color);
    text[0] = '\0';
    if (v.get("effect").getString(text, sizeof(text))) {
      const int e = lightEffectOf(text);
      l.effect = effectFits(l, e) ? static_cast<int8_t>(e) : -1;
    }
    int64_t n = 0;
    if (v.get("brightness").getInt(n) && n >= 1 && n <= 100) l.brightness = static_cast<uint8_t>(n);
    if (v.get("speed").getInt(n) && n >= 1 && n <= 100) l.speed = static_cast<uint8_t>(n);
  };
  const JsonValue lights = doc.get("lights");
  if (lights.isArray()) {
    for (const JsonMember m : lights) {
      int64_t i = -1;
      if (m.value.get("i").getInt(i) && i >= 0 && i < count_) one(state_[i], m.value);
    }
  } else if (count_ && doc.get("color").valid()) {
    // The file of RGB Controller 1.0, before the lights: one strip, at the top.
    one(state_[0], doc);
  }
}

}  // namespace lmp::sdk
