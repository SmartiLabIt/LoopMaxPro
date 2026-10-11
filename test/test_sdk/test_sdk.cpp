// Tests on the PC of the parts the SDK gives to a module: pins, schedules, a button, a value
// read again and again, a chip on a bus, a service on the network.
#include "../support/online_system.h"
#include "sdk/devices.h"
#include "sdk/pin_bank.h"
#include "sdk/pin_schedule.h"
#include "sdk/state.h"
#include "sdk/tools.h"

using namespace lmp;
using namespace lmp::test;
using lmp::hal::PinLevel;
using lmp::sdk::ButtonEvent;
using lmp::sdk::ModuleState;
using lmp::sdk::Schedule;
using lmp::sdk::ScheduleKind;

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

constexpr uint8_t kPinA = 30;
constexpr uint8_t kPinB = 31;
constexpr uint8_t kPinC = 32;

// Three outputs with their schedules.
class Relay : public sdk::Module {
  LMP_MODULE("relay", "Relay", "R", "1.0.0", sdk::ModuleKind::Hardware, 50)

 public:
  sdk::PinBank<3> pins;
  sdk::PinSchedule<3, 2> schedules{pins};
  Status onInit(sdk::ModuleContext& c) override {
    static const uint8_t map[3] = {kPinA, kPinB, kPinC};
    LMP_TRY(pins.declare(c, map, hal::PinMode::Output, "Line %u"));
    LMP_TRY(use(pins));
    return use(schedules);
  }
};

// Two contacts.
class Contacts : public sdk::Module {
  LMP_MODULE("contacts", "Contacts", "C", "1.0.0", sdk::ModuleKind::Hardware, 50)

 public:
  sdk::PinBank<2> pins;
  Status onInit(sdk::ModuleContext& c) override {
    static const uint8_t map[2] = {34, 35};
    LMP_TRY(pins.declare(c, map, hal::PinMode::InputPullUp, "Door %u"));
    return use(pins);
  }
};

// Wants a pin of the relay.
class Thief : public sdk::Module {
  LMP_MODULE("thief", "Thief", "T", "1.0.0", sdk::ModuleKind::Hardware, 0)

 public:
  sdk::PinBank<2> pins;
  Status onInit(sdk::ModuleContext& c) override {
    static const uint8_t map[2] = {36, kPinB};
    LMP_TRY(pins.declare(c, map, hal::PinMode::Output, "%u"));
    return use(pins);
  }
};

// Reads a service on the network.
class Weather : public sdk::Module {
  LMP_MODULE("weather", "Weather", "W", "1.0.0", sdk::ModuleKind::Online, 100)

 public:
  sdk::OnlineSource source;
  char room[256];
  int news = 0;
  Status begun = Status::Error;
  Status onInit(sdk::ModuleContext&) override { return Status::Ok; }
  Status onStart() override {
    begun = source.begin(ctx().http, "http://meteo.test/now", 60, room, sizeof(room));
    return Status::Ok;
  }
  void onTick(uint32_t nowMs) override {
    if (source.tick(nowMs)) ++news;
  }
};

bool high(OnlineSystem& sys, uint8_t pin) {
  PinLevel level = PinLevel::Low;
  sys.pc().gpio.read(pin, level);
  return level == PinLevel::High;
}

std::string fileOf(OnlineSystem& sys, const char* name) {
  IStorage* storage = sys.kernel().api<IStorage>();
  char path[48];
  snprintf(path, sizeof(path), "/cfg/m/%s.json", name);
  static char text[4096];
  size_t length = 0;
  if (storage->readFile(path, text, sizeof(text), length) != Status::Ok) return std::string();
  return std::string(text, length);
}

// The time of the device, as the schedules see it.
struct Now {
  int64_t unix = 0;
  uint16_t minute = 0;
  uint8_t weekday = 0;
};

// Gives the device a time that is the beginning of a minute, far from midnight.
Now setClock(OnlineSystem& sys, int64_t unixSeconds = kSomeTime) {
  unixSeconds -= unixSeconds % 60;
  TEST_ASSERT_TRUE(sys.app.clock().setTime(unixSeconds, TimeSource::Client) == Status::Ok);
  hal::DateTime t;
  TEST_ASSERT_TRUE(sys.app.clock().localTime(t));
  Now now;
  now.unix = unixSeconds;
  now.minute = static_cast<uint16_t>(t.hour * 60 + t.minute);
  now.weekday = t.weekday;
  // The tests move the time by a few minutes: they must stay in the same day.
  TEST_ASSERT_TRUE(now.minute > 10 && now.minute < 1400);
  return now;
}

// Moves the clock and lets the schedules see it.
void pass(OnlineSystem& sys, int seconds) {
  sys.tb.clock.unix += seconds;
  sys.advance(1200);
}

Schedule weekly(uint8_t channel, uint16_t on, uint16_t off, uint8_t days = sdk::kEveryDay) {
  Schedule s;
  s.id = 1;
  s.channel = channel;
  s.kind = ScheduleKind::Weekly;
  s.days = days;
  s.onMin = on;
  s.offMin = off;
  return s;
}

}  // namespace

// ---------------------------------------------------------------- bank of pins

