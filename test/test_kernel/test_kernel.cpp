// Tests of the kernel: events, service registry, command bus, cross task calls, report.
#include <string.h>
#include <unity.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "../support/test_support.h"

using namespace lmp;
using namespace lmp::test;

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

// ---------------------------------------------------------------- a service for the tests

namespace {

class Probe : public Service {
 public:
  Probe(ServiceId id, const char* name, DepList deps = DepList(), Mode modes = Mode::Any,
        RestartPolicy policy = RestartPolicy::Retry, uint16_t tickMs = 0)
      : info_{id, name, "", modes, policy, tickMs, deps} {}

  const ServiceInfo& info() const override { return info_; }

  Status start(Kernel&) override {
    ++starts;
    if (order) order->append(info_.name).append(" ");
    return startResult;
  }
  void stop() override { ++stops; }
  void tick(uint32_t) override { ++ticks; }
  void onEvent(const Event& e) override {
    ++events;
    lastEvent = e.id;
  }
  void describe(JsonWriter& w) const override { w.u32("ticks", static_cast<uint32_t>(ticks)); }
  void commands(CommandSet& set) override {
    if (withCommand) {
      set.add("ping", "Test command", [this](const Args&, CmdContext&) -> Status {
        ++pings;
        return Status::Ok;
      });
    }
  }
  const char* reportPart() const override { return part; }

  // What the base class keeps protected.
  void nowReady() { ready(); }
  void nowFail(const char* why) { fail(why); }
  void nowDegrade(const char* why) { degrade(why); }
  void nowRecover() { recover(); }

  Status startResult = Status::Ok;
  std::string* order = nullptr;
  const char* part = nullptr;
  bool withCommand = false;
  int starts = 0;
  int stops = 0;
  int ticks = 0;
  int events = 0;
  int pings = 0;
  EventId lastEvent = EventId::None;

 private:
  ServiceInfo info_;
};

}  // namespace

// ---------------------------------------------------------------- events

void test_events_are_delivered_by_dispatch() {
  EventBus bus(nullptr);
  int got = 0;
  EventId last = EventId::None;
  TEST_ASSERT_TRUE(bus.subscribe([&got, &last](const Event& e) {
    ++got;
    last = e.id;
  }));

  TEST_ASSERT_TRUE(bus.publish(Event(EventId::TimeSynced, 1, 0, 1790000000u)));
  TEST_ASSERT_EQUAL_INT(0, got);  // nothing happens until dispatch
  TEST_ASSERT_EQUAL_UINT(1, bus.pending());

  TEST_ASSERT_EQUAL_UINT(1, bus.dispatch());
  TEST_ASSERT_EQUAL_INT(1, got);
  TEST_ASSERT_TRUE(last == EventId::TimeSynced);
  TEST_ASSERT_EQUAL_UINT(0, bus.dispatch());
}

void test_events_published_by_handlers_wait_for_the_next_dispatch() {
  EventBus bus(nullptr);
  int got = 0;
  EventBus* self = &bus;
  bus.subscribe([&got, self](const Event& e) {
    ++got;
    if (e.id == EventId::NetApUp) self->publish(Event(EventId::InternetUp));
  });
  bus.publish(Event(EventId::NetApUp));
  TEST_ASSERT_EQUAL_UINT(1, bus.dispatch());
  TEST_ASSERT_EQUAL_INT(1, got);
  TEST_ASSERT_EQUAL_UINT(1, bus.dispatch());
  TEST_ASSERT_EQUAL_INT(2, got);
}

void test_events_queue_full() {
  EventBus bus(nullptr);
  for (int i = 0; i < LMP_EVENT_QUEUE; ++i) TEST_ASSERT_TRUE(bus.publish(Event(EventId::ModuleData)));
  TEST_ASSERT_FALSE(bus.publish(Event(EventId::ModuleData)));
  TEST_ASSERT_EQUAL_UINT32(1, bus.lost());
  TEST_ASSERT_EQUAL_UINT(LMP_EVENT_QUEUE, bus.dispatch());
  TEST_ASSERT_TRUE(bus.publish(Event(EventId::ModuleData)));
}

void test_events_subscriber_table_full() {
  EventBus bus(nullptr);
  for (int i = 0; i < LMP_MAX_SUBSCRIBERS; ++i) {
    TEST_ASSERT_TRUE(bus.subscribe([](const Event&) {}));
  }
  TEST_ASSERT_FALSE(bus.subscribe([](const Event&) {}));
  TEST_ASSERT_FALSE(bus.subscribe(EventHandler()));
}

// ---------------------------------------------------------------- registry

void test_registry_starts_in_dependency_order() {
  TestBoard tb;
  Kernel kernel(tb.board);
  std::string order;
  // Registered in the wrong order on purpose.
  Probe web(ServiceId::Web, "web", DepList(ServiceId::Net, ServiceId::Config));
  Probe net(ServiceId::Net, "net", DepList(ServiceId::Config));
  Probe config(ServiceId::Config, "config", DepList(ServiceId::Storage));
  Probe storage(ServiceId::Storage, "storage");
  for (Probe* p : {&web, &net, &config, &storage}) {
    p->order = &order;
    TEST_ASSERT_TRUE(kernel.add(*p) == Status::Ok);
  }
  TEST_ASSERT_TRUE(kernel.begin() == Status::Ok);

  kernel.tick();
  TEST_ASSERT_EQUAL_STRING("storage config net web ", order.c_str());
  TEST_ASSERT_TRUE(web.state() == ServiceState::Ready);
}

