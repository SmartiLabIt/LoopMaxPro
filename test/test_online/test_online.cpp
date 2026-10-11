// Tests on the PC of what a device does with the network around it: the time from a
// server, the requests to servers, the check of Internet, the position.
#include "../support/online_system.h"

using namespace lmp;
using namespace lmp::test;

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

constexpr const char* kLookupUrl = "https://where.test/json";
constexpr const char* kLookupAnswer =
    "{\"status\":\"success\",\"country\":\"Italy\",\"countryCode\":\"IT\",\"regionName\":\"Lazio\","
    "\"city\":\"Rome\",\"lat\":41.8919,\"lon\":12.5113,\"isp\":\"Provider One\","
    "\"query\":\"93.40.1.2\"}";

void withProbe(Config& config) { config.net.probeUrl.assign(kProbeUrl); }
void withLookup(Config& config) {
  config.net.probeUrl.assign(kProbeUrl);
  config.geo.lookupUrl.assign(kLookupUrl);
}

// What a request gave back, kept for the test.
struct Got {
  int calls = 0;
  HttpAnswer answer;
};

}  // namespace

// ---------------------------------------------------------------- time from the network

void test_the_time_comes_from_the_network() {
  OnlineSystem sys;
  hal::native::Ntp& ntp = sys.pc().ntp;
  TEST_ASSERT_EQUAL_UINT(1, ntp.starts());
  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", ntp.server().c_str());
  TEST_ASSERT_TRUE(ntp.running());
  TEST_ASSERT_TRUE(sys.stateOf(ServiceId::Clock) == ServiceState::Degraded);
  TEST_ASSERT_FALSE(sys.app.clock().synced());

  ntp.answer(kSomeTime);
  sys.advance(600);
  TEST_ASSERT_TRUE(sys.app.clock().synced());
  TEST_ASSERT_EQUAL_INT64(kSomeTime, sys.app.clock().unixTime());
  TEST_ASSERT_TRUE(sys.stateOf(ServiceId::Clock) == ServiceState::Ready);
  Event synced;
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::TimeSynced, &synced));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TimeSource::Ntp), synced.a);
  TEST_ASSERT_TRUE(sys.logHas("time set from ntp"));

  Doc doc(sys.kernel(), "sys.clock");
  TEST_ASSERT_TRUE(doc.v["synced"].asBool());
  TEST_ASSERT_TRUE(doc.v["source"].stringEquals("ntp"));
  TEST_ASSERT_TRUE(doc.v["ntp"]["available"].asBool());
  TEST_ASSERT_TRUE(doc.v["ntp"]["running"].asBool());
  TEST_ASSERT_EQUAL_INT64(1, doc.v["ntp"]["answers"].asInt());

  // The server answers again, an hour later: the clock follows.
  ntp.answer(kSomeTime + 3600);
  sys.advance(600);
  TEST_ASSERT_EQUAL_INT64(kSomeTime + 3600, sys.app.clock().unixTime());
  TEST_ASSERT_EQUAL_INT(2, sys.count(EventId::TimeSynced));
}

void test_without_a_network_nobody_is_asked() {
  OnlineSystem sys(false);
  TEST_ASSERT_EQUAL_UINT(0, sys.pc().ntp.starts());
  TEST_ASSERT_TRUE(sys.command("sys.clock", "sync", "{}") == Status::Denied);
  // The time from the browser still works.
  TEST_ASSERT_TRUE(sys.command("sys.clock", "set", "{\"unix\":1790000000}") == Status::Ok);
  TEST_ASSERT_TRUE(sys.app.clock().synced());
  Doc doc(sys.kernel(), "sys.clock");
  TEST_ASSERT_TRUE(doc.v["source"].stringEquals("client"));
  TEST_ASSERT_FALSE(doc.v["ntp"]["running"].asBool(true));
}

void test_an_answer_that_makes_no_sense_is_ignored() {
  OnlineSystem sys;
  sys.pc().ntp.answer(1000);   // a day of 1970
  sys.advance(600);
  TEST_ASSERT_FALSE(sys.app.clock().synced());
  TEST_ASSERT_EQUAL_INT(0, sys.count(EventId::TimeSynced));
  TEST_ASSERT_TRUE(sys.logHas("not usable"));
}

