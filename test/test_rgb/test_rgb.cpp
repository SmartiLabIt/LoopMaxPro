// Tests on the PC of the lights of the SDK and of the modules made of them: RGB Controller,
// one analog RGB strip, and GanjaLeaf, a strip and a ring of addressable LEDs. The duty on the
// pins, the frames of the ring, on and off, the schedules, what comes back after a restart.
// And the PWM and the LEDs of the pin service they stand on.
#include "../support/online_system.h"

#include <fstream>
#include <iterator>
#include <string>

using namespace lmp;
using namespace lmp::test;

lmp::sdk::Module& lmp_module_rgb();
lmp::sdk::Module& lmp_module_ganjaleaf();

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

// The pins of the module on the PC.
constexpr uint8_t kR = 43, kG = 44, kB = 45;
constexpr uint32_t kFull = 1023;   // 10 bits

struct Strip {
  OnlineSystem sys{true, nullptr, 1000, {&lmp_module_rgb()}};
  Strip() {
    // What an earlier test left in the module goes: every test starts from the same place.
    TEST_ASSERT_TRUE(cmd("color", "{\"hex\":\"#ff0000\"}") == Status::Ok);
    TEST_ASSERT_TRUE(cmd("brightness", "{\"value\":100}") == Status::Ok);
    TEST_ASSERT_TRUE(cmd("speed", "{\"value\":50}") == Status::Ok);
    TEST_ASSERT_TRUE(cmd("power", "{\"value\":false}") == Status::Ok);
    sys.advance(100);
  }
  Status cmd(const char* name, const char* json, const char** field = nullptr) {
    return sys.command("mod.rgb", name, json, field);
  }
  uint32_t duty(uint8_t pin) { return sys.pc().pwm.duty(pin); }
};

}  // namespace

// ---------------------------------------------------------------- colour and brightness

