// Tests on the PC of the service that hosts the modules: the life of a module, its
// settings, its file, what it tells to the rest of the system.
#include <fstream>

#include "../support/online_system.h"
#include "sdk/pin_bank.h"
#include "sdk/state.h"

using namespace lmp;
using namespace lmp::test;
using lmp::sdk::ModuleState;

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

struct ProbeCfg {
  FixedString<16> label;
  uint16_t every;
  FixedString<24> token;
};

struct ProbeState {
  uint32_t count;
  bool ok;
};

constexpr ArgSpec kCountArgs[] = {
    Arg::u32("by", 1, 1000),
};

}  // namespace

LMP_FIELDS(ProbeCfg,
  LMP_F(label, "label", Str(0, 16, "probe")),
  LMP_F(every, "every", U16(1, 3600, 30)),
  LMP_F(token, "token", Str(0, 24, "").secret()))

LMP_FIELDS(ProbeState,
  LMP_F(count, "count", U32()),
  LMP_F(ok,    "ok",    Bool(true)))

namespace {

// A module that counts what happens to it.
class Probe : public sdk::Module {
  LMP_MODULE("probe", "Probe", "P", "1.2.3", sdk::ModuleKind::Headless, 100)

 public:
  Status initResult = Status::Ok;
  Status startResult = Status::Ok;
  int inits = 0, starts = 0, ticks = 0, stops = 0, events = 0;
  uint32_t kept = 0;          // kept on file by the module itself
  sdk::ModuleContext* seen = nullptr;
  sdk::ModuleConfig<ProbeCfg> cfg;
  sdk::State<ProbeState> data;

  Status onInit(sdk::ModuleContext& c) override {
    ++inits;
    seen = &c;
    if (initResult != Status::Ok) return initResult;
    LMP_TRY(useSettings(cfg));
    LMP_TRY(use(data));
    c.commands->add("count", "Counts", kCountArgs,
                    [this](const Args& a, CmdContext& ctx) -> Status {
                      data->count += a.u32("by");
                      data.touch();
                      if (ctx.result) ctx.result->u32("count", data->count);
                      return Status::Ok;
                    });
    c.commands->add("keep", "Keeps a number on file", kCountArgs,
                    [this](const Args& a, CmdContext&) -> Status {
                      kept = a.u32("by");
                      markUnsaved();
                      return Status::Ok;
                    });
    c.commands->add("break", "The module says it no longer works",
                    [this](const Args&, CmdContext&) -> Status {
                      setState(ModuleState::Degraded, "sensor lost");
                      return Status::Ok;
                    });
    return Status::Ok;
  }
  Status onStart() override {
    ++starts;
    return startResult;
  }
  void onTick(uint32_t) override { ++ticks; }
  void onEvent(const Event&) override { ++events; }
  void onStop() override { ++stops; }

  void describe(JsonWriter& w) const override { w.u32("kept", kept); }
  void save(JsonWriter& w) const override { w.u32("kept", kept); }
  void load(const JsonValue& doc) override {
    kept = static_cast<uint32_t>(doc.get("kept").asInt(0));
  }
};

// A second module, with another name and nothing else.
class Other : public sdk::Module {
  LMP_MODULE("other", "Other", "O", "0.1.0", sdk::ModuleKind::Bridge, 0)

 public:
  int ticks = 0;
  Status onInit(sdk::ModuleContext& c) override {
    c.commands->add("ask", "Asks the probe to count",
                    [this](const Args&, CmdContext&) -> Status {
                      return ctx().modules->command("probe", "count",
                                                    JsonValue::parse("{\"by\":5}"));
                    });
    return Status::Ok;
  }
  void onTick(uint32_t) override { ++ticks; }
};

class BadName : public sdk::Module {
  LMP_MODULE("Bad Name", "Bad", "B", "0.1.0", sdk::ModuleKind::Headless, 0)
 public:
  Status onInit(sdk::ModuleContext&) override { return Status::Ok; }
};

// Two outputs on pins of the PC.
class Relay : public sdk::Module {
  LMP_MODULE("relay", "Relay", "R", "1.0.0", sdk::ModuleKind::Hardware, 50)

