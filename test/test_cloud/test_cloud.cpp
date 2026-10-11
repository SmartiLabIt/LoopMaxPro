// Tests on the PC of the cloud, public part: what the device tells the server and when, and
// what it does with the requests of the server. The bridge is a fake that keeps what it is
// given and hands over what the test writes.
#include <string>
#include <vector>

#include "../support/online_system.h"

using namespace lmp;
using namespace lmp::test;

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

class FakeBridge : public ICloudBridge {
 public:
  bool up = false;
  bool busy = false;
  int polls = 0;
  std::vector<std::string> sent;

  const char* name() const override { return "fake"; }
  void poll(uint32_t) override { ++polls; }
  bool connected() const override { return up; }
  Status send(StringView envelope) override {
    if (busy) return Status::Busy;
    sent.emplace_back(envelope.data, envelope.size);
    return Status::Ok;
  }
  void onReceive(CloudReceiveFn fn) override { deliver_ = fn; }
  void describe(JsonWriter& w) const override { w.str("server", "test.example"); }

  // The server sends an envelope.
  void receive(const std::string& text) { deliver_(StringView(text.data(), text.size())); }

  // Envelopes of a type, oldest first.
  std::vector<std::string> ofType(const char* type) const {
    std::vector<std::string> out;
    for (const std::string& e : sent) {
      if (JsonValue::parse(StringView(e.data(), e.size()))["type"].stringEquals(type)) {
        out.push_back(e);
      }
    }
    return out;
  }

 private:
  CloudReceiveFn deliver_;
};

// A parsed envelope, with its text kept alive.
struct Env {
  std::string text;
  JsonValue v;
  explicit Env(const std::string& t) : text(t) { v = JsonValue::parse(StringView(text.data(), text.size())); }
};

std::string keyOf(OnlineSystem& sys) { return sys.kernel().config().sys.key.c_str(); }

// The body of an envelope, as text.
std::string bodyOf(const std::string& envelope) {
  const Env e(envelope);
  const StringView raw = e.v["body"].raw();
  return std::string(raw.data, raw.size);
}

// The server of the test asks, and takes the answer with its ref.
std::string ask(OnlineSystem& sys, FakeBridge& bridge, const std::string& request, const char* ref) {
  bridge.receive(request);
  sys.advance(50);
  for (auto it = bridge.sent.rbegin(); it != bridge.sent.rend(); ++it) {
    if (Env(*it).v["ref"].stringEquals(ref)) return bodyOf(*it);
  }
  TEST_FAIL_MESSAGE(ref);
  return std::string();
}

// The console on the device: a session, and the answers of the API.
struct Console {
  OnlineSystem& sys;
  std::string token;
  explicit Console(OnlineSystem& s) : sys(s) {
    const auto r = sys.pc().http.post("/api/auth/login", "{\"password\":\"loopmaxap\"}");
    TEST_ASSERT_EQUAL_UINT16(200, r.status);
    char text[64] = "";
    JsonValue::parse(StringView(r.body.data(), r.body.size()))["token"].getString(text, sizeof(text));
    token = text;
  }
  std::string get(const char* path) {
    const auto r = sys.pc().http.get(path, token.c_str());
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(200, r.status, path);
    return r.body;
  }
};

}  // namespace

// ---------------------------------------------------------------- without a bridge

void test_without_a_bridge_nothing_happens() {
  OnlineSystem sys;
  TEST_ASSERT_TRUE(sys.stateOf(ServiceId::Cloud) == ServiceState::Ready);
  Doc doc(sys.kernel(), "sys.cloud");
  TEST_ASSERT_FALSE(doc.v["attached"].asBool(true));
  TEST_ASSERT_FALSE(doc.v["connected"].asBool(true));
  TEST_ASSERT_EQUAL_INT64(15, doc.v["heartbeatMin"].asInt());
  TEST_ASSERT_TRUE(sys.command("sys.cloud", "report", "") == Status::Unsupported);
}

// ---------------------------------------------------------------- connection

