// Tests of the whole system on the PC: the kernel with the real services.
#include <string.h>
#include <unity.h>

#include <string>

#include "../support/test_support.h"
#include "app/loopmax.h"

using namespace lmp;
using namespace lmp::test;

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

constexpr int64_t kSomeTime = 1790000000;  // 21 September 2026

// The PC build has everything, simulated: the base services, the buses, the LoRa radio,
// network and web, the services that need the network, and the one that hosts the modules.
constexpr unsigned kServices =
    8 + (LMP_FEATURE_BUS ? 1 : 0) + (LMP_FEATURE_LORA ? 1 : 0) +
    (LMP_FEATURE_NET ? 2 : 0) +
    (LMP_FEATURE_HTTPCLIENT ? 1 : 0) + (LMP_FEATURE_MQTT ? 1 : 0) + (LMP_FEATURE_OTA ? 1 : 0) +
    (LMP_FEATURE_GEO ? 1 : 0) + (LMP_FEATURE_CLOUD ? 1 : 0) + (LMP_FEATURE_MODULES ? 1 : 0) +
    (LMP_FEATURE_PROBE ? 1 : 0);
// The build of the PC has one module, the demo: a target more than the services.
constexpr unsigned kModules = LMP_FEATURE_MODULES ? 1 : 0;

struct System {
  TestBoard tb;
  LoopMax app{tb.board};

  System() {
    Logger::setInstance(&app.kernel().log());
    TEST_ASSERT_TRUE(app.begin() == Status::Ok);
    run(app.kernel(), tb.clock, 200);
    tb.output();
  }

  Kernel& kernel() { return app.kernel(); }

  // Sends a line to the command line and returns what comes back.
  std::string cli(const char* line) {
    tb.output();
    app.serial().execute(line);
    return tb.output();
  }

  // Last line of the answer, parsed as JSON.
  static JsonValue lastJson(const std::string& text, std::string& storage) {
    size_t end = text.find_last_not_of("\r\n");
    if (end == std::string::npos) return JsonValue();
    size_t start = text.rfind('\n', end);
    start = start == std::string::npos ? 0 : start + 1;
    storage = text.substr(start, end - start + 1);
    return JsonValue::parse(StringView(storage.data(), storage.size()));
  }

  Status command(const char* target, const char* name, const char* json) {
    CmdContext ctx;
    ctx.source = CmdSource::Local;
    return kernel().commands().execute(target, name, JsonValue::parse(json), ctx);
  }
};

ServiceState stateOf(Kernel& kernel, ServiceId id) {
  Service* s = kernel.services().find(id);
  return s ? s->state() : ServiceState::Stopped;
}

}  // namespace

// ---------------------------------------------------------------- startup

void test_every_service_starts() {
  System sys;
  const ServiceId ready[] = {ServiceId::System, ServiceId::Log, ServiceId::Serial,
                             ServiceId::Storage, ServiceId::Config, ServiceId::Gpio};
  for (ServiceId id : ready) {
    TEST_ASSERT_TRUE_MESSAGE(stateOf(sys.kernel(), id) == ServiceState::Ready,
                             sys.kernel().services().find(id)->info().name);
  }
  // The clock has no time yet, the PC board has a reset button on the configured pin or none.
  TEST_ASSERT_TRUE(stateOf(sys.kernel(), ServiceId::Clock) == ServiceState::Degraded);
  TEST_ASSERT_TRUE(sys.kernel().services().find(ServiceId::Reset)->running());
  TEST_ASSERT_TRUE(sys.kernel().log().bootDoneMs() > 0);
  TEST_ASSERT_TRUE(sys.kernel().config().loaded());
}

void test_boot_log_reaches_the_serial_port() {
  TestBoard tb;
  LoopMax app(tb.board);
  Logger::setInstance(&app.kernel().log());
  app.begin();
  run(app.kernel(), tb.clock, 200);
  const std::string out = tb.output();
  // Written before the serial service started, printed when it came up.
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "LoopMaxPro"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "first start: device key created"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "boot done"));
}

void test_targets_of_the_base_system() {
  System sys;
  std::string storage;
  const JsonValue list = System::lastJson(sys.cli("targets"), storage);
  TEST_ASSERT_TRUE(list.isArray());
  TEST_ASSERT_EQUAL_UINT(kServices + kModules, list.size());
  TEST_ASSERT_TRUE(list.at(0).stringEquals("sys.system"));
#if LMP_FEATURE_MODULES
  TEST_ASSERT_TRUE(list.at(list.size() - 1).stringEquals("mod.demo"));
#endif
}

// ---------------------------------------------------------------- report and documents