void test_outputs_are_driven_by_channel() {
  Relay relay;
  OnlineSystem sys(false, nullptr, 1500, {&relay});
  IGpio* pins = sys.kernel().api<IGpio>();

  TEST_ASSERT_TRUE(relay.state() == ModuleState::Ready);
  TEST_ASSERT_EQUAL_STRING("relay", pins->ownerOf(kPinA));
  TEST_ASSERT_EQUAL_STRING("relay", pins->ownerOf(kPinC));
  TEST_ASSERT_TRUE(sys.pc().gpio.mode(kPinA) == hal::PinMode::Output);
  TEST_ASSERT_FALSE(high(sys, kPinA));

  TEST_ASSERT_TRUE(sys.command("mod.relay", "set", "{\"channel\":1,\"value\":true}") ==
                   Status::Ok);
  TEST_ASSERT_TRUE(relay.pins.on(1));
  TEST_ASSERT_TRUE(high(sys, kPinB));
  TEST_ASSERT_FALSE(high(sys, kPinA));

  TEST_ASSERT_TRUE(sys.command("mod.relay", "toggle", "{\"channel\":1}") == Status::Ok);
  TEST_ASSERT_FALSE(high(sys, kPinB));
  TEST_ASSERT_TRUE(sys.command("mod.relay", "toggle", "{\"channel\":2}") == Status::Ok);
  TEST_ASSERT_TRUE(high(sys, kPinC));

  TEST_ASSERT_TRUE(sys.command("mod.relay", "all", "{\"value\":true}") == Status::Ok);
  TEST_ASSERT_TRUE(high(sys, kPinA) && high(sys, kPinB) && high(sys, kPinC));
  TEST_ASSERT_TRUE(sys.command("mod.relay", "all", "{\"value\":false}") == Status::Ok);
  TEST_ASSERT_FALSE(high(sys, kPinA) || high(sys, kPinB) || high(sys, kPinC));

  // A channel the bank does not have.
  const char* field = nullptr;
  TEST_ASSERT_TRUE(sys.command("mod.relay", "set", "{\"channel\":3,\"value\":true}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("channel", field);
  TEST_ASSERT_TRUE(sys.command("mod.relay", "toggle", "{\"channel\":9}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_TRUE(sys.command("mod.relay", "set", "{\"channel\":1}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("value", field);
}

void test_names_and_inverted_channels() {
  Relay relay;
  OnlineSystem sys(false, nullptr, 1500, {&relay});
  const char* field = nullptr;

  TEST_ASSERT_TRUE(sys.command("mod.relay", "name",
                               "{\"channel\":0,\"name\":\"Garden light\"}") == Status::Ok);
  TEST_ASSERT_TRUE(sys.command("mod.relay", "name", "{\"channel\":0,\"name\":\"\"}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("name", field);
  TEST_ASSERT_TRUE(sys.command("mod.relay", "name", "{\"channel\":7,\"name\":\"x\"}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("channel", field);

  // A relay board that closes with the pin low.
  TEST_ASSERT_TRUE(sys.command("mod.relay", "invert", "{\"channel\":2,\"value\":true}") ==
                   Status::Ok);
  TEST_ASSERT_FALSE(relay.pins.on(2));
  TEST_ASSERT_TRUE(high(sys, kPinC));    // off: the pin is high
  TEST_ASSERT_TRUE(sys.command("mod.relay", "set", "{\"channel\":2,\"value\":true}") ==
                   Status::Ok);
  TEST_ASSERT_FALSE(high(sys, kPinC));   // on: the pin is low

  Doc doc(sys.kernel(), "mod.relay");
  TEST_ASSERT_EQUAL_UINT(3, doc.v["pins"].size());
  TEST_ASSERT_TRUE(doc.v["pins"].at(0)["name"].stringEquals("Garden light"));
  TEST_ASSERT_TRUE(doc.v["pins"].at(1)["name"].stringEquals("Line 2"));
  TEST_ASSERT_EQUAL_INT64(kPinC, doc.v["pins"].at(2)["gpio"].asInt());
  TEST_ASSERT_EQUAL_INT64(1, doc.v["pins"].at(2)["level"].asInt());
  TEST_ASSERT_TRUE(doc.v["pins"].at(2)["inverted"].asBool());
  TEST_ASSERT_TRUE(doc.v["output"].asBool());
  TEST_ASSERT_TRUE(doc.v["restore"].asBool());
}

void test_channels_come_back_after_a_restart() {
  TestBoard tb;
  {
    Relay relay;
    LoopMax app(tb.board);
    Logger::setInstance(&app.kernel().log());
    TEST_ASSERT_TRUE(app.modules().add(relay) == Status::Ok);
    TEST_ASSERT_TRUE(app.begin() == Status::Ok);
    run(app.kernel(), tb.clock, 1500);
    TEST_ASSERT_TRUE(relay.pins.rename(0, "Pump") == Status::Ok);
    TEST_ASSERT_TRUE(relay.pins.invert(2, true) == Status::Ok);
    TEST_ASSERT_TRUE(relay.pins.set(0, true) == Status::Ok);
    TEST_ASSERT_TRUE(relay.pins.set(2, true) == Status::Ok);
    run(app.kernel(), tb.clock, LMP_CONFIG_SAVE_DELAY_MS + 200);
    Logger::setInstance(nullptr);
  }
  tb.native().fs.unmount();
  // The pins of a chip that starts are all low.
  for (uint8_t pin : {kPinA, kPinB, kPinC}) tb.native().gpio.write(pin, PinLevel::Low);

  {
    Relay relay;
    LoopMax app(tb.board);
    Logger::setInstance(&app.kernel().log());
    TEST_ASSERT_TRUE(app.modules().add(relay) == Status::Ok);
    TEST_ASSERT_TRUE(app.begin() == Status::Ok);
    run(app.kernel(), tb.clock, 1500);

    TEST_ASSERT_EQUAL_STRING("Pump", relay.pins.entry(0)->name.c_str());
    TEST_ASSERT_TRUE(relay.pins.on(0));
    TEST_ASSERT_FALSE(relay.pins.on(1));
    TEST_ASSERT_TRUE(relay.pins.on(2));
    PinLevel level = PinLevel::Low;
    tb.native().gpio.read(kPinA, level);
    TEST_ASSERT_TRUE(level == PinLevel::High);
    tb.native().gpio.read(kPinC, level);
    TEST_ASSERT_TRUE(level == PinLevel::Low);   // inverted and on

    // From now on every start finds the channels off.
    CmdContext ctx;
    TEST_ASSERT_TRUE(app.kernel().commands().execute("mod.relay", "restore",
                                                     JsonValue::parse("{\"value\":false}"),
                                                     ctx) == Status::Ok);
    run(app.kernel(), tb.clock, LMP_CONFIG_SAVE_DELAY_MS + 200);
    Logger::setInstance(nullptr);
  }
  tb.native().fs.unmount();

  Relay relay;
  LoopMax app(tb.board);
  Logger::setInstance(&app.kernel().log());
  TEST_ASSERT_TRUE(app.modules().add(relay) == Status::Ok);
  TEST_ASSERT_TRUE(app.begin() == Status::Ok);
  run(app.kernel(), tb.clock, 1500);
  TEST_ASSERT_FALSE(relay.pins.restore());
  TEST_ASSERT_FALSE(relay.pins.on(0));
  TEST_ASSERT_FALSE(relay.pins.on(2));
  TEST_ASSERT_EQUAL_STRING("Pump", relay.pins.entry(0)->name.c_str());   // names stay
  TEST_ASSERT_TRUE(relay.pins.entry(2)->inverted);
  PinLevel level = PinLevel::Low;
  tb.native().gpio.read(kPinC, level);
  TEST_ASSERT_TRUE(level == PinLevel::High);   // inverted and off
}

void test_levels_are_not_written_when_they_do_not_come_back() {
  Relay relay;
  OnlineSystem sys(false, nullptr, 1500, {&relay});
  relay.pins.setRestore(false);
  sys.advance(LMP_CONFIG_SAVE_DELAY_MS + 200);
  const std::string before = fileOf(sys, "relay");
  TEST_ASSERT_NOT_NULL(strstr(before.c_str(), "\"restore\":false"));
  TEST_ASSERT_NULL(strstr(before.c_str(), "\"level\""));

  // A relay that turns a hundred times a day does not wear the memory out.
  sys.events.clear();
  TEST_ASSERT_TRUE(relay.pins.set(0, true) == Status::Ok);
  sys.advance(LMP_CONFIG_SAVE_DELAY_MS + 200);
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::ModuleData));   // told, not written
  TEST_ASSERT_FALSE(sys.logHas("settings not saved"));
  TEST_ASSERT_EQUAL_STRING(before.c_str(), fileOf(sys, "relay").c_str());
}

void test_two_modules_cannot_have_the_same_pin() {
  Relay relay;
  Thief thief;
  OnlineSystem sys(false, nullptr, 1500, {&relay, &thief});
  IGpio* pins = sys.kernel().api<IGpio>();

  TEST_ASSERT_TRUE(relay.state() == ModuleState::Ready);
  TEST_ASSERT_TRUE(thief.state() == ModuleState::Error);
  TEST_ASSERT_EQUAL_STRING("relay", pins->ownerOf(kPinB));
  // The pin it had taken before the refusal is free again.
  TEST_ASSERT_NULL(pins->ownerOf(36));
  TEST_ASSERT_TRUE(sys.stateOf(ServiceId::Modules) == ServiceState::Degraded);
}

void test_inputs_are_read() {
  Contacts contacts;
  OnlineSystem sys(false, nullptr, 200, {&contacts});
  sys.pc().gpio.setInput(34, PinLevel::Low);
  sys.pc().gpio.setInput(35, PinLevel::Low);
  sys.advance(1500);
  sys.events.clear();

  TEST_ASSERT_TRUE(contacts.state() == ModuleState::Ready);
  TEST_ASSERT_FALSE(contacts.pins.output());
  TEST_ASSERT_FALSE(contacts.pins.on(0));

  sys.pc().gpio.setInput(35, PinLevel::High);
  sys.advance(LMP_MODULE_DATA_DELAY_MS + 200);
  TEST_ASSERT_TRUE(contacts.pins.on(1));
  TEST_ASSERT_FALSE(contacts.pins.on(0));
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::ModuleData));

  // An input is not driven: the commands that do it are not there.
  TEST_ASSERT_TRUE(contacts.pins.set(0, true) == Status::Denied);
  TEST_ASSERT_TRUE(contacts.pins.invert(0, true) == Status::Denied);
  TEST_ASSERT_FALSE(sys.kernel().commands().hasCommand("mod.contacts", "set"));
  TEST_ASSERT_FALSE(sys.kernel().commands().hasCommand("mod.contacts", "toggle"));
  TEST_ASSERT_TRUE(sys.kernel().commands().hasCommand("mod.contacts", "name"));

  Doc doc(sys.kernel(), "mod.contacts");
  TEST_ASSERT_FALSE(doc.v["output"].asBool(true));
  TEST_ASSERT_FALSE(doc.v["restore"].valid());
  TEST_ASSERT_EQUAL_INT64(1, doc.v["pins"].at(1)["level"].asInt());
  TEST_ASSERT_TRUE(doc.v["pins"].at(0)["name"].stringEquals("Door 1"));
}

// ---------------------------------------------------------------- schedules

void test_when_a_schedule_is_active() {
  // Monday to Friday, from 8:00 to 18:30.
  const uint8_t workdays = 0x3E;
  Schedule s = weekly(0, 8 * 60, 18 * 60 + 30, workdays);
  TEST_ASSERT_FALSE(sdk::scheduleActive(s, 0, 7 * 60 + 59, 1));
  TEST_ASSERT_TRUE(sdk::scheduleActive(s, 0, 8 * 60, 1));
  TEST_ASSERT_TRUE(sdk::scheduleActive(s, 0, 18 * 60 + 29, 5));
  TEST_ASSERT_FALSE(sdk::scheduleActive(s, 0, 18 * 60 + 30, 5));
  TEST_ASSERT_FALSE(sdk::scheduleActive(s, 0, 12 * 60, 0));   // Sunday
  TEST_ASSERT_FALSE(sdk::scheduleActive(s, 0, 12 * 60, 6));   // Saturday

  // Across midnight, on Friday and Saturday: from 22:00 to 2:00 of the day after.
  const uint8_t weekend = (1 << 5) | (1 << 6);
  s = weekly(0, 22 * 60, 2 * 60, weekend);
  TEST_ASSERT_FALSE(sdk::scheduleActive(s, 0, 21 * 60 + 59, 5));
  TEST_ASSERT_TRUE(sdk::scheduleActive(s, 0, 22 * 60, 5));
  TEST_ASSERT_TRUE(sdk::scheduleActive(s, 0, 1 * 60, 6));      // the night of Friday
  TEST_ASSERT_TRUE(sdk::scheduleActive(s, 0, 23 * 60, 6));
  TEST_ASSERT_TRUE(sdk::scheduleActive(s, 0, 1 * 60 + 59, 0)); // the night of Saturday
  TEST_ASSERT_FALSE(sdk::scheduleActive(s, 0, 2 * 60, 0));
  TEST_ASSERT_FALSE(sdk::scheduleActive(s, 0, 23 * 60, 0));    // Sunday evening
  TEST_ASSERT_FALSE(sdk::scheduleActive(s, 0, 1 * 60, 5));     // the night of Thursday

  // Turned off, or without a number: never.
  s = weekly(0, 0, 1439);
  TEST_ASSERT_TRUE(sdk::scheduleActive(s, 0, 600, 3));
  s.enabled = false;
  TEST_ASSERT_FALSE(sdk::scheduleActive(s, 0, 600, 3));
  s.enabled = true;
  s.id = 0;
  TEST_ASSERT_FALSE(sdk::scheduleActive(s, 0, 600, 3));

  // Once: between two moments.
  Schedule once;
  once.id = 2;
  once.kind = ScheduleKind::Once;
  once.onUnix = 1000;
  once.offUnix = 2000;
  TEST_ASSERT_FALSE(sdk::scheduleActive(once, 999, 0, 0));
  TEST_ASSERT_TRUE(sdk::scheduleActive(once, 1000, 0, 0));
  TEST_ASSERT_TRUE(sdk::scheduleActive(once, 1999, 0, 0));
  TEST_ASSERT_FALSE(sdk::scheduleActive(once, 2000, 0, 0));
}

void test_a_weekly_schedule_turns_its_channel() {
  Relay relay;
  OnlineSystem sys(false, nullptr, 1500, {&relay});
  const Now now = setClock(sys);

  char json[160];
  snprintf(json, sizeof(json), "{\"channel\":1,\"on\":%u,\"off\":%u}", now.minute + 2,
           now.minute + 5);
  Json<256> answer;
  TEST_ASSERT_TRUE(sys.kernel().commands().answer("mod.relay", "sched.add", json,
                                                  ArgsForm::Json, CmdSource::Local,
                                                  answer.w) == Status::Ok);
  const int64_t id = answer.parse()["result"]["id"].asInt();
  TEST_ASSERT_EQUAL_INT64(1, id);

  sys.advance(1200);
  TEST_ASSERT_FALSE(high(sys, kPinB));
  pass(sys, 60);
  TEST_ASSERT_FALSE(high(sys, kPinB));
  pass(sys, 60);                          // the minute it begins
  TEST_ASSERT_TRUE(high(sys, kPinB));
  TEST_ASSERT_FALSE(high(sys, kPinA));

  Doc doc(sys.kernel(), "mod.relay");
  TEST_ASSERT_EQUAL_UINT(1, doc.v["schedules"].size());
  const JsonValue s = doc.v["schedules"].at(0);
  TEST_ASSERT_EQUAL_INT64(1, s["id"].asInt());
  TEST_ASSERT_EQUAL_INT64(1, s["channel"].asInt());
  TEST_ASSERT_TRUE(s["type"].stringEquals("weekly"));
  TEST_ASSERT_EQUAL_INT64(127, s["days"].asInt());
  TEST_ASSERT_EQUAL_INT64(now.minute + 2, s["on"].asInt());
  TEST_ASSERT_TRUE(s["active"].asBool());
  TEST_ASSERT_TRUE(doc.v["schedulesRunning"].asBool());
  TEST_ASSERT_EQUAL_INT64(2, doc.v["schedulesMax"].asInt());

  pass(sys, 120);
  TEST_ASSERT_TRUE(high(sys, kPinB));
  pass(sys, 60);                          // the minute it ends
  TEST_ASSERT_FALSE(high(sys, kPinB));
}

void test_in_its_span_the_schedule_holds_the_channel() {
  Relay relay;
  OnlineSystem sys(false, nullptr, 1500, {&relay});
  const Now now = setClock(sys);

  Schedule s = weekly(0, now.minute + 1, now.minute + 3);
  uint16_t id = 0;
  TEST_ASSERT_TRUE(relay.schedules.add(s, id) == Status::Ok);
  // Before its span the channel is free.
  TEST_ASSERT_TRUE(sys.command("mod.relay", "set", "{\"channel\":0,\"value\":true}") == Status::Ok);
  TEST_ASSERT_TRUE(sys.command("mod.relay", "set", "{\"channel\":0,\"value\":false}") == Status::Ok);
  pass(sys, 60);
  TEST_ASSERT_TRUE(relay.pins.on(0));

  // In its span the commands leave the channel alone, as in LoopMax v1.
  const char* field = nullptr;
  TEST_ASSERT_TRUE(sys.command("mod.relay", "set", "{\"channel\":0,\"value\":false}", &field) ==
                   Status::Denied);
  TEST_ASSERT_EQUAL_STRING("channel", field);
  TEST_ASSERT_TRUE(sys.command("mod.relay", "toggle", "{\"channel\":0}") == Status::Denied);
  TEST_ASSERT_TRUE(sys.command("mod.relay", "all", "{\"value\":false}") == Status::Ok);
  TEST_ASSERT_TRUE(relay.pins.on(0));
  Doc doc(sys.kernel(), "mod.relay");
  TEST_ASSERT_TRUE(doc.v["pins"].at(0)["locked"].asBool());
  TEST_ASSERT_FALSE(doc.v["pins"].at(1)["locked"].valid());
  // The code of the module is not stopped.
  TEST_ASSERT_TRUE(relay.pins.set(0, false) == Status::Ok);
  TEST_ASSERT_TRUE(relay.pins.set(0, true) == Status::Ok);

  // The end of the span takes it off, and the channel is free again.
  pass(sys, 120);
  TEST_ASSERT_FALSE(relay.pins.on(0));
  TEST_ASSERT_TRUE(sys.command("mod.relay", "toggle", "{\"channel\":0}") == Status::Ok);
  TEST_ASSERT_TRUE(relay.pins.on(0));

  // A channel without schedules is left alone.
  TEST_ASSERT_TRUE(relay.pins.set(2, true) == Status::Ok);
  pass(sys, 600);
  TEST_ASSERT_TRUE(relay.pins.on(2));
}

void test_flip_leaves_the_channels_a_schedule_holds() {
  Relay relay;
  OnlineSystem sys(false, nullptr, 1500, {&relay});
  const Now now = setClock(sys);
  uint16_t id = 0;
  // A schedule that starts in a minute: out of its span the channel is flipped as the others.
  TEST_ASSERT_TRUE(relay.schedules.add(weekly(1, now.minute + 1, now.minute + 3), id) == Status::Ok);
  TEST_ASSERT_TRUE(relay.pins.set(0, true) == Status::Ok);
  TEST_ASSERT_TRUE(sys.command("mod.relay", "flip", "{}") == Status::Ok);
  TEST_ASSERT_FALSE(relay.pins.on(0));
  TEST_ASSERT_TRUE(relay.pins.on(1));
  TEST_ASSERT_TRUE(relay.pins.on(2));
  // In its span the schedule holds it: flip leaves it, and flips the others.
  pass(sys, 60);
  TEST_ASSERT_TRUE(relay.pins.on(1));
  TEST_ASSERT_TRUE(sys.command("mod.relay", "flip", "{}") == Status::Ok);
  TEST_ASSERT_TRUE(relay.pins.on(0));
  TEST_ASSERT_TRUE(relay.pins.on(1));   // left alone
  TEST_ASSERT_FALSE(relay.pins.on(2));
}

void test_a_day_that_is_not_in_the_schedule() {
  Relay relay;
  OnlineSystem sys(false, nullptr, 1500, {&relay});
  const Now now = setClock(sys);

  // Every day but today.
  const uint8_t others = static_cast<uint8_t>(sdk::kEveryDay & ~(1u << now.weekday));
  uint16_t id = 0;
  TEST_ASSERT_TRUE(relay.schedules.add(weekly(0, now.minute + 1, now.minute + 3, others), id) ==
                   Status::Ok);
  pass(sys, 60);
  TEST_ASSERT_FALSE(relay.pins.on(0));
  pass(sys, 60);
  TEST_ASSERT_FALSE(relay.pins.on(0));
}

void test_a_schedule_that_happens_once() {
  Relay relay;
  OnlineSystem sys(false, nullptr, 1500, {&relay});
  const Now now = setClock(sys);

  char json[200];
  snprintf(json, sizeof(json), "{\"channel\":2,\"type\":\"once\",\"on\":%lld,\"off\":%lld}",
           static_cast<long long>(now.unix + 120), static_cast<long long>(now.unix + 300));
  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.add", json) == Status::Ok);
  TEST_ASSERT_EQUAL_UINT8(1, relay.schedules.count());

  pass(sys, 119);
  TEST_ASSERT_FALSE(relay.pins.on(2));
  pass(sys, 1);
  TEST_ASSERT_TRUE(relay.pins.on(2));
  pass(sys, 179);
  TEST_ASSERT_TRUE(relay.pins.on(2));
  pass(sys, 1);
  // Over: the channel is off and the schedule is gone.
  TEST_ASSERT_FALSE(relay.pins.on(2));
  TEST_ASSERT_EQUAL_UINT8(0, relay.schedules.count());
  sys.advance(LMP_CONFIG_SAVE_DELAY_MS + 200);
  TEST_ASSERT_NULL(strstr(fileOf(sys, "relay").c_str(), "once"));

  // One that ended while the device was off changes nothing.
  TEST_ASSERT_TRUE(relay.pins.set(2, true) == Status::Ok);
  Schedule gone;
  gone.channel = 2;
  gone.kind = ScheduleKind::Once;
  gone.onUnix = now.unix + 10;
  gone.offUnix = now.unix + 20;
  uint16_t id = 0;
  TEST_ASSERT_TRUE(relay.schedules.add(gone, id) == Status::Ok);
  sys.advance(1200);
  TEST_ASSERT_EQUAL_UINT8(0, relay.schedules.count());
  TEST_ASSERT_TRUE(relay.pins.on(2));
}

void test_without_the_time_nothing_is_followed() {
  Relay relay;
  OnlineSystem sys(false, nullptr, 1500, {&relay});
  TEST_ASSERT_FALSE(sys.app.clock().synced());

  uint16_t id = 0;
  TEST_ASSERT_TRUE(relay.schedules.add(weekly(0, 0, 1439), id) == Status::Ok);
  TEST_ASSERT_TRUE(relay.pins.set(1, true) == Status::Ok);
  sys.advance(3000);
  TEST_ASSERT_FALSE(relay.pins.on(0));
  TEST_ASSERT_TRUE(relay.pins.on(1));
  Doc doc(sys.kernel(), "mod.relay");
  TEST_ASSERT_FALSE(doc.v["schedulesRunning"].asBool(true));
  TEST_ASSERT_FALSE(doc.v["schedules"].at(0)["active"].asBool(true));

  // The time arrives: the channel goes where its schedule wants it.
  setClock(sys);
  sys.advance(1200);
  TEST_ASSERT_TRUE(relay.pins.on(0));
}

void test_schedules_are_checked() {
  Relay relay;
  OnlineSystem sys(false, nullptr, 1500, {&relay});
  const char* field = nullptr;

  struct Case {
    const char* json;
    const char* field;
  };
  const Case cases[] = {
      {"{\"channel\":3,\"on\":60,\"off\":120}", "channel"},
      {"{\"channel\":0,\"on\":1440,\"off\":120}", "on"},
      {"{\"channel\":0,\"on\":-1,\"off\":120}", "on"},
      {"{\"channel\":0,\"on\":60,\"off\":60}", "off"},
      {"{\"channel\":0,\"on\":60,\"off\":120,\"days\":0}", "days"},
      {"{\"channel\":0,\"on\":60,\"off\":120,\"days\":128}", "days"},
      {"{\"channel\":0,\"on\":60,\"off\":120,\"type\":\"daily\"}", "type"},
      {"{\"channel\":0,\"type\":\"once\",\"on\":2000,\"off\":1000}", "off"},
      {"{\"channel\":0,\"type\":\"once\",\"on\":0,\"off\":1000}", "off"},
      {"{\"channel\":0,\"on\":60}", "off"},
  };
  for (const Case& c : cases) {
    field = nullptr;
    TEST_ASSERT_TRUE_MESSAGE(sys.command("mod.relay", "sched.add", c.json, &field) ==
                                 Status::Invalid, c.json);
    TEST_ASSERT_NOT_NULL_MESSAGE(field, c.json);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c.field, field, c.json);
  }
  TEST_ASSERT_EQUAL_UINT8(0, relay.schedules.count());

  // Two for each channel, in this module.
  TEST_ASSERT_EQUAL_UINT8(6, relay.schedules.capacity());
  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.add", "{\"channel\":0,\"on\":60,\"off\":120}") ==
                   Status::Ok);
  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.add", "{\"channel\":0,\"on\":200,\"off\":300}") ==
                   Status::Ok);
  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.add", "{\"channel\":0,\"on\":400,\"off\":500}") ==
                   Status::NoMem);
  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.add", "{\"channel\":1,\"on\":400,\"off\":500}") ==
                   Status::Ok);
  TEST_ASSERT_EQUAL_UINT8(3, relay.schedules.count());
}