void test_the_time_is_asked_by_command() {
  OnlineSystem sys;
  uint32_t ref = 0;
  TEST_ASSERT_TRUE(sys.command("sys.clock", "sync", "{}", nullptr, &ref) == Status::Pending);
  TEST_ASSERT_TRUE(ref != 0);
  TEST_ASSERT_EQUAL_UINT(2, sys.pc().ntp.starts());
  TEST_ASSERT_TRUE(sys.command("sys.clock", "sync", "{}") == Status::Busy);

  Status outcome = Status::Error;
  sys.advance(1000);
  TEST_ASSERT_FALSE(sys.done(ref, outcome));
  sys.pc().ntp.answer(kSomeTime);
  sys.advance(600);
  TEST_ASSERT_TRUE(sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Ok);

  // A server that does not answer: the command ends by itself.
  uint32_t second = 0;
  TEST_ASSERT_TRUE(sys.command("sys.clock", "sync", "{}", nullptr, &second) == Status::Pending);
  TEST_ASSERT_TRUE(second != ref);
  sys.advance(14000);
  TEST_ASSERT_FALSE(sys.done(second, outcome));
  sys.advance(2000);
  TEST_ASSERT_TRUE(sys.done(second, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Timeout);
  TEST_ASSERT_TRUE(sys.command("sys.clock", "sync", "{}") == Status::Pending);
}

void test_the_time_server_is_in_the_settings() {
  OnlineSystem sys;
  TEST_ASSERT_TRUE(sys.command("sys.clock", "set", "{\"ntp\":\"time.example.org\"}") == Status::Ok);
  sys.advance(100);
  TEST_ASSERT_EQUAL_STRING("time.example.org", sys.pc().ntp.server().c_str());
  TEST_ASSERT_EQUAL_UINT(2, sys.pc().ntp.starts());
  TEST_ASSERT_EQUAL_STRING("time.example.org", sys.kernel().config().clock.ntpServer.c_str());

  // No server: nobody is asked any more.
  TEST_ASSERT_TRUE(sys.command("sys.clock", "set", "{\"ntp\":\"\"}") == Status::Ok);
  sys.advance(100);
  TEST_ASSERT_FALSE(sys.pc().ntp.running());
  TEST_ASSERT_TRUE(sys.command("sys.clock", "sync", "{}") == Status::Denied);
}

void test_the_time_follows_the_network() {
  OnlineSystem sys;
  sys.pc().net.dropSta();
  sys.advance(300);
  TEST_ASSERT_FALSE(sys.pc().ntp.running());
  // The network comes back by itself two seconds later.
  sys.advance(4000);
  TEST_ASSERT_TRUE(sys.app.net().connected());
  TEST_ASSERT_TRUE(sys.pc().ntp.running());
  TEST_ASSERT_EQUAL_UINT(2, sys.pc().ntp.starts());
}

void test_a_board_without_time_server() {
  TestBoard tb;
  tb.board.ntp = nullptr;
  tb.native().net.addNetwork(kHome, kHomePassword);
  LoopMax app(tb.board);
  Logger::setInstance(&app.kernel().log());
  TEST_ASSERT_TRUE(app.begin() == Status::Ok);
  run(app.kernel(), tb.clock, 500);
  CmdContext ctx;
  TEST_ASSERT_TRUE(app.kernel().commands().execute("sys.clock", "sync", JsonValue(), ctx) ==
                   Status::Unsupported);
  TEST_ASSERT_TRUE(app.kernel().commands().execute(
                       "sys.clock", "set", JsonValue::parse("{\"unix\":1790000000}"), ctx) ==
                   Status::Ok);
  TEST_ASSERT_TRUE(app.clock().synced());
}

// ---------------------------------------------------------------- requests to servers

void test_a_request_is_stuck_only_when_silent_too_long() {
  const uint32_t limit = 70000;
  TEST_ASSERT_FALSE(requestStuck(100000, 100000, limit));
  TEST_ASSERT_FALSE(requestStuck(170000, 100000, limit));
  TEST_ASSERT_TRUE(requestStuck(170001, 100000, limit));
  // The N4 on 2026-10-01: a piece of the download arrived after the round took its time. It is
  // a request alive, not one silent for four billion milliseconds.
  TEST_ASSERT_FALSE(requestStuck(314288, 314290, limit));
  // Across the wrap of the counter, after 49 days.
  TEST_ASSERT_FALSE(requestStuck(5000, 0xFFFFF000u, limit));
  TEST_ASSERT_TRUE(requestStuck(80000, 0xFFFFF000u, limit));
}

void test_host_of_an_address() {
  char host[32];
  hostOf("https://api.example.org/v1/x?key=secret", host, sizeof(host));
  TEST_ASSERT_EQUAL_STRING("api.example.org", host);
  hostOf("http://192.168.1.10:8080/", host, sizeof(host));
  TEST_ASSERT_EQUAL_STRING("192.168.1.10", host);
  hostOf("http://user:password@server.test/x", host, sizeof(host));
  TEST_ASSERT_EQUAL_STRING("server.test", host);
  hostOf("https://server.test", host, sizeof(host));
  TEST_ASSERT_EQUAL_STRING("server.test", host);
  hostOf("https://a-very-long-name-that-does-not-fit.example.org/", host, 12);
  TEST_ASSERT_EQUAL_STRING("a-very-long", host);
  hostOf(nullptr, host, sizeof(host));
  TEST_ASSERT_EQUAL_STRING("", host);
}

void test_a_request_is_answered_later() {
  OnlineSystem sys;
  sys.pc().httpClient.when("https://server.test/hello", 200, "hiya");
  TEST_ASSERT_TRUE(sys.stateOf(ServiceId::HttpClient) == ServiceState::Ready);

  static Got got;
  got = Got();
  char body[32] = "x";
  HttpFetch what;
  what.url = "https://server.test/hello";
  what.out = body;
  what.cap = sizeof(body);
  TEST_ASSERT_TRUE(sys.app.httpClient().fetch(what, [](const HttpAnswer& a) {
    ++got.calls;
    got.answer = a;
  }) == Status::Ok);
  // Nothing happened yet: the request travels while the system goes on.
  TEST_ASSERT_EQUAL_INT(0, got.calls);
  TEST_ASSERT_EQUAL_STRING("", body);

  sys.advance(200);
  TEST_ASSERT_EQUAL_INT(1, got.calls);
  TEST_ASSERT_TRUE(got.answer.good());
  TEST_ASSERT_EQUAL_INT(200, got.answer.status);
  TEST_ASSERT_EQUAL_UINT(4, got.answer.length);
  TEST_ASSERT_FALSE(got.answer.truncated);
  TEST_ASSERT_EQUAL_STRING("hiya", body);

  const std::vector<hal::native::HttpSeen> seen = sys.pc().httpClient.seen();
  TEST_ASSERT_EQUAL_UINT(1, seen.size());
  TEST_ASSERT_EQUAL_STRING("GET", seen[0].method.c_str());
  TEST_ASSERT_EQUAL_STRING("https://server.test/hello", seen[0].url.c_str());

  Doc doc(sys.kernel(), "sys.httpclient");
  TEST_ASSERT_TRUE(doc.v["available"].asBool());
  TEST_ASSERT_TRUE(doc.v["secure"].asBool());
  TEST_ASSERT_FALSE(doc.v["busy"].asBool(true));
  TEST_ASSERT_EQUAL_INT64(1, doc.v["requests"].asInt());
  TEST_ASSERT_EQUAL_INT64(0, doc.v["failures"].asInt());
  TEST_ASSERT_TRUE(doc.v["last"]["host"].stringEquals("server.test"));
  TEST_ASSERT_EQUAL_INT64(200, doc.v["last"]["status"].asInt());
}

void test_a_secure_request_waits_for_memory() {
  OnlineSystem sys;
  sys.pc().httpClient.when("https://server.test/hello", 200, "hiya");
  sys.pc().httpClient.when("http://server.test/plain", 200, "ok");
  static Got got;
  got = Got();
  HttpFetch what;
  what.url = "https://server.test/hello";
  const auto done = [](const HttpAnswer& a) {
    ++got.calls;
    got.answer = a;
  };

  // No memory to set up a secure connection: the request stays in its queue, nothing leaves.
  sys.pc().httpClient.setRoom(false);
  TEST_ASSERT_TRUE(sys.app.httpClient().fetch(what, done) == Status::Ok);
  sys.advance(2000);
  TEST_ASSERT_EQUAL_INT(0, got.calls);
  TEST_ASSERT_EQUAL_UINT(0, sys.pc().httpClient.count());
  // The memory is back: it leaves by itself.
  sys.pc().httpClient.setRoom(true);
  sys.advance(400);
  TEST_ASSERT_EQUAL_INT(1, got.calls);
  TEST_ASSERT_TRUE(got.answer.good());

  // It never comes back: given up as no_mem after the wait, and who asked is told.
  got = Got();
  sys.pc().httpClient.setRoom(false);
  TEST_ASSERT_TRUE(sys.app.httpClient().fetch(what, done) == Status::Ok);
  sys.advance(LMP_SECURE_WAIT_S * 1000u - 2000);
  TEST_ASSERT_EQUAL_INT(0, got.calls);
  sys.advance(3000);
  TEST_ASSERT_EQUAL_INT(1, got.calls);
  TEST_ASSERT_TRUE(got.answer.result == Status::NoMem);
  TEST_ASSERT_EQUAL_UINT(1, sys.pc().httpClient.count());

  // An address in the clear takes no such memory: it leaves at once, also from behind a
  // secure request that is waiting.
  got = Got();
  static int secure;
  secure = 0;
  TEST_ASSERT_TRUE(sys.app.httpClient().fetch(what, [](const HttpAnswer&) { ++secure; }) == Status::Ok);
  what.url = "http://server.test/plain";
  TEST_ASSERT_TRUE(sys.app.httpClient().fetch(what, done) == Status::Ok);
  sys.advance(400);
  TEST_ASSERT_EQUAL_INT(1, got.calls);
  TEST_ASSERT_TRUE(got.answer.good());
  TEST_ASSERT_EQUAL_INT(0, secure);
  sys.pc().httpClient.setRoom(true);
  sys.advance(400);
  TEST_ASSERT_EQUAL_INT(1, secure);
}

void test_a_request_carries_what_it_was_given() {
  OnlineSystem sys;
  sys.pc().httpClient.when("https://server.test/*", 201, "{}");
  static Got got;
  got = Got();
  static const hal::HttpHeader headers[] = {{"X-Device", "lmp"}, {"Accept", "application/json"}};
  HttpFetch what;
  what.url = "https://server.test/things?key=topsecret";
  what.method = hal::HttpMethod::Post;
  what.contentType = "application/json";
  what.body = StringView("{\"a\":1}");
  what.headers = Span<const hal::HttpHeader>(headers, 2);
  TEST_ASSERT_TRUE(sys.app.httpClient().fetch(what, [](const HttpAnswer& a) {
    ++got.calls;
    got.answer = a;
  }) == Status::Ok);
  sys.advance(200);
  TEST_ASSERT_EQUAL_INT(1, got.calls);
  TEST_ASSERT_EQUAL_INT(201, got.answer.status);
  TEST_ASSERT_EQUAL_UINT(2, got.answer.length);   // counted, even if nobody wanted it

  const std::vector<hal::native::HttpSeen> seen = sys.pc().httpClient.seen();
  TEST_ASSERT_EQUAL_STRING("POST", seen[0].method.c_str());
  TEST_ASSERT_EQUAL_STRING("{\"a\":1}", seen[0].body.c_str());
  TEST_ASSERT_EQUAL_STRING("application/json", seen[0].contentType.c_str());
  TEST_ASSERT_EQUAL_UINT(2, seen[0].headers.size());
  TEST_ASSERT_EQUAL_STRING("X-Device", seen[0].headers[0].first.c_str());

  // The address can carry a key: documents and log show the host only.
  Doc doc(sys.kernel(), "sys.httpclient");
  TEST_ASSERT_NULL(strstr(doc.text(), "topsecret"));
  TEST_ASSERT_FALSE(sys.logHas("topsecret"));
}

void test_requests_wait_for_their_turn() {
  OnlineSystem sys;
  sys.pc().httpClient.when("http://server.test/*", 200, "ok");
  static int order[8];
  static uint32_t took[8];
  static int calls;
  calls = 0;
  static const char* urls[] = {"http://server.test/0", "http://server.test/1",
                               "http://server.test/2", "http://server.test/3",
                               "http://server.test/4", "http://server.test/5"};
  HttpFetch what;
  // One travels, four wait, the sixth finds no room.
  what.url = urls[0];
  TEST_ASSERT_TRUE(sys.app.httpClient().fetch(what, [](const HttpAnswer& a) { took[calls] = a.ms; order[calls++] = 0; }) == Status::Ok);
  what.url = urls[1];
  TEST_ASSERT_TRUE(sys.app.httpClient().fetch(what, [](const HttpAnswer& a) { took[calls] = a.ms; order[calls++] = 1; }) == Status::Ok);
  what.url = urls[2];
  TEST_ASSERT_TRUE(sys.app.httpClient().fetch(what, [](const HttpAnswer& a) { took[calls] = a.ms; order[calls++] = 2; }) == Status::Ok);
  what.url = urls[3];
  TEST_ASSERT_TRUE(sys.app.httpClient().fetch(what, [](const HttpAnswer& a) { took[calls] = a.ms; order[calls++] = 3; }) == Status::Ok);
  what.url = urls[4];
  TEST_ASSERT_TRUE(sys.app.httpClient().fetch(what, [](const HttpAnswer& a) { took[calls] = a.ms; order[calls++] = 4; }) == Status::Ok);
  what.url = urls[5];
  TEST_ASSERT_TRUE(sys.app.httpClient().fetch(what, [](const HttpAnswer&) { order[calls++] = 5; }) == Status::Busy);
  TEST_ASSERT_EQUAL_UINT(4, sys.app.httpClient().waiting());

  sys.advance(1000);
  TEST_ASSERT_EQUAL_INT(5, calls);
  for (int i = 0; i < 5; ++i) TEST_ASSERT_EQUAL_INT(i, order[i]);
  // The last one waited for the other four, but the time of an answer is the one of its travel:
  // the server of the test answers at once.
  for (int i = 0; i < 5; ++i) TEST_ASSERT_EQUAL_UINT32(0, took[i]);
  const std::vector<hal::native::HttpSeen> seen = sys.pc().httpClient.seen();
  TEST_ASSERT_EQUAL_UINT(5, seen.size());
  for (size_t i = 0; i < 5; ++i) TEST_ASSERT_EQUAL_STRING(urls[i], seen[i].url.c_str());
}

void test_an_answer_longer_than_the_buffer() {
  OnlineSystem sys;
  sys.pc().httpClient.when("http://server.test/long", 200, "0123456789abcdefghij");
  sys.pc().httpClient.setChunk(7);
  static Got got;
  got = Got();
  char body[12];
  HttpFetch what;
  what.url = "http://server.test/long";
  what.out = body;
  what.cap = sizeof(body);
  sys.app.httpClient().fetch(what, [](const HttpAnswer& a) {
    ++got.calls;
    got.answer = a;
  });
  sys.advance(200);
  TEST_ASSERT_EQUAL_INT(1, got.calls);
  TEST_ASSERT_TRUE(got.answer.truncated);
  TEST_ASSERT_EQUAL_UINT(11, got.answer.length);
  TEST_ASSERT_EQUAL_STRING("0123456789a", body);
}

void test_a_body_taken_in_pieces() {
  OnlineSystem sys;
  sys.pc().httpClient.when("http://server.test/big", 200, std::string(1000, 'x'));
  sys.pc().httpClient.setChunk(300);
  static Got got;
  static size_t pieces, bytes;
  got = Got();
  pieces = bytes = 0;
  HttpFetch what;
  what.url = "http://server.test/big";
  what.sink = [](Span<const uint8_t> piece) {
    ++pieces;
    bytes += piece.size();
    return true;
  };
  sys.app.httpClient().fetch(what, [](const HttpAnswer& a) {
    ++got.calls;
    got.answer = a;
  });
  sys.advance(200);
  TEST_ASSERT_EQUAL_INT(1, got.calls);
  TEST_ASSERT_EQUAL_UINT(4, pieces);
  TEST_ASSERT_EQUAL_UINT(1000, bytes);
  TEST_ASSERT_EQUAL_UINT(1000, got.answer.length);

  // Who takes the pieces can stop.
  got = Got();
  pieces = bytes = 0;
  what.sink = [](Span<const uint8_t> piece) {
    ++pieces;
    bytes += piece.size();
    return pieces < 2;
  };
  sys.app.httpClient().fetch(what, [](const HttpAnswer& a) {
    ++got.calls;
    got.answer = a;
  });
  sys.advance(200);
  TEST_ASSERT_EQUAL_INT(1, got.calls);
  TEST_ASSERT_EQUAL_UINT(2, pieces);
  TEST_ASSERT_FALSE(got.answer.good());
  TEST_ASSERT_EQUAL_UINT(300, got.answer.length);
}

void test_a_server_that_is_not_there() {
  OnlineSystem sys;
  sys.pc().httpClient.whenFails("https://server.test/gone", Status::Timeout);
  sys.pc().httpClient.whenFails("https://server.test/cert", Status::Denied);
  static Got got;
  got = Got();
  HttpFetch what;
  what.url = "https://server.test/gone";
  sys.app.httpClient().fetch(what, [](const HttpAnswer& a) {
    ++got.calls;
    got.answer = a;
  });
  sys.advance(200);
  TEST_ASSERT_EQUAL_INT(1, got.calls);
  TEST_ASSERT_TRUE(got.answer.result == Status::Timeout);
  TEST_ASSERT_EQUAL_INT(0, got.answer.status);
  TEST_ASSERT_FALSE(got.answer.good());

  what.url = "https://server.test/cert";
  sys.app.httpClient().fetch(what, [](const HttpAnswer& a) {
    ++got.calls;
    got.answer = a;
  });
  sys.advance(200);
  TEST_ASSERT_TRUE(got.answer.result == Status::Denied);

  // An answer that says no is an answer.
  what.url = "https://server.test/nothing";
  sys.app.httpClient().fetch(what, [](const HttpAnswer& a) {
    ++got.calls;
    got.answer = a;
  });
  sys.advance(200);
  TEST_ASSERT_EQUAL_INT(3, got.calls);
  TEST_ASSERT_TRUE(got.answer.result == Status::Ok);
  TEST_ASSERT_EQUAL_INT(404, got.answer.status);
  TEST_ASSERT_FALSE(got.answer.good());

  Doc doc(sys.kernel(), "sys.httpclient");
  TEST_ASSERT_EQUAL_INT64(3, doc.v["requests"].asInt());
  TEST_ASSERT_EQUAL_INT64(2, doc.v["failures"].asInt());
  TEST_ASSERT_TRUE(sys.logHas("server.test: timeout"));
}

void test_a_quiet_request_leaves_the_failure_to_who_asked() {
  OnlineSystem sys;
  sys.pc().httpClient.whenFails("http://quiet.test/*", Status::Timeout);
  static Got got;
  got = Got();
  HttpFetch what;
  what.url = "http://quiet.test/";
  what.quiet = true;
  sys.app.httpClient().fetch(what, [](const HttpAnswer& a) {
    ++got.calls;
    got.answer = a;
  });
  sys.advance(200);
  TEST_ASSERT_EQUAL_INT(1, got.calls);
  TEST_ASSERT_TRUE(got.answer.result == Status::Timeout);
  // Counted, but no warning: who asked tells it in its own way.
  Doc doc(sys.kernel(), "sys.httpclient");
  TEST_ASSERT_EQUAL_INT64(1, doc.v["failures"].asInt());
  static LogEntry entries[LMP_LOG_RING];
  const size_t n = sys.kernel().log().read(0, entries, LMP_LOG_RING);
  for (size_t i = 0; i < n; ++i) {
    if (strstr(entries[i].msg, "quiet.test")) TEST_ASSERT_TRUE(entries[i].level > LogLevel::Warn);
  }
}

void test_addresses_are_checked() {
  OnlineSystem sys;
  HttpFetch what;
  const HttpDone nothing = [](const HttpAnswer&) {};
  TEST_ASSERT_TRUE(sys.app.httpClient().fetch(what, nothing) == Status::Invalid);
  what.url = "";
  TEST_ASSERT_TRUE(sys.app.httpClient().fetch(what, nothing) == Status::Invalid);
  what.url = "ftp://server.test/x";
  TEST_ASSERT_TRUE(sys.app.httpClient().fetch(what, nothing) == Status::Invalid);
  what.url = "server.test/x";
  TEST_ASSERT_TRUE(sys.app.httpClient().fetch(what, nothing) == Status::Invalid);

  // A board that cannot make a secure connection says so at once.
  sys.pc().httpClient.setSecure(false);
  TEST_ASSERT_FALSE(sys.app.httpClient().secure());
  what.url = "https://server.test/x";
  TEST_ASSERT_TRUE(sys.app.httpClient().fetch(what, nothing) == Status::Unsupported);
  what.url = "http://server.test/x";
  TEST_ASSERT_TRUE(sys.app.httpClient().fetch(what, nothing) == Status::Ok);
  TEST_ASSERT_EQUAL_UINT(0, sys.pc().httpClient.count());   // not yet
}

void test_no_requests_from_the_access_point() {
  OnlineSystem sys(false);
  TEST_ASSERT_FALSE(sys.app.httpClient().running());
  HttpFetch what;
  what.url = "http://server.test/x";
  TEST_ASSERT_TRUE(sys.app.httpClient().fetch(what, [](const HttpAnswer&) {}) == Status::Denied);
}

void test_who_waits_is_told_when_the_service_stops() {
  OnlineSystem sys;
  sys.pc().httpClient.when("http://server.test/*", 200, "ok");
  static int calls, denied;
  calls = denied = 0;
  HttpFetch what;
  what.url = "http://server.test/a";
  const HttpDone count = [](const HttpAnswer& a) {
    ++calls;
    if (a.result == Status::Denied) ++denied;
  };
  sys.app.httpClient().fetch(what, count);
  sys.app.httpClient().fetch(what, count);
  sys.app.httpClient().fetch(what, count);

  // Back to the access point: the service of the requests stops.
  TEST_ASSERT_TRUE(sys.command("sys.net", "setMode", "{\"mode\":\"AP\"}") == Status::Ok);
  sys.advance(500);
  TEST_ASSERT_FALSE(sys.app.httpClient().running());
  TEST_ASSERT_EQUAL_INT(3, calls);
  TEST_ASSERT_EQUAL_INT(3, denied);
  // The one that was travelling ends out there: nobody is told twice.
  sys.advance(1000);
  TEST_ASSERT_EQUAL_INT(3, calls);
}

// ---------------------------------------------------------------- Internet

void test_internet_is_checked_after_the_connection() {
  OnlineSystem sys(true, withProbe, 1000);
  sys.pc().httpClient.when(kProbeUrl, 204, "");
  TEST_ASSERT_FALSE(sys.app.net().internet());
  Doc before(sys.kernel(), "sys.net");
  TEST_ASSERT_FALSE(before.v["internet"].asBool(true));
  TEST_ASSERT_FALSE(before.v["internetChecked"].asBool(true));

  sys.advance(3000);
  TEST_ASSERT_TRUE(sys.app.net().internet());
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::InternetUp));
  TEST_ASSERT_EQUAL_UINT(1, sys.pc().httpClient.count());
  TEST_ASSERT_EQUAL_STRING(kProbeUrl, sys.pc().httpClient.seen()[0].url.c_str());
  TEST_ASSERT_TRUE(sys.logHas("Internet reachable"));
  Doc after(sys.kernel(), "sys.net");
  TEST_ASSERT_TRUE(after.v["internet"].asBool());
  TEST_ASSERT_TRUE(after.v["internetChecked"].asBool());

  // Checked again every five minutes, without a word while nothing changes.
  sys.advance(295000);
  TEST_ASSERT_EQUAL_UINT(1, sys.pc().httpClient.count());
  sys.advance(6000);
  TEST_ASSERT_EQUAL_UINT(2, sys.pc().httpClient.count());
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::InternetUp));
}