void test_connection_sends_everything() {
  OnlineSystem sys;
  FakeBridge bridge;
  TEST_ASSERT_TRUE(sys.app.cloud().attach(bridge) == Status::Ok);
  FakeBridge other;
  TEST_ASSERT_TRUE(sys.app.cloud().attach(other) == Status::Busy);
  sys.advance(200);
  TEST_ASSERT_TRUE(bridge.polls > 0);
  TEST_ASSERT_EQUAL_UINT(0, bridge.sent.size());   // not connected yet

  bridge.up = true;
  sys.advance(1000);
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::CloudConnected));
  const auto reports = bridge.ofType("report");
  TEST_ASSERT_EQUAL_UINT(1, reports.size());
  Env r(reports[0]);
  TEST_ASSERT_EQUAL_INT64(2, r.v["v"].asInt());
  TEST_ASSERT_TRUE(r.v["key"].stringEquals(keyOf(sys).c_str()));
  // The time of the device: 0 while it does not know it, as here.
  TEST_ASSERT_TRUE(r.v["ts"].valid());
  TEST_ASSERT_TRUE(r.v["body"]["device"]["key"].valid());
  TEST_ASSERT_TRUE(r.v["body"]["system"].isObject());
  TEST_ASSERT_TRUE(r.v["body"]["services"].size() > 5);
  TEST_ASSERT_TRUE(r.v["body"]["modules"].isObject());
  // The data of every module follow the report.
  const auto docs = bridge.ofType("doc");
  TEST_ASSERT_EQUAL_UINT(1, docs.size());
  Env d(docs[0]);
  TEST_ASSERT_TRUE(d.v["target"].stringEquals("mod.demo"));
  TEST_ASSERT_EQUAL_UINT(2, d.v["body"]["pins"].size());

  Doc doc(sys.kernel(), "sys.cloud");
  TEST_ASSERT_TRUE(doc.v["connected"].asBool());
  TEST_ASSERT_TRUE(doc.v["bridge"].stringEquals("fake"));
  TEST_ASSERT_TRUE(doc.v["detail"]["server"].stringEquals("test.example"));
  TEST_ASSERT_EQUAL_INT64(2, doc.v["sent"].asInt());
}

void test_connection_lost_and_found_again() {
  OnlineSystem sys;
  FakeBridge bridge;
  sys.app.cloud().attach(bridge);
  bridge.up = true;
  sys.advance(1000);
  bridge.up = false;
  sys.advance(200);
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::CloudLost));
  bridge.sent.clear();
  bridge.up = true;
  sys.advance(1000);
  // The server knows nothing of the time away: the whole report again.
  TEST_ASSERT_EQUAL_UINT(1, bridge.ofType("report").size());
  TEST_ASSERT_TRUE(Env(bridge.ofType("report")[0]).v["body"]["system"].isObject());
  TEST_ASSERT_EQUAL_INT(2, sys.count(EventId::CloudConnected));
}

void test_a_busy_bridge_gets_it_later() {
  OnlineSystem sys;
  FakeBridge bridge;
  sys.app.cloud().attach(bridge);
  bridge.busy = true;
  bridge.up = true;
  sys.advance(2000);
  TEST_ASSERT_EQUAL_UINT(0, bridge.sent.size());
  Doc doc(sys.kernel(), "sys.cloud");
  TEST_ASSERT_TRUE(doc.v["pending"]["report"].asBool());
  bridge.busy = false;
  sys.advance(1000);
  TEST_ASSERT_EQUAL_UINT(1, bridge.ofType("report").size());
  TEST_ASSERT_EQUAL_UINT(1, bridge.ofType("doc").size());
}

// ---------------------------------------------------------------- what changes

void test_changes_are_told_grouped() {
  OnlineSystem sys;
  FakeBridge bridge;
  sys.app.cloud().attach(bridge);
  bridge.up = true;
  sys.advance(1500);
  bridge.sent.clear();

  // Data of a module: one envelope for many changes close together.
  for (int i = 0; i < 3; ++i) {
    TEST_ASSERT_TRUE(sys.command("mod.demo", "toggle", "{\"channel\":1}") == Status::Ok);
    sys.advance(60);
  }
  sys.advance(LMP_MODULE_DATA_DELAY_MS + LMP_CLOUD_DATA_MS + 100);
  const auto docs = bridge.ofType("doc");
  TEST_ASSERT_EQUAL_UINT(1, docs.size());
  TEST_ASSERT_EQUAL_INT64(1, Env(docs[0]).v["body"]["pins"].at(1)["level"].asInt());

  // A change of the settings of the device: a partial report, a moment later.
  bridge.sent.clear();
  TEST_ASSERT_TRUE(sys.command("sys.config", "set", "{\"deviceName\":\"Office Plugs\"}") ==
                   Status::Ok);
  sys.advance(LMP_CLOUD_PARTIAL_MS / 2);
  TEST_ASSERT_EQUAL_UINT(0, bridge.ofType("report").size());
  sys.advance(LMP_CLOUD_PARTIAL_MS);
  const auto reports = bridge.ofType("report");
  TEST_ASSERT_EQUAL_UINT(1, reports.size());
  Env r(reports[0]);
  TEST_ASSERT_TRUE(r.v["body"]["device"]["name"].stringEquals("Office Plugs"));
  TEST_ASSERT_FALSE(r.v["body"]["services"].valid());   // only the part that changed
}