void test_registry_rejects_duplicates_and_cycles() {
  TestBoard tb;
  {
    Kernel kernel(tb.board);
    Probe a(ServiceId::Net, "net");
    Probe b(ServiceId::Net, "net2");
    TEST_ASSERT_TRUE(kernel.add(a) == Status::Ok);
    TEST_ASSERT_TRUE(kernel.add(b) == Status::Busy);
  }
  {
    Kernel kernel(tb.board);
    Probe a(ServiceId::Net, "net", DepList(ServiceId::Web));
    Probe b(ServiceId::Web, "web", DepList(ServiceId::Net));
    kernel.add(a);
    kernel.add(b);
    TEST_ASSERT_TRUE(kernel.begin() == Status::Invalid);
  }
}

void test_registry_missing_dependency_blocks_only_that_branch() {
  TestBoard tb;
  Kernel kernel(tb.board);
  Probe storage(ServiceId::Storage, "storage");
  Probe mqtt(ServiceId::Mqtt, "mqtt", DepList(ServiceId::Net));      // net is not in the build
  Probe cloud(ServiceId::Cloud, "cloud", DepList(ServiceId::Mqtt));  // depends on a blocked one
  kernel.add(storage);
  kernel.add(mqtt);
  kernel.add(cloud);
  TEST_ASSERT_TRUE(kernel.begin() == Status::Ok);

  run(kernel, tb.clock, 100);
  TEST_ASSERT_TRUE(storage.state() == ServiceState::Ready);
  TEST_ASSERT_TRUE(mqtt.state() == ServiceState::Stopped);
  TEST_ASSERT_TRUE(cloud.state() == ServiceState::Stopped);
  TEST_ASSERT_EQUAL_INT(0, mqtt.starts);
  TEST_ASSERT_EQUAL_STRING("missing dependency", mqtt.reason());
}

void test_registry_mode_gating() {
  TestBoard tb;
  Kernel kernel(tb.board);
  Probe lanOnly(ServiceId::Mqtt, "mqtt", DepList(), Mode::Lan);
  Probe always(ServiceId::Log, "log");
  kernel.add(lanOnly);
  kernel.add(always);
  kernel.begin();

  run(kernel, tb.clock, 50);  // the kernel starts in AP mode
  TEST_ASSERT_TRUE(always.state() == ServiceState::Ready);
  TEST_ASSERT_TRUE(lanOnly.state() == ServiceState::Waiting);

  kernel.setMode(Mode::Lan);
  run(kernel, tb.clock, 50);
  TEST_ASSERT_TRUE(lanOnly.state() == ServiceState::Ready);

  kernel.setMode(Mode::Ap);
  run(kernel, tb.clock, 50);
  TEST_ASSERT_TRUE(lanOnly.state() == ServiceState::Waiting);
  TEST_ASSERT_EQUAL_INT(1, lanOnly.stops);
  TEST_ASSERT_TRUE(always.state() == ServiceState::Ready);
}

void test_registry_stops_dependents_when_a_dependency_fails() {
  TestBoard tb;
  Kernel kernel(tb.board);
  Probe net(ServiceId::Net, "net", DepList(), Mode::Any, RestartPolicy::Never);
  Probe web(ServiceId::Web, "web", DepList(ServiceId::Net));
  Probe mqtt(ServiceId::Mqtt, "mqtt", DepList(ServiceId::Web));
  kernel.add(net);
  kernel.add(web);
  kernel.add(mqtt);
  kernel.begin();
  run(kernel, tb.clock, 50);
  TEST_ASSERT_TRUE(mqtt.state() == ServiceState::Ready);

  net.nowFail("radio broken");
  run(kernel, tb.clock, 50);
  TEST_ASSERT_TRUE(net.state() == ServiceState::Error);
  TEST_ASSERT_TRUE(web.state() == ServiceState::Waiting);
  TEST_ASSERT_TRUE(mqtt.state() == ServiceState::Waiting);
  TEST_ASSERT_EQUAL_INT(1, web.stops);
  TEST_ASSERT_EQUAL_INT(1, mqtt.stops);
}

void test_registry_degraded_dependency_keeps_dependents_running() {
  TestBoard tb;
  Kernel kernel(tb.board);
  Probe net(ServiceId::Net, "net");
  Probe web(ServiceId::Web, "web", DepList(ServiceId::Net));
  kernel.add(net);
  kernel.add(web);
  kernel.begin();
  run(kernel, tb.clock, 50);

  net.nowDegrade("fallback");
  run(kernel, tb.clock, 50);
  TEST_ASSERT_TRUE(net.state() == ServiceState::Degraded);
  TEST_ASSERT_EQUAL_STRING("fallback", net.reason());
  TEST_ASSERT_TRUE(web.state() == ServiceState::Ready);

  net.nowRecover();
  TEST_ASSERT_TRUE(net.state() == ServiceState::Ready);
  TEST_ASSERT_EQUAL_STRING("", net.reason());
}