void test_a_network_without_internet() {
  OnlineSystem sys(true, withProbe, 1000);
  sys.pc().httpClient.whenFails(kProbeUrl, Status::Timeout);
  sys.advance(3000);
  TEST_ASSERT_FALSE(sys.app.net().internet());
  TEST_ASSERT_EQUAL_INT(0, sys.count(EventId::InternetUp));
  TEST_ASSERT_EQUAL_INT(0, sys.count(EventId::InternetDown));
  TEST_ASSERT_TRUE(sys.logHas("Internet not reachable"));
  Doc doc(sys.kernel(), "sys.net");
  TEST_ASSERT_FALSE(doc.v["internet"].asBool(true));
  TEST_ASSERT_TRUE(doc.v["internetChecked"].asBool());
  // The device works all the same.
  TEST_ASSERT_TRUE(sys.app.net().connected());
  TEST_ASSERT_TRUE(sys.stateOf(ServiceId::Net) == ServiceState::Ready);

  // Asked again after a minute. Then Internet arrives.
  sys.advance(55000);
  TEST_ASSERT_EQUAL_UINT(1, sys.pc().httpClient.count());
  sys.pc().httpClient.when(kProbeUrl, 204, "");
  sys.advance(6000);
  TEST_ASSERT_EQUAL_UINT(2, sys.pc().httpClient.count());
  TEST_ASSERT_TRUE(sys.app.net().internet());
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::InternetUp));

  // And goes away again.
  sys.pc().httpClient.when(kProbeUrl, 503, "");
  sys.advance(301000);
  TEST_ASSERT_FALSE(sys.app.net().internet());
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::InternetDown));
}

