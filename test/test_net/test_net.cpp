// Tests of the network service on the PC, with a network that exists only in memory.
#include "lmp_version.h"
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "../support/test_support.h"
#include "app/loopmax.h"

using namespace lmp;
using namespace lmp::test;

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

constexpr const char* kHome = "Home";
constexpr const char* kHomePassword = "home-password";
constexpr const char* kOffice = "Office";
constexpr const char* kOfficePassword = "office-password";

// Writes the settings a device has after somebody configured a network.
void provision(TestBoard& tb, const char* ssid, const char* password) {
  tb.board.kv->begin();
  Config config(tb.board.kv, tb.board.rng, tb.board.sys);
  config.load();
  config.net.staSsid.assign(ssid);
  config.net.staPsw.assign(password);
  config.sys.mode = 2;
  config.touch(Config::kNet);
  config.touch(Config::kSys);
  TEST_ASSERT_TRUE(config.save() == Status::Ok);
}

struct System {
  TestBoard& tb;
  LoopMax app;
  std::vector<Event> events;

  explicit System(TestBoard& board, uint32_t runMs = 1000) : tb(board), app(board.board) {
    Logger::setInstance(&app.kernel().log());
    TEST_ASSERT_TRUE(app.begin() == Status::Ok);
    app.kernel().events().subscribe([this](const Event& e) { events.push_back(e); });
    advance(runMs);
  }

  Kernel& kernel() { return app.kernel(); }
  NetService& net() { return app.net(); }
  hal::native::Net& radio() { return tb.native().net; }
  const Config& config() { return app.kernel().config(); }
  void advance(uint32_t ms) { run(app.kernel(), tb.clock, ms); }

  Status command(const char* name, const char* json, const char** field = nullptr) {
    CmdContext ctx;
    ctx.source = CmdSource::Local;
    const Status s = kernel().commands().execute("sys.net", name, JsonValue::parse(json), ctx);
    if (field) *field = ctx.errorField;
    return s;
  }

  // How many events of a kind arrived, and the last one.
  int count(EventId id, Event* last = nullptr) const {
    int n = 0;
    for (const Event& e : events) {
      if (e.id != id) continue;
      ++n;
      if (last) *last = e;
    }
    return n;
  }
};

// State of the service, as the console reads it.
struct Doc {
  Json<4096> out;
  JsonValue v;

  explicit Doc(Kernel& kernel) {
    out.w.beginObject();
    TEST_ASSERT_TRUE(kernel.commands().describe("sys.net", out.w) == Status::Ok);
    out.w.endObject();
    TEST_ASSERT_TRUE(out.w.complete());
    v = out.parse();
    TEST_ASSERT_TRUE(v.valid());
  }
};

bool logHas(Kernel& kernel, const char* text) {
  LogEntry entries[LMP_LOG_RING];
  const size_t n = kernel.log().read(0, entries, LMP_LOG_RING);
  for (size_t i = 0; i < n; ++i) {
    if (strstr(entries[i].msg, text)) return true;
  }
  return false;
}

}  // namespace

// ---------------------------------------------------------------- access point

void test_first_start_is_an_access_point() {
  TestBoard tb;
  System sys(tb);

  TEST_ASSERT_TRUE(sys.net().state() == ServiceState::Ready);
  TEST_ASSERT_TRUE(sys.kernel().mode() == Mode::Ap);
  TEST_ASSERT_TRUE(sys.net().netState() == NetState::ApOnly);
  TEST_ASSERT_TRUE(sys.net().apUp());
  TEST_ASSERT_FALSE(sys.net().connected());
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::NetApUp));
  TEST_ASSERT_EQUAL_UINT(0, sys.radio().connects());

  hal::ApInfo ap;
  sys.radio().apInfo(ap);
  TEST_ASSERT_TRUE(ap.up);
  TEST_ASSERT_EQUAL_STRING("LoopMax-0001", ap.ssid);
  TEST_ASSERT_EQUAL_STRING("loopmaxap", sys.radio().apPassword().c_str());
  TEST_ASSERT_TRUE(ap.ip == hal::Ip4::of(192, 168, 4, 1));
  TEST_ASSERT_TRUE(ap.channel == 1 || ap.channel == 6 || ap.channel == 11);
  TEST_ASSERT_TRUE(sys.radio().captiveActive());
  TEST_ASSERT_EQUAL_STRING("loopmax-0001", sys.radio().mdnsName().c_str());
  TEST_ASSERT_EQUAL_STRING("loopmax-0001", sys.radio().hostname().c_str());

  Doc doc(sys.kernel());
  TEST_ASSERT_TRUE(doc.v["mode"].stringEquals("AP"));
  TEST_ASSERT_TRUE(doc.v["state"].stringEquals("ap"));
  TEST_ASSERT_TRUE(doc.v["ap"]["up"].asBool());
  TEST_ASSERT_TRUE(doc.v["ap"]["ip"].stringEquals("192.168.4.1"));
  TEST_ASSERT_TRUE(doc.v["ap"]["captive"].asBool());
  TEST_ASSERT_TRUE(doc.v["ap"]["pswDefault"].asBool());
  TEST_ASSERT_FALSE(doc.v["sta"]["connected"].asBool(true));
  TEST_ASSERT_TRUE(doc.v["sta"]["mac"].stringEquals("02:4C:4D:50:00:01"));
  TEST_ASSERT_TRUE(doc.v["host"].stringEquals("loopmax-0001.local"));
  TEST_ASSERT_TRUE(doc.v["mdns"].asBool());
}

void test_the_report_has_the_network() {
  TestBoard tb;
  System sys(tb);
  Json<8192> out;
  sys.kernel().report(out.w, StringView("net"));
  TEST_ASSERT_TRUE(out.w.complete());
  const JsonValue r = out.parse();
  TEST_ASSERT_TRUE(r["net"]["ap"]["up"].asBool());
  TEST_ASSERT_FALSE(r["system"].valid());
}

void test_captive_portal_can_be_turned_off() {
  TestBoard tb;
  tb.board.kv->begin();
  {
    Config config(tb.board.kv, tb.board.rng, tb.board.sys);
    config.load();
    config.net.captive = false;
    config.touch(Config::kNet);
    config.save();
  }
  System sys(tb);
  TEST_ASSERT_TRUE(sys.net().apUp());
  TEST_ASSERT_FALSE(sys.radio().captiveActive());
  TEST_ASSERT_FALSE(sys.net().captive());
}

void test_access_point_clients_are_counted() {
  TestBoard tb;
  System sys(tb);
  sys.radio().setApClients(2);
  sys.advance(200);
  Doc doc(sys.kernel());
  TEST_ASSERT_EQUAL_INT64(2, doc.v["ap"]["clients"].asInt());
}