void test_schedules_are_changed_and_removed() {
  Relay relay;
  OnlineSystem sys(false, nullptr, 1500, {&relay});
  const char* field = nullptr;
  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.add",
                               "{\"channel\":0,\"on\":60,\"off\":120,\"days\":62}") ==
                   Status::Ok);
  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.add", "{\"channel\":0,\"on\":200,\"off\":300}") ==
                   Status::Ok);
  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.add", "{\"channel\":1,\"on\":200,\"off\":300}") ==
                   Status::Ok);

  // What is not said stays.
  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.update", "{\"id\":1,\"off\":150}") ==
                   Status::Ok);
  const Schedule* s = relay.schedules.find(1);
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT_EQUAL_UINT16(60, s->onMin);
  TEST_ASSERT_EQUAL_UINT16(150, s->offMin);
  TEST_ASSERT_EQUAL_UINT8(62, s->days);
  TEST_ASSERT_TRUE(s->enabled);

  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.update", "{\"id\":1,\"enabled\":false}") ==
                   Status::Ok);
  TEST_ASSERT_FALSE(relay.schedules.find(1)->enabled);
  TEST_ASSERT_EQUAL_UINT16(150, relay.schedules.find(1)->offMin);

  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.update", "{\"id\":1,\"on\":150}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("off", field);
  TEST_ASSERT_EQUAL_UINT16(60, relay.schedules.find(1)->onMin);
  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.update", "{\"id\":9,\"on\":150}", &field) ==
                   Status::NotFound);
  TEST_ASSERT_EQUAL_STRING("id", field);

  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.remove", "{\"id\":2}") == Status::Ok);
  TEST_ASSERT_NULL(relay.schedules.find(2));
  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.remove", "{\"id\":2}", &field) ==
                   Status::NotFound);
  // A number is not given twice: who kept the old one must not find another schedule.
  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.add", "{\"channel\":0,\"on\":10,\"off\":20}") ==
                   Status::Ok);
  TEST_ASSERT_NULL(relay.schedules.find(2));
  TEST_ASSERT_NOT_NULL(relay.schedules.find(4));

  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.clear", "{\"channel\":0}") == Status::Ok);
  TEST_ASSERT_EQUAL_UINT8(1, relay.schedules.count());
  TEST_ASSERT_NOT_NULL(relay.schedules.find(3));
  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.clear", "{\"channel\":5}", &field) ==
                   Status::Invalid);
}