void test_internet_goes_with_the_network() {
  OnlineSystem sys(true, withProbe, 1000);
  sys.pc().httpClient.when(kProbeUrl, 204, "");
  sys.advance(3000);
  TEST_ASSERT_TRUE(sys.app.net().internet());

  sys.pc().net.removeNetwork(kHome);
  sys.pc().net.dropSta();
  sys.advance(500);
  TEST_ASSERT_FALSE(sys.app.net().internet());
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::InternetDown));
  const size_t asked = sys.pc().httpClient.count();
  sys.advance(20000);
  TEST_ASSERT_EQUAL_UINT(asked, sys.pc().httpClient.count());   // nobody to ask through

  sys.pc().net.addNetwork(kHome, kHomePassword);
  sys.advance(40000);
  TEST_ASSERT_TRUE(sys.app.net().connected());
  TEST_ASSERT_TRUE(sys.app.net().internet());
  TEST_ASSERT_EQUAL_INT(2, sys.count(EventId::InternetUp));
}

void test_the_check_can_be_turned_off() {
  OnlineSystem sys;   // no address for the check
  sys.advance(10000);
  TEST_ASSERT_EQUAL_UINT(0, sys.pc().httpClient.count());
  Doc doc(sys.kernel(), "sys.net");
  TEST_ASSERT_FALSE(doc.v["internetChecked"].asBool(true));
}

// ---------------------------------------------------------------- position