void test_who_comes_from_the_access_point() {
  const hal::Ip4 client = hal::Ip4::of(192, 168, 4, 2);
  const hal::Ip4 other = hal::Ip4::of(192, 168, 1, 77);
  {
    TestBoard tb;
    tb.native().net.addNetwork(kHome, kHomePassword);
    System sys(tb);
    TEST_ASSERT_TRUE(sys.net().fromAccessPoint(client));
    TEST_ASSERT_TRUE(sys.net().fromAccessPoint(hal::Ip4::of(192, 168, 4, 254)));
    TEST_ASSERT_FALSE(sys.net().fromAccessPoint(other));
    TEST_ASSERT_FALSE(sys.net().fromAccessPoint(hal::Ip4::of(192, 168, 5, 2)));
    TEST_ASSERT_FALSE(sys.net().fromAccessPoint(hal::Ip4()));
    // The address of the device itself is not of a client.
    TEST_ASSERT_FALSE(sys.net().fromAccessPoint(hal::Ip4::of(192, 168, 4, 1)));

    // After a change of network, for as long as the access point stays up.
    TEST_ASSERT_TRUE(sys.command("join", "{\"ssid\":\"Home\",\"psw\":\"home-password\"}") ==
                     Status::Pending);
    sys.advance(1000);
    TEST_ASSERT_TRUE(sys.net().connected());
    TEST_ASSERT_TRUE(sys.net().fromAccessPoint(client));
    TEST_ASSERT_FALSE(sys.net().fromAccessPoint(other));
    sys.advance(31000);
    TEST_ASSERT_FALSE(sys.net().apUp());
    TEST_ASSERT_FALSE(sys.net().fromAccessPoint(client));
  }
  {
    // A network that gives the addresses of the access point: nobody can be told apart.
    TestBoard tb;
    tb.native().net.addNetwork(kHome, kHomePassword);
    tb.native().net.setStaAddress(hal::Ip4::of(192, 168, 4, 50));
    System sys(tb);
    TEST_ASSERT_TRUE(sys.net().fromAccessPoint(client));
    TEST_ASSERT_TRUE(sys.command("join", "{\"ssid\":\"Home\",\"psw\":\"home-password\"}") ==
                     Status::Pending);
    sys.advance(1000);
    TEST_ASSERT_TRUE(sys.net().connected());
    TEST_ASSERT_TRUE(sys.net().apUp());
    TEST_ASSERT_FALSE(sys.net().fromAccessPoint(client));
  }
}

// ---------------------------------------------------------------- client of a network

void test_lan_connects_to_the_configured_network() {
  TestBoard tb;
  tb.native().net.addNetwork(kHome, kHomePassword, -58, 6);
  provision(tb, kHome, kHomePassword);
  System sys(tb);

  TEST_ASSERT_TRUE(sys.kernel().mode() == Mode::Lan);
  TEST_ASSERT_TRUE(sys.net().netState() == NetState::Connected);
  TEST_ASSERT_TRUE(sys.net().connected());
  TEST_ASSERT_FALSE(sys.net().apUp());
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::NetStaConnected));
  TEST_ASSERT_EQUAL_INT(0, sys.count(EventId::NetApUp));
  TEST_ASSERT_EQUAL_UINT(1, sys.radio().connects());
  TEST_ASSERT_TRUE(sys.net().address() == hal::Ip4::of(192, 168, 1, 50));

  Doc doc(sys.kernel());
  TEST_ASSERT_TRUE(doc.v["state"].stringEquals("connected"));
  TEST_ASSERT_TRUE(doc.v["sta"]["connected"].asBool());
  TEST_ASSERT_TRUE(doc.v["sta"]["ssid"].stringEquals(kHome));
  TEST_ASSERT_TRUE(doc.v["sta"]["ip"].stringEquals("192.168.1.50"));
  TEST_ASSERT_TRUE(doc.v["sta"]["gateway"].stringEquals("192.168.1.1"));
  TEST_ASSERT_EQUAL_INT64(-58, doc.v["sta"]["rssi"].asInt());
  TEST_ASSERT_FALSE(doc.v["ap"]["up"].asBool(true));
}

void test_mode_lan_without_a_network_is_an_access_point() {
  TestBoard tb;
  provision(tb, "", "");
  System sys(tb);
  TEST_ASSERT_TRUE(sys.kernel().mode() == Mode::Ap);
  TEST_ASSERT_TRUE(sys.net().netState() == NetState::ApOnly);
  TEST_ASSERT_TRUE(sys.net().apUp());
  TEST_ASSERT_EQUAL_UINT8(1, sys.config().sys.mode);
}

void test_connection_lost_and_found() {
  TestBoard tb;
  tb.native().net.addNetwork(kHome, kHomePassword);
  provision(tb, kHome, kHomePassword);
  System sys(tb);
  TEST_ASSERT_TRUE(sys.net().connected());

  sys.radio().dropSta();
  sys.advance(300);
  TEST_ASSERT_TRUE(sys.net().netState() == NetState::Retrying);
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::NetStaLost));
  TEST_ASSERT_FALSE(sys.net().apUp());

  // The first new attempt comes two seconds later.
  sys.advance(1000);
  TEST_ASSERT_EQUAL_UINT(1, sys.radio().connects());
  sys.advance(2000);
  TEST_ASSERT_EQUAL_UINT(2, sys.radio().connects());
  TEST_ASSERT_TRUE(sys.net().connected());
  TEST_ASSERT_EQUAL_INT(2, sys.count(EventId::NetStaConnected));
  TEST_ASSERT_EQUAL_INT(0, sys.count(EventId::NetFallback));
}

void test_attempts_are_spaced_further_and_further() {
  TestBoard tb;
  provision(tb, kHome, kHomePassword);  // the network is not around
  tb.native().net.setDelays(1, 1);
  System sys(tb, 100);

  // Attempts at about 0, 2, 6 and 14 seconds: the wait doubles every time.
  sys.advance(1500);
  TEST_ASSERT_EQUAL_UINT(1, sys.radio().connects());
  sys.advance(1500);
  TEST_ASSERT_EQUAL_UINT(2, sys.radio().connects());
  sys.advance(2500);
  TEST_ASSERT_EQUAL_UINT(2, sys.radio().connects());
  sys.advance(2000);
  TEST_ASSERT_EQUAL_UINT(3, sys.radio().connects());
  sys.advance(6000);
  TEST_ASSERT_EQUAL_UINT(3, sys.radio().connects());
  sys.advance(3000);
  TEST_ASSERT_EQUAL_UINT(4, sys.radio().connects());
  TEST_ASSERT_TRUE(sys.net().netState() == NetState::Retrying);
}

void test_fallback_after_repeated_failures() {
  TestBoard tb;
  provision(tb, kHome, kHomePassword);  // the network is not around
  tb.native().net.setDelays(1, 1);
  System sys(tb, 100);
  TEST_ASSERT_FALSE(sys.net().apUp());
  TEST_ASSERT_EQUAL_INT(0, sys.count(EventId::NetApUp));

  // Five attempts: 0, 2, 6, 14 and 30 seconds.
  sys.advance(29000);
  TEST_ASSERT_TRUE(sys.net().netState() == NetState::Retrying);
  TEST_ASSERT_FALSE(sys.net().apUp());
  sys.advance(3000);
  TEST_ASSERT_EQUAL_UINT(LMP_NET_FALLBACK_AFTER, sys.radio().connects());
  TEST_ASSERT_TRUE(sys.net().netState() == NetState::Fallback);
  TEST_ASSERT_TRUE(sys.net().apUp());
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::NetFallback));
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::NetApUp));
  // The mode of the device does not change: it is still meant to be on the network.
  TEST_ASSERT_TRUE(sys.kernel().mode() == Mode::Lan);
  TEST_ASSERT_EQUAL_UINT8(2, sys.config().sys.mode);

  // In fallback the attempts are rare: each one disturbs who uses the access point.
  sys.advance(60000);
  TEST_ASSERT_EQUAL_UINT(LMP_NET_FALLBACK_AFTER, sys.radio().connects());

  // The network comes back.
  sys.radio().addNetwork(kHome, kHomePassword);
  sys.advance(62000);
  TEST_ASSERT_TRUE(sys.net().connected());
  TEST_ASSERT_TRUE(sys.net().apUp());  // still up, for who was using it
  sys.advance(31000);
  TEST_ASSERT_FALSE(sys.net().apUp());
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::NetApDown));
}