 public:
  sdk::PinBank<2> pins;
  Status onInit(sdk::ModuleContext& c) override {
    static const uint8_t map[2] = {30, 31};
    LMP_TRY(pins.declare(c, map, hal::PinMode::Output, "Line %u"));
    return use(pins);
  }
};

// Writes a file on the storage of the board, before the system starts.
void putFile(TestBoard& tb, const char* path, const char* content) {
  tb.native().fs.mount(true);
  tb.native().fs.mkdir("/cfg");
  tb.native().fs.mkdir("/cfg/m");
  std::ofstream out(tb.native().fs.root() + path, std::ios::binary);
  out << content;
  TEST_ASSERT_TRUE(out.good());
  out.close();
  tb.native().fs.unmount();
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

}  // namespace

// ---------------------------------------------------------------- life

void test_a_module_goes_through_its_life() {
  Probe probe;
  OnlineSystem sys(false, nullptr, 1500, {&probe});

  TEST_ASSERT_TRUE(sys.stateOf(ServiceId::Modules) == ServiceState::Ready);
  TEST_ASSERT_EQUAL_INT(1, probe.inits);
  TEST_ASSERT_EQUAL_INT(1, probe.starts);
  TEST_ASSERT_TRUE(probe.state() == ModuleState::Ready);
  TEST_ASSERT_EQUAL_STRING("", probe.reason());
  // Ticked every 100 ms, from the moment the network was ready.
  TEST_ASSERT_TRUE(probe.ticks >= 5 && probe.ticks <= 15);
  TEST_ASSERT_TRUE(probe.events > 0);
  TEST_ASSERT_EQUAL_INT(0, probe.stops);

  // What the module finds.
  TEST_ASSERT_NOT_NULL(probe.seen);
  TEST_ASSERT_TRUE(probe.seen->kernel == &sys.kernel());
  TEST_ASSERT_EQUAL_STRING("probe", probe.seen->manifest->name);
  TEST_ASSERT_NOT_NULL(probe.seen->gpio);
  TEST_ASSERT_NOT_NULL(probe.seen->clock);
  TEST_ASSERT_NOT_NULL(probe.seen->storage);
  TEST_ASSERT_NOT_NULL(probe.seen->bus);
  TEST_ASSERT_NOT_NULL(probe.seen->net);
  TEST_ASSERT_NOT_NULL(probe.seen->http);
  TEST_ASSERT_NOT_NULL(probe.seen->mqtt);
  TEST_ASSERT_NOT_NULL(probe.seen->modules);
  TEST_ASSERT_EQUAL_UINT(1, probe.seen->modules->count());
  TEST_ASSERT_TRUE(probe.seen->modules->find("probe") == &probe);
  TEST_ASSERT_NULL(probe.seen->modules->find("nobody"));

  // The service tells who is there.
  Doc list(sys.kernel(), "sys.modules");
  TEST_ASSERT_EQUAL_INT64(1, list.v["count"].asInt());
  TEST_ASSERT_EQUAL_INT64(LMP_MAX_MODULES, list.v["max"].asInt());
  const JsonValue entry = list.v["list"].at(0);
  TEST_ASSERT_TRUE(entry["name"].stringEquals("probe"));
  TEST_ASSERT_TRUE(entry["title"].stringEquals("Probe"));
  TEST_ASSERT_TRUE(entry["kind"].stringEquals("headless"));
  TEST_ASSERT_TRUE(entry["version"].stringEquals("1.2.3"));
  TEST_ASSERT_TRUE(entry["state"].stringEquals("Ready"));
  TEST_ASSERT_EQUAL_INT64(100, entry["tickMs"].asInt());
  TEST_ASSERT_TRUE(entry["settings"].asBool());

  // The state changed once, to Ready.
  Event last;
  TEST_ASSERT_TRUE(sys.count(EventId::ModuleState, &last) >= 1);
  TEST_ASSERT_EQUAL_UINT8(0, last.a);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ModuleState::Ready), last.b);
}