void test_answers_of_the_position_servers() {
  GeoFound a;
  TEST_ASSERT_TRUE(geoParse(StringView(kLookupAnswer), a));
  TEST_ASSERT_TRUE(a.hasPosition);
  TEST_ASSERT_EQUAL_DOUBLE(41.8919, a.lat);
  TEST_ASSERT_EQUAL_DOUBLE(12.5113, a.lon);
  TEST_ASSERT_EQUAL_STRING("Rome", a.city.c_str());
  TEST_ASSERT_EQUAL_STRING("Lazio", a.region.c_str());
  TEST_ASSERT_EQUAL_STRING("IT", a.country.c_str());
  TEST_ASSERT_EQUAL_STRING("93.40.1.2", a.ip.c_str());
  TEST_ASSERT_EQUAL_STRING("Provider One", a.isp.c_str());

  // Other names for the same things, numbers written as text, the country in small letters.
  GeoFound b;
  TEST_ASSERT_TRUE(geoParse(
      StringView("{\"ip\":\"2.3.4.5\",\"city\":\"Milan\",\"region\":\"Lombardy\","
                 "\"country_code\":\"it\",\"latitude\":\"45.4643\",\"longitude\":\"9.1895\","
                 "\"org\":\"Provider Two\"}"),
      b));
  TEST_ASSERT_TRUE(b.hasPosition);
  TEST_ASSERT_EQUAL_DOUBLE(45.4643, b.lat);
  TEST_ASSERT_EQUAL_STRING("IT", b.country.c_str());
  TEST_ASSERT_EQUAL_STRING("Provider Two", b.isp.c_str());
  TEST_ASSERT_EQUAL_STRING("2.3.4.5", b.ip.c_str());

  // Only the address.
  GeoFound c;
  TEST_ASSERT_TRUE(geoParse(StringView("{\"ip\":\"2.3.4.5\"}"), c));
  TEST_ASSERT_FALSE(c.hasPosition);

  // Nothing useful, positions that are not on the Earth, text that is not JSON.
  GeoFound d;
  TEST_ASSERT_FALSE(geoParse(StringView("{\"status\":\"fail\"}"), d));
  TEST_ASSERT_FALSE(geoParse(StringView("{\"lat\":141.0,\"lon\":12.0}"), d));
  TEST_ASSERT_FALSE(geoParse(StringView("<html>no</html>"), d));
  TEST_ASSERT_FALSE(geoParse(StringView("[1,2]"), d));
  TEST_ASSERT_FALSE(geoParse(StringView(""), d));
}

void test_the_position_written_by_hand() {
  {
    OnlineSystem sys(false);
    Doc before(sys.kernel(), "sys.geo");
    TEST_ASSERT_FALSE(before.v["known"].asBool(true));
    TEST_ASSERT_TRUE(before.v["source"].stringEquals("none"));
    TEST_ASSERT_FALSE(before.v["lat"].valid());

    TEST_ASSERT_TRUE(sys.command("sys.geo", "set",
                                 "{\"lat\":41.8919,\"lon\":12.5113,\"city\":\"Rome\","
                                 "\"region\":\"Lazio\",\"country\":\"it\"}") == Status::Ok);
    TEST_ASSERT_TRUE(sys.app.geo().known());
    TEST_ASSERT_TRUE(sys.app.geo().source() == GeoSource::Manual);
    TEST_ASSERT_EQUAL_DOUBLE(41.8919, sys.app.geo().latitude());
    Doc doc(sys.kernel(), "sys.geo");
    TEST_ASSERT_TRUE(doc.v["known"].asBool());
    TEST_ASSERT_TRUE(doc.v["source"].stringEquals("manual"));
    TEST_ASSERT_EQUAL_DOUBLE(41.8919, doc.v["lat"].asDouble());
    TEST_ASSERT_EQUAL_DOUBLE(12.5113, doc.v["lon"].asDouble());
    TEST_ASSERT_TRUE(doc.v["country"].stringEquals("IT"));

    const char* field = nullptr;
    TEST_ASSERT_TRUE(sys.command("sys.geo", "set", "{\"lat\":41.0}", &field) == Status::Invalid);
    TEST_ASSERT_EQUAL_STRING("lon", field);
    TEST_ASSERT_TRUE(sys.command("sys.geo", "set", "{\"lat\":91.0,\"lon\":1}", &field) ==
                     Status::Invalid);
    TEST_ASSERT_EQUAL_STRING("lat", field);
    TEST_ASSERT_TRUE(sys.command("sys.geo", "set", "{\"country\":\"I1\"}", &field) ==
                     Status::Invalid);
    TEST_ASSERT_EQUAL_STRING("country", field);
    TEST_ASSERT_TRUE(sys.command("sys.geo", "set", "{\"lookupUrl\":\"where.test\"}", &field) ==
                     Status::Invalid);
    TEST_ASSERT_EQUAL_STRING("lookupUrl", field);
    TEST_ASSERT_TRUE(sys.command("sys.geo", "set", "{}") == Status::Invalid);
    TEST_ASSERT_EQUAL_DOUBLE(41.8919, sys.app.geo().latitude());

    // Only the city changes: the rest stays.
    TEST_ASSERT_TRUE(sys.command("sys.geo", "set", "{\"city\":\"Fiumicino\"}") == Status::Ok);
    TEST_ASSERT_EQUAL_DOUBLE(12.5113, sys.app.geo().longitude());
    sys.advance(LMP_CONFIG_SAVE_DELAY_MS + 1200);
    Logger::setInstance(nullptr);
  }

  // Same storage, new system: as after a power cycle.
  hal::native::Objects& pc = hal::native::objects();
  pc.fs.unmount();
  pc.net.reset();
  pc.http.reset();
  pc.ntp.reset();
  FakeClock clock;
  hal::Board board = hal::board();
  board.clock = &clock;
  LoopMax app(board);
  Logger::setInstance(&app.kernel().log());
  TEST_ASSERT_TRUE(app.begin() == Status::Ok);
  run(app.kernel(), clock, 500);
  TEST_ASSERT_TRUE(app.geo().known());
  TEST_ASSERT_TRUE(app.geo().source() == GeoSource::Manual);
  TEST_ASSERT_EQUAL_DOUBLE(41.8919, app.geo().latitude());
  TEST_ASSERT_EQUAL_STRING("Fiumicino", app.kernel().config().geo.city.c_str());

  CmdContext ctx;
  TEST_ASSERT_TRUE(app.kernel().commands().execute("sys.geo", "clear", JsonValue(), ctx) ==
                   Status::Ok);
  TEST_ASSERT_FALSE(app.geo().known());
  TEST_ASSERT_EQUAL_STRING("", app.kernel().config().geo.city.c_str());
}

void test_the_position_asked_to_a_server() {
  OnlineSystem sys(true, withLookup, 1000);
  sys.pc().httpClient.when(kProbeUrl, 204, "");
  sys.pc().httpClient.when(kLookupUrl, 200, kLookupAnswer);
  TEST_ASSERT_FALSE(sys.app.geo().known());

  // Internet is there: the server is asked, once.
  sys.advance(4000);
  TEST_ASSERT_TRUE(sys.app.geo().known());
  TEST_ASSERT_TRUE(sys.app.geo().source() == GeoSource::Lookup);
  TEST_ASSERT_EQUAL_DOUBLE(41.8919, sys.app.geo().latitude());
  TEST_ASSERT_EQUAL_STRING("93.40.1.2", sys.app.geo().publicIp());
  Doc doc(sys.kernel(), "sys.geo");
  TEST_ASSERT_TRUE(doc.v["source"].stringEquals("lookup"));
  TEST_ASSERT_TRUE(doc.v["city"].stringEquals("Rome"));
  TEST_ASSERT_TRUE(doc.v["publicIp"].stringEquals("93.40.1.2"));
  TEST_ASSERT_TRUE(doc.v["isp"].stringEquals("Provider One"));
  TEST_ASSERT_TRUE(doc.v["lookup"]["configured"].asBool());
  TEST_ASSERT_TRUE(doc.v["lookup"]["state"].stringEquals("done"));
  TEST_ASSERT_EQUAL_UINT(2, sys.pc().httpClient.count());
  TEST_ASSERT_TRUE(sys.logHas("position from lookup: 41.8919, 12.5113 Rome IT"));

  sys.advance(310000);
  size_t lookups = 0;
  for (const hal::native::HttpSeen& s : sys.pc().httpClient.seen()) {
    if (s.url == kLookupUrl) ++lookups;
  }
  TEST_ASSERT_EQUAL_UINT(1, lookups);
  // The report has the position.
  Json<8192> report;
  sys.kernel().report(report.w, StringView("geo"));
  TEST_ASSERT_TRUE(report.parse()["geo"]["city"].stringEquals("Rome"));
}

void test_a_failed_question_is_asked_again() {
  OnlineSystem sys(true, withLookup, 1000);
  sys.pc().httpClient.when(kProbeUrl, 204, "");
  sys.pc().httpClient.whenFails(kLookupUrl, Status::NoMem);
  sys.advance(4000);
  TEST_ASSERT_FALSE(sys.app.geo().known());
  // A minute later it is asked again, and this time the server answers.
  sys.pc().httpClient.when(kLookupUrl, 200, kLookupAnswer);
  sys.advance(61000);
  TEST_ASSERT_TRUE(sys.app.geo().known());
  size_t lookups = 0;
  for (const hal::native::HttpSeen& s : sys.pc().httpClient.seen()) {
    if (s.url == kLookupUrl) ++lookups;
  }
  TEST_ASSERT_EQUAL_UINT(2, lookups);
}