void test_registry_retries_with_growing_delay() {
  TestBoard tb;
  Kernel kernel(tb.board);
  Probe net(ServiceId::Net, "net");
  net.startResult = Status::Io;
  kernel.add(net);
  kernel.begin();

  run(kernel, tb.clock, 100);
  TEST_ASSERT_EQUAL_INT(1, net.starts);
  TEST_ASSERT_TRUE(net.state() == ServiceState::Error);
  TEST_ASSERT_EQUAL_STRING("io", net.reason());

  run(kernel, tb.clock, 1500);
  TEST_ASSERT_EQUAL_INT(1, net.starts);  // first retry after 2 seconds
  run(kernel, tb.clock, 1000);
  TEST_ASSERT_EQUAL_INT(2, net.starts);
  run(kernel, tb.clock, 3000);
  TEST_ASSERT_EQUAL_INT(2, net.starts);  // second retry after 4 seconds
  run(kernel, tb.clock, 1500);
  TEST_ASSERT_EQUAL_INT(3, net.starts);

  net.startResult = Status::Ok;
  run(kernel, tb.clock, 9000);
  TEST_ASSERT_TRUE(net.state() == ServiceState::Ready);
  TEST_ASSERT_EQUAL_UINT32(3, net.stats().failures);
}

void test_registry_policy_never() {
  TestBoard tb;
  Kernel kernel(tb.board);
  Probe net(ServiceId::Net, "net", DepList(), Mode::Any, RestartPolicy::Never);
  net.startResult = Status::Unsupported;
  kernel.add(net);
  kernel.begin();
  run(kernel, tb.clock, 120000, 100);
  TEST_ASSERT_EQUAL_INT(1, net.starts);
  TEST_ASSERT_TRUE(net.state() == ServiceState::Error);
}

void test_registry_critical_failure_restarts_the_board() {
  TestBoard tb;
  Kernel kernel(tb.board);
  Probe storage(ServiceId::Storage, "storage", DepList(), Mode::Any, RestartPolicy::Critical);
  storage.startResult = Status::Io;
  kernel.add(storage);
  kernel.begin();

  const unsigned before = tb.native().sys.restartCount();
  run(kernel, tb.clock, 200000, 100);
  TEST_ASSERT_EQUAL_INT(ServiceRegistry::kCriticalAttempts, storage.starts);
  TEST_ASSERT_TRUE(kernel.services().criticalFailure());
  TEST_ASSERT_EQUAL_UINT(before + 1, tb.native().sys.restartCount());
}

void test_registry_pending_start() {
  TestBoard tb;
  Kernel kernel(tb.board);
  Probe net(ServiceId::Net, "net");
  Probe web(ServiceId::Web, "web", DepList(ServiceId::Net));
  net.startResult = Status::Pending;
  kernel.add(net);
  kernel.add(web);
  kernel.begin();

  run(kernel, tb.clock, 100);
  TEST_ASSERT_TRUE(net.state() == ServiceState::Starting);
  TEST_ASSERT_TRUE(web.state() == ServiceState::Waiting);
  TEST_ASSERT_EQUAL_INT(1, net.starts);

  net.nowReady();
  run(kernel, tb.clock, 50);
  TEST_ASSERT_TRUE(net.state() == ServiceState::Ready);
  TEST_ASSERT_TRUE(web.state() == ServiceState::Ready);
}

void test_registry_start_timeout() {
  TestBoard tb;
  Kernel kernel(tb.board);
  Probe net(ServiceId::Net, "net", DepList(), Mode::Any, RestartPolicy::Never);
  net.startResult = Status::Pending;
  kernel.add(net);
  kernel.begin();
  run(kernel, tb.clock, ServiceRegistry::kStartTimeoutMs + 500, 100);
  TEST_ASSERT_TRUE(net.state() == ServiceState::Error);
  TEST_ASSERT_EQUAL_STRING("start timeout", net.reason());
}

void test_registry_ticks_at_the_declared_period() {
  TestBoard tb;
  Kernel kernel(tb.board);
  Probe fast(ServiceId::Reset, "reset", DepList(), Mode::Any, RestartPolicy::Retry, 20);
  Probe slow(ServiceId::Config, "config", DepList(), Mode::Any, RestartPolicy::Retry, 500);
  Probe never(ServiceId::Log, "log");
  kernel.add(fast);
  kernel.add(slow);
  kernel.add(never);
  kernel.begin();

  run(kernel, tb.clock, 1000);
  TEST_ASSERT_INT_WITHIN(2, 50, fast.ticks);
  TEST_ASSERT_INT_WITHIN(1, 2, slow.ticks);
  TEST_ASSERT_EQUAL_INT(0, never.ticks);
}

void test_registry_publishes_state_changes() {
  TestBoard tb;
  Kernel kernel(tb.board);
  Probe net(ServiceId::Net, "net");
  kernel.add(net);
  kernel.begin();

  int changes = 0;
  ServiceState last = ServiceState::Stopped;
  kernel.events().subscribe([&changes, &last](const Event& e) {
    if (e.id != EventId::ServiceState) return;
    ++changes;
    last = static_cast<ServiceState>(e.b);
  });
  run(kernel, tb.clock, 50);
  TEST_ASSERT_EQUAL_INT(1, changes);
  TEST_ASSERT_TRUE(last == ServiceState::Ready);
  // Running services receive the events, their own state change included.
  TEST_ASSERT_TRUE(net.events >= 1);
}