void test_an_attempt_without_answer_is_given_up() {
  TestBoard tb;
  tb.native().net.addNetwork(kHome, kHomePassword);
  tb.native().net.setDelays(100000, 1);  // the driver never reports the outcome
  provision(tb, kHome, kHomePassword);
  System sys(tb, 100);

  sys.advance(19000);
  TEST_ASSERT_TRUE(sys.net().netState() == NetState::Connecting);
  sys.advance(2000);
  TEST_ASSERT_TRUE(sys.net().netState() == NetState::Retrying);
  TEST_ASSERT_TRUE(logHas(sys.kernel(), "no answer"));
}

// ---------------------------------------------------------------- change of network

void test_join_from_the_access_point() {
  TestBoard tb;
  tb.native().net.addNetwork(kHome, kHomePassword, -52, 11);
  System sys(tb);

  TEST_ASSERT_TRUE(sys.command("join", "{\"ssid\":\"Home\",\"psw\":\"home-password\"}") ==
                   Status::Pending);
  TEST_ASSERT_TRUE(sys.net().joinPhase() == JoinPhase::Trying);
  // Nothing is written until the new network has accepted the device.
  TEST_ASSERT_EQUAL_STRING("", sys.config().net.staSsid.c_str());
  TEST_ASSERT_EQUAL_UINT8(1, sys.config().sys.mode);
  TEST_ASSERT_TRUE(sys.net().apUp());

  sys.advance(1000);
  TEST_ASSERT_TRUE(sys.net().connected());
  TEST_ASSERT_TRUE(sys.net().joinPhase() == JoinPhase::None);
  TEST_ASSERT_TRUE(sys.net().lastJoin() == JoinResult::Joined);
  TEST_ASSERT_TRUE(sys.kernel().mode() == Mode::Lan);
  TEST_ASSERT_EQUAL_STRING(kHome, sys.config().net.staSsid.c_str());
  TEST_ASSERT_EQUAL_STRING(kHomePassword, sys.config().net.staPsw.c_str());
  TEST_ASSERT_EQUAL_UINT8(2, sys.config().sys.mode);
  TEST_ASSERT_FALSE(sys.kernel().config().dirty());  // already written

  Event result;
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::NetJoinResult, &result));
  TEST_ASSERT_EQUAL_UINT8(1, result.b);
  TEST_ASSERT_EQUAL_UINT8(0, result.a);

  // Who asked for the change was on the access point: it stays up long enough to read
  // the new address.
  TEST_ASSERT_TRUE(sys.net().apUp());
  sys.advance(29000);
  TEST_ASSERT_TRUE(sys.net().apUp());
  sys.advance(2000);
  TEST_ASSERT_FALSE(sys.net().apUp());
  TEST_ASSERT_TRUE(sys.net().connected());
  TEST_ASSERT_EQUAL_UINT(1, sys.radio().connects());
}

void test_a_joined_network_survives_a_restart() {
  TestBoard tb;
  tb.native().net.addNetwork(kHome, kHomePassword);
  {
    System sys(tb);
    sys.command("join", "{\"ssid\":\"Home\",\"psw\":\"home-password\"}");
    sys.advance(1000);
    TEST_ASSERT_TRUE(sys.net().connected());
    Logger::setInstance(nullptr);
  }
  // Same storage, new system: as after a power cycle. The radio starts from nothing.
  tb.native().fs.unmount();
  tb.native().net.reset();
  tb.native().http.reset();
  tb.native().net.addNetwork(kHome, kHomePassword);
  System sys(tb);
  TEST_ASSERT_TRUE(sys.kernel().mode() == Mode::Lan);
  TEST_ASSERT_TRUE(sys.net().connected());
  TEST_ASSERT_FALSE(sys.net().apUp());
}

void test_join_refused_goes_back_to_the_access_point() {
  TestBoard tb;
  tb.native().net.addNetwork(kHome, kHomePassword);
  System sys(tb);

  TEST_ASSERT_TRUE(sys.command("join", "{\"ssid\":\"Home\",\"psw\":\"wrong-one-123\"}") ==
                   Status::Pending);
  // Three attempts, two seconds apart.
  sys.advance(6000);
  TEST_ASSERT_TRUE(sys.net().joinPhase() == JoinPhase::None);
  TEST_ASSERT_TRUE(sys.net().lastJoin() == JoinResult::Refused);
  TEST_ASSERT_TRUE(sys.net().netState() == NetState::ApOnly);
  TEST_ASSERT_TRUE(sys.net().apUp());
  TEST_ASSERT_TRUE(sys.kernel().mode() == Mode::Ap);
  TEST_ASSERT_EQUAL_STRING("", sys.config().net.staSsid.c_str());
  TEST_ASSERT_EQUAL_UINT8(1, sys.config().sys.mode);
  TEST_ASSERT_EQUAL_UINT(3, sys.radio().connects());

  Event result;
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::NetJoinResult, &result));
  TEST_ASSERT_EQUAL_UINT8(0, result.b);

  // Nothing keeps trying afterwards.
  sys.advance(60000);
  TEST_ASSERT_EQUAL_UINT(3, sys.radio().connects());

  Doc doc(sys.kernel());
  TEST_ASSERT_TRUE(doc.v["join"]["last"].stringEquals("refused"));
  TEST_ASSERT_TRUE(doc.v["join"]["ssid"].stringEquals(kHome));
}

void test_join_tells_a_missing_network_from_a_wrong_password() {
  TestBoard tb;
  tb.native().net.addNetwork(kHome, kHomePassword);
  System sys(tb);

  // A name written wrong: a hyphen in place of the underscore.
  sys.command("join", "{\"ssid\":\"Hom-e\",\"psw\":\"home-password\"}");
  sys.advance(6000);
  TEST_ASSERT_TRUE(sys.net().lastJoin() == JoinResult::NotFound);
  Doc missing(sys.kernel());
  TEST_ASSERT_TRUE(missing.v["join"]["last"].stringEquals("not_found"));
  TEST_ASSERT_TRUE(logHas(sys.kernel(), "'Hom-e': not found"));

  sys.command("join", "{\"ssid\":\"Home\",\"psw\":\"wrong-one-123\"}");
  sys.advance(6000);
  TEST_ASSERT_TRUE(sys.net().lastJoin() == JoinResult::Refused);
  Doc refused(sys.kernel());
  TEST_ASSERT_TRUE(refused.v["join"]["last"].stringEquals("refused"));
  TEST_ASSERT_TRUE(logHas(sys.kernel(), "'Home': not accepted"));
  TEST_ASSERT_TRUE(sys.net().netState() == NetState::ApOnly);
}