void test_the_modules_of_the_build() {
  // Nothing given by hand: the system has the modules the build chose.
  OnlineSystem sys(false);
  IModulesService* modules = sys.kernel().api<IModulesService>();
  TEST_ASSERT_NOT_NULL(modules);
  TEST_ASSERT_EQUAL_UINT(1, modules->count());
  sdk::Module* demo = modules->find("demo");
  TEST_ASSERT_NOT_NULL(demo);
  TEST_ASSERT_TRUE(demo->state() == ModuleState::Ready);

  Doc doc(sys.kernel(), "mod.demo");
  TEST_ASSERT_EQUAL_UINT(2, doc.v["pins"].size());
  TEST_ASSERT_EQUAL_INT64(40, doc.v["pins"].at(0)["gpio"].asInt());
  TEST_ASSERT_EQUAL_INT64(41, doc.v["pins"].at(1)["gpio"].asInt());
  TEST_ASSERT_TRUE(doc.v["pins"].at(0)["name"].stringEquals("Line 1"));
  TEST_ASSERT_TRUE(doc.v["schedules"].isArray());
  TEST_ASSERT_TRUE(doc.v["config"]["label"].stringEquals("Demo"));
  TEST_ASSERT_EQUAL_INT64(1000, doc.v["config"]["pulseMs"].asInt());
  TEST_ASSERT_TRUE(doc.v["presses"].valid());

  IGpio* pins = sys.kernel().api<IGpio>();
  TEST_ASSERT_EQUAL_STRING("demo", pins->ownerOf(40));
  TEST_ASSERT_EQUAL_STRING("demo", pins->ownerOf(41));
  TEST_ASSERT_EQUAL_STRING("demo", pins->ownerOf(42));

  // A pulse turns the channel on, and off when its time is over.
  TEST_ASSERT_TRUE(sys.command("mod.demo", "config.set", "{\"pulseMs\":300}") == Status::Ok);
  TEST_ASSERT_TRUE(sys.command("mod.demo", "pulse", "{\"channel\":1}") == Status::Ok);
  hal::PinLevel level = hal::PinLevel::Low;
  sys.pc().gpio.read(41, level);
  TEST_ASSERT_TRUE(level == hal::PinLevel::High);
  sys.advance(200);
  sys.pc().gpio.read(41, level);
  TEST_ASSERT_TRUE(level == hal::PinLevel::High);
  sys.advance(200);
  sys.pc().gpio.read(41, level);
  TEST_ASSERT_TRUE(level == hal::PinLevel::Low);

  // The button turns the first channel.
  sys.pc().gpio.setInput(42, hal::PinLevel::Low);
  sys.advance(200);
  sys.pc().gpio.read(40, level);
  TEST_ASSERT_TRUE(level == hal::PinLevel::High);
  sys.pc().gpio.setInput(42, hal::PinLevel::High);
  sys.advance(200);
  Doc after(sys.kernel(), "mod.demo");
  TEST_ASSERT_EQUAL_INT64(1, after.v["presses"].asInt());
  TEST_ASSERT_EQUAL_INT64(1, after.v["pulses"].asInt());
  TEST_ASSERT_EQUAL_INT64(1, after.v["pins"].at(0)["level"].asInt());
}

void test_names_are_checked() {
  TestBoard tb;
  LoopMax app(tb.board);
  Probe probe, twin;
  BadName bad;
  TEST_ASSERT_TRUE(app.modules().add(bad) == Status::Invalid);
  TEST_ASSERT_TRUE(app.modules().add(probe) == Status::Ok);
  TEST_ASSERT_TRUE(app.modules().add(twin) == Status::Busy);
  TEST_ASSERT_EQUAL_UINT(1, app.modules().count());
}

void test_a_module_that_does_not_start() {
  Probe probe;
  Other other;
  probe.initResult = Status::Io;
  OnlineSystem sys(false, nullptr, 1500, {&probe, &other});

  // The service works in part, the other module works.
  TEST_ASSERT_TRUE(sys.stateOf(ServiceId::Modules) == ServiceState::Degraded);
  TEST_ASSERT_TRUE(probe.state() == ModuleState::Error);
  TEST_ASSERT_EQUAL_STRING("init failed", probe.reason());
  TEST_ASSERT_EQUAL_INT(0, probe.starts);
  TEST_ASSERT_EQUAL_INT(0, probe.ticks);
  TEST_ASSERT_EQUAL_INT(1, probe.stops);   // what it took before failing is given back
  TEST_ASSERT_TRUE(other.state() == ModuleState::Ready);
  TEST_ASSERT_TRUE(sys.logHas("probe: init failed"));

  Doc list(sys.kernel(), "sys.modules");
  TEST_ASSERT_TRUE(list.v["list"].at(0)["state"].stringEquals("Error"));
  TEST_ASSERT_TRUE(list.v["list"].at(0)["reason"].stringEquals("init failed"));
  TEST_ASSERT_TRUE(list.v["list"].at(1)["state"].stringEquals("Ready"));
}