// Two schedules share time: the same days and hours, midnight, the end of the week, once.
void test_when_two_schedules_share_time() {
  const uint8_t mon = 1u << 1, tue = 1u << 2, sat = 1u << 6, sun = 1u << 0;
  // 08:00-18:00 and 08:00-14:00, every day: yes.
  TEST_ASSERT_TRUE(sdk::schedulesOverlap(weekly(0, 480, 1080), weekly(0, 480, 840), 0));
  // One ends when the other begins: no.
  TEST_ASSERT_FALSE(sdk::schedulesOverlap(weekly(0, 480, 1080), weekly(0, 1080, 1200), 0));
  // Other days, other channel: no.
  TEST_ASSERT_FALSE(sdk::schedulesOverlap(weekly(0, 480, 1080, mon), weekly(0, 480, 1080, tue), 0));
  TEST_ASSERT_FALSE(sdk::schedulesOverlap(weekly(0, 480, 1080), weekly(1, 480, 1080), 0));
  // 22:00-02:00 on Monday takes the night into Tuesday.
  TEST_ASSERT_TRUE(sdk::schedulesOverlap(weekly(0, 1320, 120, mon), weekly(0, 60, 90, tue), 0));
  TEST_ASSERT_FALSE(sdk::schedulesOverlap(weekly(0, 1320, 120, mon), weekly(0, 120, 180, tue), 0));
  // Saturday night into Sunday morning, across the end of the week.
  TEST_ASSERT_TRUE(sdk::schedulesOverlap(weekly(0, 1380, 60, sat), weekly(0, 30, 45, sun), 0));

  // Once, Tuesday 29 September 2026 10:00-12:00 in Rome (UTC+2).
  Schedule once;
  once.channel = 0;
  once.kind = ScheduleKind::Once;
  once.onUnix = 1790668800;    // 08:00 UTC
  once.offUnix = 1790676000;   // 10:00 UTC
  const int32_t rome = 7200;
  TEST_ASSERT_TRUE(sdk::schedulesOverlap(once, weekly(0, 480, 1080, tue), rome));
  TEST_ASSERT_FALSE(sdk::schedulesOverlap(once, weekly(0, 480, 1080, mon), rome));
  TEST_ASSERT_FALSE(sdk::schedulesOverlap(once, weekly(0, 720, 1080, tue), rome));
  Schedule later = once;
  later.onUnix = once.offUnix;
  later.offUnix = once.offUnix + 3600;
  TEST_ASSERT_FALSE(sdk::schedulesOverlap(once, later, rome));
  later.onUnix = once.offUnix - 60;
  TEST_ASSERT_TRUE(sdk::schedulesOverlap(once, later, rome));
}