void test_join_from_a_network_goes_back_when_the_new_one_fails() {
  TestBoard tb;
  tb.native().net.addNetwork(kHome, kHomePassword);
  tb.native().net.addNetwork(kOffice, kOfficePassword);
  provision(tb, kHome, kHomePassword);
  System sys(tb);
  TEST_ASSERT_TRUE(sys.net().connected());

  TEST_ASSERT_TRUE(sys.command("join", "{\"ssid\":\"Office\",\"psw\":\"wrong-one-123\"}") ==
                   Status::Pending);
  sys.advance(100);
  // Who depends on the connection is told at once.
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::NetStaLost));
  sys.advance(5400);
  TEST_ASSERT_TRUE(sys.net().lastJoin() == JoinResult::Refused);

  // Back on the network of before, by itself.
  sys.advance(1500);
  TEST_ASSERT_TRUE(sys.net().connected());
  TEST_ASSERT_TRUE(sys.net().joinPhase() == JoinPhase::None);
  hal::StaInfo sta;
  sys.radio().staInfo(sta);
  TEST_ASSERT_EQUAL_STRING(kHome, sta.ssid);
  TEST_ASSERT_EQUAL_STRING(kHome, sys.config().net.staSsid.c_str());
  TEST_ASSERT_EQUAL_STRING(kHomePassword, sys.config().net.staPsw.c_str());
  TEST_ASSERT_FALSE(sys.net().apUp());
  TEST_ASSERT_TRUE(logHas(sys.kernel(), "going back to 'Home'"));
  TEST_ASSERT_TRUE(logHas(sys.kernel(), "back on 'Home'"));
}

void test_join_from_a_network_to_another() {
  TestBoard tb;
  tb.native().net.addNetwork(kHome, kHomePassword);
  tb.native().net.addNetwork(kOffice, kOfficePassword);
  provision(tb, kHome, kHomePassword);
  System sys(tb);

  TEST_ASSERT_TRUE(sys.command("join", "{\"ssid\":\"Office\",\"psw\":\"office-password\"}") ==
                   Status::Pending);
  sys.advance(1000);
  TEST_ASSERT_TRUE(sys.net().connected());
  TEST_ASSERT_TRUE(sys.net().lastJoin() == JoinResult::Joined);
  TEST_ASSERT_EQUAL_STRING(kOffice, sys.config().net.staSsid.c_str());
  // The network of before is remembered.
  TEST_ASSERT_EQUAL_STRING(kHome, sys.config().net.prevSsid.c_str());
  TEST_ASSERT_EQUAL_STRING(kHomePassword, sys.config().net.prevPsw.c_str());
  TEST_ASSERT_FALSE(sys.net().apUp());
}

void test_join_times_out() {
  TestBoard tb;
  tb.native().net.addNetwork(kHome, kHomePassword);
  tb.native().net.addNetwork(kOffice, kOfficePassword);
  provision(tb, kHome, kHomePassword);
  tb.board.kv->begin();
  {
    Config config(tb.board.kv, tb.board.rng, tb.board.sys);
    config.load();
    config.net.rollbackSec = 30;
    config.touch(Config::kNet);
    config.save();
  }
  System sys(tb);
  TEST_ASSERT_TRUE(sys.net().connected());

  // The new network answers, but the address never arrives.
  sys.radio().setDelays(100000, 1);
  TEST_ASSERT_TRUE(sys.command("join", "{\"ssid\":\"Office\",\"psw\":\"office-password\"}") ==
                   Status::Pending);
  Doc during(sys.kernel());
  TEST_ASSERT_TRUE(during.v["join"]["phase"].stringEquals("trying"));
  TEST_ASSERT_TRUE(during.v["join"]["leftSec"].asInt() >= 29);

  sys.advance(29000);
  TEST_ASSERT_TRUE(sys.net().joinPhase() == JoinPhase::Trying);
  sys.radio().setDelays(3, 1);
  sys.advance(1500);
  TEST_ASSERT_TRUE(sys.net().lastJoin() == JoinResult::TimedOut);
  sys.advance(1500);
  TEST_ASSERT_TRUE(sys.net().connected());
  hal::StaInfo sta;
  sys.radio().staInfo(sta);
  TEST_ASSERT_EQUAL_STRING(kHome, sta.ssid);
  TEST_ASSERT_EQUAL_STRING(kHome, sys.config().net.staSsid.c_str());
}

void test_join_asked_during_a_round_of_the_system() {
  // On a board the clock moves while the services run: a request that arrives from the
  // command line starts after the time the round was given.
  struct MovingClock : FakeClock {
    uint32_t millis() const override { return const_cast<MovingClock*>(this)->now++; }
  };
  TestBoard tb;
  tb.native().net.addNetwork(kHome, kHomePassword);
  tb.native().net.setDelays(40, 1);
  MovingClock clock;
  hal::Board board = tb.board;
  board.clock = &clock;

  LoopMax app(board);
  Logger::setInstance(&app.kernel().log());
  TEST_ASSERT_TRUE(app.begin() == Status::Ok);
  for (int i = 0; i < 50; ++i) app.kernel().tick();
  TEST_ASSERT_TRUE(app.net().netState() == NetState::ApOnly);

  tb.native().serial.feedInput("cmd sys.net join ssid=Home psw=home-password\n");
  // Enough rounds for the serial service to read the line and for the network service to
  // look at the attempt, not enough for its time to run out.
  for (int i = 0; i < 400; ++i) app.kernel().tick();
  TEST_ASSERT_EQUAL_UINT(1, tb.native().net.connects());
  TEST_ASSERT_TRUE(app.net().joinPhase() == JoinPhase::Trying);
  TEST_ASSERT_FALSE(logHas(app.kernel(), "no answer"));
  TEST_ASSERT_FALSE(logHas(app.kernel(), "timeout"));

  for (int i = 0; i < 4000 && !app.net().connected(); ++i) app.kernel().tick();
  TEST_ASSERT_TRUE(app.net().connected());
  TEST_ASSERT_TRUE(app.net().lastJoin() == JoinResult::Joined);
  TEST_ASSERT_EQUAL_UINT(1, tb.native().net.connects());
}