void test_report() {
  System sys;
  Json<8192> out;
  sys.kernel().report(out.w, StringView(), true);
  TEST_ASSERT_TRUE(out.w.complete());
  const JsonValue r = out.parse();
  TEST_ASSERT_TRUE(r.valid());

  TEST_ASSERT_EQUAL_UINT(kDeviceKeyLength, r["device"]["key"].stringLength());
  TEST_ASSERT_TRUE(r["device"]["board"].stringEquals("native"));
  TEST_ASSERT_TRUE(r["device"]["lang"].stringEquals("en"));
  TEST_ASSERT_TRUE(r["system"]["chip"].stringEquals("PC"));
  TEST_ASSERT_TRUE(r["system"]["heap"]["free"].asInt() > 0);
  TEST_ASSERT_TRUE(r["system"]["resetReason"].stringEquals("poweron"));
  TEST_ASSERT_FALSE(r["clock"]["synced"].asBool(true));
  TEST_ASSERT_TRUE(r["storage"]["fs"]["mounted"].asBool());
  TEST_ASSERT_TRUE(r["storage"]["kv"]["ok"].asBool());
  TEST_ASSERT_EQUAL_UINT(kServices, r["services"].size());
}

void test_documents_never_show_secrets() {
  System sys;
  sys.kernel().config().net.staPsw.assign("home-password");
  sys.kernel().config().mqtt.psw.assign("home-password");
  const std::string key = sys.kernel().config().sys.key.c_str();

  const char* targets[] = {"sys.system", "sys.log", "sys.serial", "sys.storage",
                           "sys.config", "sys.gpio", "sys.clock", "sys.reset",
#if LMP_FEATURE_BUS
                           "sys.bus",
#endif
#if LMP_FEATURE_LORA
                           "sys.lora",
#endif
#if LMP_FEATURE_NET
                           "sys.net", "sys.web",
#endif
#if LMP_FEATURE_HTTPCLIENT
                           "sys.httpclient",
#endif
#if LMP_FEATURE_MQTT
                           "sys.mqtt",
#endif
#if LMP_FEATURE_OTA
                           "sys.ota",
#endif
#if LMP_FEATURE_GEO
                           "sys.geo",
#endif
#if LMP_FEATURE_CLOUD
                           "sys.cloud",
#endif
#if LMP_FEATURE_MODULES
                           "sys.modules", "mod.demo",
#endif
  };
  for (const char* target : targets) {
    Json<4096> out;
    out.w.beginObject();
    TEST_ASSERT_TRUE(sys.kernel().commands().describe(target, out.w) == Status::Ok);
    out.w.endObject();
    TEST_ASSERT_TRUE_MESSAGE(out.w.complete(), target);
    TEST_ASSERT_TRUE_MESSAGE(out.parse().valid(), target);
    TEST_ASSERT_NULL_MESSAGE(strstr(out.text(), "home-password"), target);
    TEST_ASSERT_NULL_MESSAGE(strstr(out.text(), "loopmaxap"), target);
    TEST_ASSERT_NULL_MESSAGE(strstr(out.text(), key.c_str()), target);
  }

  Json<4096> report;
  sys.kernel().report(report.w);
  TEST_ASSERT_NULL(strstr(report.text(), key.c_str()));
}

// ---------------------------------------------------------------- settings

void test_settings_change_and_survive_a_restart() {
  TestBoard tb;
  std::string key;
  {
    LoopMax app(tb.board);
    Logger::setInstance(&app.kernel().log());
    app.begin();
    run(app.kernel(), tb.clock, 200);
    key = app.kernel().config().sys.key.c_str();

    CmdContext ctx;
    TEST_ASSERT_TRUE(app.kernel().commands().execute(
        "sys.config", "set",
        JsonValue::parse("{\"lang\":\"it\",\"deviceName\":\"Office Plugs\",\"hostname\":\"plugs\"}"),
        ctx) == Status::Ok);
    TEST_ASSERT_TRUE(app.kernel().config().dirty());
    // Written after the delay, by the config service.
    run(app.kernel(), tb.clock, LMP_CONFIG_SAVE_DELAY_MS + 1200);
    TEST_ASSERT_FALSE(app.kernel().config().dirty());
    Logger::setInstance(nullptr);
  }

  // Same storage, new system: as after a power cycle.
  tb.native().fs.unmount();
  LoopMax app(tb.board);
  Logger::setInstance(&app.kernel().log());
  app.begin();
  run(app.kernel(), tb.clock, 200);
  const Config& config = app.kernel().config();
  TEST_ASSERT_FALSE(config.firstBoot());
  TEST_ASSERT_EQUAL_STRING(key.c_str(), config.sys.key.c_str());
  TEST_ASSERT_EQUAL_STRING("it", config.sys.lang.c_str());
  TEST_ASSERT_EQUAL_STRING("Office Plugs", config.sys.deviceName.c_str());
  TEST_ASSERT_EQUAL_STRING("plugs", config.sys.hostname.c_str());
}