void test_the_position_asked_by_command() {
  OnlineSystem sys;
  TEST_ASSERT_TRUE(sys.command("sys.geo", "lookup", "{}") == Status::Denied);   // nobody to ask

  TEST_ASSERT_TRUE(sys.command("sys.geo", "set", "{\"lookupUrl\":\"https://where.test/json\"}") ==
                   Status::Ok);
  sys.pc().httpClient.when(kLookupUrl, 200, kLookupAnswer);
  uint32_t ref = 0;
  TEST_ASSERT_TRUE(sys.command("sys.geo", "lookup", "{}", nullptr, &ref) == Status::Pending);
  TEST_ASSERT_TRUE(sys.command("sys.geo", "lookup", "{}") == Status::Busy);
  Doc running(sys.kernel(), "sys.geo");
  TEST_ASSERT_TRUE(running.v["lookup"]["state"].stringEquals("running"));
  sys.advance(300);
  Status outcome = Status::Error;
  TEST_ASSERT_TRUE(sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Ok);
  TEST_ASSERT_TRUE(sys.app.geo().source() == GeoSource::Lookup);

  // A server that says no, one that says nothing useful, one that is not there.
  sys.pc().httpClient.when(kLookupUrl, 429, "slow down");
  TEST_ASSERT_TRUE(sys.command("sys.geo", "lookup", "{}", nullptr, &ref) == Status::Pending);
  sys.advance(300);
  TEST_ASSERT_TRUE(sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Io);
  sys.pc().httpClient.when(kLookupUrl, 200, "<html>");
  TEST_ASSERT_TRUE(sys.command("sys.geo", "lookup", "{}", nullptr, &ref) == Status::Pending);
  sys.advance(300);
  TEST_ASSERT_TRUE(sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Invalid);
  sys.pc().httpClient.whenFails(kLookupUrl, Status::Timeout);
  TEST_ASSERT_TRUE(sys.command("sys.geo", "lookup", "{}", nullptr, &ref) == Status::Pending);
  sys.advance(300);
  TEST_ASSERT_TRUE(sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Timeout);
  Doc failed(sys.kernel(), "sys.geo");
  TEST_ASSERT_TRUE(failed.v["lookup"]["state"].stringEquals("failed"));
  // What was known stays.
  TEST_ASSERT_TRUE(failed.v["city"].stringEquals("Rome"));
}

void test_what_was_written_by_hand_is_kept() {
  OnlineSystem sys;
  TEST_ASSERT_TRUE(sys.command("sys.geo", "set",
                               "{\"lat\":45.4643,\"lon\":9.1895,\"city\":\"Milan\","
                               "\"lookupUrl\":\"https://where.test/json\"}") == Status::Ok);
  sys.pc().httpClient.when(kLookupUrl, 200, kLookupAnswer);
  TEST_ASSERT_TRUE(sys.command("sys.geo", "lookup", "{}") == Status::Pending);
  sys.advance(300);
  TEST_ASSERT_TRUE(sys.app.geo().source() == GeoSource::Manual);
  TEST_ASSERT_EQUAL_DOUBLE(45.4643, sys.app.geo().latitude());
  TEST_ASSERT_EQUAL_STRING("Milan", sys.kernel().config().geo.city.c_str());
  // The address on Internet is news all the same.
  TEST_ASSERT_EQUAL_STRING("93.40.1.2", sys.app.geo().publicIp());
}

void test_the_position_given_by_another_part_of_the_system() {
  OnlineSystem sys(false);
  IGeoService* geo = sys.kernel().api<IGeoService>();
  TEST_ASSERT_NOT_NULL(geo);
  GeoPlace place;
  place.lat = -33.8688;
  place.lon = 151.2093;
  place.city = "Sydney";
  place.country = "au";
  TEST_ASSERT_TRUE(geo->set(place, GeoSource::Cloud) == Status::Ok);
  TEST_ASSERT_TRUE(geo->source() == GeoSource::Cloud);
  TEST_ASSERT_EQUAL_STRING("AU", sys.kernel().config().geo.country.c_str());
  TEST_ASSERT_TRUE(sys.logHas("position from cloud: -33.8688, 151.2093 Sydney AU"));
  sys.advance(50);
  TEST_ASSERT_TRUE(sys.count(EventId::ConfigChanged) >= 1);

  place.lat = 200;
  TEST_ASSERT_TRUE(geo->set(place, GeoSource::Cloud) == Status::Invalid);
  place.lat = 10;
  place.country = "AUS";
  TEST_ASSERT_TRUE(geo->set(place, GeoSource::Cloud) == Status::Invalid);
  place.country = "AU";
  TEST_ASSERT_TRUE(geo->set(place, GeoSource::None) == Status::Invalid);
  TEST_ASSERT_EQUAL_DOUBLE(-33.8688, geo->latitude());
}

// ---------------------------------------------------------------- a service chosen by the owner

namespace {

// A service that answers in groups under "data", with the rules of the time zone.
constexpr const char* kApiAnswer =
    "{\"success\":true,\"data\":{"
    "\"api\":{\"name\":\"Example API\",\"version\":\"1.0\",\"time\":1790551381},"
    "\"client\":{\"ip\":\"151.15.32.133\"},"
    "\"network\":{\"type\":\"IPv4\",\"org\":\"WIND TRE S.P.A.\",\"isp\":\"WIND TRE S.P.A.\"},"
    "\"geo\":{\"continent\":\"Europe\",\"country\":\"Italy\",\"country_code\":\"IT\","
    "\"region\":\"Lazio\",\"city\":\"Rome\",\"latitude\":41.8919326,\"longitude\":12.5113342},"
    "\"timezone\":{\"name\":\"Europe/Rome\",\"gmtOffset\":7200,\"dstOffset\":3600,"
    "\"posix\":\"CET-1CEST,M3.5.0,M10.5.0/3\"}},\"error\":null}";

// Another service, written by the owner in the settings: asked as the default one is.
constexpr const char* kOtherService = "https://geo.example.org/where";

// The public service of the default, shortened.
constexpr const char* kIpwhoAnswer =
    "{\"ip\":\"203.0.113.7\",\"success\":true,\"type\":\"IPv4\",\"continent\":\"Europe\","
    "\"country\":\"Italy\",\"country_code\":\"IT\",\"region\":\"Lombardy\","
    "\"city\":\"Milan\",\"latitude\":45.4642035,\"longitude\":9.189982,"
    "\"connection\":{\"asn\":1267,\"isp\":\"Example ISP\"},"
    "\"timezone\":{\"id\":\"Europe/Rome\",\"utc\":\"+02:00\"}}";

// A device whose owner chose that service.
void withOther(Config& config) {
  config.net.probeUrl.assign(kProbeUrl);
  config.geo.lookupUrl.assign(kOtherService);
}

void withDefault(Config& config) {
  config.net.probeUrl.assign(kProbeUrl);
  config.geo.lookupUrl.assign(LMP_GEO_LOOKUP_URL);
}

GeoPlace placeAt(double lat, double lon) {
  GeoPlace place;
  place.lat = lat;
  place.lon = lon;
  return place;
}

}  // namespace

void test_an_answer_in_groups() {
  GeoFound a;
  TEST_ASSERT_TRUE(geoParse(StringView(kApiAnswer), a));
  TEST_ASSERT_TRUE(a.hasPosition);
  TEST_ASSERT_EQUAL_DOUBLE(41.8919326, a.lat);
  TEST_ASSERT_EQUAL_DOUBLE(12.5113342, a.lon);
  TEST_ASSERT_EQUAL_STRING("Rome", a.city.c_str());
  TEST_ASSERT_EQUAL_STRING("Lazio", a.region.c_str());
  TEST_ASSERT_EQUAL_STRING("IT", a.country.c_str());
  TEST_ASSERT_EQUAL_STRING("151.15.32.133", a.ip.c_str());
  TEST_ASSERT_EQUAL_STRING("WIND TRE S.P.A.", a.isp.c_str());
  TEST_ASSERT_EQUAL_STRING("Europe/Rome", a.tz.c_str());
  TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", a.posixTz.c_str());

  // The server that says no.
  GeoFound b;
  TEST_ASSERT_FALSE(geoParse(
      StringView("{\"success\":false,\"command\":null,\"data\":null,\"error\":\"Invalid request\"}"),
      b));
}