void test_join_checks_its_arguments() {
  TestBoard tb;
  System sys(tb);
  const char* field = nullptr;
  TEST_ASSERT_TRUE(sys.command("join", "{\"psw\":\"home-password\"}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("ssid", field);
  TEST_ASSERT_TRUE(sys.command("join", "{\"ssid\":\"\"}", &field) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("ssid", field);
  TEST_ASSERT_TRUE(sys.command("join", "{\"ssid\":\"Home\",\"psw\":\"short\"}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("psw", field);
  TEST_ASSERT_TRUE(sys.net().joinPhase() == JoinPhase::None);
  TEST_ASSERT_EQUAL_UINT(0, sys.radio().connects());

  // One change at a time.
  sys.radio().addNetwork(kHome, kHomePassword);
  sys.radio().setDelays(50, 1);
  TEST_ASSERT_TRUE(sys.command("join", "{\"ssid\":\"Home\",\"psw\":\"home-password\"}") ==
                   Status::Pending);
  TEST_ASSERT_TRUE(sys.command("join", "{\"ssid\":\"Home\",\"psw\":\"home-password\"}") ==
                   Status::Busy);
  TEST_ASSERT_TRUE(sys.command("scan", "{}") == Status::Busy);
  TEST_ASSERT_TRUE(sys.command("setMode", "{\"mode\":\"AP\"}") == Status::Busy);
}

void test_an_open_network_needs_no_password() {
  TestBoard tb;
  tb.native().net.addNetwork("Guests", "");
  System sys(tb);
  TEST_ASSERT_TRUE(sys.command("join", "{\"ssid\":\"Guests\"}") == Status::Pending);
  sys.advance(1000);
  TEST_ASSERT_TRUE(sys.net().connected());
  TEST_ASSERT_EQUAL_STRING("", sys.config().net.staPsw.c_str());
}

// ---------------------------------------------------------------- test of credentials

void test_credentials_can_be_tried_without_keeping_them() {
  TestBoard tb;
  tb.native().net.addNetwork(kHome, kHomePassword);
  System sys(tb);
  sys.radio().setApClients(1);

  TEST_ASSERT_TRUE(sys.command("test", "{\"ssid\":\"Home\",\"psw\":\"home-password\"}") ==
                   Status::Pending);
  sys.advance(1000);
  TEST_ASSERT_TRUE(sys.net().lastJoin() == JoinResult::Tested);
  TEST_ASSERT_TRUE(sys.net().joinPhase() == JoinPhase::None);
  // The device is where it was.
  TEST_ASSERT_TRUE(sys.net().netState() == NetState::ApOnly);
  TEST_ASSERT_TRUE(sys.net().apUp());
  TEST_ASSERT_FALSE(sys.net().connected());
  TEST_ASSERT_TRUE(sys.kernel().mode() == Mode::Ap);
  TEST_ASSERT_EQUAL_STRING("", sys.config().net.staSsid.c_str());
  hal::StaInfo sta;
  sys.radio().staInfo(sta);
  TEST_ASSERT_TRUE(sta.state == hal::StaState::Idle);

  Event result;
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::NetJoinResult, &result));
  TEST_ASSERT_EQUAL_UINT8(1, result.a);  // it was a test
  TEST_ASSERT_EQUAL_UINT8(1, result.b);
  TEST_ASSERT_EQUAL_INT(0, sys.count(EventId::NetStaConnected));

  TEST_ASSERT_TRUE(sys.command("test", "{\"ssid\":\"Home\",\"psw\":\"wrong-one-123\"}") ==
                   Status::Pending);
  sys.advance(6000);
  TEST_ASSERT_TRUE(sys.net().lastJoin() == JoinResult::Refused);
  TEST_ASSERT_TRUE(sys.net().netState() == NetState::ApOnly);
  TEST_ASSERT_TRUE(sys.net().apUp());

  sys.advance(60000);
  TEST_ASSERT_EQUAL_UINT(4, sys.radio().connects());
}

void test_a_test_is_refused_while_connected() {
  TestBoard tb;
  tb.native().net.addNetwork(kHome, kHomePassword);
  tb.native().net.addNetwork(kOffice, kOfficePassword);
  provision(tb, kHome, kHomePassword);
  System sys(tb);
  const char* field = nullptr;
  TEST_ASSERT_TRUE(sys.command("test", "{\"ssid\":\"Office\",\"psw\":\"office-password\"}",
                               &field) == Status::Denied);
  TEST_ASSERT_TRUE(sys.net().connected());
  TEST_ASSERT_EQUAL_INT(0, sys.count(EventId::NetStaLost));
}

// ---------------------------------------------------------------- scan

void test_scan() {
  TestBoard tb;
  System sys(tb);
  sys.radio().addNetwork("Weak", "weak-password", -88, 1);
  sys.radio().addNetwork(kHome, kHomePassword, -52, 6);
  sys.radio().addNetwork("Guests", "", -70, 11);
  sys.radio().addNetwork("", "hidden-password", -40, 3);  // hidden: no name

  TEST_ASSERT_TRUE(sys.command("scan", "{}") == Status::Pending);
  Doc running(sys.kernel());
  TEST_ASSERT_TRUE(running.v["scan"]["state"].stringEquals("running"));
  TEST_ASSERT_TRUE(sys.command("scan", "{}") == Status::Pending);  // the same scan

  sys.advance(1000);
  Doc doc(sys.kernel());
  TEST_ASSERT_TRUE(doc.v["scan"]["state"].stringEquals("done"));
  const JsonValue list = doc.v["scan"]["networks"];
  TEST_ASSERT_EQUAL_UINT(3, list.size());
  TEST_ASSERT_TRUE(list.at(0)["ssid"].stringEquals(kHome));
  TEST_ASSERT_EQUAL_INT64(-52, list.at(0)["rssi"].asInt());
  TEST_ASSERT_TRUE(list.at(0)["secured"].asBool());
  TEST_ASSERT_TRUE(list.at(1)["ssid"].stringEquals("Guests"));
  TEST_ASSERT_FALSE(list.at(1)["secured"].asBool(true));
  TEST_ASSERT_TRUE(list.at(2)["ssid"].stringEquals("Weak"));
  TEST_ASSERT_EQUAL_UINT(3, sys.net().scanCount());
  // The access point is still there.
  TEST_ASSERT_TRUE(sys.net().apUp());
}

void test_scan_keeps_the_strongest_networks() {
  TestBoard tb;
  System sys(tb);
  for (int i = 0; i < LMP_NET_SCAN_MAX + 6; ++i) {
    char name[16];
    snprintf(name, sizeof(name), "Wifi%02d", i);
    sys.radio().addNetwork(name, "wifi-password", static_cast<int8_t>(-90 + i), 6);
  }
  sys.command("scan", "{}");
  sys.advance(1000);
  TEST_ASSERT_EQUAL_UINT(LMP_NET_SCAN_MAX, sys.net().scanCount());
  Doc doc(sys.kernel());
  const JsonValue list = doc.v["scan"]["networks"];
  char strongest[16];
  snprintf(strongest, sizeof(strongest), "Wifi%02d", LMP_NET_SCAN_MAX + 5);
  TEST_ASSERT_TRUE(list.at(0)["ssid"].stringEquals(strongest));
  int64_t before = 0;
  for (const JsonMember m : list) {
    TEST_ASSERT_TRUE(m.value["rssi"].asInt() <= before);
    before = m.value["rssi"].asInt();
  }
}

void test_scan_keeps_the_devices_of_the_system() {
  TestBoard tb;
  System sys(tb);
  // More networks than the list holds, all stronger than the two devices that wait to be set up.
  for (int i = 0; i < LMP_NET_SCAN_MAX + 6; ++i) {
    char name[16];
    snprintf(name, sizeof(name), "Wifi%02d", i);
    sys.radio().addNetwork(name, "wifi-password", static_cast<int8_t>(-70 + i), 6);
  }
  sys.radio().addNetwork("LoopMax-3F2A", "wifi-password", -88, 1);
  sys.radio().addNetwork("LoopMax-00B7", "wifi-password", -91, 1);
  sys.command("scan", "{}");
  sys.advance(1000);
  TEST_ASSERT_EQUAL_UINT(LMP_NET_SCAN_MAX, sys.net().scanCount());
  Doc doc(sys.kernel());
  const JsonValue list = doc.v["scan"]["networks"];
  int devices = 0;
  int64_t before = 0;
  for (const JsonMember m : list) {
    const bool ours = m.value["ssid"].stringEquals("LoopMax-3F2A") ||
                      m.value["ssid"].stringEquals("LoopMax-00B7");
    // Told apart from the networks of the others, and still by signal.
    TEST_ASSERT_TRUE(m.value["device"].asBool(false) == ours);
    devices += ours ? 1 : 0;
    TEST_ASSERT_TRUE(m.value["rssi"].asInt() <= before);
    before = m.value["rssi"].asInt();
  }
  TEST_ASSERT_EQUAL_INT(2, devices);
  // The strongest of the others is still the first.
  char strongest[16];
  snprintf(strongest, sizeof(strongest), "Wifi%02d", LMP_NET_SCAN_MAX + 5);
  TEST_ASSERT_TRUE(list.at(0)["ssid"].stringEquals(strongest));
}

void test_scan_without_answer() {
  TestBoard tb;
  System sys(tb);
  sys.radio().setDelays(3, 100000);
  TEST_ASSERT_TRUE(sys.command("scan", "{}") == Status::Pending);
  sys.advance(16000);
  Doc doc(sys.kernel());
  TEST_ASSERT_TRUE(doc.v["scan"]["state"].stringEquals("failed"));
  // The next one can start.
  sys.radio().setDelays(3, 3);
  TEST_ASSERT_TRUE(sys.command("scan", "{}") == Status::Pending);
}

// ---------------------------------------------------------------- settings

void test_mode_is_changed_by_command() {
  TestBoard tb;
  tb.native().net.addNetwork(kHome, kHomePassword);
  System sys(tb);
  const char* field = nullptr;

  // No network was ever accepted.
  TEST_ASSERT_TRUE(sys.command("setMode", "{\"mode\":\"LAN\"}", &field) == Status::Denied);
  TEST_ASSERT_EQUAL_STRING("mode", field);
  TEST_ASSERT_TRUE(sys.command("setMode", "{\"mode\":\"WAN\"}", &field) == Status::Invalid);
  TEST_ASSERT_TRUE(sys.kernel().mode() == Mode::Ap);

  sys.command("join", "{\"ssid\":\"Home\",\"psw\":\"home-password\"}");
  sys.advance(35000);
  TEST_ASSERT_TRUE(sys.net().connected());
  TEST_ASSERT_FALSE(sys.net().apUp());

  TEST_ASSERT_TRUE(sys.command("setMode", "{\"mode\":\"AP\"}") == Status::Ok);
  sys.advance(500);
  TEST_ASSERT_TRUE(sys.kernel().mode() == Mode::Ap);
  TEST_ASSERT_TRUE(sys.net().netState() == NetState::ApOnly);
  TEST_ASSERT_TRUE(sys.net().apUp());
  TEST_ASSERT_FALSE(sys.net().connected());
  TEST_ASSERT_EQUAL_UINT8(1, sys.config().sys.mode);
  // The network stays in the settings.
  TEST_ASSERT_EQUAL_STRING(kHome, sys.config().net.staSsid.c_str());

  TEST_ASSERT_TRUE(sys.command("setMode", "{\"mode\":\"LAN\"}") == Status::Ok);
  sys.advance(1000);
  TEST_ASSERT_TRUE(sys.net().connected());
  TEST_ASSERT_FALSE(sys.net().apUp());
}

void test_access_point_settings() {
  TestBoard tb;
  System sys(tb);
  const char* field = nullptr;
  TEST_ASSERT_TRUE(sys.command("setAp", "{}") == Status::Invalid);
  TEST_ASSERT_TRUE(sys.command("setAp", "{\"psw\":\"short\"}", &field) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("psw", field);

  TEST_ASSERT_TRUE(sys.command("setAp", "{\"ssid\":\"Plugs\",\"psw\":\"some-password\"}") ==
                   Status::Ok);
  TEST_ASSERT_EQUAL_STRING("Plugs", sys.config().net.apSsid.c_str());
  // From the next restart: who is connected now is not dropped.
  hal::ApInfo ap;
  sys.radio().apInfo(ap);
  TEST_ASSERT_EQUAL_STRING("LoopMax-0001", ap.ssid);
  Doc doc(sys.kernel());
  TEST_ASSERT_FALSE(doc.v["ap"]["pswDefault"].asBool(true));
}

void test_the_name_follows_the_host_name() {
  TestBoard tb;
  System sys(tb);
  CmdContext ctx;
  TEST_ASSERT_TRUE(sys.kernel().commands().execute("sys.config", "set",
                                                   JsonValue::parse("{\"hostname\":\"plugs\"}"),
                                                   ctx) == Status::Ok);
  sys.advance(300);
  TEST_ASSERT_EQUAL_STRING("plugs", sys.radio().mdnsName().c_str());
  Doc doc(sys.kernel());
  TEST_ASSERT_TRUE(doc.v["host"].stringEquals("plugs.local"));
}

// ---------------------------------------------------------------- the others of the system

namespace {

// Another device of the system on the network of the test.
hal::NetPeer peerOf(const char* id, const char* host, uint8_t last, const char* name,
                    const char* code, const char* modules) {
  hal::NetPeer p;
  snprintf(p.id, sizeof(p.id), "%s", id);
  snprintf(p.host, sizeof(p.host), "%s", host);
  snprintf(p.name, sizeof(p.name), "%s", name);
  snprintf(p.code, sizeof(p.code), "%s", code);
  snprintf(p.modules, sizeof(p.modules), "%s", modules);
  snprintf(p.fw, sizeof(p.fw), "1.0.6");
  snprintf(p.board, sizeof(p.board), "esp32dev");
  snprintf(p.edition, sizeof(p.edition), "full");
  p.ip = hal::Ip4::of(192, 168, 1, last);
  p.port = LMP_HTTP_PORT;
  p.api = 2;
  return p;
}

std::string idOf(TestBoard& tb) {
  hal::Mac mac;
  char text[18];
  tb.board.sys->mac(mac);
  mac.toString(text, sizeof(text));
  return text;
}

}  // namespace

void test_the_device_announces_itself() {
  TestBoard tb;
  tb.native().net.addNetwork(kHome, kHomePassword);
  provision(tb, kHome, kHomePassword);
  System sys(tb, 3000);
  TEST_ASSERT_TRUE(sys.net().connected());

  // Next to its name, the service of the system with what the device is.
  hal::native::Net& radio = sys.radio();
  TEST_ASSERT_EQUAL_STRING("loopmax-0001", radio.announcedAs().c_str());
  TEST_ASSERT_EQUAL_STRING("2", radio.announced("api").c_str());
  TEST_ASSERT_EQUAL_STRING(idOf(tb).c_str(), radio.announced("id").c_str());
  TEST_ASSERT_EQUAL_STRING(LMP_VERSION, radio.announced("fw").c_str());
  TEST_ASSERT_EQUAL_STRING("native", radio.announced("board").c_str());
  TEST_ASSERT_EQUAL_STRING("full", radio.announced("ed").c_str());
#if LMP_FEATURE_MODULES
  TEST_ASSERT_EQUAL_STRING("demo", radio.announced("mod").c_str());
  // What it is, for who reads a list: title and icon of its first module.
  TEST_ASSERT_EQUAL_STRING("Demo", radio.announced("title").c_str());
  TEST_ASSERT_EQUAL_STRING("\xF0\x9F\xA7\xAA", radio.announced("icon").c_str());
#endif
  // Nobody gave it a name yet, and the build of the PC is no product: not said.
  TEST_ASSERT_EQUAL_STRING("", radio.announced("name").c_str());
  TEST_ASSERT_EQUAL_STRING("", radio.announced("code").c_str());

  // The owner names it: it is announced again, with the name.
  const unsigned before = radio.announces();
  CmdContext ctx;
  TEST_ASSERT_TRUE(sys.kernel().commands().execute(
      "sys.config", "set", JsonValue::parse("{\"deviceName\":\"Office Plugs\"}"), ctx) == Status::Ok);
  sys.advance(300);
  TEST_ASSERT_TRUE(radio.announces() > before);
  TEST_ASSERT_EQUAL_STRING("Office Plugs", radio.announced("name").c_str());
}

void test_the_device_finds_the_others_of_the_system() {
  TestBoard tb;
  tb.native().net.addNetwork(kHome, kHomePassword);
  provision(tb, kHome, kHomePassword);
  const std::string self = idOf(tb);
  hal::NetPeer weather = peerOf("02:4C:4D:50:00:09", "garage-weather", 182, "Garage weather",
                                "demo_w32", "meteowifi,rgb");
  hal::peerTake(weather, "title", "MeteoWifi");
  hal::peerTake(weather, "icon", "\xF0\x9F\x8C\xA4");
  const hal::NetPeer sockets = peerOf("02:4C:4D:50:00:03", "loopmax-0003", 60, "", "", "zappy2");
  // Two others on the network. The device itself answers its own question too.
  tb.native().net.addPeer(weather);
  tb.native().net.addPeer(peerOf(self.c_str(), "loopmax-0001", 50, "", "", "demo"));
  tb.native().net.addPeer(sockets);
  // Internet is there: a device that loses it for long restarts, and the test lasts hours.
  tb.native().httpClient.when("http://connectivitycheck.gstatic.com/*", 204, "");
  System sys(tb, 3000);
  hal::native::Net& radio = sys.radio();
  {
    // Not at once: the others must have the time to see this device too.
    Doc doc(sys.kernel());
    TEST_ASSERT_TRUE(doc.v["near"]["state"].stringEquals("idle"));
    TEST_ASSERT_EQUAL_UINT(0, doc.v["near"]["devices"].size());
    TEST_ASSERT_EQUAL_UINT(0, radio.searches());
  }
  sys.advance(46000);
  TEST_ASSERT_EQUAL_UINT(1, radio.searches());
  {
    Doc doc(sys.kernel());
    const JsonValue near = doc.v["near"];
    TEST_ASSERT_TRUE(near["state"].stringEquals("done"));
    // The two others, by id; not the device itself.
    TEST_ASSERT_EQUAL_UINT(2, near["devices"].size());
    const JsonValue first = near["devices"].at(0), second = near["devices"].at(1);
    TEST_ASSERT_TRUE(first["id"].stringEquals("02:4C:4D:50:00:03"));
    TEST_ASSERT_TRUE(first["host"].stringEquals("loopmax-0003"));
    TEST_ASSERT_TRUE(first["ip"].stringEquals("192.168.1.60"));
    TEST_ASSERT_TRUE(first["mod"].stringEquals("zappy2"));
    TEST_ASSERT_FALSE(first["title"].valid());   // a device that did not say what it is
    TEST_ASSERT_TRUE(second["id"].stringEquals("02:4C:4D:50:00:09"));
    TEST_ASSERT_TRUE(second["name"].stringEquals("Garage weather"));
    TEST_ASSERT_TRUE(second["code"].stringEquals("demo_w32"));
    TEST_ASSERT_TRUE(second["mod"].stringEquals("meteowifi,rgb"));
    TEST_ASSERT_TRUE(second["title"].stringEquals("MeteoWifi"));
    TEST_ASSERT_TRUE(second["icon"].stringEquals("\xF0\x9F\x8C\xA4"));
    TEST_ASSERT_TRUE(second["fw"].stringEquals("1.0.6"));
    TEST_ASSERT_EQUAL_INT64(2, second["api"].asInt());
    TEST_ASSERT_FALSE(second["port"].valid());   // the usual one is not said
  }
  // Console and server are told: the list is in the part of the network of the report.
  Event last;
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::NetNear, &last));
  TEST_ASSERT_EQUAL_UINT8(2, last.b);
  TEST_ASSERT_TRUE(logHas(sys.kernel(), "2 other devices of the system"));

  // Asked by hand: the command ends when the network has answered. Nothing changed: no news.
  TEST_ASSERT_TRUE(sys.command("near", "{}") == Status::Pending);
  sys.advance(300);
  TEST_ASSERT_EQUAL_UINT(2, radio.searches());
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::NetNear));

  // One of them goes away.
  radio.clearPeers();
  radio.addPeer(sockets);
  TEST_ASSERT_TRUE(sys.command("near", "{}") == Status::Pending);
  sys.advance(300);
  {
    Doc doc(sys.kernel());
    TEST_ASSERT_EQUAL_UINT(1, doc.v["near"]["devices"].size());
  }
  TEST_ASSERT_EQUAL_INT(2, sys.count(EventId::NetNear, &last));
  TEST_ASSERT_EQUAL_UINT8(1, last.b);

  // And every six hours the device looks by itself.
  const unsigned searched = radio.searches();
  run(sys.kernel(), tb.clock, 6u * 3600u * 1000u + 60000u, 1000);
  TEST_ASSERT_EQUAL_UINT(searched + 1, radio.searches());
}