void test_the_heartbeat() {
  OnlineSystem sys;
  FakeBridge bridge;
  sys.app.cloud().attach(bridge);
  TEST_ASSERT_TRUE(sys.command("sys.cloud", "set", "{\"heartbeatMin\":1}") == Status::Ok);
  bridge.up = true;
  sys.advance(1500);
  bridge.sent.clear();
  sys.advance(61000);
  const auto reports = bridge.ofType("report");
  TEST_ASSERT_EQUAL_UINT(1, reports.size());
  Env r(reports[0]);
  TEST_ASSERT_TRUE(r.v["body"]["system"].isObject());
  TEST_ASSERT_TRUE(r.v["body"]["net"].isObject());
  TEST_ASSERT_TRUE(r.v["body"]["clock"].isObject());
  TEST_ASSERT_FALSE(r.v["body"]["services"].valid());

  // 0: no periodic report.
  TEST_ASSERT_TRUE(sys.command("sys.cloud", "set", "{\"heartbeatMin\":0}") == Status::Ok);
  sys.advance(LMP_CLOUD_PARTIAL_MS + 200);
  bridge.sent.clear();
  sys.advance(130000);
  TEST_ASSERT_EQUAL_UINT(0, bridge.ofType("report").size());
}

void test_errors_are_pushed_when_asked() {
  OnlineSystem sys;
  FakeBridge bridge;
  sys.app.cloud().attach(bridge);
  bridge.up = true;
  sys.advance(1500);
  bridge.sent.clear();

  LMP_LOGE(kLogSys, "something broke: %d", 7);
  sys.advance(200);
  TEST_ASSERT_EQUAL_UINT(0, bridge.ofType("log").size());   // off by default

  TEST_ASSERT_TRUE(sys.command("sys.cloud", "set", "{\"pushLogLevel\":2}") == Status::Ok);
  LMP_LOGW(kLogSys, "watch out");
  LMP_LOGI(kLogSys, "all good");
  sys.advance(200);
  const auto logs = bridge.ofType("log");
  TEST_ASSERT_EQUAL_UINT(1, logs.size());
  Env l(logs[0]);
  TEST_ASSERT_EQUAL_UINT(1, l.v["body"]["items"].size());
  TEST_ASSERT_TRUE(l.v["body"]["items"].at(0)["msg"].stringEquals("watch out"));
  TEST_ASSERT_TRUE(l.v["body"]["items"].at(0)["lvl"].stringEquals("warn"));
}

// ---------------------------------------------------------------- what the server asks

void test_a_command_of_the_server() {
  OnlineSystem sys;
  FakeBridge bridge;
  sys.app.cloud().attach(bridge);
  bridge.up = true;
  sys.advance(1500);
  bridge.sent.clear();

  bridge.receive("{\"v\":2,\"type\":\"cmd\",\"key\":\"" + keyOf(sys) +
                 "\",\"target\":\"mod.demo\",\"name\":\"set\",\"ref\":\"a81f\","
                 "\"body\":{\"channel\":1,\"value\":true}}");
  sys.advance(100);
  const auto acks = bridge.ofType("ack");
  TEST_ASSERT_EQUAL_UINT(1, acks.size());
  Env a(acks[0]);
  TEST_ASSERT_TRUE(a.v["ref"].stringEquals("a81f"));
  TEST_ASSERT_TRUE(a.v["target"].stringEquals("mod.demo"));
  TEST_ASSERT_TRUE(a.v["body"]["ok"].asBool());
  hal::PinLevel level = hal::PinLevel::Low;
  sys.pc().gpio.read(41, level);
  TEST_ASSERT_TRUE(level == hal::PinLevel::High);
  TEST_ASSERT_TRUE(sys.logHas("mod.demo set from remote: ok"));

  // A value refused: the answer says which one.
  bridge.receive("{\"type\":\"cmd\",\"target\":\"mod.demo\",\"name\":\"set\",\"ref\":\"b2\","
                 "\"body\":{\"channel\":9,\"value\":true}}");
  sys.advance(100);
  Env refused(bridge.ofType("ack").back());
  TEST_ASSERT_TRUE(refused.v["ref"].stringEquals("b2"));
  TEST_ASSERT_FALSE(refused.v["body"]["ok"].asBool(true));
  TEST_ASSERT_TRUE(refused.v["body"]["field"].stringEquals("channel"));
}