void test_by_default_a_public_service_is_asked() {
  // A new device asks the public service of the default.
  {
    TestBoard tb;
    tb.board.kv->begin();
    Config config(tb.board.kv, tb.board.rng, tb.board.sys);
    config.load();
    TEST_ASSERT_EQUAL_STRING("https://ipwho.is/", config.geo.lookupUrl.c_str());
  }
  OnlineSystem sys(true, withDefault, 1000);
  sys.pc().httpClient.when(kProbeUrl, 204, "");
  sys.pc().httpClient.when(LMP_GEO_LOOKUP_URL, 200, kIpwhoAnswer);
  sys.advance(4000);

  std::string method;
  for (const hal::native::HttpSeen& s : sys.pc().httpClient.seen()) {
    if (s.url == LMP_GEO_LOOKUP_URL) method = s.method;
  }
  TEST_ASSERT_EQUAL_STRING("GET", method.c_str());
  TEST_ASSERT_TRUE(sys.app.geo().source() == GeoSource::Lookup);
  TEST_ASSERT_EQUAL_DOUBLE(45.4642035, sys.app.geo().latitude());
  TEST_ASSERT_EQUAL_STRING("203.0.113.7", sys.app.geo().publicIp());
  // No rules of the time zone in the answer: the clock stays as it is.
  TEST_ASSERT_EQUAL_STRING("UTC", sys.kernel().config().clock.tz.c_str());
}

void test_another_service_when_chosen() {
  OnlineSystem sys(true, withOther, 1000);
  sys.pc().httpClient.when(kProbeUrl, 204, "");
  sys.pc().httpClient.when(kOtherService, 200, kApiAnswer);
  sys.advance(4000);

  // Asked as any service: a plain GET, nothing of the device in the request.
  std::string method = "not asked";
  std::string body;
  for (const hal::native::HttpSeen& s : sys.pc().httpClient.seen()) {
    if (s.url == kOtherService) {
      method = s.method;
      body = s.body;
    }
  }
  TEST_ASSERT_EQUAL_STRING("GET", method.c_str());
  TEST_ASSERT_EQUAL_STRING("", body.c_str());

  TEST_ASSERT_TRUE(sys.app.geo().source() == GeoSource::Lookup);
  TEST_ASSERT_EQUAL_DOUBLE(41.8919326, sys.app.geo().latitude());
  TEST_ASSERT_EQUAL_STRING("151.15.32.133", sys.app.geo().publicIp());
  // The time zone too, since nobody chose one.
  TEST_ASSERT_EQUAL_STRING("Europe/Rome", sys.kernel().config().clock.tz.c_str());
  TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", sys.kernel().config().clock.posixTz.c_str());
  TEST_ASSERT_TRUE(sys.logHas("time zone from the server: Europe/Rome"));
}

void test_a_time_zone_chosen_by_the_user_stays() {
  OnlineSystem sys(true, [](Config& config) {
    withOther(config);
    config.clock.tz.assign("Europe/London");
    config.clock.posixTz.assign("GMT0BST,M3.5.0/1,M10.5.0");
  }, 1000);
  sys.pc().httpClient.when(kProbeUrl, 204, "");
  sys.pc().httpClient.when(kOtherService, 200, kApiAnswer);
  sys.advance(4000);
  TEST_ASSERT_TRUE(sys.app.geo().source() == GeoSource::Lookup);
  TEST_ASSERT_EQUAL_STRING("Europe/London", sys.kernel().config().clock.tz.c_str());
}

// The order of the sources: a GPS on the board, then the owner, then the network.
void test_gps_then_the_owner_then_the_network() {
  OnlineSystem sys(false);
  IGeoService& geo = sys.app.geo();

  // The network first, then the owner: his point is more precise.
  TEST_ASSERT_TRUE(geo.set(placeAt(41.89, 12.51), GeoSource::Lookup) == Status::Ok);
  TEST_ASSERT_TRUE(sys.command("sys.geo", "set", "{\"lat\":45.4643,\"lon\":9.1895}") == Status::Ok);
  TEST_ASSERT_TRUE(geo.source() == GeoSource::Manual);
  // The network does not overwrite the owner.
  TEST_ASSERT_TRUE(geo.set(placeAt(41.89, 12.51), GeoSource::Lookup) == Status::Denied);
  TEST_ASSERT_EQUAL_DOUBLE(45.4643, geo.latitude());

  // A GPS on the board beats the owner: his point was there while the GPS had none.
  TEST_ASSERT_TRUE(geo.set(placeAt(41.95, 12.45), GeoSource::Gps) == Status::Ok);
  TEST_ASSERT_TRUE(geo.source() == GeoSource::Gps);
  TEST_ASSERT_TRUE(sys.logHas("position from gps: 41.9500, 12.4500"));
  TEST_ASSERT_EQUAL_DOUBLE(41.95, geo.latitude());
  // With a GPS the owner cannot give a point any more, and the command says why.
  const char* field = nullptr;
  TEST_ASSERT_TRUE(sys.command("sys.geo", "set", "{\"lat\":45.4643,\"lon\":9.1895}", &field) ==
                   Status::Denied);
  TEST_ASSERT_EQUAL_STRING("gps", field);
  TEST_ASSERT_EQUAL_DOUBLE(41.95, geo.latitude());
  Doc doc(sys.kernel(), "sys.geo");
  TEST_ASSERT_TRUE(doc.v["source"].stringEquals("gps"));
  // Neither does the network.
  TEST_ASSERT_TRUE(geo.set(placeAt(41.89, 12.51), GeoSource::Lookup) == Status::Denied);

  // Cleared: automatic again, and the owner can write it until the GPS finds its point.
  TEST_ASSERT_TRUE(sys.command("sys.geo", "clear", "{}") == Status::Ok);
  TEST_ASSERT_TRUE(sys.command("sys.geo", "set", "{\"lat\":45.4643,\"lon\":9.1895}") == Status::Ok);
  Doc after(sys.kernel(), "sys.geo");
  TEST_ASSERT_TRUE(after.v["source"].stringEquals("manual"));
}