void test_a_module_that_fails_at_start() {
  Probe probe;
  probe.startResult = Status::Timeout;
  OnlineSystem sys(false, nullptr, 1500, {&probe});

  TEST_ASSERT_TRUE(probe.state() == ModuleState::Error);
  TEST_ASSERT_EQUAL_STRING("start failed", probe.reason());
  TEST_ASSERT_EQUAL_INT(1, probe.starts);   // not asked again at every round
  TEST_ASSERT_EQUAL_INT(0, probe.ticks);
  TEST_ASSERT_EQUAL_INT(1, probe.stops);
  TEST_ASSERT_TRUE(sys.stateOf(ServiceId::Modules) == ServiceState::Degraded);
}

void test_a_module_without_ticks() {
  Other other;
  OnlineSystem sys(false, nullptr, 1500, {&other});
  TEST_ASSERT_TRUE(other.state() == ModuleState::Ready);
  TEST_ASSERT_EQUAL_INT(0, other.ticks);
}

void test_a_module_says_how_it_is() {
  Probe probe;
  OnlineSystem sys(false, nullptr, 1500, {&probe});
  sys.events.clear();

  TEST_ASSERT_TRUE(sys.command("mod.probe", "break", "") == Status::Ok);
  sys.advance(50);
  TEST_ASSERT_TRUE(probe.state() == ModuleState::Degraded);
  Event last;
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::ModuleState, &last));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ModuleState::Degraded), last.b);
  Doc list(sys.kernel(), "sys.modules");
  TEST_ASSERT_TRUE(list.v["list"].at(0)["state"].stringEquals("Degraded"));
  TEST_ASSERT_TRUE(list.v["list"].at(0)["reason"].stringEquals("sensor lost"));

  // A module that works in part is still ticked.
  const int before = probe.ticks;
  sys.advance(500);
  TEST_ASSERT_TRUE(probe.ticks > before);
}

// ---------------------------------------------------------------- commands and documents

void test_commands_and_document() {
  Probe probe;
  OnlineSystem sys(false, nullptr, 1500, {&probe});

  TEST_ASSERT_TRUE(sys.kernel().commands().hasCommand("mod.probe", "count"));
  TEST_ASSERT_TRUE(sys.kernel().commands().hasCommand("mod.probe", "config.set"));
  TEST_ASSERT_TRUE(sys.kernel().commands().hasCommand("mod.probe", "config.reset"));

  Json<512> answer;
  TEST_ASSERT_TRUE(sys.kernel().commands().answer("mod.probe", "count", "{\"by\":3}",
                                                  ArgsForm::Json, CmdSource::Local,
                                                  answer.w) == Status::Ok);
  TEST_ASSERT_EQUAL_INT64(3, answer.parse()["result"]["count"].asInt());

  const char* field = nullptr;
  TEST_ASSERT_TRUE(sys.command("mod.probe", "count", "{\"by\":0}", &field) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("by", field);

  // The document: the settings in their place, then the state, then what the module adds.
  Doc doc(sys.kernel(), "mod.probe");
  TEST_ASSERT_TRUE(doc.v["config"]["label"].stringEquals("probe"));
  TEST_ASSERT_EQUAL_INT64(30, doc.v["config"]["every"].asInt());
  TEST_ASSERT_EQUAL_INT64(3, doc.v["count"].asInt());
  TEST_ASSERT_TRUE(doc.v["ok"].asBool());
  TEST_ASSERT_EQUAL_INT64(0, doc.v["kept"].asInt());

  // The manifest says what the module is and how its settings are made.
  Json<4096> manifest;
  TEST_ASSERT_TRUE(sys.kernel().commands().manifest("mod.probe", manifest.w) == Status::Ok);
  const JsonValue m = manifest.parse();
  TEST_ASSERT_TRUE(m["target"].stringEquals("mod.probe"));
  TEST_ASSERT_TRUE(m["module"]["title"].stringEquals("Probe"));
  TEST_ASSERT_TRUE(m["module"]["kind"].stringEquals("headless"));
  TEST_ASSERT_TRUE(m["module"]["version"].stringEquals("1.2.3"));
  TEST_ASSERT_EQUAL_UINT(3, m["config"].size());
  TEST_ASSERT_TRUE(m["config"].at(1)["key"].stringEquals("every"));
  TEST_ASSERT_EQUAL_INT64(3600, m["config"].at(1)["max"].asInt());
  TEST_ASSERT_TRUE(m["commands"].size() >= 5);
}

void test_a_module_asks_another() {
  Probe probe;
  Other other;
  OnlineSystem sys(false, nullptr, 1500, {&probe, &other});

  TEST_ASSERT_TRUE(sys.command("mod.other", "ask", "") == Status::Ok);
  TEST_ASSERT_EQUAL_UINT32(5, probe.data->count);
  TEST_ASSERT_TRUE(other.state() == ModuleState::Ready);
  TEST_ASSERT_TRUE(probe.seen->modules->command("nobody", "count", JsonValue()) ==
                   Status::NotFound);
}

void test_changes_close_to_each_other_are_told_once() {
  Probe probe;
  OnlineSystem sys(false, nullptr, 1500, {&probe});
  sys.events.clear();

  for (int i = 0; i < 5; ++i) {
    TEST_ASSERT_TRUE(sys.command("mod.probe", "count", "{\"by\":1}") == Status::Ok);
    sys.advance(20);
  }
  TEST_ASSERT_EQUAL_INT(0, sys.count(EventId::ModuleData));
  sys.advance(LMP_MODULE_DATA_DELAY_MS + 50);
  Event last;
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::ModuleData, &last));
  TEST_ASSERT_EQUAL_UINT8(0, last.a);

  // Nothing changes, nothing is told. The state is not something to keep: no file.
  sys.advance(LMP_CONFIG_SAVE_DELAY_MS + 1000);
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::ModuleData));
  TEST_ASSERT_EQUAL_STRING("", fileOf(sys, "probe").c_str());
}