// A schedule that shares time with an enabled one is kept disabled; enabling it asks for force,
// which disables the other.
void test_schedules_that_share_time() {
  Relay relay;
  OnlineSystem sys(false, nullptr, 1500, {&relay});
  Json<512> answer;
  TEST_ASSERT_TRUE(sys.kernel().commands().answer("mod.relay", "sched.add",
                   "{\"channel\":0,\"on\":480,\"off\":1080}", ArgsForm::Json, CmdSource::Local,
                   answer.w) == Status::Ok);
  TEST_ASSERT_TRUE(answer.parse()["result"]["enabled"].asBool());
  TEST_ASSERT_EQUAL_UINT(0, answer.parse()["result"]["conflicts"].size());

  Json<512> second;
  TEST_ASSERT_TRUE(sys.kernel().commands().answer("mod.relay", "sched.add",
                   "{\"channel\":0,\"on\":480,\"off\":840}", ArgsForm::Json, CmdSource::Local,
                   second.w) == Status::Ok);
  const JsonValue r = second.parse()["result"];
  TEST_ASSERT_FALSE(r["enabled"].asBool());
  TEST_ASSERT_EQUAL_UINT(1, r["conflicts"].size());
  TEST_ASSERT_EQUAL_INT64(1, r["conflicts"].at(0).asInt());
  const uint16_t id = static_cast<uint16_t>(r["id"].asInt());
  TEST_ASSERT_FALSE(relay.schedules.find(id)->enabled);

  // Enabling it is refused, and the answer says with which.
  Json<512> refused;
  char json[80];
  snprintf(json, sizeof(json), "{\"id\":%u,\"enabled\":true}", id);
  TEST_ASSERT_TRUE(sys.kernel().commands().answer("mod.relay", "sched.update", json,
                   ArgsForm::Json, CmdSource::Local, refused.w) == Status::Busy);
  TEST_ASSERT_TRUE(refused.parse()["field"].stringEquals("enabled"));
  TEST_ASSERT_EQUAL_INT64(1, refused.parse()["result"]["conflicts"].at(0).asInt());
  TEST_ASSERT_FALSE(relay.schedules.find(id)->enabled);
  TEST_ASSERT_TRUE(relay.schedules.find(1)->enabled);

  // With force it is enabled, and the other disabled.
  snprintf(json, sizeof(json), "{\"id\":%u,\"enabled\":true,\"force\":true}", id);
  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.update", json) == Status::Ok);
  TEST_ASSERT_TRUE(relay.schedules.find(id)->enabled);
  TEST_ASSERT_FALSE(relay.schedules.find(1)->enabled);

  // The document says which ones share time, for the console.
  Doc doc(sys.kernel(), "mod.relay");
  TEST_ASSERT_EQUAL_UINT(1, doc.v["schedules"].at(0)["conflicts"].size());
  TEST_ASSERT_EQUAL_UINT(0, doc.v["schedules"].at(1)["conflicts"].size());

  // One after the other on another channel: both enabled.
  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.add", "{\"channel\":1,\"on\":480,\"off\":1080}") ==
                   Status::Ok);
  TEST_ASSERT_TRUE(sys.command("mod.relay", "sched.add", "{\"channel\":1,\"on\":1080,\"off\":1200}") ==
                   Status::Ok);
  uint8_t enabled = 0;
  for (uint16_t i = 1; i < 10; ++i) {
    const Schedule* x = relay.schedules.find(i);
    if (x && x->channel == 1 && x->enabled) ++enabled;
  }
  TEST_ASSERT_EQUAL_UINT8(2, enabled);
}