// What a device says of itself comes from the network: anybody can announce anything.
void test_what_another_device_says_is_made_safe() {
  hal::NetPeer p;
  // Too long for its place: it ends at a whole character, not in the middle of one.
  hal::peerTake(p, "title", "Greenhouse thermometer, \xC2\xB0" "C");
  TEST_ASSERT_EQUAL_STRING("Greenhouse thermometer, ", p.title);
  hal::peerTake(p, "title", "Fridge 4 \xC2\xB0" "C");
  TEST_ASSERT_EQUAL_STRING("Fridge 4 \xC2\xB0" "C", p.title);
  // Not UTF-8: a question mark for every byte that is not, and nothing read past the end.
  hal::peerTake(p, "name", "A\xFF\x80Z");
  TEST_ASSERT_EQUAL_STRING("A??Z", p.name);
  hal::peerTake(p, "name", "ab\xE2\x82");
  TEST_ASSERT_EQUAL_STRING("ab??", p.name);
  // Longer than it must be, and half of a pair: refused as well.
  hal::peerTake(p, "name", "\xC0\xAF.\xED\xA0\x80");
  TEST_ASSERT_EQUAL_STRING("??.???", p.name);
  // An icon made of several characters that does not fit keeps the whole ones.
  hal::peerTake(p, "icon", "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9");
  TEST_ASSERT_EQUAL_STRING("\xF0\x9F\x91\xA8\xE2\x80\x8D", p.icon);
  // The list of the device stays a valid document with all of it inside.
  TestBoard tb;
  tb.native().net.addNetwork(kHome, kHomePassword);
  provision(tb, kHome, kHomePassword);
  snprintf(p.id, sizeof(p.id), "02:4C:4D:50:00:07");
  snprintf(p.host, sizeof(p.host), "strange");
  p.ip = hal::Ip4::of(192, 168, 1, 77);
  p.port = LMP_HTTP_PORT;
  tb.native().net.addPeer(p);
  System sys(tb, 3000);
  TEST_ASSERT_TRUE(sys.command("near", "{}") == Status::Pending);
  sys.advance(300);
  Doc doc(sys.kernel());
  TEST_ASSERT_EQUAL_UINT(1, doc.v["near"]["devices"].size());
  TEST_ASSERT_TRUE(doc.v["near"]["devices"].at(0)["name"].stringEquals("??.???"));
}