void test_a_colour_on_the_pins() {
  Strip s;
  TEST_ASSERT_EQUAL_UINT8(10, s.sys.pc().pwm.bits(kR));
  TEST_ASSERT_EQUAL_UINT32(0, s.duty(kR));   // off

  TEST_ASSERT_TRUE(s.cmd("color", "{\"hex\":\"#ff8000\"}") == Status::Ok);
  s.sys.advance(60);
  TEST_ASSERT_EQUAL_UINT32(kFull, s.duty(kR));
  // 128 of 255: a quarter of the duty, the eye sees half.
  TEST_ASSERT_UINT32_WITHIN(3, kFull * 128 * 128 / (255 * 255), s.duty(kG));
  TEST_ASSERT_EQUAL_UINT32(0, s.duty(kB));

  Doc doc(s.sys.kernel(), "mod.rgb");
  TEST_ASSERT_TRUE(doc.v["lights"].at(0)["on"].asBool());
  TEST_ASSERT_TRUE(doc.v["lights"].at(0)["mode"].stringEquals("color"));
  TEST_ASSERT_TRUE(doc.v["lights"].at(0)["color"].stringEquals("#ff8000"));
  TEST_ASSERT_EQUAL_UINT(5, doc.v["lights"].at(0)["effects"].size());

  // r, g and b one by one: the others stay.
  TEST_ASSERT_TRUE(s.cmd("color", "{\"b\":255}") == Status::Ok);
  TEST_ASSERT_TRUE(Doc(s.sys.kernel(), "mod.rgb").v["lights"].at(0)["color"].stringEquals("#ff80ff"));

  const char* field = nullptr;
  TEST_ASSERT_TRUE(s.cmd("color", "{\"hex\":\"#12345g\"}", &field) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("hex", field);
  TEST_ASSERT_TRUE(s.cmd("color", "{}", &field) == Status::Invalid);
}

void test_brightness() {
  Strip s;
  TEST_ASSERT_TRUE(s.cmd("color", "{\"hex\":\"#ffffff\"}") == Status::Ok);
  TEST_ASSERT_TRUE(s.cmd("brightness", "{\"value\":50}") == Status::Ok);
  s.sys.advance(60);
  TEST_ASSERT_UINT32_WITHIN(2, kFull / 4, s.duty(kR));
  TEST_ASSERT_TRUE(s.cmd("brightness", "{\"value\":0}") == Status::Invalid);
  TEST_ASSERT_TRUE(s.cmd("brightness", "{\"value\":101}") == Status::Invalid);
  TEST_ASSERT_EQUAL_INT64(50, Doc(s.sys.kernel(), "mod.rgb").v["lights"].at(0)["brightness"].asInt());
}

// ---------------------------------------------------------------- on and off

void test_on_and_off_keep_the_colour() {
  Strip s;
  TEST_ASSERT_TRUE(s.cmd("color", "{\"hex\":\"#0000ff\"}") == Status::Ok);
  s.sys.advance(60);
  TEST_ASSERT_EQUAL_UINT32(kFull, s.duty(kB));
  TEST_ASSERT_TRUE(s.cmd("power", "{\"value\":false}") == Status::Ok);
  s.sys.advance(60);
  TEST_ASSERT_EQUAL_UINT32(0, s.duty(kB));
  TEST_ASSERT_FALSE(Doc(s.sys.kernel(), "mod.rgb").v["lights"].at(0)["on"].asBool(true));
  // On again: the same blue. The channel of the SDK does the same.
  TEST_ASSERT_TRUE(s.cmd("set", "{\"channel\":0,\"value\":true}") == Status::Ok);
  s.sys.advance(60);
  TEST_ASSERT_EQUAL_UINT32(kFull, s.duty(kB));
  Doc doc(s.sys.kernel(), "mod.rgb");
  TEST_ASSERT_TRUE(doc.v["lights"].at(0)["on"].asBool());
  // A channel without a pin: no gpio in the document, no "invert".
  TEST_ASSERT_FALSE(doc.v["pins"].at(0)["gpio"].valid());
  TEST_ASSERT_TRUE(s.cmd("invert", "{\"channel\":0,\"value\":true}") == Status::NotFound);
}

// ---------------------------------------------------------------- effects

void test_effects() {
  Strip s;
  const char* field = nullptr;
  TEST_ASSERT_TRUE(s.cmd("effect", "{\"name\":\"disco\"}", &field) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("name", field);

  // Christmas at speed 50: red, green, blue, white, dark, a step every 1100 ms.
  TEST_ASSERT_TRUE(s.cmd("effect", "{\"name\":\"christmas\"}") == Status::Ok);
  s.sys.advance(100);
  TEST_ASSERT_EQUAL_UINT32(kFull, s.duty(kR));
  TEST_ASSERT_EQUAL_UINT32(0, s.duty(kG));
  s.sys.advance(1100);
  TEST_ASSERT_EQUAL_UINT32(0, s.duty(kR));
  TEST_ASSERT_EQUAL_UINT32(kFull, s.duty(kG));
  Doc doc(s.sys.kernel(), "mod.rgb");
  TEST_ASSERT_TRUE(doc.v["lights"].at(0)["mode"].stringEquals("effect"));
  TEST_ASSERT_TRUE(doc.v["lights"].at(0)["effect"].stringEquals("christmas"));

  // Rainbow: the colour moves by itself, and the document does not change with it.
  TEST_ASSERT_TRUE(s.cmd("effect", "{\"name\":\"rainbow\"}") == Status::Ok);
  s.sys.advance(1500);
  const size_t before = s.sys.events.size();
  const uint32_t g1 = s.duty(kG);
  s.sys.advance(1000);
  TEST_ASSERT_NOT_EQUAL(g1, s.duty(kG));
  int data = 0;
  for (size_t i = before; i < s.sys.events.size(); ++i) {
    if (s.sys.events[i].id == EventId::ModuleData) ++data;
  }
  TEST_ASSERT_EQUAL_INT(0, data);

  // Blink: the colour of the strip, on and off.
  TEST_ASSERT_TRUE(s.cmd("color", "{\"hex\":\"#00ff00\"}") == Status::Ok);
  TEST_ASSERT_TRUE(s.cmd("effect", "{\"name\":\"blink\"}") == Status::Ok);
  TEST_ASSERT_TRUE(s.cmd("speed", "{\"value\":100}") == Status::Ok);   // 300 ms
  s.sys.advance(40);
  TEST_ASSERT_EQUAL_UINT32(kFull, s.duty(kG));
  s.sys.advance(150);
  TEST_ASSERT_EQUAL_UINT32(0, s.duty(kG));

  // A colour ends the effect.
  TEST_ASSERT_TRUE(s.cmd("color", "{\"hex\":\"#00ff00\"}") == Status::Ok);
  s.sys.advance(400);
  TEST_ASSERT_EQUAL_UINT32(kFull, s.duty(kG));
  TEST_ASSERT_TRUE(Doc(s.sys.kernel(), "mod.rgb").v["lights"].at(0)["mode"].stringEquals("color"));
}

// The effect and its speed are things too (sdk/home.h): chosen and set as every thing, by who
// commands the device without knowing the module.
void test_the_effect_and_its_speed_are_things() {
  Strip s;
  Json<4096> out;
  TEST_ASSERT_TRUE(s.sys.kernel().commands().answer("mod.rgb", "things", "{}", ArgsForm::Json,
                                                    CmdSource::Remote, out.w) == Status::Ok);
  const JsonValue things = out.parse()["result"]["things"];
  TEST_ASSERT_EQUAL_UINT(3, things.size());
  TEST_ASSERT_TRUE(things.at(0)["id"].stringEquals("light0"));
  // A strip has the fixed colour and the five effects of every light, not those of the LEDs.
  TEST_ASSERT_TRUE(things.at(1)["id"].stringEquals("effect0"));
  TEST_ASSERT_TRUE(things.at(1)["kind"].stringEquals("select"));
  TEST_ASSERT_TRUE(things.at(1)["writable"].asBool(false));
  TEST_ASSERT_TRUE(things.at(1)["name"]["en"].stringEquals("Effect"));
  TEST_ASSERT_EQUAL_UINT(6, things.at(1)["options"].size());
  TEST_ASSERT_TRUE(things.at(1)["options"].at(0)["id"].stringEquals("color"));
  TEST_ASSERT_TRUE(things.at(1)["options"].at(5)["id"].stringEquals("fade"));
  TEST_ASSERT_TRUE(things.at(2)["id"].stringEquals("speed0"));
  TEST_ASSERT_TRUE(things.at(2)["kind"].stringEquals("number"));
  TEST_ASSERT_TRUE(things.at(2)["writable"].asBool(false));
  TEST_ASSERT_EQUAL_INT64(1, things.at(2)["min"].asInt(-1));
  TEST_ASSERT_EQUAL_INT64(100, things.at(2)["max"].asInt(-1));

  // An effect chosen: the strip turns on with it, as with the command.
  TEST_ASSERT_TRUE(s.cmd("thing.set", "{\"thing\":\"effect0\",\"value\":\"christmas\"}") == Status::Ok);
  s.sys.advance(100);
  TEST_ASSERT_EQUAL_UINT32(kFull, s.duty(kR));
  {
    Doc doc(s.sys.kernel(), "mod.rgb");
    TEST_ASSERT_TRUE(doc.v["lights"].at(0)["on"].asBool());
    TEST_ASSERT_TRUE(doc.v["lights"].at(0)["effect"].stringEquals("christmas"));
    TEST_ASSERT_TRUE(doc.v["things"]["values"]["effect0"].stringEquals("christmas"));
    TEST_ASSERT_EQUAL_INT64(50, doc.v["things"]["values"]["speed0"].asInt());
  }
  // Its speed, within its limits.
  TEST_ASSERT_TRUE(s.cmd("thing.set", "{\"thing\":\"speed0\",\"number\":100}") == Status::Ok);
  const char* field = nullptr;
  TEST_ASSERT_TRUE(s.cmd("thing.set", "{\"thing\":\"speed0\",\"number\":0}", &field) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("number", field);
  TEST_ASSERT_EQUAL_INT64(100, Doc(s.sys.kernel(), "mod.rgb").v["lights"].at(0)["speed"].asInt());
  // An effect of the LEDs is not for a strip, and a word that is none is not taken.
  TEST_ASSERT_TRUE(s.cmd("thing.set", "{\"thing\":\"effect0\",\"value\":\"comet\"}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("value", field);
  TEST_ASSERT_TRUE(s.cmd("thing.set", "{\"thing\":\"effect0\",\"value\":\"disco\"}") == Status::Invalid);
  // The fixed colour ends the effect.
  TEST_ASSERT_TRUE(s.cmd("thing.set", "{\"thing\":\"effect0\",\"value\":\"color\"}") == Status::Ok);
  s.sys.advance(100);
  Doc doc(s.sys.kernel(), "mod.rgb");
  TEST_ASSERT_TRUE(doc.v["lights"].at(0)["mode"].stringEquals("color"));
  TEST_ASSERT_TRUE(doc.v["things"]["values"]["effect0"].stringEquals("color"));
}

// ---------------------------------------------------------------- schedules

void test_a_schedule_turns_the_strip_on() {
  Strip s;
  TEST_ASSERT_TRUE(s.cmd("color", "{\"hex\":\"#ffffff\"}") == Status::Ok);
  TEST_ASSERT_TRUE(s.cmd("power", "{\"value\":false}") == Status::Ok);
  const int64_t start = kSomeTime - kSomeTime % 60;
  TEST_ASSERT_TRUE(s.sys.app.clock().setTime(start, TimeSource::Client) == Status::Ok);
  hal::DateTime t;
  TEST_ASSERT_TRUE(s.sys.app.clock().localTime(t));
  const int minute = t.hour * 60 + t.minute;
  char json[96];
  snprintf(json, sizeof(json), "{\"channel\":0,\"on\":%d,\"off\":%d}", minute + 1, minute + 3);
  TEST_ASSERT_TRUE(s.cmd("sched.add", json) == Status::Ok);
  s.sys.advance(1200);
  TEST_ASSERT_EQUAL_UINT32(0, s.duty(kR));

  s.sys.tb.clock.unix += 90;
  s.sys.advance(1200);
  TEST_ASSERT_EQUAL_UINT32(kFull, s.duty(kR));
  // In its span the schedule decides: turning off by hand is refused, as for the relays.
  TEST_ASSERT_TRUE(s.cmd("power", "{\"value\":false}") == Status::Denied);
  TEST_ASSERT_TRUE(Doc(s.sys.kernel(), "mod.rgb").v["pins"].at(0)["locked"].asBool());

  s.sys.tb.clock.unix += 120;
  s.sys.advance(1200);
  TEST_ASSERT_EQUAL_UINT32(0, s.duty(kR));
  TEST_ASSERT_TRUE(s.cmd("sched.clear", "{\"channel\":0}") == Status::Ok);
}

// ---------------------------------------------------------------- after a restart

void test_it_comes_back_after_a_restart() {
  TestBoard tb;
  {
    LoopMax app(tb.board);
    Logger::setInstance(&app.kernel().log());
    TEST_ASSERT_TRUE(app.modules().add(lmp_module_rgb()) == Status::Ok);
    TEST_ASSERT_TRUE(app.begin() == Status::Ok);
    run(app.kernel(), tb.clock, 1000);
    CmdContext ctx;
    auto cmd = [&](const char* name, const char* json) {
      return app.kernel().commands().execute("mod.rgb", name, JsonValue::parse(json), ctx);
    };
    TEST_ASSERT_TRUE(cmd("effect", "{\"name\":\"breath\"}") == Status::Ok);
    TEST_ASSERT_TRUE(cmd("color", "{\"hex\":\"#123456\"}") == Status::Ok);
    TEST_ASSERT_TRUE(cmd("effect", "{\"name\":\"fade\"}") == Status::Ok);
    TEST_ASSERT_TRUE(cmd("brightness", "{\"value\":40}") == Status::Ok);
    TEST_ASSERT_TRUE(cmd("speed", "{\"value\":70}") == Status::Ok);
    run(app.kernel(), tb.clock, LMP_CONFIG_SAVE_DELAY_MS + 200);
    Logger::setInstance(nullptr);
  }
  // On file: what the strip does, and that it was on.
  std::ifstream in(tb.native().fs.root() + "/cfg/m/rgb.json", std::ios::binary);
  TEST_ASSERT_TRUE(in.good());
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const JsonValue file = JsonValue::parse(StringView(text.data(), text.size()));
  TEST_ASSERT_TRUE(file["lights"].at(0)["color"].stringEquals("#123456"));
  TEST_ASSERT_TRUE(file["lights"].at(0)["effect"].stringEquals("fade"));
  TEST_ASSERT_EQUAL_INT64(40, file["lights"].at(0)["brightness"].asInt());
  TEST_ASSERT_EQUAL_INT64(70, file["lights"].at(0)["speed"].asInt());
  TEST_ASSERT_EQUAL_INT64(1, file["pins"].at(0)["level"].asInt());
}

// ---------------------------------------------------------------- the PWM of the pin service

void test_pwm_of_the_pin_service() {
  OnlineSystem sys;
  IGpio* gpio = sys.kernel().api<IGpio>();
  TEST_ASSERT_NOT_NULL(gpio);
  TEST_ASSERT_TRUE(gpio->pwm(30, 1000, 8, "me") == Status::NotFound);   // not claimed
  TEST_ASSERT_TRUE(gpio->claim(30, hal::PinMode::Output, "me", "fan") == Status::Ok);
  TEST_ASSERT_TRUE(gpio->duty(30, 10, "me") == Status::Invalid);         // not a PWM yet
  TEST_ASSERT_TRUE(gpio->pwm(30, 1000, 8, "other") == Status::Denied);
  TEST_ASSERT_TRUE(gpio->pwm(30, 1000, 8, "me") == Status::Ok);
  TEST_ASSERT_TRUE(gpio->duty(30, 255, "me") == Status::Ok);
  TEST_ASSERT_EQUAL_UINT32(255, sys.pc().pwm.duty(30));
  Doc doc(sys.kernel(), "sys.gpio");
  bool seen = false;
  for (size_t i = 0; i < doc.v["pins"].size(); ++i) {
    const JsonValue p = doc.v["pins"].at(i);
    if (p["pin"].asInt() != 30) continue;
    seen = true;
    TEST_ASSERT_TRUE(p["mode"].stringEquals("pwm"));
    TEST_ASSERT_EQUAL_INT64(1000, p["duty"].asInt());
  }
  TEST_ASSERT_TRUE(seen);
  // The release ends the PWM.
  TEST_ASSERT_TRUE(gpio->release(30, "me") == Status::Ok);
  TEST_ASSERT_EQUAL_UINT8(0, sys.pc().pwm.bits(30));
}

// ---------------------------------------------------------------- GanjaLeaf: leaf and ring

namespace {

constexpr uint8_t kLeafR = 46, kRingPin = 49;
constexpr uint16_t kLeds = 12;

struct Leaf {
  OnlineSystem sys{true, nullptr, 1000, {&lmp_module_ganjaleaf()}};
  Leaf() {
    for (int ch = 0; ch < 2; ++ch) {
      char json[64];
      snprintf(json, sizeof(json), "{\"channel\":%d,\"hex\":\"#ff0000\"}", ch);
      TEST_ASSERT_TRUE(cmd("color", json) == Status::Ok);
      snprintf(json, sizeof(json), "{\"channel\":%d,\"value\":100}", ch);
      TEST_ASSERT_TRUE(cmd("brightness", json) == Status::Ok);
      TEST_ASSERT_TRUE(cmd("speed", "{\"channel\":0,\"value\":50}") == Status::Ok);
      TEST_ASSERT_TRUE(cmd("speed", "{\"channel\":1,\"value\":50}") == Status::Ok);
      snprintf(json, sizeof(json), "{\"channel\":%d,\"value\":false}", ch);
      TEST_ASSERT_TRUE(cmd("power", json) == Status::Ok);
    }
    sys.advance(100);
  }
  Status cmd(const char* name, const char* json, const char** field = nullptr) {
    return sys.command("mod.ganjaleaf", name, json, field);
  }
  // Red, green, blue of a LED of the ring, as it was last sent.
  void led(uint16_t i, uint8_t& r, uint8_t& g, uint8_t& b) { sys.pc().pixels.rgbOf(i, r, g, b); }
  int lit() {
    int n = 0;
    for (uint16_t i = 0; i < kLeds; ++i) {
      uint8_t r, g, b;
      led(i, r, g, b);
      if (r || g || b) ++n;
    }
    return n;
  }
};

}  // namespace

void test_leaf_and_ring_are_two_lights() {
  Leaf s;
  TEST_ASSERT_EQUAL_UINT8(kRingPin, s.sys.pc().pixels.pin());
  TEST_ASSERT_EQUAL_UINT16(kLeds, s.sys.pc().pixels.count());
  Doc doc(s.sys.kernel(), "mod.ganjaleaf");
  TEST_ASSERT_EQUAL_UINT(2, doc.v["lights"].size());
  TEST_ASSERT_TRUE(doc.v["lights"].at(0)["kind"].stringEquals("pwm"));
  TEST_ASSERT_TRUE(doc.v["lights"].at(1)["kind"].stringEquals("pixels"));
  TEST_ASSERT_EQUAL_INT64(12, doc.v["lights"].at(1)["count"].asInt());
  TEST_ASSERT_EQUAL_UINT(5, doc.v["lights"].at(0)["effects"].size());
  TEST_ASSERT_EQUAL_UINT(9, doc.v["lights"].at(1)["effects"].size());
  TEST_ASSERT_TRUE(doc.v["pins"].at(0)["name"].stringEquals("Leaf"));
  TEST_ASSERT_TRUE(doc.v["pins"].at(1)["name"].stringEquals("Ring"));

  // The ring blue at half brightness: every LED the same, a quarter of the light.
  TEST_ASSERT_TRUE(s.cmd("color", "{\"channel\":1,\"hex\":\"#0000ff\"}") == Status::Ok);
  TEST_ASSERT_TRUE(s.cmd("brightness", "{\"channel\":1,\"value\":50}") == Status::Ok);
  s.sys.advance(60);
  for (uint16_t i = 0; i < kLeds; ++i) {
    uint8_t r, g, b;
    s.led(i, r, g, b);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_UINT8_WITHIN(1, 63, b);
  }
  // A fixed colour is sent again every couple of seconds: LEDs that a spike on the wire left
  // with other colours get theirs back, though nothing changed.
  s.sys.pc().pixels.spoil();
  s.sys.advance(2500);
  for (uint16_t i = 0; i < kLeds; ++i) {
    uint8_t r, g, b;
    s.led(i, r, g, b);
    TEST_ASSERT_EQUAL_UINT8(0, r);
    TEST_ASSERT_UINT8_WITHIN(1, 63, b);
  }
  // The leaf stays off: each light on its own.
  TEST_ASSERT_EQUAL_UINT32(0, s.sys.pc().pwm.duty(kLeafR));
  TEST_ASSERT_TRUE(s.cmd("color", "{\"channel\":0,\"hex\":\"#ff0000\"}") == Status::Ok);
  s.sys.advance(60);
  TEST_ASSERT_EQUAL_UINT32(1023, s.sys.pc().pwm.duty(kLeafR));
  // Off: the ring dark, the leaf still on.
  TEST_ASSERT_TRUE(s.cmd("power", "{\"channel\":1,\"value\":false}") == Status::Ok);
  s.sys.advance(60);
  TEST_ASSERT_EQUAL_INT(0, s.lit());
  TEST_ASSERT_EQUAL_UINT32(1023, s.sys.pc().pwm.duty(kLeafR));

  const char* field = nullptr;
  TEST_ASSERT_TRUE(s.cmd("color", "{\"channel\":2,\"hex\":\"#ff0000\"}", &field) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("channel", field);
  // The effects of the LEDs are not for the strip.
  TEST_ASSERT_TRUE(s.cmd("effect", "{\"channel\":0,\"name\":\"comet\"}", &field) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("name", field);
}

// With two lights each has its effect and its speed to choose, and their names say whose.
void test_each_light_has_its_effect_to_choose() {
  Leaf s;
  Json<8192> out;
  TEST_ASSERT_TRUE(s.sys.kernel().commands().answer("mod.ganjaleaf", "things", "{}", ArgsForm::Json,
                                                    CmdSource::Remote, out.w) == Status::Ok);
  TEST_ASSERT_TRUE_MESSAGE(out.w.complete(), "the things of two lights fit in an answer");
  const JsonValue things = out.parse()["result"]["things"];
  TEST_ASSERT_EQUAL_UINT(6, things.size());
  TEST_ASSERT_TRUE(things.at(1)["id"].stringEquals("effect0"));
  TEST_ASSERT_TRUE(things.at(1)["name"]["en"].stringEquals("Leaf: effect"));
  TEST_ASSERT_EQUAL_UINT(6, things.at(1)["options"].size());
  TEST_ASSERT_TRUE(things.at(4)["id"].stringEquals("effect1"));
  TEST_ASSERT_TRUE(things.at(4)["name"]["it"].stringEquals("Ring: effetto"));
  // The ring is a line of LEDs: it has their effects too.
  TEST_ASSERT_EQUAL_UINT(10, things.at(4)["options"].size());
  TEST_ASSERT_TRUE(things.at(4)["options"].at(9)["id"].stringEquals("sparkle"));
  TEST_ASSERT_TRUE(things.at(5)["id"].stringEquals("speed1"));

  TEST_ASSERT_TRUE(s.cmd("thing.set", "{\"thing\":\"effect1\",\"value\":\"chase\"}") == Status::Ok);
  TEST_ASSERT_TRUE(s.cmd("thing.set", "{\"thing\":\"effect0\",\"value\":\"chase\"}") == Status::Invalid);
  TEST_ASSERT_TRUE(s.cmd("thing.set", "{\"thing\":\"speed1\",\"number\":80}") == Status::Ok);
  s.sys.advance(100);
  Doc doc(s.sys.kernel(), "mod.ganjaleaf");
  TEST_ASSERT_TRUE(doc.v["lights"].at(1)["effect"].stringEquals("chase"));
  TEST_ASSERT_EQUAL_INT64(80, doc.v["lights"].at(1)["speed"].asInt());
  TEST_ASSERT_EQUAL_INT64(50, doc.v["lights"].at(0)["speed"].asInt());
  TEST_ASSERT_TRUE(doc.v["lights"].at(0)["mode"].stringEquals("color"));
}

void test_the_effects_of_the_ring() {
  Leaf s;
  // Chase: eleven on and one dark, the dark one moving every 100 ms at speed 50.
  TEST_ASSERT_TRUE(s.cmd("effect", "{\"channel\":1,\"name\":\"chase\"}") == Status::Ok);
  s.sys.advance(40);
  TEST_ASSERT_EQUAL_INT(11, s.lit());
  int darkBefore = -1, darkAfter = -1;
  for (uint16_t i = 0; i < kLeds; ++i) {
    uint8_t r, g, b;
    s.led(i, r, g, b);
    if (!r && !g && !b) darkBefore = i;
  }
  s.sys.advance(100);
  for (uint16_t i = 0; i < kLeds; ++i) {
    uint8_t r, g, b;
    s.led(i, r, g, b);
    if (!r && !g && !b) darkAfter = i;
  }
  TEST_ASSERT_EQUAL_INT((darkBefore + 1) % kLeds, darkAfter);

  // Comet: a head and a tail half the ring long.
  TEST_ASSERT_TRUE(s.cmd("color", "{\"channel\":1,\"hex\":\"#00ff00\"}") == Status::Ok);
  TEST_ASSERT_TRUE(s.cmd("effect", "{\"channel\":1,\"name\":\"comet\"}") == Status::Ok);
  s.sys.advance(40);
  TEST_ASSERT_EQUAL_INT(6, s.lit());

  // Turning rainbow: every LED lit, not all the same colour.
  TEST_ASSERT_TRUE(s.cmd("effect", "{\"channel\":1,\"name\":\"wheel\"}") == Status::Ok);
  s.sys.advance(40);
  TEST_ASSERT_EQUAL_INT(12, s.lit());
  uint8_t r0, g0, b0, r6, g6, b6;
  s.led(0, r0, g0, b0);
  s.led(6, r6, g6, b6);
  TEST_ASSERT_TRUE(r0 != r6 || g0 != g6 || b0 != b6);

  // Sparkles: some LEDs on, never all, and the document keeps quiet.
  TEST_ASSERT_TRUE(s.cmd("effect", "{\"channel\":1,\"name\":\"sparkle\"}") == Status::Ok);
  s.sys.advance(1000);
  const size_t before = s.sys.events.size();
  s.sys.advance(1000);
  TEST_ASSERT_TRUE(s.lit() > 0);
  TEST_ASSERT_TRUE(s.lit() < 12);
  int data = 0;
  for (size_t i = before; i < s.sys.events.size(); ++i) {
    if (s.sys.events[i].id == EventId::ModuleData) ++data;
  }
  TEST_ASSERT_EQUAL_INT(0, data);
}

void test_each_light_has_its_schedules() {
  Leaf s;
  const int64_t start = kSomeTime - kSomeTime % 60;
  TEST_ASSERT_TRUE(s.sys.app.clock().setTime(start, TimeSource::Client) == Status::Ok);
  hal::DateTime t;
  TEST_ASSERT_TRUE(s.sys.app.clock().localTime(t));
  const int minute = t.hour * 60 + t.minute;
  char json[96];
  // The ring only: from the next minute for two minutes.
  snprintf(json, sizeof(json), "{\"channel\":1,\"on\":%d,\"off\":%d}", minute + 1, minute + 3);
  TEST_ASSERT_TRUE(s.cmd("sched.add", json) == Status::Ok);
  s.sys.tb.clock.unix += 90;
  s.sys.advance(1200);
  TEST_ASSERT_EQUAL_INT(12, s.lit());
  TEST_ASSERT_EQUAL_UINT32(0, s.sys.pc().pwm.duty(kLeafR));
  TEST_ASSERT_TRUE(s.cmd("power", "{\"channel\":1,\"value\":false}") == Status::Denied);
  TEST_ASSERT_TRUE(s.cmd("power", "{\"channel\":0,\"value\":true}") == Status::Ok);
  s.sys.tb.clock.unix += 120;
  s.sys.advance(1200);
  TEST_ASSERT_EQUAL_INT(0, s.lit());
  TEST_ASSERT_TRUE(s.cmd("sched.clear", "{\"channel\":1}") == Status::Ok);
}

// ---------------------------------------------------------------- the LEDs of the pin service

void test_leds_of_the_pin_service() {
  OnlineSystem sys;
  IGpio* gpio = sys.kernel().api<IGpio>();
  const uint8_t grb[6] = {1, 2, 3, 4, 5, 6};
  TEST_ASSERT_TRUE(gpio->pixels(31, 2, "me") == Status::NotFound);   // not claimed
  TEST_ASSERT_TRUE(gpio->claim(31, hal::PinMode::Output, "me", "ring") == Status::Ok);
  TEST_ASSERT_TRUE(gpio->show(31, grb, 6, "me") == Status::Invalid);   // not a line yet
  TEST_ASSERT_TRUE(gpio->pixels(31, 2, "other") == Status::Denied);
  TEST_ASSERT_TRUE(gpio->pixels(31, 2, "me") == Status::Ok);
  TEST_ASSERT_TRUE(gpio->show(31, grb, 6, "me") == Status::Ok);
  uint8_t r, g, b;
  sys.pc().pixels.rgbOf(1, r, g, b);
  TEST_ASSERT_EQUAL_UINT8(5, r);
  TEST_ASSERT_EQUAL_UINT8(4, g);
  TEST_ASSERT_EQUAL_UINT8(6, b);
  TEST_ASSERT_TRUE(gpio->release(31, "me") == Status::Ok);
  TEST_ASSERT_EQUAL_UINT16(0, sys.pc().pixels.count());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_colour_on_the_pins);
  RUN_TEST(test_brightness);
  RUN_TEST(test_on_and_off_keep_the_colour);
  RUN_TEST(test_effects);
  RUN_TEST(test_the_effect_and_its_speed_are_things);
  RUN_TEST(test_a_schedule_turns_the_strip_on);
  RUN_TEST(test_it_comes_back_after_a_restart);
  RUN_TEST(test_pwm_of_the_pin_service);
  RUN_TEST(test_leaf_and_ring_are_two_lights);
  RUN_TEST(test_each_light_has_its_effect_to_choose);
  RUN_TEST(test_the_effects_of_the_ring);
  RUN_TEST(test_each_light_has_its_schedules);
  RUN_TEST(test_leds_of_the_pin_service);
  return UNITY_END();
}