void test_settings_are_validated() {
  System sys;
  struct Case {
    const char* json;
    const char* field;
  };
  const Case cases[] = {
      {"{\"lang\":\"IT\"}", "lang"},
      {"{\"lang\":\"i\"}", "lang"},
      {"{\"hostname\":\"-plugs\"}", "hostname"},
      {"{\"hostname\":\"office plugs\"}", "hostname"},
      {"{\"hostname\":\"\"}", "hostname"},
  };
  for (const Case& c : cases) {
    CmdContext ctx;
    const Status s = sys.kernel().commands().execute("sys.config", "set", JsonValue::parse(c.json),
                                                     ctx);
    TEST_ASSERT_TRUE_MESSAGE(s == Status::Invalid, c.json);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c.field, ctx.errorField, c.json);
  }
  TEST_ASSERT_EQUAL_STRING("en", sys.kernel().config().sys.lang.c_str());
  TEST_ASSERT_TRUE(sys.command("sys.config", "set", "{}") == Status::Invalid);

  // One bad value stops the whole request.
  TEST_ASSERT_TRUE(sys.command("sys.config", "set", "{\"lang\":\"it\",\"hostname\":\"-x\"}") ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("en", sys.kernel().config().sys.lang.c_str());
}

// ---------------------------------------------------------------- clock

void test_clock_is_set_by_command() {
  System sys;
  IClockService* clock = sys.kernel().api<IClockService>();
  TEST_ASSERT_NOT_NULL(clock);
  TEST_ASSERT_FALSE(clock->synced());
  TEST_ASSERT_EQUAL_INT64(0, clock->unixTime());

  int synced = 0;
  sys.kernel().events().subscribe([&synced](const Event& e) {
    if (e.id == EventId::TimeSynced) ++synced;
  });

  TEST_ASSERT_TRUE(sys.command(
      "sys.clock", "set",
      "{\"unix\":1790000000,\"tz\":\"Europe/Rome\",\"posix\":\"CET-1CEST,M3.5.0,M10.5.0/3\"}") ==
                   Status::Ok);
  run(sys.kernel(), sys.tb.clock, 50);

  TEST_ASSERT_TRUE(clock->synced());
  TEST_ASSERT_EQUAL_INT64(kSomeTime, clock->unixTime());
  TEST_ASSERT_TRUE(stateOf(sys.kernel(), ServiceId::Clock) == ServiceState::Ready);
  TEST_ASSERT_EQUAL_INT(1, synced);
  TEST_ASSERT_EQUAL_STRING("Europe/Rome", sys.kernel().config().clock.tz.c_str());

  hal::DateTime t;
  TEST_ASSERT_TRUE(clock->localTime(t));
  TEST_ASSERT_EQUAL_UINT16(2026, t.year);
  TEST_ASSERT_EQUAL_UINT8(9, t.month);
  TEST_ASSERT_EQUAL_UINT8(21, t.day);
  TEST_ASSERT_EQUAL_UINT8(16, t.hour);  // summer time: two hours ahead of UTC
  TEST_ASSERT_EQUAL_INT32(7200, t.utcOffset);
}

void test_clock_rejects_bad_values() {
  System sys;
  TEST_ASSERT_TRUE(sys.command("sys.clock", "set", "{\"unix\":1000}") == Status::Invalid);
  TEST_ASSERT_TRUE(sys.command("sys.clock", "set", "{\"unix\":9999999999}") == Status::Invalid);
  TEST_ASSERT_TRUE(sys.command("sys.clock", "set", "{}") == Status::Invalid);
  TEST_ASSERT_TRUE(sys.command("sys.clock", "set", "{\"tz\":\"Europe/Rome\"}") == Status::Invalid);
  TEST_ASSERT_FALSE(sys.kernel().api<IClockService>()->synced());
}

// ---------------------------------------------------------------- log

void test_log_commands() {
  System sys;
  Logger& log = sys.kernel().log();
  TEST_ASSERT_TRUE(log.count() > 0);

  TEST_ASSERT_TRUE(sys.command("sys.log", "setLevel", "{\"level\":\"debug\",\"source\":\"clock\"}") ==
                   Status::Ok);
  LogSource clock = kLogSys;
  TEST_ASSERT_TRUE(log.findSource("clock", clock));
  TEST_ASSERT_TRUE(log.level(clock) == LogLevel::Debug);
  TEST_ASSERT_TRUE(log.level(kLogSys) == LogLevel::Info);

  TEST_ASSERT_TRUE(sys.command("sys.log", "setLevel", "{\"level\":\"warn\"}") == Status::Ok);
  TEST_ASSERT_TRUE(log.level(clock) == LogLevel::Warn);

  TEST_ASSERT_TRUE(sys.command("sys.log", "setLevel", "{\"level\":\"loud\"}") == Status::Invalid);
  TEST_ASSERT_TRUE(sys.command("sys.log", "setLevel", "{\"level\":\"info\",\"source\":\"none\"}") ==
                   Status::NotFound);

  TEST_ASSERT_TRUE(sys.command("sys.log", "clear", "{}") == Status::Ok);
  TEST_ASSERT_EQUAL_UINT(0, log.count());
}