// A city written by hand is a name, not a point: in 1.0.5 it made the position of the network
// "by hand", and the server took it for the place of the device.
void test_a_city_written_by_hand_is_not_a_point() {
  OnlineSystem sys(false);
  IGeoService& geo = sys.app.geo();
  GeoPlace place = placeAt(41.89, 12.51);
  place.city = "Rome";
  place.country = "IT";
  TEST_ASSERT_TRUE(geo.set(place, GeoSource::Lookup) == Status::Ok);

  TEST_ASSERT_TRUE(sys.command("sys.geo", "set", "{\"city\":\"Tivoli\"}") == Status::Ok);
  TEST_ASSERT_TRUE(geo.source() == GeoSource::Lookup);
  Doc doc(sys.kernel(), "sys.geo");
  TEST_ASSERT_TRUE(doc.v["source"].stringEquals("lookup"));
  TEST_ASSERT_TRUE(doc.v["city"].stringEquals("Tivoli"));
  // The network tells its city again: the point moves, the name of the owner stays.
  place.lat = 41.90;
  TEST_ASSERT_TRUE(geo.set(place, GeoSource::Cloud) == Status::Ok);
  TEST_ASSERT_EQUAL_DOUBLE(41.90, geo.latitude());
  TEST_ASSERT_EQUAL_STRING("Tivoli", sys.kernel().config().geo.city.c_str());
  // A GPS knows no names: it leaves the ones there.
  TEST_ASSERT_TRUE(geo.set(placeAt(41.95, 12.45), GeoSource::Gps) == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("Tivoli", sys.kernel().config().geo.city.c_str());
  // Automatic again: the names are of the network again.
  TEST_ASSERT_TRUE(sys.command("sys.geo", "clear", "{}") == Status::Ok);
  TEST_ASSERT_TRUE(geo.set(place, GeoSource::Lookup) == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("Rome", sys.kernel().config().geo.city.c_str());

  // Without any position a name alone leaves it not known.
  OnlineSystem empty(false);
  TEST_ASSERT_TRUE(empty.command("sys.geo", "set", "{\"city\":\"Tivoli\"}") == Status::Ok);
  TEST_ASSERT_FALSE(empty.app.geo().known());
}

// The console asks the owner where the device is, once: the device remembers it was asked.
void test_the_owner_is_asked_where_the_device_is_once() {
  OnlineSystem sys(false);
  Doc before(sys.kernel(), "sys.geo");
  TEST_ASSERT_FALSE(before.v["asked"].asBool(true));
  // "Not now": asked, with no point.
  TEST_ASSERT_TRUE(sys.command("sys.geo", "later", "{}") == Status::Ok);
  Doc later(sys.kernel(), "sys.geo");
  TEST_ASSERT_TRUE(later.v["asked"].asBool());
  TEST_ASSERT_FALSE(later.v["known"].asBool(true));

  // A point given is an answer too.
  OnlineSystem other(false);
  TEST_ASSERT_TRUE(other.command("sys.geo", "set", "{\"lat\":45.4643,\"lon\":9.1895}") == Status::Ok);
  Doc given(other.kernel(), "sys.geo");
  TEST_ASSERT_TRUE(given.v["asked"].asBool());
  TEST_ASSERT_TRUE(given.v["source"].stringEquals("manual"));
}

void test_a_gps_does_not_wear_the_flash() {
  OnlineSystem sys(false);
  IGeoService& geo = sys.app.geo();
  TEST_ASSERT_TRUE(geo.set(placeAt(41.9000, 12.5000), GeoSource::Gps) == Status::Ok);
  sys.advance(50);
  const int first = sys.count(EventId::ConfigChanged);

  // Fixes a few metres apart: nothing changes.
  for (int i = 0; i < 20; ++i) {
    TEST_ASSERT_TRUE(geo.set(placeAt(41.9000 + i * 0.00001, 12.5000), GeoSource::Gps) == Status::Ok);
    sys.advance(1000);
  }
  TEST_ASSERT_EQUAL_DOUBLE(41.9000, geo.latitude());
  TEST_ASSERT_EQUAL_INT(first, sys.count(EventId::ConfigChanged));

  // A real move: the position follows at once, the flash only later.
  TEST_ASSERT_TRUE(geo.set(placeAt(41.9100, 12.5000), GeoSource::Gps) == Status::Ok);
  TEST_ASSERT_EQUAL_DOUBLE(41.9100, geo.latitude());
  sys.advance(50);
  TEST_ASSERT_EQUAL_INT(first, sys.count(EventId::ConfigChanged));
  sys.advance(LMP_GEO_GPS_SAVE_MS);
  TEST_ASSERT_EQUAL_INT(first + 1, sys.count(EventId::ConfigChanged));
}

void test_a_network_lost_for_long_restarts_the_device() {
  OnlineSystem sys;
  const unsigned before = sys.pc().sys.restartCount();
  sys.pc().net.removeNetwork(kHome);
  sys.pc().net.dropSta();
  // Somebody on the access point of the fallback is using the device: no restart while they
  // are there.
  run(sys.kernel(), sys.tb.clock, 2 * 60000u, 1000);
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::NetFallback));
  sys.pc().net.setApClients(1);
  run(sys.kernel(), sys.tb.clock, (LMP_NET_GUARD_MIN + 2) * 60000u, 1000);
  TEST_ASSERT_EQUAL_UINT(before, sys.pc().sys.restartCount());

  // The time counts from when the last one leaves.
  sys.pc().net.setApClients(0);
  run(sys.kernel(), sys.tb.clock, (LMP_NET_GUARD_MIN - 1) * 60000u, 1000);
  TEST_ASSERT_EQUAL_UINT(before, sys.pc().sys.restartCount());
  run(sys.kernel(), sys.tb.clock, 3 * 60000u, 1000);
  TEST_ASSERT_EQUAL_UINT(before + 1, sys.pc().sys.restartCount());
  TEST_ASSERT_EQUAL_STRING("guard: network lost", sys.pc().sys.lastWordsReason().c_str());
}

void test_after_a_restart_for_the_network_the_guard_waits_longer() {
  hal::LastWords previous;
  snprintf(previous.why, sizeof(previous.why), "guard: network lost");
  OnlineSystem sys;
  sys.pc().sys.setPrevious(&previous);   // the guard asks when it is time to decide
  const unsigned before = sys.pc().sys.restartCount();
  sys.pc().net.removeNetwork(kHome);
  sys.pc().net.dropSta();
  run(sys.kernel(), sys.tb.clock, (LMP_NET_GUARD_MIN + 5) * 60000u, 1000);
  TEST_ASSERT_EQUAL_UINT(before, sys.pc().sys.restartCount());
  run(sys.kernel(), sys.tb.clock, (LMP_NET_GUARD_AGAIN_MIN - LMP_NET_GUARD_MIN) * 60000u, 1000);
  TEST_ASSERT_EQUAL_UINT(before + 1, sys.pc().sys.restartCount());
}

void test_a_device_on_its_access_point_by_choice_never_restarts() {
  OnlineSystem sys(false);
  const unsigned before = sys.pc().sys.restartCount();
  run(sys.kernel(), sys.tb.clock, (LMP_NET_GUARD_MIN + 5) * 60000u, 1000);
  TEST_ASSERT_EQUAL_UINT(before, sys.pc().sys.restartCount());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_time_comes_from_the_network);
  RUN_TEST(test_without_a_network_nobody_is_asked);
  RUN_TEST(test_an_answer_that_makes_no_sense_is_ignored);
  RUN_TEST(test_the_time_is_asked_by_command);
  RUN_TEST(test_the_time_server_is_in_the_settings);
  RUN_TEST(test_the_time_follows_the_network);
  RUN_TEST(test_a_board_without_time_server);
  RUN_TEST(test_host_of_an_address);
  RUN_TEST(test_a_request_is_answered_later);
  RUN_TEST(test_a_secure_request_waits_for_memory);
  RUN_TEST(test_a_request_carries_what_it_was_given);
  RUN_TEST(test_requests_wait_for_their_turn);
  RUN_TEST(test_an_answer_longer_than_the_buffer);
  RUN_TEST(test_a_body_taken_in_pieces);
  RUN_TEST(test_a_server_that_is_not_there);
  RUN_TEST(test_a_quiet_request_leaves_the_failure_to_who_asked);
  RUN_TEST(test_addresses_are_checked);
  RUN_TEST(test_no_requests_from_the_access_point);
  RUN_TEST(test_who_waits_is_told_when_the_service_stops);
  RUN_TEST(test_internet_is_checked_after_the_connection);
  RUN_TEST(test_a_network_without_internet);
  RUN_TEST(test_internet_goes_with_the_network);
  RUN_TEST(test_the_check_can_be_turned_off);
  RUN_TEST(test_answers_of_the_position_servers);
  RUN_TEST(test_the_position_written_by_hand);
  RUN_TEST(test_the_position_asked_to_a_server);
  RUN_TEST(test_a_failed_question_is_asked_again);
  RUN_TEST(test_the_position_asked_by_command);
  RUN_TEST(test_what_was_written_by_hand_is_kept);
  RUN_TEST(test_the_position_given_by_another_part_of_the_system);
  RUN_TEST(test_an_answer_in_groups);
  RUN_TEST(test_by_default_a_public_service_is_asked);
  RUN_TEST(test_another_service_when_chosen);
  RUN_TEST(test_a_time_zone_chosen_by_the_user_stays);
  RUN_TEST(test_gps_then_the_owner_then_the_network);
  RUN_TEST(test_a_city_written_by_hand_is_not_a_point);
  RUN_TEST(test_the_owner_is_asked_where_the_device_is_once);
  RUN_TEST(test_a_gps_does_not_wear_the_flash);
  RUN_TEST(test_a_request_is_stuck_only_when_silent_too_long);
  RUN_TEST(test_a_network_lost_for_long_restarts_the_device);
  RUN_TEST(test_after_a_restart_for_the_network_the_guard_waits_longer);
  RUN_TEST(test_a_device_on_its_access_point_by_choice_never_restarts);
  return UNITY_END();
}