void test_nobody_is_searched_from_the_access_point() {
  TestBoard tb;
  System sys(tb);
  TEST_ASSERT_TRUE(sys.net().netState() == NetState::ApOnly);
  TEST_ASSERT_TRUE(sys.command("near", "{}") == Status::Denied);
  sys.advance(60000);
  TEST_ASSERT_EQUAL_UINT(0, sys.radio().searches());
}

void test_passwords_never_appear() {
  TestBoard tb;
  tb.native().net.addNetwork(kHome, kHomePassword);
  System sys(tb);
  sys.kernel().log().setLevel(LogLevel::Trace);
  sys.command("join", "{\"ssid\":\"Home\",\"psw\":\"wrong-one-123\"}");
  sys.advance(7000);
  sys.command("join", "{\"ssid\":\"Home\",\"psw\":\"home-password\"}");
  sys.advance(2000);
  sys.command("setAp", "{\"psw\":\"some-password\"}");
  TEST_ASSERT_TRUE(sys.net().connected());

  const char* secrets[] = {kHomePassword, "wrong-one-123", "some-password", "loopmaxap"};
  Doc doc(sys.kernel());
  Json<8192> report;
  sys.kernel().report(report.w, StringView(), true);
  for (const char* secret : secrets) {
    TEST_ASSERT_NULL_MESSAGE(strstr(doc.out.text(), secret), secret);
    TEST_ASSERT_NULL_MESSAGE(strstr(report.text(), secret), secret);
    TEST_ASSERT_FALSE_MESSAGE(logHas(sys.kernel(), secret), secret);
  }
  TEST_ASSERT_NULL(strstr(sys.tb.output().c_str(), kHomePassword));
}