void test_schedules_survive_a_restart() {
  TestBoard tb;
  {
    Relay relay;
    LoopMax app(tb.board);
    Logger::setInstance(&app.kernel().log());
    TEST_ASSERT_TRUE(app.modules().add(relay) == Status::Ok);
    TEST_ASSERT_TRUE(app.begin() == Status::Ok);
    run(app.kernel(), tb.clock, 1500);
    uint16_t id = 0;
    TEST_ASSERT_TRUE(relay.schedules.add(weekly(0, 60, 120, 0x3E), id) == Status::Ok);
    TEST_ASSERT_TRUE(relay.schedules.add(weekly(2, 1300, 100), id) == Status::Ok);
    Schedule once;
    once.channel = 1;
    once.kind = ScheduleKind::Once;
    once.onUnix = kSomeTime + 1000;
    once.offUnix = kSomeTime + 2000;
    once.enabled = false;
    TEST_ASSERT_TRUE(relay.schedules.add(once, id) == Status::Ok);
    TEST_ASSERT_EQUAL_UINT16(3, id);
    run(app.kernel(), tb.clock, LMP_CONFIG_SAVE_DELAY_MS + 200);
    Logger::setInstance(nullptr);
  }
  tb.native().fs.unmount();

  Relay relay;
  LoopMax app(tb.board);
  Logger::setInstance(&app.kernel().log());
  TEST_ASSERT_TRUE(app.modules().add(relay) == Status::Ok);
  TEST_ASSERT_TRUE(app.begin() == Status::Ok);
  run(app.kernel(), tb.clock, 1500);

  TEST_ASSERT_EQUAL_UINT8(3, relay.schedules.count());
  const Schedule* a = relay.schedules.find(1);
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_EQUAL_UINT8(0, a->channel);
  TEST_ASSERT_EQUAL_UINT8(0x3E, a->days);
  TEST_ASSERT_EQUAL_UINT16(60, a->onMin);
  TEST_ASSERT_EQUAL_UINT16(120, a->offMin);
  const Schedule* b = relay.schedules.find(2);
  TEST_ASSERT_NOT_NULL(b);
  TEST_ASSERT_EQUAL_UINT16(1300, b->onMin);   // across midnight
  TEST_ASSERT_EQUAL_UINT16(100, b->offMin);
  const Schedule* c = relay.schedules.find(3);
  TEST_ASSERT_NOT_NULL(c);
  TEST_ASSERT_TRUE(c->kind == ScheduleKind::Once);
  TEST_ASSERT_TRUE(c->onUnix == kSomeTime + 1000);
  TEST_ASSERT_TRUE(c->offUnix == kSomeTime + 2000);
  TEST_ASSERT_FALSE(c->enabled);

  // The next number follows the ones on file.
  uint16_t id = 0;
  TEST_ASSERT_TRUE(relay.schedules.add(weekly(1, 10, 20), id) == Status::Ok);
  TEST_ASSERT_EQUAL_UINT16(4, id);
}

// ---------------------------------------------------------------- small tools

void test_something_to_do_later() {
  sdk::Debounced later;
  TEST_ASSERT_FALSE(later.armed());
  TEST_ASSERT_FALSE(later.due(1000));

  later.arm(1000, 500);
  TEST_ASSERT_TRUE(later.armed());
  TEST_ASSERT_FALSE(later.due(1499));
  // Asked again: the wait starts again.
  later.arm(1400, 500);
  TEST_ASSERT_FALSE(later.due(1500));
  TEST_ASSERT_FALSE(later.due(1899));
  TEST_ASSERT_TRUE(later.due(1900));
  TEST_ASSERT_FALSE(later.due(1901));   // once

  later.arm(2000, 100);
  later.cancel();
  TEST_ASSERT_FALSE(later.due(5000));

  // The counter of the milliseconds goes back to zero after 49 days.
  later.arm(0xFFFFFF00u, 0x200);
  TEST_ASSERT_FALSE(later.due(0xFFFFFFF0u));
  TEST_ASSERT_FALSE(later.due(0x000000FFu));
  TEST_ASSERT_TRUE(later.due(0x00000100u));
}