void test_a_command_that_ends_later() {
  OnlineSystem sys;
  FakeBridge bridge;
  sys.app.cloud().attach(bridge);
  bridge.up = true;
  sys.advance(1500);
  bridge.sent.clear();

  bridge.receive("{\"type\":\"cmd\",\"target\":\"sys.net\",\"name\":\"scan\",\"ref\":\"s1\"}");
  sys.advance(3000);
  const auto acks = bridge.ofType("ack");
  TEST_ASSERT_EQUAL_UINT(2, acks.size());
  Env first(acks[0]);
  TEST_ASSERT_TRUE(first.v["body"]["status"].stringEquals("pending"));
  Env last(acks[1]);
  TEST_ASSERT_TRUE(last.v["ref"].stringEquals("s1"));
  TEST_ASSERT_TRUE(last.v["body"]["status"].stringEquals("ok"));
  TEST_ASSERT_TRUE(last.v["body"]["ok"].asBool());

  // The bridge busy when the command ends: the outcome leaves as soon as it can.
  bridge.sent.clear();
  bridge.receive("{\"type\":\"cmd\",\"target\":\"sys.net\",\"name\":\"scan\",\"ref\":\"s2\"}");
  sys.advance(100);
  TEST_ASSERT_EQUAL_UINT(1, bridge.ofType("ack").size());
  bridge.busy = true;
  sys.advance(3000);
  TEST_ASSERT_EQUAL_UINT(1, bridge.ofType("ack").size());
  bridge.busy = false;
  sys.advance(50);
  TEST_ASSERT_EQUAL_UINT(2, bridge.ofType("ack").size());
  TEST_ASSERT_TRUE(Env(bridge.ofType("ack")[1]).v["ref"].stringEquals("s2"));
}

void test_documents_asked_by_the_server() {
  OnlineSystem sys;
  FakeBridge bridge;
  sys.app.cloud().attach(bridge);
  bridge.up = true;
  sys.advance(1500);
  bridge.sent.clear();

  bridge.receive("{\"type\":\"doc\",\"target\":\"sys.net\",\"ref\":\"d1\"}");
  sys.advance(50);
  Env doc(bridge.sent.back());
  TEST_ASSERT_TRUE(doc.v["type"].stringEquals("doc"));
  TEST_ASSERT_TRUE(doc.v["ref"].stringEquals("d1"));
  TEST_ASSERT_TRUE(doc.v["body"]["sta"]["connected"].asBool());

  bridge.receive("{\"type\":\"doc\",\"target\":\"sys.nothing\",\"ref\":\"d2\"}");
  sys.advance(50);
  TEST_ASSERT_TRUE(Env(bridge.sent.back()).v["body"]["error"].stringEquals("not_found"));

  bridge.receive("{\"type\":\"report\",\"ref\":\"r1\",\"body\":\"system,clock\"}");
  sys.advance(50);
  Env rep(bridge.sent.back());
  TEST_ASSERT_TRUE(rep.v["ref"].stringEquals("r1"));
  TEST_ASSERT_TRUE(rep.v["body"]["system"].isObject());
  TEST_ASSERT_FALSE(rep.v["body"]["net"].valid());

  bridge.receive("{\"type\":\"manifest\",\"target\":\"mod.demo\",\"ref\":\"m1\"}");
  sys.advance(50);
  Env man(bridge.sent.back());
  TEST_ASSERT_TRUE(man.v["body"]["commands"].size() > 5);
  TEST_ASSERT_TRUE(man.v["body"]["config"].isArray());

  bridge.receive("{\"type\":\"manifest\",\"ref\":\"m2\"}");
  sys.advance(50);
  TEST_ASSERT_TRUE(Env(bridge.sent.back()).v["body"]["targets"].size() > 10);

  bridge.receive("{\"type\":\"log\",\"ref\":\"l1\",\"body\":{\"since\":0,\"max\":3}}");
  sys.advance(50);
  Env log(bridge.sent.back());
  TEST_ASSERT_TRUE(log.v["ref"].stringEquals("l1"));
  TEST_ASSERT_EQUAL_UINT(3, log.v["body"]["items"].size());
  TEST_ASSERT_EQUAL_INT64(1, log.v["body"]["items"].at(0)["seq"].asInt());
}