void test_registry_describe() {
  TestBoard tb;
  Kernel kernel(tb.board);
  Probe net(ServiceId::Net, "net");
  Probe web(ServiceId::Web, "web", DepList(ServiceId::Net));
  kernel.add(net);
  kernel.add(web);
  kernel.begin();
  run(kernel, tb.clock, 50);

  Json<1024> out;
  kernel.services().describe(out.w);
  const JsonValue list = out.parse();
  TEST_ASSERT_TRUE(list.isArray());
  TEST_ASSERT_EQUAL_UINT(2, list.size());
  TEST_ASSERT_TRUE(list.at(1)["name"].stringEquals("web"));
  TEST_ASSERT_TRUE(list.at(1)["state"].stringEquals("Ready"));
  TEST_ASSERT_TRUE(list.at(1)["deps"].at(0).stringEquals("net"));
}

// ---------------------------------------------------------------- commands

namespace {

struct Relay {
  bool level[4] = {};
  char name[32] = "";
  int calls = 0;
};

constexpr ArgSpec kSetArgs[] = {
    Arg::u8("channel", 0, 3),
    Arg::boolean("value"),
    Arg::str("name", 1, 16).optional(),
};

constexpr ArgSpec kLoginArgs[] = {
    Arg::secret("psw", 8, 32),
};

uint8_t addRelay(CommandBus& bus, Relay& relay) {
  uint8_t target = CommandBus::kNoTarget;
  Relay* r = &relay;
  TEST_ASSERT_TRUE(bus.addTarget("mod", "relay", [r](JsonWriter& w) {
    w.beginArray("level");
    for (bool l : r->level) w.boolean(l);
    w.endArray();
  }, target) == Status::Ok);

  CommandSet set = bus.commandsFor(target);
  set.add("set", "Switches one channel", kSetArgs, [r](const Args& a, CmdContext& ctx) -> Status {
    ++r->calls;
    r->level[a.u8("channel")] = a.boolean("value");
    if (a.has("name")) snprintf(r->name, sizeof(r->name), "%s", a.str("name"));
    if (ctx.result) ctx.result->u32("channel", a.u8("channel"));
    return Status::Ok;
  });
  set.add("login", "Secret argument", kLoginArgs, [](const Args&, CmdContext&) -> Status {
    return Status::Ok;
  });
  set.add("slow", "Asynchronous", [](const Args&, CmdContext&) -> Status {
    return Status::Pending;
  }, kCmdAsync | kCmdConfirm);
  TEST_ASSERT_TRUE(set.ok());
  return target;
}

Status exec(CommandBus& bus, const char* command, const char* json, CmdContext& ctx) {
  return bus.execute("mod.relay", command, JsonValue::parse(json), ctx);
}

}  // namespace

void test_commands_execute_with_json() {
  CommandBus bus;
  Relay relay;
  addRelay(bus, relay);
  CmdContext ctx;

  TEST_ASSERT_TRUE(exec(bus, "set", "{\"channel\":2,\"value\":true}", ctx) == Status::Ok);
  TEST_ASSERT_TRUE(relay.level[2]);
  TEST_ASSERT_EQUAL_STRING("", relay.name);

  TEST_ASSERT_TRUE(
      exec(bus, "set", "{\"channel\":2,\"value\":0,\"name\":\"Light \\\"A\\\"\"}", ctx) == Status::Ok);
  TEST_ASSERT_FALSE(relay.level[2]);
  TEST_ASSERT_EQUAL_STRING("Light \"A\"", relay.name);
  TEST_ASSERT_EQUAL_INT(2, relay.calls);
}

void test_commands_reject_bad_arguments() {
  CommandBus bus;
  Relay relay;
  addRelay(bus, relay);

  struct Case {
    const char* json;
    const char* field;
  };
  const Case cases[] = {
      {"{\"value\":true}", "channel"},                 // required argument missing
      {"{\"channel\":4,\"value\":true}", "channel"},   // out of range
      {"{\"channel\":\"1\",\"value\":true}", "channel"},
      {"{\"channel\":1,\"value\":\"yes\"}", "value"},
      {"{\"channel\":1,\"value\":true,\"name\":\"\"}", "name"},
      {"{\"channel\":1,\"value\":true,\"name\":\"12345678901234567\"}", "name"},
      {"{\"channel\":null,\"value\":true}", "channel"},
  };
  for (const Case& c : cases) {
    CmdContext ctx;
    TEST_ASSERT_TRUE_MESSAGE(exec(bus, "set", c.json, ctx) == Status::Invalid, c.json);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c.field, ctx.errorField, c.json);
  }
  TEST_ASSERT_EQUAL_INT(0, relay.calls);  // the handler never runs with bad arguments

  CmdContext ctx;
  TEST_ASSERT_TRUE(exec(bus, "set", "[1,2]", ctx) == Status::Invalid);
  TEST_ASSERT_TRUE(exec(bus, "set", "not json", ctx) == Status::Invalid);
}

void test_commands_unknown_target_or_command() {
  CommandBus bus;
  Relay relay;
  addRelay(bus, relay);
  CmdContext ctx;
  TEST_ASSERT_TRUE(bus.execute("mod.none", "set", JsonValue(), ctx) == Status::NotFound);
  TEST_ASSERT_TRUE(bus.execute("mod.relay", "none", JsonValue(), ctx) == Status::NotFound);
  TEST_ASSERT_TRUE(bus.execute("relay", "set", JsonValue(), ctx) == Status::NotFound);
  TEST_ASSERT_TRUE(bus.hasCommand("mod.relay", "set"));
  TEST_ASSERT_FALSE(bus.hasCommand("mod.relay", "SET"));
}