void test_log_reading_is_paged() {
  System sys;
  Logger& log = sys.kernel().log();
  log.setLevel(LogLevel::Info);
  log.clear();
  const LogSource net = log.addSource("net");
  const uint32_t first = log.lastSeq();
  for (int i = 1; i <= 10; ++i) log.write(LogLevel::Info, net, "entry %d", i);
  log.write(LogLevel::Warn, kLogSys, "a warning with \"quotes\"");

  ILog* api = sys.kernel().api<ILog>();
  TEST_ASSERT_NOT_NULL(api);

  ILog::Query q;
  q.since = first;
  q.max = 4;
  Json<2048> page;
  api->writeEntries(page.w, q);
  JsonValue v = page.parse();
  TEST_ASSERT_TRUE(v.valid());
  TEST_ASSERT_EQUAL_UINT(4, v["items"].size());
  TEST_ASSERT_TRUE(v["items"].at(0)["msg"].stringEquals("entry 1"));
  TEST_ASSERT_TRUE(v["items"].at(0)["src"].stringEquals("net"));
  TEST_ASSERT_EQUAL_INT64(log.lastSeq(), v["seqLast"].asInt());

  // Next page: from the last sequence number received.
  q.since = static_cast<uint32_t>(v["items"].at(3)["seq"].asInt());
  Json<2048> next;
  api->writeEntries(next.w, q);
  v = next.parse();
  TEST_ASSERT_TRUE(v["items"].at(0)["msg"].stringEquals("entry 5"));

  // Filters.
  q = ILog::Query();
  q.since = first;
  q.level = LogLevel::Warn;
  Json<2048> warnings;
  api->writeEntries(warnings.w, q);
  v = warnings.parse();
  TEST_ASSERT_TRUE(v.valid());
  TEST_ASSERT_EQUAL_UINT(1, v["items"].size());
  TEST_ASSERT_TRUE(v["items"].at(0)["msg"].stringEquals("a warning with \"quotes\""));

  q = ILog::Query();
  q.since = first;
  q.source = "net";
  Json<4096> onlyNet;
  api->writeEntries(onlyNet.w, q);
  TEST_ASSERT_EQUAL_UINT(10, onlyNet.parse()["items"].size());

  q.source = "unknown";
  Json<512> none;
  api->writeEntries(none.w, q);
  TEST_ASSERT_EQUAL_UINT(0, none.parse()["items"].size());
}

// ---------------------------------------------------------------- storage