void test_a_button() {
  OnlineSystem sys(false, nullptr, 200);
  IGpio* pins = sys.kernel().api<IGpio>();
  hal::native::Gpio& pc = sys.pc().gpio;
  const uint8_t pin = 33;
  pc.setInput(pin, PinLevel::High);

  sdk::Button button;
  TEST_ASSERT_TRUE(button.poll(0) == ButtonEvent::None);   // not begun
  TEST_ASSERT_TRUE(button.begin(*pins, pin, true, "") == Status::Invalid);
  TEST_ASSERT_TRUE(button.begin(*pins, pin, true, "test", 1000) == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("test", pins->ownerOf(pin));
  TEST_ASSERT_TRUE(pc.mode(pin) == hal::PinMode::InputPullUp);

  uint32_t t = 1000;
  TEST_ASSERT_TRUE(button.poll(t) == ButtonEvent::None);

  // A short press: told when the contacts have been still for a while.
  pc.setInput(pin, PinLevel::Low);
  TEST_ASSERT_TRUE(button.poll(t += 10) == ButtonEvent::None);
  TEST_ASSERT_TRUE(button.poll(t += 20) == ButtonEvent::None);
  TEST_ASSERT_FALSE(button.held());
  TEST_ASSERT_TRUE(button.poll(t += 30) == ButtonEvent::Pressed);
  TEST_ASSERT_TRUE(button.held());
  TEST_ASSERT_TRUE(button.poll(t += 100) == ButtonEvent::None);
  TEST_ASSERT_TRUE(button.heldMs(t) >= 100);
  pc.setInput(pin, PinLevel::High);
  TEST_ASSERT_TRUE(button.poll(t += 10) == ButtonEvent::None);
  TEST_ASSERT_TRUE(button.poll(t += 50) == ButtonEvent::Released);
  TEST_ASSERT_FALSE(button.held());
  TEST_ASSERT_EQUAL_UINT32(0, button.heldMs(t));

  // Noise: the level does not hold.
  pc.setInput(pin, PinLevel::Low);
  TEST_ASSERT_TRUE(button.poll(t += 10) == ButtonEvent::None);
  pc.setInput(pin, PinLevel::High);
  TEST_ASSERT_TRUE(button.poll(t += 10) == ButtonEvent::None);
  pc.setInput(pin, PinLevel::Low);
  TEST_ASSERT_TRUE(button.poll(t += 10) == ButtonEvent::None);
  pc.setInput(pin, PinLevel::High);
  TEST_ASSERT_TRUE(button.poll(t += 10) == ButtonEvent::None);
  TEST_ASSERT_TRUE(button.poll(t += 100) == ButtonEvent::None);
  TEST_ASSERT_FALSE(button.held());

  // A long press: told once, and its end is another thing than the end of a short one.
  pc.setInput(pin, PinLevel::Low);
  TEST_ASSERT_TRUE(button.poll(t += 10) == ButtonEvent::None);
  TEST_ASSERT_TRUE(button.poll(t += 50) == ButtonEvent::Pressed);
  TEST_ASSERT_TRUE(button.poll(t += 500) == ButtonEvent::None);
  TEST_ASSERT_TRUE(button.poll(t += 500) == ButtonEvent::LongPress);
  TEST_ASSERT_TRUE(button.poll(t += 500) == ButtonEvent::None);
  pc.setInput(pin, PinLevel::High);
  TEST_ASSERT_TRUE(button.poll(t += 10) == ButtonEvent::None);
  TEST_ASSERT_TRUE(button.poll(t += 50) == ButtonEvent::LongReleased);

  button.end();
  TEST_ASSERT_NULL(pins->ownerOf(pin));
  TEST_ASSERT_TRUE(button.poll(t += 10) == ButtonEvent::None);

  // A button that takes the pin high, on a pin of somebody else.
  TEST_ASSERT_TRUE(pins->claim(pin, hal::PinMode::Output, "relay") == Status::Ok);
  TEST_ASSERT_TRUE(button.begin(*pins, pin, false, "test") == Status::Busy);
  TEST_ASSERT_TRUE(button.begin(*pins, 37, false, "test") == Status::Ok);
  TEST_ASSERT_TRUE(pc.mode(37) == hal::PinMode::InputPullDown);
  pc.setInput(37, PinLevel::High);
  TEST_ASSERT_TRUE(button.poll(t += 10) == ButtonEvent::None);
  TEST_ASSERT_TRUE(button.poll(t += 50) == ButtonEvent::Pressed);
  button.end();
}

void test_a_value_read_again_and_again() {
  sdk::Poller poller;
  poller.begin(1000, 4, 0.5f);
  TEST_ASSERT_FALSE(poller.valid());

  // Time of the readings.
  TEST_ASSERT_TRUE(poller.due(5000));
  TEST_ASSERT_FALSE(poller.due(5999));
  TEST_ASSERT_TRUE(poller.due(6000));
  TEST_ASSERT_FALSE(poller.due(6001));

  // The first value is news. Then only what moves enough.
  TEST_ASSERT_TRUE(poller.add(20.0f));
  TEST_ASSERT_TRUE(poller.valid());
  TEST_ASSERT_EQUAL_FLOAT(20.0f, poller.value());
  TEST_ASSERT_FALSE(poller.add(20.4f));          // average 20.2
  TEST_ASSERT_EQUAL_FLOAT(20.2f, poller.value());
  TEST_ASSERT_FALSE(poller.add(20.6f));          // 20.33
  TEST_ASSERT_TRUE(poller.add(21.4f));           // 20.6: half a degree from what was told
  TEST_ASSERT_EQUAL_FLOAT(20.6f, poller.value());
  // The window is of four readings: the oldest leaves.
  TEST_ASSERT_FALSE(poller.add(20.0f));          // 20.4, 20.6, 21.4, 20.0 = 20.6
  TEST_ASSERT_EQUAL_FLOAT(20.6f, poller.value());

  // A spike is smoothed by the average.
  poller.begin(1000, 4, 2.0f);
  TEST_ASSERT_TRUE(poller.add(10.0f));
  TEST_ASSERT_FALSE(poller.add(10.0f));
  TEST_ASSERT_FALSE(poller.add(10.0f));
  TEST_ASSERT_FALSE(poller.add(16.0f));          // 11.5
  TEST_ASSERT_EQUAL_FLOAT(11.5f, poller.value());

  // Readings that fail: after three the value is no longer good, the first good one is news.
  poller.fail();
  poller.fail();
  TEST_ASSERT_TRUE(poller.valid());
  poller.fail();
  TEST_ASSERT_FALSE(poller.valid());
  TEST_ASSERT_EQUAL_UINT8(3, poller.failures());
  TEST_ASSERT_TRUE(poller.add(10.0f));
  TEST_ASSERT_TRUE(poller.valid());
  TEST_ASSERT_EQUAL_UINT8(0, poller.failures());

  // Limits of the settings.
  poller.begin(0, 0, -1.0f);
  TEST_ASSERT_TRUE(poller.add(1.0f));
  TEST_ASSERT_FALSE(poller.add(1.5f));           // window of one: the value is the last
  TEST_ASSERT_EQUAL_FLOAT(1.5f, poller.value());
  TEST_ASSERT_TRUE(poller.add(2.0f));
  poller.begin(1000, 200, 0);
  for (int i = 0; i < 40; ++i) poller.add(static_cast<float>(i));
  TEST_ASSERT_EQUAL_FLOAT(31.5f, poller.value());   // the last sixteen: 24..39
}

// ---------------------------------------------------------------- a chip on a bus

void test_a_chip_on_a_bus() {
  OnlineSystem sys(false, nullptr, 200);
  IBus* bus = sys.kernel().api<IBus>();
  hal::native::I2c& wires = sys.pc().i2c[0];
  wires.addDevice(0x48);
  wires.setRegister(0x48, 0x00, 0x12);
  wires.setRegister(0x48, 0x01, 0x34);
  wires.setRegister(0x48, 0x0F, 0xA5);

  sdk::I2cDevice chip(0x48);
  sdk::I2cDevice nobody(0x50);
  TEST_ASSERT_FALSE(chip.attached());
  uint8_t value = 0;
  TEST_ASSERT_TRUE(chip.read8(0x0F, value) == Status::Denied);
  TEST_ASSERT_TRUE(chip.attach(nullptr) == Status::Unsupported);

  TEST_ASSERT_TRUE(bus->openI2c(0, 21, 22, 100000, "test") == Status::Ok);
  TEST_ASSERT_TRUE(chip.attach(bus->i2c(0)) == Status::Ok);
  TEST_ASSERT_TRUE(chip.present());
  TEST_ASSERT_EQUAL_UINT8(0x48, chip.address());
  TEST_ASSERT_TRUE(nobody.attach(bus->i2c(0)) == Status::NotFound);
  TEST_ASSERT_FALSE(nobody.present());

  TEST_ASSERT_TRUE(chip.read8(0x0F, value) == Status::Ok);
  TEST_ASSERT_EQUAL_HEX8(0xA5, value);
  uint16_t wide = 0;
  TEST_ASSERT_TRUE(chip.read16(0x00, wide) == Status::Ok);
  TEST_ASSERT_EQUAL_HEX16(0x1234, wide);

  TEST_ASSERT_TRUE(chip.write8(0x02, 0x77) == Status::Ok);
  TEST_ASSERT_EQUAL_HEX8(0x77, wires.getRegister(0x48, 0x02));
  const uint8_t block[3] = {1, 2, 3};
  TEST_ASSERT_TRUE(chip.write(0x10, block, sizeof(block)) == Status::Ok);
  TEST_ASSERT_EQUAL_HEX8(1, wires.getRegister(0x48, 0x10));
  TEST_ASSERT_EQUAL_HEX8(3, wires.getRegister(0x48, 0x12));
  uint8_t back[3] = {};
  TEST_ASSERT_TRUE(chip.read(0x10, back, sizeof(back)) == Status::Ok);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(block, back, 3);
  TEST_ASSERT_TRUE(chip.command(0x05) == Status::Ok);

  uint8_t large[40] = {};
  TEST_ASSERT_TRUE(chip.write(0x00, large, sizeof(large)) == Status::NoMem);
  TEST_ASSERT_TRUE(chip.write(0x00, nullptr, 2) == Status::Invalid);
  TEST_ASSERT_EQUAL_UINT32(0, chip.errors());

  // The chip goes away: after three transfers it is known as gone.
  wires.removeDevice(0x48);
  TEST_ASSERT_TRUE(chip.read8(0x0F, value) != Status::Ok);
  TEST_ASSERT_TRUE(chip.present());
  TEST_ASSERT_TRUE(chip.read8(0x0F, value) != Status::Ok);
  TEST_ASSERT_TRUE(chip.write8(0x02, 1) != Status::Ok);
  TEST_ASSERT_FALSE(chip.present());
  TEST_ASSERT_EQUAL_UINT32(3, chip.errors());

  // It comes back: the first answer is enough.
  wires.addDevice(0x48);
  wires.setRegister(0x48, 0x0F, 0x5A);
  TEST_ASSERT_TRUE(chip.read8(0x0F, value) == Status::Ok);
  TEST_ASSERT_EQUAL_HEX8(0x5A, value);
  TEST_ASSERT_TRUE(chip.present());
  TEST_ASSERT_EQUAL_UINT32(3, chip.errors());
}

// ---------------------------------------------------------------- a service on the network

void test_a_service_on_the_network() {
  Weather weather;
  OnlineSystem sys(true, nullptr, 1500, {&weather});
  const char* url = "http://meteo.test/now";

  // Asked as soon as the device was on the network: nobody was there.
  TEST_ASSERT_TRUE(weather.begun == Status::Ok);
  TEST_ASSERT_EQUAL_INT(0, weather.news);
  TEST_ASSERT_FALSE(weather.source.fresh());
  TEST_ASSERT_EQUAL_STRING("", weather.source.body());
  TEST_ASSERT_EQUAL_UINT32(0, weather.source.ageSec(sys.tb.clock.now));
  TEST_ASSERT_EQUAL_UINT(1, sys.pc().httpClient.count());
  TEST_ASSERT_EQUAL_UINT32(1, weather.source.failures());

  // Asked now, without waiting for its time.
  sys.pc().httpClient.when(url, 200, "{\"celsius\":21.5}");
  weather.source.refresh();
  sys.advance(500);
  TEST_ASSERT_EQUAL_INT(1, weather.news);
  TEST_ASSERT_TRUE(weather.source.fresh());
  TEST_ASSERT_EQUAL_STRING("{\"celsius\":21.5}", weather.source.body());
  TEST_ASSERT_EQUAL_UINT(16, weather.source.length());
  TEST_ASSERT_EQUAL_INT(200, weather.source.lastStatus());
  TEST_ASSERT_EQUAL_UINT(2, sys.pc().httpClient.count());

  // Not asked again before its time.
  sys.advance(30000);
  TEST_ASSERT_EQUAL_UINT(2, sys.pc().httpClient.count());
  TEST_ASSERT_TRUE(weather.source.ageSec(sys.tb.clock.now) >= 30);

  // The server fails: the answer of before stays, and its age grows.
  sys.pc().httpClient.when(url, 503, "down for a while");
  sys.advance(31000);
  TEST_ASSERT_EQUAL_UINT(3, sys.pc().httpClient.count());
  TEST_ASSERT_EQUAL_INT(1, weather.news);
  TEST_ASSERT_EQUAL_INT(503, weather.source.lastStatus());
  TEST_ASSERT_EQUAL_UINT32(2, weather.source.failures());
  TEST_ASSERT_EQUAL_STRING("{\"celsius\":21.5}", weather.source.body());
  TEST_ASSERT_TRUE(weather.source.ageSec(sys.tb.clock.now) >= 60);

  sys.pc().httpClient.when(url, 200, "{\"celsius\":19}");
  weather.source.refresh();
  sys.advance(500);
  TEST_ASSERT_EQUAL_INT(2, weather.news);
  TEST_ASSERT_EQUAL_STRING("{\"celsius\":19}", weather.source.body());
  TEST_ASSERT_TRUE(weather.source.ageSec(sys.tb.clock.now) < 2);

  // An answer longer than its room is not an answer by halves: the one of before stays.
  sys.pc().httpClient.when(url, 200, std::string(400, 'x'));
  weather.source.refresh();
  sys.advance(500);
  TEST_ASSERT_EQUAL_INT(2, weather.news);
  TEST_ASSERT_EQUAL_STRING("{\"celsius\":19}", weather.source.body());
  TEST_ASSERT_EQUAL_UINT32(3, weather.source.failures());
}

void test_a_source_needs_what_it_needs() {
  OnlineSystem sys(true, nullptr, 1500);
  IHttpClientService* http = sys.kernel().api<IHttpClientService>();
  sdk::OnlineSource source;
  char room[128];
  TEST_ASSERT_TRUE(source.begin(nullptr, "http://a.test/", 60, room, sizeof(room)) ==
                   Status::Unsupported);
  TEST_ASSERT_TRUE(source.begin(http, "", 60, room, sizeof(room)) == Status::Invalid);
  TEST_ASSERT_TRUE(source.begin(http, nullptr, 60, room, sizeof(room)) == Status::Invalid);
  TEST_ASSERT_TRUE(source.begin(http, "http://a.test/", 0, room, sizeof(room)) ==
                   Status::Invalid);
  TEST_ASSERT_TRUE(source.begin(http, "http://a.test/", 60, nullptr, 128) == Status::Invalid);
  TEST_ASSERT_TRUE(source.begin(http, "http://a.test/", 60, room, 16) == Status::Invalid);
  const std::string longUrl = "http://a.test/" + std::string(200, 'a');
  TEST_ASSERT_TRUE(source.begin(http, longUrl.c_str(), 60, room, sizeof(room)) ==
                   Status::Invalid);
  // Never begun: nothing happens, nothing is shown.
  TEST_ASSERT_FALSE(source.tick(1000));
  TEST_ASSERT_EQUAL_STRING("", source.body());
  TEST_ASSERT_FALSE(source.fresh());
}

void test_a_source_without_a_network_waits() {
  Weather weather;
  OnlineSystem sys(false, nullptr, 1500, {&weather});   // access point: no way out
  sys.pc().httpClient.when("http://meteo.test/now", 200, "{\"celsius\":21.5}");
  sys.advance(5000);
  TEST_ASSERT_TRUE(weather.begun == Status::Ok);
  TEST_ASSERT_EQUAL_INT(0, weather.news);
  TEST_ASSERT_FALSE(weather.source.fresh());
  // Refused before leaving is not a failure of the service.
  TEST_ASSERT_EQUAL_UINT32(0, weather.source.failures());
  TEST_ASSERT_EQUAL_UINT(0, sys.pc().httpClient.count());
}

// ---------------------------------------------------------------- Zappy 2, free edition

lmp::sdk::Module& lmp_module_zappy2();

void test_zappy2_free() {
  OnlineSystem sys(false, nullptr, 1500, {&lmp_module_zappy2()});
  // On the PC, as on every board but the ESP32: pins 4 and 5.
  Doc doc(sys.kernel(), "mod.zappy2");
  TEST_ASSERT_EQUAL_UINT(2, doc.v["pins"].size());
  TEST_ASSERT_EQUAL_INT64(4, doc.v["pins"].at(0)["gpio"].asInt());
  TEST_ASSERT_EQUAL_INT64(5, doc.v["pins"].at(1)["gpio"].asInt());
  TEST_ASSERT_TRUE(doc.v["pins"].at(0)["name"].stringEquals("Line 1"));
  TEST_ASSERT_TRUE(doc.v["restore"].asBool());
  TEST_ASSERT_TRUE(doc.v["schedules"].isArray());

  TEST_ASSERT_TRUE(sys.command("mod.zappy2", "set", "{\"channel\":0,\"value\":true}") == Status::Ok);
  TEST_ASSERT_TRUE(high(sys, 4));
  TEST_ASSERT_TRUE(sys.command("mod.zappy2", "name", "{\"channel\":1,\"name\":\"Heater\"}") ==
                   Status::Ok);

  // A schedule on the second relay: out of its span flip takes it too, in its span it is held.
  const Now now = setClock(sys);
  char json[96];
  snprintf(json, sizeof(json), "{\"channel\":1,\"on\":%u,\"off\":%u}", now.minute + 1,
           now.minute + 3);
  TEST_ASSERT_TRUE(sys.command("mod.zappy2", "sched.add", json) == Status::Ok);
  TEST_ASSERT_TRUE(sys.command("mod.zappy2", "flip", "{}") == Status::Ok);
  TEST_ASSERT_FALSE(high(sys, 4));
  TEST_ASSERT_TRUE(high(sys, 5));
  pass(sys, 60);
  TEST_ASSERT_TRUE(high(sys, 5));
  TEST_ASSERT_TRUE(sys.command("mod.zappy2", "set", "{\"channel\":1,\"value\":false}") ==
                   Status::Denied);
  Doc held(sys.kernel(), "mod.zappy2");
  TEST_ASSERT_TRUE(held.v["pins"].at(1)["locked"].asBool());
  TEST_ASSERT_TRUE(held.v["pins"].at(1)["name"].stringEquals("Heater"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_zappy2_free);
  RUN_TEST(test_outputs_are_driven_by_channel);
  RUN_TEST(test_names_and_inverted_channels);
  RUN_TEST(test_channels_come_back_after_a_restart);
  RUN_TEST(test_levels_are_not_written_when_they_do_not_come_back);
  RUN_TEST(test_two_modules_cannot_have_the_same_pin);
  RUN_TEST(test_inputs_are_read);
  RUN_TEST(test_when_a_schedule_is_active);
  RUN_TEST(test_a_weekly_schedule_turns_its_channel);
  RUN_TEST(test_in_its_span_the_schedule_holds_the_channel);
  RUN_TEST(test_flip_leaves_the_channels_a_schedule_holds);
  RUN_TEST(test_a_day_that_is_not_in_the_schedule);
  RUN_TEST(test_a_schedule_that_happens_once);
  RUN_TEST(test_without_the_time_nothing_is_followed);
  RUN_TEST(test_schedules_are_checked);
  RUN_TEST(test_schedules_are_changed_and_removed);
  RUN_TEST(test_when_two_schedules_share_time);
  RUN_TEST(test_schedules_that_share_time);
  RUN_TEST(test_schedules_survive_a_restart);
  RUN_TEST(test_something_to_do_later);
  RUN_TEST(test_a_button);
  RUN_TEST(test_a_value_read_again_and_again);
  RUN_TEST(test_a_chip_on_a_bus);
  RUN_TEST(test_a_service_on_the_network);
  RUN_TEST(test_a_source_needs_what_it_needs);
  RUN_TEST(test_a_source_without_a_network_waits);
  return UNITY_END();
}