void test_requests_that_are_refused() {
  OnlineSystem sys;
  FakeBridge bridge;
  sys.app.cloud().attach(bridge);
  bridge.up = true;
  sys.advance(1500);
  bridge.sent.clear();

  // For another device: not run, not answered.
  bridge.receive("{\"type\":\"cmd\",\"key\":\"someone-else\",\"target\":\"mod.demo\","
                 "\"name\":\"all\",\"ref\":\"x\",\"body\":{\"value\":true}}");
  sys.advance(50);
  TEST_ASSERT_EQUAL_UINT(0, bridge.sent.size());
  TEST_ASSERT_TRUE(sys.logHas("request for another device"));

  bridge.receive("not json at all");
  bridge.receive("{\"type\":\"dance\",\"ref\":\"z\"}");
  sys.advance(100);
  Env err(bridge.sent.back());
  TEST_ASSERT_TRUE(err.v["type"].stringEquals("error"));
  TEST_ASSERT_TRUE(err.v["body"]["error"].stringEquals("unknown_type"));

  bridge.receive("{\"type\":\"cmd\",\"ref\":\"n\"}");
  sys.advance(50);
  TEST_ASSERT_TRUE(Env(bridge.sent.back()).v["body"]["error"].stringEquals("invalid"));

  bridge.receive(std::string("{\"type\":\"cmd\",\"body\":\"") + std::string(LMP_CLOUD_IN_MAX, 'x') + "\"}");
  sys.advance(50);
  TEST_ASSERT_TRUE(sys.logHas("too long"));

  Doc doc(sys.kernel(), "sys.cloud");
  // Another device, not JSON, unknown type, command without target, too long.
  TEST_ASSERT_EQUAL_INT64(5, doc.v["refused"].asInt());

  // More requests than the inbox holds, in one go: the ones in excess are dropped.
  for (int i = 0; i < LMP_CLOUD_INBOX + 2; ++i) bridge.receive("{\"type\":\"doc\",\"target\":\"sys.clock\"}");
  sys.advance(200);
  Doc after(sys.kernel(), "sys.cloud");
  TEST_ASSERT_EQUAL_INT64(2, after.v["dropped"].asInt());
}

void test_the_time_from_the_server() {
  OnlineSystem sys(true, nullptr, 1500);
  FakeBridge bridge;
  sys.pc().ntp.setAuto(false);
  sys.app.cloud().attach(bridge);
  bridge.up = true;
  sys.advance(300);
  if (!sys.app.clock().synced()) {
    bridge.receive("{\"type\":\"time\",\"body\":{\"unix\":1790000000}}");
    sys.advance(50);
    TEST_ASSERT_TRUE(sys.app.clock().synced());
    Doc clock(sys.kernel(), "sys.clock");
    TEST_ASSERT_TRUE(clock.v["source"].stringEquals("cloud"));
  }
}

void test_detach() {
  OnlineSystem sys;
  FakeBridge bridge;
  sys.app.cloud().attach(bridge);
  bridge.up = true;
  sys.advance(1000);
  sys.app.cloud().detach(bridge);
  sys.advance(20);
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::CloudLost));
  TEST_ASSERT_FALSE(sys.app.cloud().connected());
  const size_t before = bridge.sent.size();
  TEST_ASSERT_TRUE(sys.command("mod.demo", "toggle", "{\"channel\":0}") == Status::Ok);
  sys.advance(2000);
  TEST_ASSERT_EQUAL_UINT(before, bridge.sent.size());
  FakeBridge other;
  TEST_ASSERT_TRUE(sys.app.cloud().attach(other) == Status::Ok);
}

// ---------------------------------------------------------------- the console on the server