void test_files_are_written_whole() {
  System sys;
  IStorage* storage = sys.kernel().api<IStorage>();
  TEST_ASSERT_NOT_NULL(storage);

  TEST_ASSERT_FALSE(storage->exists("/cfg/m/zappy2.json"));
  TEST_ASSERT_TRUE(storage->writeFile("/cfg/m/zappy2.json", "{\"v\":1}") == Status::Ok);
  TEST_ASSERT_TRUE(storage->exists("/cfg/m/zappy2.json"));
  TEST_ASSERT_FALSE(storage->exists("/cfg/m/zappy2.json.tmp"));

  char buf[64];
  size_t len = 0;
  TEST_ASSERT_TRUE(storage->readFile("/cfg/m/zappy2.json", buf, sizeof(buf), len) == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("{\"v\":1}", buf);
  TEST_ASSERT_EQUAL_UINT(7, len);

  // Overwrite with something shorter: nothing of the old content is left.
  TEST_ASSERT_TRUE(storage->writeFile("/cfg/m/zappy2.json", "{}") == Status::Ok);
  TEST_ASSERT_TRUE(storage->readFile("/cfg/m/zappy2.json", buf, sizeof(buf), len) == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("{}", buf);

  char small[2];
  TEST_ASSERT_TRUE(storage->readFile("/cfg/m/zappy2.json", small, sizeof(small), len) ==
                   Status::NoMem);
  TEST_ASSERT_EQUAL_UINT(2, len);  // how much room is needed
  TEST_ASSERT_TRUE(storage->readFile("/none.json", buf, sizeof(buf), len) == Status::NotFound);

  TEST_ASSERT_TRUE(storage->remove("/cfg/m/zappy2.json") == Status::Ok);
  TEST_ASSERT_TRUE(storage->remove("/cfg/m/zappy2.json") == Status::NotFound);
}

// ---------------------------------------------------------------- pins

void test_pins_have_one_owner() {
  System sys;
  IGpio* gpio = sys.kernel().api<IGpio>();
  TEST_ASSERT_NOT_NULL(gpio);

  TEST_ASSERT_TRUE(gpio->claim(22, hal::PinMode::Output, "zappy2", "Line 1") == Status::Ok);
  TEST_ASSERT_TRUE(gpio->claim(22, hal::PinMode::Output, "other") == Status::Busy);
  TEST_ASSERT_TRUE(gpio->claim(22, hal::PinMode::Output, "zappy2", "Renamed") == Status::Ok);
  TEST_ASSERT_TRUE(gpio->claim(200, hal::PinMode::Output, "zappy2") == Status::Invalid);
  TEST_ASSERT_TRUE(gpio->claim(23, hal::PinMode::Output, "") == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("zappy2", gpio->ownerOf(22));
  TEST_ASSERT_NULL(gpio->ownerOf(23));

  TEST_ASSERT_TRUE(gpio->write(22, true, "zappy2") == Status::Ok);
  bool high = false;
  TEST_ASSERT_TRUE(gpio->read(22, high) == Status::Ok);
  TEST_ASSERT_TRUE(high);
  TEST_ASSERT_TRUE(gpio->write(22, false, "other") == Status::Denied);
  TEST_ASSERT_TRUE(gpio->write(23, true, "zappy2") == Status::NotFound);
  gpio->read(22, high);
  TEST_ASSERT_TRUE(high);  // the refused write changed nothing

  Json<1024> doc;
  doc.w.beginObject();
  sys.kernel().commands().describe("sys.gpio", doc.w);
  doc.w.endObject();
  const JsonValue pins = doc.parse()["pins"];
  bool found = false;
  for (const JsonMember m : pins) {
    if (m.value["pin"].asInt() != 22) continue;
    found = true;
    TEST_ASSERT_TRUE(m.value["owner"].stringEquals("zappy2"));
    TEST_ASSERT_TRUE(m.value["label"].stringEquals("Renamed"));
    TEST_ASSERT_TRUE(m.value["mode"].stringEquals("out"));
    TEST_ASSERT_EQUAL_INT64(1, m.value["level"].asInt());
  }
  TEST_ASSERT_TRUE(found);

  TEST_ASSERT_TRUE(gpio->release(22, "other") == Status::Denied);
  TEST_ASSERT_TRUE(gpio->release(22, "zappy2") == Status::Ok);
  TEST_ASSERT_NULL(gpio->ownerOf(22));
  TEST_ASSERT_TRUE(gpio->claim(22, hal::PinMode::Output, "other") == Status::Ok);
}

// ---------------------------------------------------------------- restart and reset

void test_restart_command() {
  System sys;
  const unsigned before = sys.tb.native().sys.restartCount();
  TEST_ASSERT_TRUE(sys.command("sys.system", "restart", "{}") == Status::Ok);
  TEST_ASSERT_EQUAL_UINT(before, sys.tb.native().sys.restartCount());
  run(sys.kernel(), sys.tb.clock, 1500);
  TEST_ASSERT_EQUAL_UINT(before + 1, sys.tb.native().sys.restartCount());
}

void test_factory_reset_command() {
  System sys;
  const std::string key = sys.kernel().config().sys.key.c_str();
  sys.command("sys.config", "set", "{\"lang\":\"it\",\"deviceName\":\"Plugs\"}");
  IStorage* storage = sys.kernel().api<IStorage>();
  storage->writeFile("/cfg/m/zappy2.json", "{\"v\":1}");
  storage->writeFile("/web/index.html", "<html>");
  run(sys.kernel(), sys.tb.clock, 3500);

  TEST_ASSERT_TRUE(sys.command("sys.system", "factoryReset", "{}") == Status::Ok);
  run(sys.kernel(), sys.tb.clock, 1500);

  const Config& config = sys.kernel().config();
  TEST_ASSERT_EQUAL_STRING(key.c_str(), config.sys.key.c_str());
  TEST_ASSERT_EQUAL_STRING("en", config.sys.lang.c_str());
  TEST_ASSERT_EQUAL_STRING("", config.sys.deviceName.c_str());

  // The settings of the modules are gone, the console files are still there.
  sys.tb.native().fs.mount(false);
  TEST_ASSERT_FALSE(sys.tb.native().fs.exists("/cfg/m/zappy2.json"));
  TEST_ASSERT_TRUE(sys.tb.native().fs.exists("/web/index.html"));
}

// ---------------------------------------------------------------- the starts of the board

namespace {

// One start of the board of the test, as after a restart: the memory is the one of before.
struct Started {
  LoopMax app;
  std::string text;
  JsonValue system;

  explicit Started(TestBoard& tb) : app(tb.board) {
    tb.native().fs.unmount();
    tb.clock.now = 0;
    Logger::setInstance(&app.kernel().log());
    TEST_ASSERT_TRUE(app.begin() == Status::Ok);
    run(app.kernel(), tb.clock, 200);
  }
  ~Started() { Logger::setInstance(nullptr); }

  const JsonValue& read() {
    Json<4096> out;
    app.kernel().report(out.w, "system", false);
    text = out.text();
    system = JsonValue::parse(StringView(text.data(), text.size()))["system"];
    return system;
  }
};

}  // namespace

void test_the_starts_of_the_board_are_counted_and_kept() {
  TestBoard tb;
  {
    Started first(tb);
    const JsonValue& s = first.read();
    TEST_ASSERT_EQUAL_INT64(1, s["starts"].asInt());
    TEST_ASSERT_EQUAL_UINT(1, s["lastStarts"].size());
    TEST_ASSERT_TRUE(s["lastStarts"].at(0)["why"].stringEquals("poweron"));
    TEST_ASSERT_FALSE(s["lastStarts"].at(0)["at"].valid());   // the clock has no hour yet

    // The hour arrives a minute and a half later: the start is the hour less that time.
    run(first.app.kernel(), tb.clock, 90000 - 200, 100);
    CmdContext ctx;
    TEST_ASSERT_TRUE(first.app.kernel().commands().execute(
        "sys.clock", "set", JsonValue::parse("{\"unix\":1791140000}"), ctx) == Status::Ok);
    run(first.app.kernel(), tb.clock, 100);
    TEST_ASSERT_EQUAL_INT64(1791139910, first.read()["lastStarts"].at(0)["at"].asInt());
    // Later the hour is set again: the start does not move.
    tb.clock.now += 3600000;
    TEST_ASSERT_TRUE(first.app.kernel().commands().execute(
        "sys.clock", "set", JsonValue::parse("{\"unix\":1791143700}"), ctx) == Status::Ok);
    run(first.app.kernel(), tb.clock, 100);
    TEST_ASSERT_EQUAL_INT64(1791139910, first.read()["lastStarts"].at(0)["at"].asInt());
  }

  // It starts again alone, after two minutes: a watchdog. The board kept its last words.
  hal::LastWords words;
  words.uptimeS = 120;
  tb.native().sys.setPrevious(&words);
  tb.native().sys.setResetReason(hal::ResetReason::Watchdog);
  {
    Started second(tb);
    const JsonValue& s = second.read();
    TEST_ASSERT_EQUAL_INT64(2, s["starts"].asInt());
    TEST_ASSERT_EQUAL_UINT(2, s["lastStarts"].size());
    TEST_ASSERT_TRUE(s["lastStarts"].at(0)["why"].stringEquals("watchdog"));
    TEST_ASSERT_FALSE(s["lastStarts"].at(0)["ran"].valid());
    TEST_ASSERT_TRUE(s["lastStarts"].at(1)["why"].stringEquals("poweron"));
    TEST_ASSERT_EQUAL_INT64(1791139910, s["lastStarts"].at(1)["at"].asInt());
    TEST_ASSERT_EQUAL_INT64(120, s["lastStarts"].at(1)["ran"].asInt());
  }

  // The power goes and comes back, many times: nothing tells how long each start lasted, and
  // the notes kept are the last six.
  tb.native().sys.setPrevious(nullptr);
  tb.native().sys.setResetReason(hal::ResetReason::PowerOn);
  for (int i = 0; i < 6; ++i) {
    Started again(tb);
  }
  Started last(tb);
  const JsonValue& s = last.read();
  TEST_ASSERT_EQUAL_INT64(9, s["starts"].asInt());
  TEST_ASSERT_EQUAL_UINT(6, s["lastStarts"].size());
  for (size_t i = 0; i < 6; ++i) {
    TEST_ASSERT_TRUE(s["lastStarts"].at(i)["why"].stringEquals("poweron"));
    TEST_ASSERT_FALSE(s["lastStarts"].at(i)["ran"].valid());
  }
}

namespace {

struct WithButton {
  static constexpr uint8_t kPin = 4;
  TestBoard tb;
  Kernel kernel{tb.board};
  SystemService system;
  LogService log;
  StorageService storage;
  ConfigService config;
  GpioService gpio;
  ResetService reset{kPin, 10000};

  WithButton() {
    Logger::setInstance(&kernel.log());
    kernel.add(system);
    kernel.add(log);
    kernel.add(storage);
    kernel.add(config);
    kernel.add(gpio);
    kernel.add(reset);
    TEST_ASSERT_TRUE(kernel.begin() == Status::Ok);
    run(kernel, tb.clock, 200);
  }

  void press(bool down) {
    tb.native().gpio.setInput(kPin, down ? hal::PinLevel::Low : hal::PinLevel::High);
  }
  unsigned restarts() { return tb.native().sys.restartCount(); }
};

}  // namespace

void test_reset_button_long_press() {
  WithButton s;
  TEST_ASSERT_TRUE(s.reset.state() == ServiceState::Ready);
  TEST_ASSERT_EQUAL_STRING("reset", s.kernel.api<IGpio>()->ownerOf(WithButton::kPin));
  const unsigned before = s.restarts();
  const std::string key = s.kernel.config().sys.key.c_str();
  s.kernel.config().sys.lang.assign("it");
  s.kernel.config().touch(Config::kSys);

  s.press(true);
  run(s.kernel, s.tb.clock, 9000);
  TEST_ASSERT_TRUE(s.reset.pressed());
  TEST_ASSERT_FALSE(s.kernel.restartPending());
  TEST_ASSERT_EQUAL_UINT(before, s.restarts());

  run(s.kernel, s.tb.clock, 1200);
  TEST_ASSERT_TRUE(s.kernel.restartPending() || s.restarts() == before + 1);
  run(s.kernel, s.tb.clock, 1500);
  TEST_ASSERT_EQUAL_UINT(before + 1, s.restarts());
  TEST_ASSERT_EQUAL_STRING("en", s.kernel.config().sys.lang.c_str());
  TEST_ASSERT_EQUAL_STRING(key.c_str(), s.kernel.config().sys.key.c_str());
}

void test_reset_button_short_press_does_nothing() {
  WithButton s;
  const unsigned before = s.restarts();
  s.press(true);
  run(s.kernel, s.tb.clock, 5000);
  s.press(false);
  run(s.kernel, s.tb.clock, 200);
  TEST_ASSERT_FALSE(s.reset.pressed());

  // A second press starts counting from zero.
  s.press(true);
  run(s.kernel, s.tb.clock, 8000);
  s.press(false);
  run(s.kernel, s.tb.clock, 3000);
  TEST_ASSERT_EQUAL_UINT(before, s.restarts());
  TEST_ASSERT_FALSE(s.kernel.restartPending());
}

void test_reset_button_ignores_noise() {
  WithButton s;
  const unsigned before = s.restarts();
  // Contact bounce: the level changes faster than the debounce time, for a long while.
  for (int i = 0; i < 1500; ++i) {
    s.press(i % 2 == 0);
    run(s.kernel, s.tb.clock, 10);
  }
  s.press(false);
  run(s.kernel, s.tb.clock, 500);
  TEST_ASSERT_EQUAL_UINT(before, s.restarts());
  TEST_ASSERT_FALSE(s.kernel.restartPending());
}

void test_reset_service_without_a_button() {
  TestBoard tb;
  Kernel kernel(tb.board);
  LogService log;
  SystemService system;
  StorageService storage;
  ConfigService config;
  GpioService gpio;
  ResetService reset(-1);
  Logger::setInstance(&kernel.log());
  for (Service* s : {static_cast<Service*>(&system), static_cast<Service*>(&log),
                     static_cast<Service*>(&storage), static_cast<Service*>(&config),
                     static_cast<Service*>(&gpio), static_cast<Service*>(&reset)}) {
    kernel.add(*s);
  }
  kernel.begin();
  run(kernel, tb.clock, 200);
  TEST_ASSERT_TRUE(reset.state() == ServiceState::Degraded);
  TEST_ASSERT_EQUAL_UINT(0, kernel.api<IGpio>()->ownerOf(4) ? 1 : 0);
}

// ---------------------------------------------------------------- command line

void test_cli_help_and_errors() {
  System sys;
  TEST_ASSERT_NOT_NULL(strstr(sys.cli("help").c_str(), "cmd <target> <command>"));
  TEST_ASSERT_NOT_NULL(strstr(sys.cli("frobnicate").c_str(), "unknown command"));
  TEST_ASSERT_NOT_NULL(strstr(sys.cli("doc").c_str(), "target missing"));
  TEST_ASSERT_NOT_NULL(strstr(sys.cli("doc sys.none").c_str(), "target not found"));
  TEST_ASSERT_NOT_NULL(strstr(sys.cli("cmd sys.log").c_str(), "use cmd"));
  TEST_ASSERT_EQUAL_STRING("", sys.cli("").c_str());
  TEST_ASSERT_EQUAL_STRING("", sys.cli("    ").c_str());
}

void test_cli_documents() {
  System sys;
  std::string storage;
  JsonValue v = System::lastJson(sys.cli("info"), storage);
  TEST_ASSERT_TRUE(v.valid());
  TEST_ASSERT_TRUE(v["device"]["board"].stringEquals("native"));
  TEST_ASSERT_TRUE(v["system"].isObject());
  TEST_ASSERT_FALSE(v["services"].valid());

  v = System::lastJson(sys.cli("report services"), storage);
  TEST_ASSERT_EQUAL_UINT(kServices, v["services"].size());

  v = System::lastJson(sys.cli("doc sys.storage"), storage);
  TEST_ASSERT_TRUE(v["fs"]["mounted"].asBool());

  v = System::lastJson(sys.cli("man sys.config"), storage);
  TEST_ASSERT_TRUE(v["target"].stringEquals("sys.config"));
  TEST_ASSERT_TRUE(v["commands"].at(0)["name"].stringEquals("set"));

  sys.kernel().log().write(LogLevel::Warn, kLogSys, "marker entry");
  v = System::lastJson(sys.cli("log 3"), storage);
  TEST_ASSERT_TRUE(v.valid());
  TEST_ASSERT_TRUE(v["items"].size() <= 3);
  bool found = false;
  for (const JsonMember m : v["items"]) {
    if (m.value["msg"].stringEquals("marker entry")) found = true;
  }
  TEST_ASSERT_TRUE(found);
}

void test_cli_commands() {
  System sys;
  std::string storage;
  JsonValue v = System::lastJson(
      sys.cli("cmd sys.config set lang=it deviceName=\"Office Plugs\""), storage);
  TEST_ASSERT_TRUE(v.valid());
  TEST_ASSERT_TRUE(v["ok"].asBool());
  TEST_ASSERT_TRUE(v["status"].stringEquals("ok"));
  TEST_ASSERT_EQUAL_STRING("it", sys.kernel().config().sys.lang.c_str());
  TEST_ASSERT_EQUAL_STRING("Office Plugs", sys.kernel().config().sys.deviceName.c_str());

  v = System::lastJson(sys.cli("cmd sys.config set hostname=-bad"), storage);
  TEST_ASSERT_FALSE(v["ok"].asBool(true));
  TEST_ASSERT_TRUE(v["status"].stringEquals("invalid"));
  TEST_ASSERT_TRUE(v["field"].stringEquals("hostname"));

  v = System::lastJson(sys.cli("cmd sys.none x"), storage);
  TEST_ASSERT_TRUE(v["status"].stringEquals("not_found"));
}

void test_cli_reads_the_serial_port() {
  System sys;
  hal::native::Serial& serial = sys.tb.native().serial;

  // A line arrives in pieces, with a byte order mark, a typo fixed with backspace, CR LF.
  serial.feedInput("\xEF\xBB\xBF" "cmd sys.conf");
  run(sys.kernel(), sys.tb.clock, 100);
  serial.feedInput("ig set lang=ix\bt\r\n");
  run(sys.kernel(), sys.tb.clock, 100);
  TEST_ASSERT_EQUAL_STRING("it", sys.kernel().config().sys.lang.c_str());

  sys.tb.output();
  serial.feedInput("\r\n\r\n   \r\n");
  run(sys.kernel(), sys.tb.clock, 100);
  TEST_ASSERT_EQUAL_STRING("", sys.tb.output().c_str());  // empty lines give no answer

  const std::string tooLong(LMP_CLI_LINE + 50, 'x');
  serial.feedInput((tooLong + "\n").c_str());
  run(sys.kernel(), sys.tb.clock, 100);
  TEST_ASSERT_NOT_NULL(strstr(sys.tb.output().c_str(), "line too long"));

  // The next line is handled normally.
  serial.feedInput("targets\n");
  run(sys.kernel(), sys.tb.clock, 100);
  TEST_ASSERT_NOT_NULL(strstr(sys.tb.output().c_str(), "sys.system"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_every_service_starts);
  RUN_TEST(test_boot_log_reaches_the_serial_port);
  RUN_TEST(test_targets_of_the_base_system);
  RUN_TEST(test_report);
  RUN_TEST(test_documents_never_show_secrets);
  RUN_TEST(test_settings_change_and_survive_a_restart);
  RUN_TEST(test_settings_are_validated);
  RUN_TEST(test_clock_is_set_by_command);
  RUN_TEST(test_clock_rejects_bad_values);
  RUN_TEST(test_log_commands);
  RUN_TEST(test_log_reading_is_paged);
  RUN_TEST(test_files_are_written_whole);
  RUN_TEST(test_pins_have_one_owner);
  RUN_TEST(test_restart_command);
  RUN_TEST(test_factory_reset_command);
  RUN_TEST(test_the_starts_of_the_board_are_counted_and_kept);
  RUN_TEST(test_reset_button_long_press);
  RUN_TEST(test_reset_button_short_press_does_nothing);
  RUN_TEST(test_reset_button_ignores_noise);
  RUN_TEST(test_reset_service_without_a_button);
  RUN_TEST(test_cli_help_and_errors);
  RUN_TEST(test_cli_documents);
  RUN_TEST(test_cli_commands);
  RUN_TEST(test_cli_reads_the_serial_port);
  return UNITY_END();
}