// ---------------------------------------------------------------- failures of the board

void test_a_board_without_network_still_starts() {
  TestBoard tb;
  tb.board.net = nullptr;
  tb.board.http = nullptr;
  System sys(tb);
  TEST_ASSERT_TRUE(sys.net().state() == ServiceState::Error);
  TEST_ASSERT_EQUAL_STRING("no network on this board", sys.net().reason());
  TEST_ASSERT_FALSE(sys.app.web().running());
  // The rest of the system is up.
  TEST_ASSERT_TRUE(sys.kernel().log().bootDoneMs() > 0);
  TEST_ASSERT_TRUE(sys.kernel().services().find(ServiceId::Serial)->running());
  TEST_ASSERT_TRUE(sys.command("scan", "{}") == Status::Unsupported);
}

void test_a_radio_that_does_not_start_is_tried_again() {
  TestBoard tb;
  tb.native().net.failNext(Status::Io, Status::Ok);
  System sys(tb);
  TEST_ASSERT_TRUE(sys.net().state() == ServiceState::Error);
  TEST_ASSERT_FALSE(sys.net().apUp());

  sys.radio().failNext(Status::Ok, Status::Ok);
  sys.advance(3000);
  TEST_ASSERT_TRUE(sys.net().state() == ServiceState::Ready);
  TEST_ASSERT_TRUE(sys.net().apUp());
}

void test_an_access_point_that_does_not_start() {
  TestBoard tb;
  tb.native().net.failNext(Status::Ok, Status::Io);
  System sys(tb);
  TEST_ASSERT_TRUE(sys.net().state() == ServiceState::Degraded);
  TEST_ASSERT_FALSE(sys.net().apUp());
  TEST_ASSERT_EQUAL_INT(0, sys.count(EventId::NetApUp));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_start_is_an_access_point);
  RUN_TEST(test_the_report_has_the_network);
  RUN_TEST(test_captive_portal_can_be_turned_off);
  RUN_TEST(test_access_point_clients_are_counted);
  RUN_TEST(test_who_comes_from_the_access_point);
  RUN_TEST(test_lan_connects_to_the_configured_network);
  RUN_TEST(test_mode_lan_without_a_network_is_an_access_point);
  RUN_TEST(test_connection_lost_and_found);
  RUN_TEST(test_attempts_are_spaced_further_and_further);
  RUN_TEST(test_fallback_after_repeated_failures);
  RUN_TEST(test_an_attempt_without_answer_is_given_up);
  RUN_TEST(test_join_from_the_access_point);
  RUN_TEST(test_a_joined_network_survives_a_restart);
  RUN_TEST(test_join_refused_goes_back_to_the_access_point);
  RUN_TEST(test_join_tells_a_missing_network_from_a_wrong_password);
  RUN_TEST(test_join_from_a_network_goes_back_when_the_new_one_fails);
  RUN_TEST(test_join_from_a_network_to_another);
  RUN_TEST(test_join_times_out);
  RUN_TEST(test_join_asked_during_a_round_of_the_system);
  RUN_TEST(test_join_checks_its_arguments);
  RUN_TEST(test_an_open_network_needs_no_password);
  RUN_TEST(test_credentials_can_be_tried_without_keeping_them);
  RUN_TEST(test_a_test_is_refused_while_connected);
  RUN_TEST(test_scan);
  RUN_TEST(test_scan_keeps_the_strongest_networks);
  RUN_TEST(test_scan_keeps_the_devices_of_the_system);
  RUN_TEST(test_scan_without_answer);
  RUN_TEST(test_mode_is_changed_by_command);
  RUN_TEST(test_access_point_settings);
  RUN_TEST(test_the_name_follows_the_host_name);
  RUN_TEST(test_the_device_announces_itself);
  RUN_TEST(test_the_device_finds_the_others_of_the_system);
  RUN_TEST(test_what_another_device_says_is_made_safe);
  RUN_TEST(test_nobody_is_searched_from_the_access_point);
  RUN_TEST(test_passwords_never_appear);
  RUN_TEST(test_a_board_without_network_still_starts);
  RUN_TEST(test_a_radio_that_does_not_start_is_tried_again);
  RUN_TEST(test_an_access_point_that_does_not_start);
  return UNITY_END();
}