// What the console reads on the device, the server has it the same: the document of the
// module, its commands, its settings, the settings of the system, the start of the console.
// With these and the package of the console (docs/web-console.md) the server draws the same interface.
void test_the_server_has_what_the_console_reads() {
  OnlineSystem sys;
  Console console(sys);
  FakeBridge bridge;
  sys.app.cloud().attach(bridge);
  bridge.up = true;
  sys.advance(1500);

  // The document of the module, sent at the connection: the same text as the API.
  const auto docs = bridge.ofType("doc");
  TEST_ASSERT_EQUAL_UINT(1, docs.size());
  TEST_ASSERT_EQUAL_STRING(console.get("/api/doc/mod.demo").c_str(), bodyOf(docs[0]).c_str());

  // Commands and schema of the settings, asked once.
  TEST_ASSERT_EQUAL_STRING(console.get("/api/manifest/mod.demo").c_str(),
                           ask(sys, bridge, "{\"type\":\"manifest\",\"target\":\"mod.demo\",\"ref\":\"m\"}", "m").c_str());

  // The settings, of the module and of the system, without secrets.
  const char* targets[] = {"mod.demo", "sys.config", "sys.net", "sys.clock", "sys.geo", "sys.mqtt",
                           "sys.web", "sys.cloud"};
  for (const char* target : targets) {
    const std::string path = std::string("/api/cfg/") + target;
    const std::string request = std::string("{\"type\":\"cfg\",\"target\":\"") + target + "\",\"ref\":\"c\"}";
    TEST_ASSERT_EQUAL_STRING_MESSAGE(console.get(path.c_str()).c_str(),
                                     ask(sys, bridge, request, "c").c_str(), target);
  }
  const std::string secret = ask(sys, bridge, "{\"type\":\"cfg\",\"target\":\"sys.net\",\"ref\":\"n\"}", "n");
  TEST_ASSERT_NULL(strstr(secret.c_str(), kHomePassword));
  TEST_ASSERT_TRUE(Env("{\"body\":" + ask(sys, bridge, "{\"type\":\"cfg\",\"target\":\"sys.none\",\"ref\":\"x\"}", "x") +
                       "}").v["body"]["error"].stringEquals("not_found"));

  // The start of the console (GET /api/state) from the report: name, language, modules.
  const std::string stateText = console.get("/api/state");
  const JsonValue state = JsonValue::parse(StringView(stateText.data(), stateText.size()));
  const std::string reportText = bodyOf(bridge.ofType("report")[0]);
  const JsonValue report = JsonValue::parse(StringView(reportText.data(), reportText.size()));
  const char* same[] = {"name", "host", "board", "code", "fw", "lang", "mode"};
  for (const char* key : same) {
    char a[48] = "", b[48] = "";
    state[key].getString(a, sizeof(a));
    report["device"][key].getString(b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(a, b, key);
  }
  TEST_ASSERT_EQUAL_UINT(state["modules"].size(), report["modules"]["list"].size());
  const char* entry[] = {"name", "title", "icon", "kind", "version"};
  for (size_t i = 0; i < state["modules"].size(); ++i) {
    for (const char* key : entry) {
      char a[48] = "", b[48] = "";
      state["modules"].at(i)[key].getString(a, sizeof(a));
      report["modules"]["list"].at(i)[key].getString(b, sizeof(b));
      TEST_ASSERT_EQUAL_STRING_MESSAGE(a, b, key);
    }
  }
}

// Device and server stay the same: what changes on one side reaches the other, whoever
// changed it; the events the console follows reach the server too.
// The things of the modules, for a server that knows no module (sdk/things.h): their
// values come with the document, what they are and the command that sets one are commands.
void test_the_things_of_the_modules_reach_the_server() {
  OnlineSystem sys;
  FakeBridge bridge;
  sys.app.cloud().attach(bridge);
  bridge.up = true;
  sys.advance(1500);

  // At the connection, inside the document of the module.
  const auto docs = bridge.ofType("doc");
  TEST_ASSERT_EQUAL_UINT(1, docs.size());
  Env first(docs[0]);
  TEST_ASSERT_TRUE(first.v["target"].stringEquals("mod.demo"));
  const JsonValue things = first.v["body"]["things"];
  TEST_ASSERT_TRUE_MESSAGE(things.valid(), docs[0].c_str());
  const int64_t gen = things["gen"].asInt();
  const int64_t rev = things["rev"].asInt();
  TEST_ASSERT_TRUE(gen > 0);
  TEST_ASSERT_TRUE(things["values"]["ch0"].isBool());
  TEST_ASSERT_FALSE(things["values"]["ch0"].asBool(true));

  // What they are: asked when the gen is not the one the server has.
  Env what("{\"body\":" + ask(sys, bridge,
      "{\"type\":\"cmd\",\"target\":\"mod.demo\",\"name\":\"things\",\"ref\":\"t1\"}", "t1") + "}");
  TEST_ASSERT_TRUE(what.v["body"]["ok"].asBool());
  const JsonValue told = what.v["body"]["result"];
  TEST_ASSERT_EQUAL_INT64(gen, told["gen"].asInt());
  TEST_ASSERT_TRUE(told["things"].at(0)["id"].stringEquals("ch0"));
  TEST_ASSERT_TRUE(told["things"].at(0)["kind"].stringEquals("toggle"));

  // A thing turned on from the server: the answer, then the document with the new value.
  bridge.sent.clear();
  Env done("{\"body\":" + ask(sys, bridge,
      "{\"type\":\"cmd\",\"target\":\"mod.demo\",\"name\":\"thing.set\",\"ref\":\"t2\","
      "\"body\":{\"thing\":\"ch0\",\"value\":\"ON\"}}", "t2") + "}");
  TEST_ASSERT_TRUE(done.v["body"]["ok"].asBool());
  sys.advance(900);
  const auto after = bridge.ofType("doc");
  TEST_ASSERT_TRUE(after.size() >= 1);
  Env last(after.back());
  TEST_ASSERT_TRUE(last.v["body"]["things"]["values"]["ch0"].asBool(false));
  TEST_ASSERT_TRUE(last.v["body"]["things"]["rev"].asInt() > rev);
  TEST_ASSERT_EQUAL_INT64(gen, last.v["body"]["things"]["gen"].asInt());
  TEST_ASSERT_TRUE(sys.logHas("mod.demo thing.set from remote: ok"));

  // A thing that is not there: refused as every command.
  Env none("{\"body\":" + ask(sys, bridge,
      "{\"type\":\"cmd\",\"target\":\"mod.demo\",\"name\":\"thing.set\",\"ref\":\"t3\","
      "\"body\":{\"thing\":\"ch9\",\"value\":\"ON\"}}", "t3") + "}");
  TEST_ASSERT_FALSE(none.v["body"]["ok"].asBool(true));
  TEST_ASSERT_TRUE(none.v["body"]["status"].stringEquals("not_found"));
}

void test_device_and_server_stay_in_step() {
  OnlineSystem sys;
  Console console(sys);
  FakeBridge bridge;
  sys.app.cloud().attach(bridge);
  bridge.up = true;
  sys.advance(1500);
  bridge.sent.clear();

  // From the console of the device: the server gets the new document.
  const auto r = sys.pc().http.post("/api/cmd/mod.demo/set", "{\"channel\":0,\"value\":true}",
                                    console.token.c_str());
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  sys.advance(LMP_MODULE_DATA_DELAY_MS + LMP_CLOUD_DATA_MS + 100);
  TEST_ASSERT_EQUAL_UINT(1, bridge.ofType("doc").size());
  TEST_ASSERT_EQUAL_STRING(console.get("/api/doc/mod.demo").c_str(),
                           bodyOf(bridge.ofType("doc")[0]).c_str());

  // From the server: the device changes, and the server gets the document again.
  bridge.sent.clear();
  bridge.receive("{\"type\":\"cmd\",\"target\":\"mod.demo\",\"name\":\"name\",\"ref\":\"n1\","
                 "\"body\":{\"channel\":1,\"name\":\"Pump\"}}");
  sys.advance(LMP_MODULE_DATA_DELAY_MS + LMP_CLOUD_DATA_MS + 100);
  const std::string local = console.get("/api/doc/mod.demo");
  TEST_ASSERT_NOT_NULL(strstr(local.c_str(), "Pump"));
  TEST_ASSERT_EQUAL_UINT(1, bridge.ofType("doc").size());
  TEST_ASSERT_EQUAL_STRING(local.c_str(), bodyOf(bridge.ofType("doc")[0]).c_str());

  // A setting of the module, from the server: in the document the console reads.
  bridge.sent.clear();
  bridge.receive("{\"type\":\"cmd\",\"target\":\"mod.demo\",\"name\":\"config.set\",\"ref\":\"c1\","
                 "\"body\":{\"pulseMs\":2500}}");
  sys.advance(LMP_MODULE_DATA_DELAY_MS + LMP_CLOUD_DATA_MS + 100);
  TEST_ASSERT_EQUAL_UINT(1, bridge.ofType("doc").size());
  TEST_ASSERT_EQUAL_INT64(2500, Env(bridge.ofType("doc")[0]).v["body"]["config"]["pulseMs"].asInt());
  TEST_ASSERT_EQUAL_STRING(console.get("/api/cfg/mod.demo").c_str(),
                           ask(sys, bridge, "{\"type\":\"cfg\",\"target\":\"mod.demo\",\"ref\":\"k\"}", "k").c_str());

  // A setting of the system from the console of the device: the same event reaches the
  // console and the server, and the server reads the new settings.
  const int client = sys.pc().http.follow(console.token.c_str());
  TEST_ASSERT_TRUE(client >= 0);
  sys.pc().http.taken(client);
  bridge.sent.clear();
  const auto m = sys.pc().http.post("/api/cmd/sys.config/set", "{\"deviceName\":\"house\"}",
                                    console.token.c_str());
  TEST_ASSERT_EQUAL_UINT16(200, m.status);
  sys.advance(100);
  const std::string stream = sys.pc().http.taken(client);
  TEST_ASSERT_NOT_NULL(strstr(stream.c_str(), "\"id\":\"config\""));
  const auto events = bridge.ofType("event");
  TEST_ASSERT_EQUAL_UINT(1, events.size());
  Env e(events[0]);
  TEST_ASSERT_TRUE(e.v["body"]["items"].at(0)["id"].stringEquals("config"));
  TEST_ASSERT_NOT_NULL(strstr(ask(sys, bridge, "{\"type\":\"cfg\",\"target\":\"sys.config\",\"ref\":\"q\"}", "q").c_str(),
                              "\"house\""));
  sys.pc().http.unfollow(client);
}

// The events go in one envelope for each round; the same event again takes the place of the
// one waiting. Not the data of a module, which goes as a document, nor the end of a command.
void test_events_for_the_server() {
  OnlineSystem sys;
  FakeBridge bridge;
  sys.app.cloud().attach(bridge);
  bridge.up = true;
  sys.advance(1500);
  bridge.sent.clear();

  sys.kernel().events().publish(Event(EventId::OtaProgress, 0, 10));
  sys.kernel().events().publish(Event(EventId::OtaProgress, 0, 20));
  sys.kernel().events().publish(Event(EventId::MqttLost));
  sys.kernel().events().publish(Event(EventId::ModuleData, 0));
  sys.advance(100);
  const auto events = bridge.ofType("event");
  TEST_ASSERT_EQUAL_UINT(1, events.size());
  Env e(events[0]);
  TEST_ASSERT_EQUAL_UINT(2, e.v["body"]["items"].size());
  TEST_ASSERT_TRUE(e.v["body"]["items"].at(0)["id"].stringEquals("ota.progress"));
  TEST_ASSERT_EQUAL_INT64(20, e.v["body"]["items"].at(0)["b"].asInt());
  TEST_ASSERT_TRUE(e.v["body"]["items"].at(1)["id"].stringEquals("mqtt.lost"));

  // Not connected: nothing waits for the server.
  bridge.up = false;
  sys.advance(100);
  sys.kernel().events().publish(Event(EventId::MqttLost));
  sys.advance(100);
  bridge.up = true;
  bridge.sent.clear();
  sys.advance(1500);
  TEST_ASSERT_EQUAL_UINT(0, bridge.ofType("event").size());
  TEST_ASSERT_EQUAL_UINT(1, bridge.ofType("report").size());   // everything again instead
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_without_a_bridge_nothing_happens);
  RUN_TEST(test_connection_sends_everything);
  RUN_TEST(test_connection_lost_and_found_again);
  RUN_TEST(test_a_busy_bridge_gets_it_later);
  RUN_TEST(test_changes_are_told_grouped);
  RUN_TEST(test_the_heartbeat);
  RUN_TEST(test_errors_are_pushed_when_asked);
  RUN_TEST(test_a_command_of_the_server);
  RUN_TEST(test_a_command_that_ends_later);
  RUN_TEST(test_documents_asked_by_the_server);
  RUN_TEST(test_requests_that_are_refused);
  RUN_TEST(test_the_time_from_the_server);
  RUN_TEST(test_detach);
  RUN_TEST(test_the_server_has_what_the_console_reads);
  RUN_TEST(test_the_things_of_the_modules_reach_the_server);
  RUN_TEST(test_device_and_server_stay_in_step);
  RUN_TEST(test_events_for_the_server);
  return UNITY_END();
}