// ---------------------------------------------------------------- settings

void test_settings_are_changed_and_checked() {
  Probe probe;
  OnlineSystem sys(false, nullptr, 1500, {&probe});
  const char* field = nullptr;

  TEST_ASSERT_TRUE(sys.command("mod.probe", "config.set",
                               "{\"label\":\"cellar\",\"every\":60}") == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("cellar", probe.cfg->label.c_str());
  TEST_ASSERT_EQUAL_UINT16(60, probe.cfg->every);

  // One value only: the others stay.
  TEST_ASSERT_TRUE(sys.command("mod.probe", "config.set", "{\"every\":90}") == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("cellar", probe.cfg->label.c_str());
  TEST_ASSERT_EQUAL_UINT16(90, probe.cfg->every);

  // A value out of its limits: nothing changes, and the answer says which one.
  TEST_ASSERT_TRUE(sys.command("mod.probe", "config.set",
                               "{\"label\":\"attic\",\"every\":9000}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("every", field);
  TEST_ASSERT_EQUAL_STRING("cellar", probe.cfg->label.c_str());
  TEST_ASSERT_EQUAL_UINT16(90, probe.cfg->every);

  TEST_ASSERT_TRUE(sys.command("mod.probe", "config.set", "{\"label\":5}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("label", field);

  // No arguments, or names that are not settings.
  TEST_ASSERT_TRUE(sys.command("mod.probe", "config.set", "") == Status::Invalid);
  TEST_ASSERT_TRUE(sys.command("mod.probe", "config.set", "{}") == Status::Invalid);
  TEST_ASSERT_TRUE(sys.command("mod.probe", "config.set", "{\"colour\":\"red\"}") ==
                   Status::Invalid);

  // From the command line too.
  CmdContext ctx;
  TEST_ASSERT_TRUE(sys.kernel().commands().executeText("mod.probe", "config.set",
                                                       "every=45 label=\"two words\"",
                                                       ctx) == Status::Ok);
  TEST_ASSERT_EQUAL_UINT16(45, probe.cfg->every);
  TEST_ASSERT_EQUAL_STRING("two words", probe.cfg->label.c_str());

  TEST_ASSERT_TRUE(sys.command("mod.probe", "config.reset", "") == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("probe", probe.cfg->label.c_str());
  TEST_ASSERT_EQUAL_UINT16(30, probe.cfg->every);
}

void test_secrets_of_a_module_are_never_shown() {
  Probe probe;
  OnlineSystem sys(false, nullptr, 1500, {&probe});

  TEST_ASSERT_TRUE(sys.command("mod.probe", "config.set",
                               "{\"token\":\"a-truly-secret-token\"}") == Status::Ok);
  sys.advance(LMP_CONFIG_SAVE_DELAY_MS + 200);

  Doc doc(sys.kernel(), "mod.probe");
  TEST_ASSERT_NULL(strstr(doc.text(), "a-truly-secret-token"));
  TEST_ASSERT_TRUE(doc.v["config"]["tokenSet"].asBool());
  TEST_ASSERT_FALSE(doc.v["config"]["token"].valid());

  Json<1024> cfg;
  cfg.w.beginObject();
  TEST_ASSERT_TRUE(sys.app.modules().describeConfig("probe", cfg.w) == Status::Ok);
  cfg.w.endObject();
  TEST_ASSERT_NULL(strstr(cfg.text(), "a-truly-secret-token"));
  TEST_ASSERT_TRUE(cfg.parse()["tokenSet"].asBool());
  TEST_ASSERT_TRUE(cfg.parse()["label"].stringEquals("probe"));
  TEST_ASSERT_TRUE(sys.app.modules().describeConfig("nobody", cfg.w) == Status::NotFound);

  // On file it is written: it is where it lives.
  TEST_ASSERT_NOT_NULL(strstr(fileOf(sys, "probe").c_str(), "a-truly-secret-token"));
  TEST_ASSERT_FALSE(sys.logHas("a-truly-secret-token"));
}

// ---------------------------------------------------------------- file

void test_what_is_kept_survives_a_restart() {
  TestBoard tb;
  {
    Probe probe;
    LoopMax app(tb.board);
    Logger::setInstance(&app.kernel().log());
    TEST_ASSERT_TRUE(app.modules().add(probe) == Status::Ok);
    TEST_ASSERT_TRUE(app.begin() == Status::Ok);
    run(app.kernel(), tb.clock, 1500);

    CmdContext ctx;
    TEST_ASSERT_TRUE(app.kernel().commands().execute(
                         "mod.probe", "config.set",
                         JsonValue::parse("{\"label\":\"cellar\",\"every\":60}"), ctx) ==
                     Status::Ok);
    TEST_ASSERT_TRUE(app.kernel().commands().execute("mod.probe", "keep",
                                                     JsonValue::parse("{\"by\":77}"), ctx) ==
                     Status::Ok);
    TEST_ASSERT_TRUE(app.kernel().commands().execute("mod.probe", "count",
                                                     JsonValue::parse("{\"by\":9}"), ctx) ==
                     Status::Ok);
    // Not at once: after the same wait of the settings of the system.
    IStorage* storage = app.kernel().api<IStorage>();
    TEST_ASSERT_FALSE(storage->exists("/cfg/m/probe.json"));
    run(app.kernel(), tb.clock, LMP_CONFIG_SAVE_DELAY_MS + 200);
    TEST_ASSERT_TRUE(storage->exists("/cfg/m/probe.json"));
    Logger::setInstance(nullptr);
  }

  // Same storage, new system and new module: as after a power cycle.
  tb.native().fs.unmount();
  Probe probe;
  LoopMax app(tb.board);
  Logger::setInstance(&app.kernel().log());
  TEST_ASSERT_TRUE(app.modules().add(probe) == Status::Ok);
  TEST_ASSERT_TRUE(app.begin() == Status::Ok);
  run(app.kernel(), tb.clock, 1500);

  TEST_ASSERT_TRUE(probe.state() == ModuleState::Ready);
  TEST_ASSERT_EQUAL_STRING("cellar", probe.cfg->label.c_str());
  TEST_ASSERT_EQUAL_UINT16(60, probe.cfg->every);
  TEST_ASSERT_EQUAL_UINT32(77, probe.kept);
  // The state is of the moment: it starts again.
  TEST_ASSERT_EQUAL_UINT32(0, probe.data->count);
}

void test_a_file_that_cannot_be_read() {
  TestBoard tb;
  putFile(tb, "/cfg/m/probe.json", "{\"v\":1,\"config\":{\"label\":\"cell");

  Probe probe;
  LoopMax app(tb.board);
  Logger::setInstance(&app.kernel().log());
  TEST_ASSERT_TRUE(app.modules().add(probe) == Status::Ok);
  TEST_ASSERT_TRUE(app.begin() == Status::Ok);
  run(app.kernel(), tb.clock, 1500);

  // The module starts with the settings it was born with.
  TEST_ASSERT_TRUE(probe.state() == ModuleState::Ready);
  TEST_ASSERT_EQUAL_STRING("probe", probe.cfg->label.c_str());
  TEST_ASSERT_EQUAL_UINT16(30, probe.cfg->every);
}

void test_old_values_leave_the_defaults() {
  TestBoard tb;
  // A value that the firmware of today refuses.
  putFile(tb, "/cfg/m/probe.json",
          "{\"v\":1,\"config\":{\"label\":\"cellar\",\"every\":9000},\"kept\":4}");

  Probe probe;
  LoopMax app(tb.board);
  Logger::setInstance(&app.kernel().log());
  TEST_ASSERT_TRUE(app.modules().add(probe) == Status::Ok);
  TEST_ASSERT_TRUE(app.begin() == Status::Ok);
  run(app.kernel(), tb.clock, 1500);

  TEST_ASSERT_TRUE(probe.state() == ModuleState::Ready);
  TEST_ASSERT_EQUAL_STRING("probe", probe.cfg->label.c_str());
  TEST_ASSERT_EQUAL_UINT16(30, probe.cfg->every);
  TEST_ASSERT_EQUAL_UINT32(4, probe.kept);   // the rest of the file is good
}

// ---------------------------------------------------------------- restart and reset

void test_a_module_is_restarted() {
  Probe probe;
  Relay relay;
  OnlineSystem sys(false, nullptr, 1500, {&probe, &relay});
  const size_t commands = sys.kernel().commands().commandCount();
  const size_t targets = sys.kernel().commands().targetCount();

  TEST_ASSERT_TRUE(sys.command("mod.probe", "config.set", "{\"every\":60}") == Status::Ok);
  TEST_ASSERT_TRUE(sys.command("mod.probe", "count", "{\"by\":2}") == Status::Ok);
  TEST_ASSERT_TRUE(sys.command("mod.relay", "set", "{\"channel\":1,\"value\":true}") ==
                   Status::Ok);

  const char* field = nullptr;
  TEST_ASSERT_TRUE(sys.command("sys.modules", "restart", "{\"module\":\"nobody\"}", &field) ==
                   Status::NotFound);
  TEST_ASSERT_EQUAL_STRING("module", field);

  TEST_ASSERT_TRUE(sys.command("sys.modules", "restart", "{\"module\":\"probe\"}") ==
                   Status::Ok);
  TEST_ASSERT_EQUAL_INT(1, probe.stops);
  TEST_ASSERT_EQUAL_INT(2, probe.inits);
  TEST_ASSERT_EQUAL_INT(2, probe.starts);
  TEST_ASSERT_TRUE(probe.state() == ModuleState::Ready);
  // What was changed a moment before was written, and read back.
  TEST_ASSERT_EQUAL_UINT16(60, probe.cfg->every);
  // Same target, same commands: nothing is registered twice.
  TEST_ASSERT_EQUAL_UINT(commands, sys.kernel().commands().commandCount());
  TEST_ASSERT_EQUAL_UINT(targets, sys.kernel().commands().targetCount());
  TEST_ASSERT_TRUE(sys.command("mod.probe", "count", "{\"by\":2}") == Status::Ok);
  TEST_ASSERT_TRUE(sys.command("mod.relay", "toggle", "{\"channel\":0}") == Status::Ok);

  // A module with pins gives them back and takes them again.
  TEST_ASSERT_TRUE(sys.command("sys.modules", "restart", "{\"module\":\"relay\"}") ==
                   Status::Ok);
  TEST_ASSERT_TRUE(relay.state() == ModuleState::Ready);
  IGpio* pins = sys.kernel().api<IGpio>();
  TEST_ASSERT_EQUAL_STRING("relay", pins->ownerOf(30));
  // The channels come back as they were.
  TEST_ASSERT_TRUE(relay.pins.on(0));
  TEST_ASSERT_TRUE(relay.pins.on(1));
  sys.advance(500);
  const int ticks = probe.ticks;
  sys.advance(500);
  TEST_ASSERT_TRUE(probe.ticks > ticks);
}

void test_a_restart_of_the_device_writes_the_files() {
  Probe probe;
  OnlineSystem sys(false, nullptr, 1500, {&probe});
  TEST_ASSERT_TRUE(sys.command("mod.probe", "keep", "{\"by\":12}") == Status::Ok);
  sys.advance(100);
  TEST_ASSERT_EQUAL_STRING("", fileOf(sys, "probe").c_str());

  TEST_ASSERT_TRUE(sys.command("sys.system", "restart", "") == Status::Ok);
  sys.advance(100);
  TEST_ASSERT_NOT_NULL(strstr(fileOf(sys, "probe").c_str(), "\"kept\":12"));
}

void test_a_factory_reset_takes_the_files_away() {
  Probe probe;
  OnlineSystem sys(false, nullptr, 1500, {&probe});
  TEST_ASSERT_TRUE(sys.command("mod.probe", "keep", "{\"by\":12}") == Status::Ok);
  sys.advance(LMP_CONFIG_SAVE_DELAY_MS + 200);
  TEST_ASSERT_NOT_NULL(strstr(fileOf(sys, "probe").c_str(), "\"kept\":12"));

  // Something changes a moment before the reset: it must not come back on file.
  TEST_ASSERT_TRUE(sys.command("mod.probe", "keep", "{\"by\":13}") == Status::Ok);
  TEST_ASSERT_TRUE(sys.command("sys.system", "factoryReset", "") == Status::Ok);
  sys.advance(LMP_CONFIG_SAVE_DELAY_MS + 1000);
  TEST_ASSERT_EQUAL_STRING("", fileOf(sys, "probe").c_str());
}

void test_the_file_is_written_by_the_worker() {
  Probe probe;
  OnlineSystem sys(false, nullptr, 1500, {&probe});
  const uint32_t before = sys.kernel().worker().done();
  TEST_ASSERT_TRUE(sys.command("mod.probe", "keep", "{\"by\":21}") == Status::Ok);
  sys.advance(LMP_CONFIG_SAVE_DELAY_MS + 200);
  // The round of the system prepared the text; the worker wrote it.
  TEST_ASSERT_EQUAL_UINT32(before + 1, sys.kernel().worker().done());
  TEST_ASSERT_NOT_NULL(strstr(fileOf(sys, "probe").c_str(), "\"kept\":21"));

  // Many changes close together: one file, written once.
  for (int i = 0; i < 5; ++i) {
    TEST_ASSERT_TRUE(sys.command("mod.probe", "keep", "{\"by\":30}") == Status::Ok);
    sys.advance(100);
  }
  sys.advance(LMP_CONFIG_SAVE_DELAY_MS + 200);
  TEST_ASSERT_EQUAL_UINT32(before + 2, sys.kernel().worker().done());
  TEST_ASSERT_NOT_NULL(strstr(fileOf(sys, "probe").c_str(), "\"kept\":30"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_module_goes_through_its_life);
  RUN_TEST(test_the_modules_of_the_build);
  RUN_TEST(test_names_are_checked);
  RUN_TEST(test_a_module_that_does_not_start);
  RUN_TEST(test_a_module_that_fails_at_start);
  RUN_TEST(test_a_module_without_ticks);
  RUN_TEST(test_a_module_says_how_it_is);
  RUN_TEST(test_commands_and_document);
  RUN_TEST(test_a_module_asks_another);
  RUN_TEST(test_changes_close_to_each_other_are_told_once);
  RUN_TEST(test_settings_are_changed_and_checked);
  RUN_TEST(test_secrets_of_a_module_are_never_shown);
  RUN_TEST(test_what_is_kept_survives_a_restart);
  RUN_TEST(test_a_file_that_cannot_be_read);
  RUN_TEST(test_old_values_leave_the_defaults);
  RUN_TEST(test_a_module_is_restarted);
  RUN_TEST(test_a_restart_of_the_device_writes_the_files);
  RUN_TEST(test_a_factory_reset_takes_the_files_away);
  RUN_TEST(test_the_file_is_written_by_the_worker);
  return UNITY_END();
}