void test_commands_ignore_unknown_arguments() {
  CommandBus bus;
  Relay relay;
  addRelay(bus, relay);
  CmdContext ctx;
  TEST_ASSERT_TRUE(exec(bus, "set", "{\"channel\":1,\"value\":true,\"extra\":5}", ctx) == Status::Ok);
  TEST_ASSERT_TRUE(relay.level[1]);
}

void test_commands_result_and_pending() {
  CommandBus bus;
  Relay relay;
  addRelay(bus, relay);

  Json<128> out;
  CmdContext ctx;
  ctx.result = &out.w;
  out.w.beginObject();
  TEST_ASSERT_TRUE(exec(bus, "set", "{\"channel\":3,\"value\":true}", ctx) == Status::Ok);
  out.w.endObject();
  TEST_ASSERT_EQUAL_STRING("{\"channel\":3}", out.text());

  TEST_ASSERT_TRUE(exec(bus, "slow", "{}", ctx) == Status::Pending);
  TEST_ASSERT_TRUE(bus.execute("mod.relay", "slow", JsonValue(), ctx) == Status::Pending);
}

void test_commands_from_text() {
  CommandBus bus;
  Relay relay;
  addRelay(bus, relay);
  CmdContext ctx;
  ctx.source = CmdSource::Serial;

  TEST_ASSERT_TRUE(bus.executeText("mod.relay", "set", "channel=1 value=true", ctx) == Status::Ok);
  TEST_ASSERT_TRUE(relay.level[1]);
  TEST_ASSERT_TRUE(bus.executeText("mod.relay", "set", "  channel=1   value=off  ", ctx) ==
                   Status::Ok);
  TEST_ASSERT_FALSE(relay.level[1]);
  TEST_ASSERT_TRUE(bus.executeText("mod.relay", "set",
                                   "channel=0 value=1 name=\"Office plugs\"", ctx) == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("Office plugs", relay.name);
  // Text that looks like a number stays text when the argument is a string.
  TEST_ASSERT_TRUE(bus.executeText("mod.relay", "set", "channel=0 value=1 name=123", ctx) ==
                   Status::Ok);
  TEST_ASSERT_EQUAL_STRING("123", relay.name);

  TEST_ASSERT_TRUE(bus.executeText("mod.relay", "set", "channel=x value=1", ctx) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("channel", ctx.errorField);
  TEST_ASSERT_TRUE(bus.executeText("mod.relay", "set", "channel=9 value=1", ctx) == Status::Invalid);
  TEST_ASSERT_TRUE(bus.executeText("mod.relay", "set", "channel", ctx) == Status::Invalid);
  TEST_ASSERT_TRUE(bus.executeText("mod.relay", "set", "name=\"open", ctx) == Status::Invalid);
  TEST_ASSERT_TRUE(bus.executeText("mod.relay", "slow", "", ctx) == Status::Pending);
}

void test_commands_never_log_secrets() {
  TestBoard tb;
  Logger log(tb.clock, nullptr);
  Logger::setInstance(&log);
  CommandBus bus;
  Relay relay;
  addRelay(bus, relay);
  CmdContext ctx;
  ctx.source = CmdSource::Remote;
  TEST_ASSERT_TRUE(exec(bus, "login", "{\"psw\":\"supersecret1\"}", ctx) == Status::Ok);
  TEST_ASSERT_TRUE(exec(bus, "login", "{\"psw\":\"short\"}", ctx) == Status::Invalid);

  LogEntry entries[4];
  const size_t n = log.read(0, entries, 4);
  TEST_ASSERT_EQUAL_UINT(2, n);
  TEST_ASSERT_EQUAL_STRING("mod.relay login from remote: ok", entries[0].msg);
  TEST_ASSERT_EQUAL_STRING("mod.relay login from remote: invalid (psw)", entries[1].msg);
  TEST_ASSERT_TRUE(entries[1].level == LogLevel::Warn);
  for (size_t i = 0; i < n; ++i) {
    TEST_ASSERT_NULL(strstr(entries[i].msg, "supersecret1"));
    TEST_ASSERT_NULL(strstr(entries[i].msg, "short"));
  }
}

void test_commands_documents() {
  CommandBus bus;
  Relay relay;
  addRelay(bus, relay);
  relay.level[1] = true;

  Json<128> doc;
  doc.w.beginObject();
  TEST_ASSERT_TRUE(bus.describe("mod.relay", doc.w) == Status::Ok);
  doc.w.endObject();
  TEST_ASSERT_EQUAL_STRING("{\"level\":[false,true,false,false]}", doc.text());
  TEST_ASSERT_TRUE(bus.describe("mod.none", doc.w) == Status::NotFound);

  Json<2048> man;
  TEST_ASSERT_TRUE(bus.manifest("mod.relay", man.w) == Status::Ok);
  const JsonValue m = man.parse();
  TEST_ASSERT_TRUE(m.valid());
  TEST_ASSERT_TRUE(m["target"].stringEquals("mod.relay"));
  TEST_ASSERT_EQUAL_UINT(3, m["commands"].size());

  const JsonValue set = m["commands"].at(0);
  TEST_ASSERT_TRUE(set["name"].stringEquals("set"));
  TEST_ASSERT_EQUAL_UINT(3, set["args"].size());
  TEST_ASSERT_TRUE(set["args"].at(0)["type"].stringEquals("u8"));
  TEST_ASSERT_EQUAL_INT64(3, set["args"].at(0)["max"].asInt());
  TEST_ASSERT_TRUE(set["args"].at(0)["required"].asBool());
  TEST_ASSERT_FALSE(set["args"].at(2)["required"].asBool(true));

  TEST_ASSERT_TRUE(m["commands"].at(1)["args"].at(0)["secret"].asBool());
  TEST_ASSERT_TRUE(m["commands"].at(2)["async"].asBool());
  TEST_ASSERT_TRUE(m["commands"].at(2)["confirm"].asBool());

  Json<128> list;
  bus.targets(list.w);
  TEST_ASSERT_EQUAL_STRING("[\"mod.relay\"]", list.text());
}

void test_a_command_says_what_it_is() {
  CommandBus bus;
  uint8_t target = 0;
  TEST_ASSERT_TRUE(bus.addTarget("mod", "desk", DescribeFn(), target) == Status::Ok);
  const CmdHandler ok = [](const Args&, CmdContext&) -> Status { return Status::Ok; };
  CommandSet set = bus.commandsFor(target);
  set.add("list", "Reads", ok, kCmdReadOnly);
  set.add("off", "Turns a PC off", ok, kCmdSensitive | kCmdConfirm);
  set.add("pass", "A pass for another service", ok, kCmdSecret | kCmdAsync);
  set.add("draw", "What the page shows", ok, kCmdReadOnly | kCmdUi);
  set.add("rename", "Changes a name", ok);
  TEST_ASSERT_TRUE(set.ok());

  Json<2048> man;
  TEST_ASSERT_TRUE(bus.manifest("mod.desk", man.w) == Status::Ok);
  const JsonValue commands = man.parse()["commands"];
  TEST_ASSERT_EQUAL_UINT(5, commands.size());
  // Only reads.
  TEST_ASSERT_TRUE(commands.at(0)["readOnly"].asBool(false));
  TEST_ASSERT_FALSE(commands.at(0)["sensitive"].valid());
  TEST_ASSERT_FALSE(commands.at(0)["secret"].valid());
  TEST_ASSERT_FALSE(commands.at(0)["ui"].valid());
  // Not to be offered lightly; what it said before is still there.
  TEST_ASSERT_TRUE(commands.at(1)["sensitive"].asBool(false));
  TEST_ASSERT_TRUE(commands.at(1)["confirm"].asBool(false));
  TEST_ASSERT_FALSE(commands.at(1)["readOnly"].valid());
  // Its answer is to keep.
  TEST_ASSERT_TRUE(commands.at(2)["secret"].asBool(false));
  TEST_ASSERT_TRUE(commands.at(2)["async"].asBool(false));
  // For the page of the module.
  TEST_ASSERT_TRUE(commands.at(3)["ui"].asBool(false));
  TEST_ASSERT_TRUE(commands.at(3)["readOnly"].asBool(false));
  // One that says nothing: it changes something, as every command did before.
  TEST_ASSERT_FALSE(commands.at(4)["readOnly"].valid());
  TEST_ASSERT_FALSE(commands.at(4)["sensitive"].valid());
  TEST_ASSERT_FALSE(commands.at(4)["secret"].valid());
  TEST_ASSERT_FALSE(commands.at(4)["ui"].valid());
}

void test_commands_registration_limits() {
  CommandBus bus;
  uint8_t a = 0;
  uint8_t b = 0;
  TEST_ASSERT_TRUE(bus.addTarget("sys", "net", DescribeFn(), a) == Status::Ok);
  TEST_ASSERT_TRUE(bus.addTarget("sys", "net", DescribeFn(), b) == Status::Busy);
  TEST_ASSERT_TRUE(bus.addTarget("mod", "net", DescribeFn(), b) == Status::Ok);  // other prefix
  TEST_ASSERT_TRUE(bus.addTarget("sys", "", DescribeFn(), b) == Status::Invalid);

  const CmdHandler ok = [](const Args&, CmdContext&) -> Status { return Status::Ok; };
  CommandSet set = bus.commandsFor(a);
  set.add("one", "", ok);
  TEST_ASSERT_TRUE(set.ok());
  set.add("one", "", ok);  // same name on the same target
  TEST_ASSERT_FALSE(set.ok());

  TEST_ASSERT_TRUE(bus.addCommand(99, "x", "", Span<const ArgSpec>(), ok, 0) == Status::Invalid);
  TEST_ASSERT_TRUE(bus.addCommand(a, "x", "", Span<const ArgSpec>(), CmdHandler(), 0) ==
                   Status::Invalid);
}

// ---------------------------------------------------------------- kernel

void test_kernel_registers_service_targets() {
  TestBoard tb;
  Kernel kernel(tb.board);
  Probe net(ServiceId::Net, "net");
  net.withCommand = true;
  kernel.add(net);
  TEST_ASSERT_TRUE(kernel.begin() == Status::Ok);
  TEST_ASSERT_TRUE(kernel.begin() == Status::Busy);
  Probe late(ServiceId::Web, "web");
  TEST_ASSERT_TRUE(kernel.add(late) == Status::Denied);

  run(kernel, tb.clock, 50);
  CmdContext ctx;
  TEST_ASSERT_TRUE(kernel.commands().execute("sys.net", "ping", JsonValue(), ctx) == Status::Ok);
  TEST_ASSERT_EQUAL_INT(1, net.pings);

  Json<128> doc;
  doc.w.beginObject();
  TEST_ASSERT_TRUE(kernel.commands().describe("sys.net", doc.w) == Status::Ok);
  doc.w.endObject();
  TEST_ASSERT_TRUE(doc.parse()["ticks"].valid());
}

void test_kernel_report_parts() {
  TestBoard tb;
  Kernel kernel(tb.board);
  Probe net(ServiceId::Net, "net");
  Probe clock(ServiceId::Clock, "clock");
  Probe gpio(ServiceId::Gpio, "gpio");  // no report part
  net.part = "net";
  clock.part = "clock";
  kernel.add(net);
  kernel.add(clock);
  kernel.add(gpio);
  kernel.begin();
  kernel.config().sys.key.assign("KEY123");
  kernel.config().sys.deviceName.assign("Plugs");
  run(kernel, tb.clock, 50);

  Json<2048> all;
  kernel.report(all.w);
  TEST_ASSERT_TRUE(all.w.complete());
  const JsonValue r = all.parse();
  TEST_ASSERT_EQUAL_INT64(2, r["v"].asInt());
  TEST_ASSERT_TRUE(r["device"]["name"].stringEquals("Plugs"));
  TEST_ASSERT_TRUE(r["device"]["mode"].stringEquals("AP"));
  TEST_ASSERT_FALSE(r["device"]["key"].valid());  // only on request
  TEST_ASSERT_TRUE(r["net"].isObject());
  TEST_ASSERT_TRUE(r["clock"].isObject());
  TEST_ASSERT_FALSE(r["gpio"].valid());
  TEST_ASSERT_EQUAL_UINT(3, r["services"].size());

  Json<1024> some;
  kernel.report(some.w, "device, clock", true);
  const JsonValue s = some.parse();
  TEST_ASSERT_TRUE(s["device"]["key"].stringEquals("KEY123"));
  TEST_ASSERT_TRUE(s["clock"].isObject());
  TEST_ASSERT_FALSE(s["net"].valid());
  TEST_ASSERT_FALSE(s["services"].valid());
}

void test_kernel_restart_request() {
  TestBoard tb;
  Kernel kernel(tb.board);
  Probe net(ServiceId::Net, "net");
  kernel.add(net);
  kernel.begin();
  run(kernel, tb.clock, 50);

  const unsigned before = tb.native().sys.restartCount();
  kernel.requestRestart("test", false, 500);
  TEST_ASSERT_TRUE(kernel.restartPending());
  run(kernel, tb.clock, 300);
  TEST_ASSERT_EQUAL_UINT(before, tb.native().sys.restartCount());  // the answer can still leave
  TEST_ASSERT_TRUE(net.lastEvent == EventId::ResetRequested);
  run(kernel, tb.clock, 400);
  TEST_ASSERT_EQUAL_UINT(before + 1, tb.native().sys.restartCount());
  TEST_ASSERT_EQUAL_INT(1, net.stops);  // services are stopped before the restart
}

bool logHas(Kernel& kernel, const char* text) {
  static LogEntry entries[LMP_LOG_RING];
  const size_t n = kernel.log().read(0, entries, LMP_LOG_RING);
  for (size_t i = 0; i < n; ++i) {
    if (strstr(entries[i].msg, text)) return true;
  }
  return false;
}

void test_kernel_tells_the_previous_start_and_keeps_the_last_words() {
  TestBoard tb;
  hal::LastWords previous;
  snprintf(previous.why, sizeof(previous.why), "guard: network lost");
  previous.uptimeS = 42;
  previous.count = 2;
  snprintf(previous.lines[0], sizeof(previous.lines[0]), "first line");
  snprintf(previous.lines[1], sizeof(previous.lines[1]), "second line");
  tb.native().sys.setPrevious(&previous);

  Kernel kernel(tb.board);
  TEST_ASSERT_EQUAL_STRING("guard: network lost", kernel.previousWhy());
  LMP_LOGI(kLogSys, "before the words are kept");
  kernel.begin();   // tells, if nobody did
  TEST_ASSERT_TRUE(logHas(kernel, "previous start ended after 42 s: guard: network lost"));
  TEST_ASSERT_TRUE(logHas(kernel, "before: second line"));
  TEST_ASSERT_EQUAL_UINT(LMP_STALL_GUARD_S, tb.native().sys.stallGuardSeconds());

  // Only what is told from now on is kept, and not the details.
  const auto& lines = tb.native().sys.lastWordsLines();
  for (const std::string& line : lines) {
    TEST_ASSERT_NULL(strstr(line.c_str(), "before the words"));
    TEST_ASSERT_NULL(strstr(line.c_str(), "previous start"));
  }
  const size_t kept = lines.size();
  LMP_LOGD(kLogSys, "a detail");
  TEST_ASSERT_EQUAL_UINT(kept, tb.native().sys.lastWordsLines().size());
  LMP_LOGW(kLogSys, "something went wrong");
  TEST_ASSERT_EQUAL_UINT(kept + 1, tb.native().sys.lastWordsLines().size());
  TEST_ASSERT_NOT_NULL(strstr(tb.native().sys.lastWordsLines().back().c_str(),
                              "W sys: something went wrong"));
}

void test_kernel_restart_leaves_its_reason() {
  TestBoard tb;
  Kernel kernel(tb.board);
  kernel.begin();
  TEST_ASSERT_EQUAL_STRING("", kernel.previousWhy());
  kernel.requestRestart("asked by the user");
  TEST_ASSERT_EQUAL_STRING("asked by the user", tb.native().sys.lastWordsReason().c_str());
}

void test_kernel_guard_restart_keeps_its_reason() {
  TestBoard tb;
  Kernel kernel(tb.board);
  kernel.begin();
  run(kernel, tb.clock, 50);
  const unsigned before = tb.native().sys.restartCount();
  kernel.guardRestart("requests to servers stuck");
  kernel.requestRestart("something else");   // does not hide what the guard saw
  TEST_ASSERT_EQUAL_STRING("guard: requests to servers stuck",
                           tb.native().sys.lastWordsReason().c_str());
  run(kernel, tb.clock, 1500);
  TEST_ASSERT_EQUAL_UINT(before + 1, tb.native().sys.restartCount());
}

void test_kernel_invoke_from_the_same_context() {
  TestBoard tb;
  Kernel kernel(tb.board);
  kernel.begin();
  kernel.tick();
  int value = 0;
  TEST_ASSERT_TRUE(kernel.invoke([&value]() { value = 7; }) == Status::Ok);
  TEST_ASSERT_EQUAL_INT(7, value);
  TEST_ASSERT_TRUE(kernel.invoke(Delegate<void()>()) == Status::Invalid);
}

void test_kernel_invoke_from_another_thread() {
  TestBoard tb;
  // This test needs real time: the kernel runs on its own thread.
  tb.board.clock = &tb.native().clock;
  Kernel kernel(tb.board);
  kernel.begin();

  std::atomic<bool> running{false};
  std::thread app([&kernel, &running]() {
    running = true;
    kernel.run();
  });
  while (!running) std::this_thread::yield();
  std::this_thread::sleep_for(std::chrono::milliseconds(30));

  std::thread::id ranOn;
  int calls = 0;
  for (int i = 0; i < 20; ++i) {
    const Status s = kernel.invoke([&ranOn, &calls]() {
      ranOn = std::this_thread::get_id();
      ++calls;
    });
    TEST_ASSERT_TRUE(s == Status::Ok);
  }
  TEST_ASSERT_EQUAL_INT(20, calls);
  TEST_ASSERT_TRUE(ranOn == app.get_id());
  TEST_ASSERT_FALSE(kernel.onAppTask());

  kernel.stop();
  app.join();
}

void test_kernel_invoke_timeout() {
  TestBoard tb;
  tb.board.clock = &tb.native().clock;
  Kernel kernel(tb.board);
  kernel.begin();
  kernel.tick();  // this thread is now the application task, and it will not tick again

  int calls = 0;
  Status result = Status::Ok;
  std::thread other([&kernel, &calls, &result]() {
    result = kernel.invoke([&calls]() { ++calls; }, 100);
  });
  other.join();
  TEST_ASSERT_TRUE(result == Status::Timeout);
  TEST_ASSERT_EQUAL_INT(0, calls);

  kernel.tick();  // a call that gave up is not run later
  TEST_ASSERT_EQUAL_INT(0, calls);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_events_are_delivered_by_dispatch);
  RUN_TEST(test_events_published_by_handlers_wait_for_the_next_dispatch);
  RUN_TEST(test_events_queue_full);
  RUN_TEST(test_events_subscriber_table_full);
  RUN_TEST(test_registry_starts_in_dependency_order);
  RUN_TEST(test_registry_rejects_duplicates_and_cycles);
  RUN_TEST(test_registry_missing_dependency_blocks_only_that_branch);
  RUN_TEST(test_registry_mode_gating);
  RUN_TEST(test_registry_stops_dependents_when_a_dependency_fails);
  RUN_TEST(test_registry_degraded_dependency_keeps_dependents_running);
  RUN_TEST(test_registry_retries_with_growing_delay);
  RUN_TEST(test_registry_policy_never);
  RUN_TEST(test_registry_critical_failure_restarts_the_board);
  RUN_TEST(test_registry_pending_start);
  RUN_TEST(test_registry_start_timeout);
  RUN_TEST(test_registry_ticks_at_the_declared_period);
  RUN_TEST(test_registry_publishes_state_changes);
  RUN_TEST(test_registry_describe);
  RUN_TEST(test_commands_execute_with_json);
  RUN_TEST(test_commands_reject_bad_arguments);
  RUN_TEST(test_commands_unknown_target_or_command);
  RUN_TEST(test_commands_ignore_unknown_arguments);
  RUN_TEST(test_commands_result_and_pending);
  RUN_TEST(test_commands_from_text);
  RUN_TEST(test_commands_never_log_secrets);
  RUN_TEST(test_commands_documents);
  RUN_TEST(test_a_command_says_what_it_is);
  RUN_TEST(test_commands_registration_limits);
  RUN_TEST(test_kernel_registers_service_targets);
  RUN_TEST(test_kernel_report_parts);
  RUN_TEST(test_kernel_restart_request);
  RUN_TEST(test_kernel_tells_the_previous_start_and_keeps_the_last_words);
  RUN_TEST(test_kernel_restart_leaves_its_reason);
  RUN_TEST(test_kernel_guard_restart_keeps_its_reason);
  RUN_TEST(test_kernel_invoke_from_the_same_context);
  RUN_TEST(test_kernel_invoke_from_another_thread);
  RUN_TEST(test_kernel_invoke_timeout);
  return UNITY_END();
}
