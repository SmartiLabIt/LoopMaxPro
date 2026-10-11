// Tests of the web service on the PC: the HTTP interface, the access, the files, the events.
#include <string.h>
#include <unity.h>

#include <fstream>
#include <functional>
#include <string>

#include "../support/test_support.h"
#include "app/loopmax.h"
#include "services/web/web_bundle.h"

using namespace lmp;
using namespace lmp::test;
using lmp::hal::native::HttpCall;
using lmp::hal::native::HttpResult;

void setUp() {}
void tearDown() {
  Logger::setInstance(nullptr);
  webBundleReplace(nullptr, 0);
}

namespace {

constexpr const char* kHome = "Home";
constexpr const char* kHomePassword = "home-password";
// Password of the console when none was chosen: the one of the access point.
constexpr const char* kPassword = "loopmaxap";

struct System {
  TestBoard tb;
  LoopMax app{tb.board};

  // lan: the device is on a network, so the console asks for the password.
  // before: what is on the board before it starts, such as the files of an older firmware.
  explicit System(bool lan = false, uint16_t sessionTtlMin = 0,
                  std::function<void(System&)> before = nullptr) {
    if (before) before(*this);
    if (lan || sessionTtlMin) {
      tb.board.kv->begin();
      Config config(tb.board.kv, tb.board.rng, tb.board.sys);
      config.load();
      if (lan) {
        tb.native().net.addNetwork(kHome, kHomePassword);
        config.net.staSsid.assign(kHome);
        config.net.staPsw.assign(kHomePassword);
        config.sys.mode = 2;
        config.touch(Config::kNet);
        config.touch(Config::kSys);
      }
      if (sessionTtlMin) {
        config.web.sessionTtlMin = sessionTtlMin;
        config.touch(Config::kWeb);
      }
      TEST_ASSERT_TRUE(config.save() == Status::Ok);
    }
    Logger::setInstance(&app.kernel().log());
    TEST_ASSERT_TRUE(app.begin() == Status::Ok);
    advance(1000);
    TEST_ASSERT_TRUE(app.web().state() == ServiceState::Ready);
    TEST_ASSERT_TRUE(lan == app.net().connected());
  }

  Kernel& kernel() { return app.kernel(); }
  hal::native::HttpServer& http() { return tb.native().http; }
  void advance(uint32_t ms) { run(app.kernel(), tb.clock, ms); }

  // Writes a file of the console.
  void file(const char* path, const char* content) {
    tb.native().fs.mount(false);
    const std::string full = tb.native().fs.root() + path;
    const size_t slash = full.rfind('/');
    tb.native().fs.mkdir(std::string(path).substr(0, std::string(path).rfind('/')).c_str());
    (void)slash;
    std::ofstream out(full, std::ios::binary);
    out << content;
    TEST_ASSERT_TRUE(out.good());
  }

  std::string login(const char* password = kPassword) {
    const std::string body = std::string("{\"password\":\"") + password + "\"}";
    const HttpResult r = http().post("/api/auth/login", body.c_str());
    if (r.status != 200) return std::string();
    std::string token;
    token.resize(40);
    JsonValue::parse(StringView(r.body.data(), r.body.size()))["token"].getString(&token[0], 40);
    token.resize(strlen(token.c_str()));
    return token;
  }
};

// Body of an answer, parsed.
struct Body {
  std::string text;
  JsonValue v;
  explicit Body(const HttpResult& r) : text(r.body) {
    v = JsonValue::parse(StringView(text.data(), text.size()));
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

// ---------------------------------------------------------------- helpers

void test_secrets_are_compared_whole() {
  TEST_ASSERT_TRUE(sameSecret("loopmaxap", "loopmaxap"));
  TEST_ASSERT_TRUE(sameSecret("", ""));
  TEST_ASSERT_FALSE(sameSecret("loopmaxap", "loopmaxaP"));
  TEST_ASSERT_FALSE(sameSecret("loopmaxap", "loopmaxa"));
  TEST_ASSERT_FALSE(sameSecret("loopmaxa", "loopmaxap"));
  TEST_ASSERT_FALSE(sameSecret("", "x"));
  TEST_ASSERT_FALSE(sameSecret(StringView("ab\0c", 4), StringView("ab\0d", 4)));
}

void test_form_values() {
  char out[32];
  TEST_ASSERT_TRUE(formValue("a=1&password=a+word%21&b=2", "password", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("a word!", out);
  TEST_ASSERT_TRUE(formValue("a=1&b=", "b", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
  TEST_ASSERT_FALSE(formValue("a=1&b=2", "c", out, sizeof(out)));
  TEST_ASSERT_FALSE(formValue("a=%zz", "a", out, sizeof(out)));
  TEST_ASSERT_FALSE(formValue("a=%4", "a", out, sizeof(out)));
  TEST_ASSERT_FALSE(formValue("a=far-too-long-for-its-room", "a", out, 8));
  TEST_ASSERT_FALSE(formValue("", "a", out, sizeof(out)));
}

void test_form_becomes_arguments() {
  char out[128];
  TEST_ASSERT_TRUE(formToArgs("lang=it&deviceName=Office+Plugs", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("lang=\"it\" deviceName=\"Office Plugs\"", out);
  TEST_ASSERT_TRUE(formToArgs("", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
  // A quote cannot be written in a value of the command line.
  TEST_ASSERT_FALSE(formToArgs("name=a%22b", out, sizeof(out)));
  TEST_ASSERT_FALSE(formToArgs("bad name=1", out, sizeof(out)));
  TEST_ASSERT_FALSE(formToArgs("=1", out, sizeof(out)));
  TEST_ASSERT_FALSE(formToArgs("lang=it&deviceName=Office+Plugs", out, 16));
}

void test_content_types_and_fingerprints() {
  TEST_ASSERT_EQUAL_STRING("text/html; charset=utf-8", contentTypeOf("/index.html"));
  TEST_ASSERT_EQUAL_STRING("text/javascript; charset=utf-8", contentTypeOf("/a/app.3f9a1c07.js"));
  TEST_ASSERT_EQUAL_STRING("text/css; charset=utf-8", contentTypeOf("/lmp.css.gz"));
  TEST_ASSERT_EQUAL_STRING("font/woff2", contentTypeOf("/f/audiowide.woff2"));
  TEST_ASSERT_EQUAL_STRING("application/octet-stream", contentTypeOf("/firmware.bin"));

  TEST_ASSERT_TRUE(hasFingerprint("/lmp-console.3f9a1c07.js"));
  TEST_ASSERT_TRUE(hasFingerprint("/a/b/lmp.0123456789abcdef.css"));
  TEST_ASSERT_FALSE(hasFingerprint("/index.html"));
  TEST_ASSERT_FALSE(hasFingerprint("/lmp-console.min.js"));
  TEST_ASSERT_FALSE(hasFingerprint("/lmp.3f9a1c0.js"));        // seven digits
  TEST_ASSERT_FALSE(hasFingerprint("/3f9a1c07.dir/app.js"));   // in the folder, not in the name
}

void test_sessions() {
  hal::native::Random rng;
  WebAuth auth(&rng);
  const hal::Ip4 me = hal::Ip4::of(192, 168, 1, 10);
  char token[WebAuth::kTokenLength + 1];
  char other[WebAuth::kTokenLength + 1];
  uint32_t wait = 0;

  TEST_ASSERT_TRUE(auth.login("loopmaxap", "loopmaxap", me, 1000, 60, token, wait) == Status::Ok);
  TEST_ASSERT_EQUAL_UINT(WebAuth::kTokenLength, strlen(token));
  TEST_ASSERT_TRUE(auth.login("loopmaxap", "loopmaxap", me, 1000, 60, other, wait) == Status::Ok);
  TEST_ASSERT_TRUE(strcmp(token, other) != 0);
  TEST_ASSERT_EQUAL_UINT(2, auth.sessions(1000));

  TEST_ASSERT_TRUE(auth.check(token, 2000));
  TEST_ASSERT_EQUAL_UINT32(59, auth.leftSec(token, 2000));
  TEST_ASSERT_FALSE(auth.check("", 2000));
  TEST_ASSERT_FALSE(auth.check("0123456789abcdef0123456789abcdef", 2000));
  TEST_ASSERT_FALSE(auth.check(StringView(token, 31), 2000));

  // The time of a session does not grow with its use.
  TEST_ASSERT_TRUE(auth.check(token, 60999));
  TEST_ASSERT_FALSE(auth.check(token, 61000));
  TEST_ASSERT_EQUAL_UINT32(0, auth.leftSec(token, 61000));
  TEST_ASSERT_EQUAL_UINT(0, auth.sessions(61000));

  TEST_ASSERT_TRUE(auth.login("loopmaxap", "loopmaxap", me, 70000, 60, token, wait) == Status::Ok);
  TEST_ASSERT_TRUE(auth.logout(token));
  TEST_ASSERT_FALSE(auth.logout(token));
  TEST_ASSERT_FALSE(auth.check(token, 70001));

  // An empty password opens nothing, even when none is expected.
  TEST_ASSERT_TRUE(auth.login("", "", me, 80000, 60, token, wait) == Status::Denied);

  WebAuth blind(nullptr);
  TEST_ASSERT_TRUE(blind.login("loopmaxap", "loopmaxap", me, 1, 60, token, wait) ==
                   Status::Unsupported);
}

void test_the_oldest_session_gives_its_place() {
  hal::native::Random rng;
  WebAuth auth(&rng);
  const hal::Ip4 me = hal::Ip4::of(192, 168, 1, 10);
  char tokens[LMP_MAX_SESSIONS + 1][WebAuth::kTokenLength + 1];
  uint32_t wait = 0;
  for (int i = 0; i < LMP_MAX_SESSIONS; ++i) {
    TEST_ASSERT_TRUE(auth.login("x-password", "x-password", me, 1000 + i, 600, tokens[i], wait) ==
                     Status::Ok);
  }
  // The first one is used again: the second becomes the one silent for longest.
  TEST_ASSERT_TRUE(auth.check(tokens[0], 5000));
  TEST_ASSERT_TRUE(auth.login("x-password", "x-password", me, 6000, 600,
                              tokens[LMP_MAX_SESSIONS], wait) == Status::Ok);
  TEST_ASSERT_EQUAL_UINT(LMP_MAX_SESSIONS, auth.sessions(6000));
  TEST_ASSERT_TRUE(auth.check(tokens[0], 6000));
  TEST_ASSERT_FALSE(auth.check(tokens[1], 6000));
  TEST_ASSERT_TRUE(auth.check(tokens[LMP_MAX_SESSIONS], 6000));
}

void test_wrong_passwords_are_slowed_down() {
  hal::native::Random rng;
  WebAuth auth(&rng);
  const hal::Ip4 bad = hal::Ip4::of(192, 168, 1, 66);
  const hal::Ip4 good = hal::Ip4::of(192, 168, 1, 10);
  char token[WebAuth::kTokenLength + 1];
  uint32_t wait = 0;
  uint64_t now = 1000;

  for (int i = 1; i < LMP_WEB_LOGIN_ATTEMPTS; ++i) {
    TEST_ASSERT_TRUE(auth.login("wrong-one", "loopmaxap", bad, now, 60, token, wait) ==
                     Status::Denied);
    TEST_ASSERT_EQUAL_UINT32(0, wait);
  }
  TEST_ASSERT_TRUE(auth.login("wrong-one", "loopmaxap", bad, now, 60, token, wait) ==
                   Status::Denied);
  TEST_ASSERT_EQUAL_UINT32(LMP_WEB_LOGIN_LOCK_SEC, wait);

  // Locked: not even the right password is looked at.
  now += 1000;
  TEST_ASSERT_TRUE(auth.login("loopmaxap", "loopmaxap", bad, now, 60, token, wait) == Status::Busy);
  TEST_ASSERT_EQUAL_UINT32(LMP_WEB_LOGIN_LOCK_SEC - 1, wait);
  TEST_ASSERT_EQUAL_STRING("", token);
  // Another address is not touched.
  TEST_ASSERT_TRUE(auth.login("loopmaxap", "loopmaxap", good, now, 60, token, wait) == Status::Ok);

  // After the wait it can try again; a new series of failures waits twice as long.
  now += static_cast<uint64_t>(LMP_WEB_LOGIN_LOCK_SEC) * 1000;
  for (int i = 1; i < LMP_WEB_LOGIN_ATTEMPTS; ++i) {
    TEST_ASSERT_TRUE(auth.login("wrong-one", "loopmaxap", bad, now, 60, token, wait) ==
                     Status::Denied);
  }
  TEST_ASSERT_TRUE(auth.login("wrong-one", "loopmaxap", bad, now, 60, token, wait) ==
                   Status::Denied);
  TEST_ASSERT_EQUAL_UINT32(2 * LMP_WEB_LOGIN_LOCK_SEC, wait);

  now += static_cast<uint64_t>(2 * LMP_WEB_LOGIN_LOCK_SEC) * 1000;
  TEST_ASSERT_TRUE(auth.login("loopmaxap", "loopmaxap", bad, now, 60, token, wait) == Status::Ok);
  // A good password clears the past.
  for (int i = 1; i < LMP_WEB_LOGIN_ATTEMPTS; ++i) {
    TEST_ASSERT_TRUE(auth.login("wrong-one", "loopmaxap", bad, now, 60, token, wait) ==
                     Status::Denied);
    TEST_ASSERT_EQUAL_UINT32(0, wait);
  }
}

// ---------------------------------------------------------------- state and access

void test_state_is_for_everybody() {
  System sys(true);
  const HttpResult r = sys.http().get("/api/state");
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  TEST_ASSERT_EQUAL_STRING("application/json", r.contentType.c_str());
  TEST_ASSERT_EQUAL_STRING("2", r.header("X-LMP-Api").c_str());
  TEST_ASSERT_EQUAL_STRING("no-store", r.header("Cache-Control").c_str());

  Body b(r);
  TEST_ASSERT_TRUE(b.v.valid());
  TEST_ASSERT_EQUAL_INT64(2, b.v["api"].asInt());
  TEST_ASSERT_TRUE(b.v["board"].stringEquals("native"));
  TEST_ASSERT_TRUE(b.v["host"].stringEquals("loopmax-0001"));
  TEST_ASSERT_TRUE(b.v["lang"].stringEquals("en"));
  TEST_ASSERT_TRUE(b.v["mode"].stringEquals("LAN"));
  TEST_ASSERT_TRUE(b.v["auth"]["required"].asBool());
  TEST_ASSERT_FALSE(b.v["console"]["installed"].asBool(true));
  TEST_ASSERT_TRUE(b.v["modules"].isArray());
  // Nothing that identifies the device to who has not logged in.
  TEST_ASSERT_NULL(strstr(r.body.c_str(), sys.kernel().config().sys.key.c_str()));
  TEST_ASSERT_NULL(strstr(r.body.c_str(), "02:4C"));

  TEST_ASSERT_EQUAL_UINT16(405, sys.http().post("/api/state", "{}").status);
}

void test_access_point_needs_no_login() {
  System sys;
  Body state(sys.http().get("/api/state"));
  TEST_ASSERT_TRUE(state.v["mode"].stringEquals("AP"));
  TEST_ASSERT_FALSE(state.v["auth"]["required"].asBool(true));

  TEST_ASSERT_EQUAL_UINT16(200, sys.http().get("/api/report").status);
  TEST_ASSERT_EQUAL_UINT16(200, sys.http().get("/api/doc/sys.net").status);
  TEST_ASSERT_EQUAL_UINT16(200, sys.http().get("/api/manifest").status);
  TEST_ASSERT_EQUAL_UINT16(200, sys.http().post("/api/cmd/sys.log/setLevel",
                                                "{\"level\":\"debug\"}").status);
  TEST_ASSERT_TRUE(sys.http().follow() >= 0);

  // Only who is on the access point: an address of any other network logs in.
  HttpCall other;
  other.from = hal::Ip4::of(192, 168, 1, 77);
  other.target = "/api/report";
  TEST_ASSERT_EQUAL_UINT16(401, sys.http().request(other).status);
  other.target = "/api/state";
  Body seen(sys.http().request(other));
  TEST_ASSERT_TRUE(seen.v["auth"]["required"].asBool());
}

void test_who_changes_the_network_reads_the_new_address() {
  System sys;
  sys.tb.native().net.addNetwork(kHome, kHomePassword);
  const HttpResult asked = sys.http().post("/api/cmd/sys.net/join",
                                           "{\"ssid\":\"Home\",\"psw\":\"home-password\"}");
  TEST_ASSERT_EQUAL_UINT16(202, asked.status);
  sys.advance(1000);
  TEST_ASSERT_TRUE(sys.app.net().connected());

  // The device is on the network now, and who asked is still on the access point.
  Body state(sys.http().get("/api/state"));
  TEST_ASSERT_TRUE(state.v["mode"].stringEquals("LAN"));
  TEST_ASSERT_FALSE(state.v["auth"]["required"].asBool(true));
  Body net(sys.http().get("/api/doc/sys.net"));
  TEST_ASSERT_TRUE(net.v["join"]["last"].stringEquals("joined"));
  TEST_ASSERT_TRUE(net.v["sta"]["ip"].stringEquals("192.168.1.50"));
  Body web(sys.http().get("/api/doc/sys.web"));
  TEST_ASSERT_FALSE(web.v["auth"]["required"].asBool(true));

  // Who comes from the network logs in, from the first moment.
  HttpCall lan;
  lan.from = hal::Ip4::of(192, 168, 1, 77);
  lan.target = "/api/doc/sys.net";
  TEST_ASSERT_EQUAL_UINT16(401, sys.http().request(lan).status);
  lan.target = "/api/state";
  Body seen(sys.http().request(lan));
  TEST_ASSERT_TRUE(seen.v["auth"]["required"].asBool());

  // The access point goes down: its addresses are worth nothing.
  sys.advance(31000);
  TEST_ASSERT_FALSE(sys.app.net().apUp());
  TEST_ASSERT_EQUAL_UINT16(401, sys.http().get("/api/doc/sys.net").status);
  Body later(sys.http().get("/api/state"));
  TEST_ASSERT_TRUE(later.v["auth"]["required"].asBool());
}

void test_a_network_being_tried_does_not_open_the_device() {
  System sys;
  sys.tb.native().net.addNetwork(kHome, kHomePassword);
  sys.tb.native().net.setDelays(1, 1);
  TEST_ASSERT_EQUAL_UINT16(202, sys.http().post("/api/cmd/sys.net/test",
                                                "{\"ssid\":\"Home\",\"psw\":\"home-password\"}")
                                    .status);
  // The mode is still AP, and for a moment the device has an address on the network.
  HttpCall lan;
  lan.from = hal::Ip4::of(192, 168, 1, 77);
  lan.target = "/api/report";
  for (int i = 0; i < 20; ++i) {
    sys.advance(20);
    TEST_ASSERT_EQUAL_UINT16(401, sys.http().request(lan).status);
    TEST_ASSERT_EQUAL_UINT16(200, sys.http().get("/api/report").status);
  }
  TEST_ASSERT_TRUE(sys.app.net().lastJoin() == JoinResult::Tested);
}

void test_lan_needs_a_login() {
  System sys(true);
  const char* closed[] = {"/api/report",   "/api/doc/sys.net", "/api/cfg/sys.net",
                          "/api/manifest", "/api/log",         "/api/manifest/sys.net"};
  for (const char* path : closed) {
    const HttpResult r = sys.http().get(path);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(401, r.status, path);
    Body b(r);
    TEST_ASSERT_TRUE_MESSAGE(b.v["error"].stringEquals("auth"), path);
  }
  TEST_ASSERT_EQUAL_UINT16(401, sys.http().post("/api/cmd/sys.system/restart", "{}").status);
  TEST_ASSERT_FALSE(sys.kernel().restartPending());
  TEST_ASSERT_EQUAL_INT(-1, sys.http().follow());
  // Refused before looking at what was asked: nothing is learned about what exists.
  TEST_ASSERT_EQUAL_UINT16(401, sys.http().get("/api/doc/sys.none").status);

  const std::string token = sys.login();
  TEST_ASSERT_EQUAL_UINT(WebAuth::kTokenLength, token.size());
  TEST_ASSERT_TRUE(logHas(sys.kernel(), "login from 192.168.4.2"));
  for (const char* path : closed) {
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(200, sys.http().get(path, token.c_str()).status, path);
  }
  TEST_ASSERT_TRUE(sys.http().follow(token.c_str()) >= 0);

  // The token in the address, for what cannot send a header.
  const std::string withToken = "/api/report?parts=device&token=" + token;
  TEST_ASSERT_EQUAL_UINT16(200, sys.http().get(withToken.c_str()).status);
  TEST_ASSERT_EQUAL_UINT16(401, sys.http().get("/api/report?token=0123").status);
  TEST_ASSERT_EQUAL_UINT16(401, sys.http().get("/api/report", "not-a-token").status);
}

void test_login() {
  System sys(true);
  HttpResult r = sys.http().post("/api/auth/login", "{\"password\":\"wrong-one\"}");
  TEST_ASSERT_EQUAL_UINT16(401, r.status);
  Body wrong(r);
  TEST_ASSERT_TRUE(wrong.v["error"].stringEquals("denied"));
  TEST_ASSERT_TRUE(wrong.v["field"].stringEquals("password"));
  TEST_ASSERT_FALSE(wrong.v["token"].valid());
  TEST_ASSERT_TRUE(logHas(sys.kernel(), "wrong password from 192.168.4.2"));

  TEST_ASSERT_EQUAL_UINT16(400, sys.http().post("/api/auth/login", "{}").status);
  TEST_ASSERT_EQUAL_UINT16(400, sys.http().post("/api/auth/login", "password").status);
  TEST_ASSERT_EQUAL_UINT16(405, sys.http().get("/api/auth/login").status);

  r = sys.http().post("/api/auth/login", "{\"user\":\"admin\",\"password\":\"loopmaxap\"}");
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  Body ok(r);
  TEST_ASSERT_EQUAL_UINT(WebAuth::kTokenLength, ok.v["token"].stringLength());
  TEST_ASSERT_EQUAL_INT64(86400, ok.v["ttl"].asInt());
  TEST_ASSERT_TRUE(ok.v["user"].stringEquals("admin"));

  // As a form, the way a plain page sends it.
  r = sys.http().post("/api/auth/login", "password=loopmaxap", nullptr,
                      "application/x-www-form-urlencoded; charset=UTF-8");
  TEST_ASSERT_EQUAL_UINT16(200, r.status);

  // No password in the log, in any case.
  TEST_ASSERT_FALSE(logHas(sys.kernel(), "loopmaxap"));
  TEST_ASSERT_FALSE(logHas(sys.kernel(), "wrong-one"));
}

void test_who_am_i_and_logout() {
  System sys(true);
  Body before(sys.http().get("/api/auth/me"));
  TEST_ASSERT_TRUE(before.v["required"].asBool());
  TEST_ASSERT_FALSE(before.v["auth"].asBool(true));

  const std::string token = sys.login();
  Body after(sys.http().get("/api/auth/me", token.c_str()));
  TEST_ASSERT_TRUE(after.v["auth"].asBool());
  TEST_ASSERT_TRUE(after.v["user"].stringEquals("admin"));
  TEST_ASSERT_TRUE(after.v["ttl"].asInt() > 86000);

  TEST_ASSERT_EQUAL_UINT16(405, sys.http().get("/api/auth/logout", token.c_str()).status);
  Body out(sys.http().post("/api/auth/logout", "", token.c_str()));
  TEST_ASSERT_TRUE(out.v["ok"].asBool());
  TEST_ASSERT_EQUAL_UINT16(401, sys.http().get("/api/report", token.c_str()).status);
  Body again(sys.http().post("/api/auth/logout", "", token.c_str()));
  TEST_ASSERT_FALSE(again.v["ok"].asBool(true));
}

void test_a_session_ends() {
  System sys(true, 1);
  const std::string token = sys.login();
  TEST_ASSERT_EQUAL_UINT16(200, sys.http().get("/api/report", token.c_str()).status);
  sys.advance(59000);
  TEST_ASSERT_EQUAL_UINT16(200, sys.http().get("/api/report", token.c_str()).status);
  sys.advance(2000);
  TEST_ASSERT_EQUAL_UINT16(401, sys.http().get("/api/report", token.c_str()).status);
}

void test_too_many_wrong_passwords() {
  System sys(true);
  for (int i = 0; i < LMP_WEB_LOGIN_ATTEMPTS; ++i) {
    TEST_ASSERT_EQUAL_UINT16(401, sys.http().post("/api/auth/login",
                                                  "{\"password\":\"wrong-one\"}").status);
  }
  HttpResult r = sys.http().post("/api/auth/login", "{\"password\":\"loopmaxap\"}");
  TEST_ASSERT_EQUAL_UINT16(429, r.status);
  TEST_ASSERT_EQUAL_STRING("30", r.header("Retry-After").c_str());
  TEST_ASSERT_TRUE(logHas(sys.kernel(), "address locked"));

  // Another address gets in.
  HttpCall other;
  other.method = "POST";
  other.target = "/api/auth/login";
  other.body = "{\"password\":\"loopmaxap\"}";
  other.from = hal::Ip4::of(192, 168, 1, 77);
  TEST_ASSERT_EQUAL_UINT16(200, sys.http().request(other).status);

  sys.advance(31000);
  TEST_ASSERT_EQUAL_UINT16(200, sys.http().post("/api/auth/login",
                                                "{\"password\":\"loopmaxap\"}").status);

  Body doc(sys.http().get("/api/doc/sys.web", sys.login().c_str()));
  TEST_ASSERT_EQUAL_INT64(LMP_WEB_LOGIN_ATTEMPTS, doc.v["auth"]["wrongPasswords"].asInt());
}

void test_fallback_needs_no_login() {
  TestBoard probe;  // only to reach the storage before the system exists
  (void)probe;
  System sys(true);
  TEST_ASSERT_EQUAL_UINT16(401, sys.http().get("/api/report").status);

  // The network goes away for good: the access point comes up, and it is the way in.
  sys.tb.native().net.removeNetwork(kHome);
  sys.tb.native().net.dropSta();
  sys.advance(40000);
  TEST_ASSERT_TRUE(sys.app.net().netState() == NetState::Fallback);
  TEST_ASSERT_EQUAL_UINT16(200, sys.http().get("/api/report").status);
  Body state(sys.http().get("/api/state"));
  TEST_ASSERT_TRUE(state.v["mode"].stringEquals("LAN"));
  TEST_ASSERT_TRUE(state.v["fallback"].asBool());
  TEST_ASSERT_FALSE(state.v["auth"]["required"].asBool(true));

  // The way in is the access point, not the state of the device.
  HttpCall other;
  other.from = hal::Ip4::of(192, 168, 1, 77);
  other.target = "/api/report";
  TEST_ASSERT_EQUAL_UINT16(401, sys.http().request(other).status);
}

// ---------------------------------------------------------------- documents

void test_report() {
  System sys;
  Body all(sys.http().get("/api/report"));
  TEST_ASSERT_TRUE(all.v.valid());
  TEST_ASSERT_EQUAL_UINT(kDeviceKeyLength, all.v["device"]["key"].stringLength());
  TEST_ASSERT_TRUE(all.v["system"]["chip"].stringEquals("PC"));
  TEST_ASSERT_TRUE(all.v["net"]["ap"]["up"].asBool());
  TEST_ASSERT_TRUE(all.v["services"].size() >= 10);

  Body part(sys.http().get("/api/report?parts=device,net"));
  TEST_ASSERT_TRUE(part.v["device"].isObject());
  TEST_ASSERT_TRUE(part.v["net"].isObject());
  TEST_ASSERT_FALSE(part.v["system"].valid());
  TEST_ASSERT_FALSE(part.v["services"].valid());

  Body spaced(sys.http().get("/api/report?parts=device%2C%20system"));
  TEST_ASSERT_TRUE(spaced.v["system"].isObject());
}

void test_documents_and_manifests() {
  System sys;
  Body doc(sys.http().get("/api/doc/sys.storage"));
  TEST_ASSERT_TRUE(doc.v["fs"]["mounted"].asBool());
  Body slash(sys.http().get("/api/doc/sys.storage/"));
  TEST_ASSERT_TRUE(slash.v["fs"]["mounted"].asBool());

  HttpResult r = sys.http().get("/api/doc/sys.none");
  TEST_ASSERT_EQUAL_UINT16(404, r.status);
  Body missing(r);
  TEST_ASSERT_TRUE(missing.v["error"].stringEquals("not_found"));
  TEST_ASSERT_TRUE(missing.v["field"].stringEquals("target"));
  TEST_ASSERT_EQUAL_UINT16(404, sys.http().get("/api/doc").status);

  Body list(sys.http().get("/api/manifest"));
  TEST_ASSERT_TRUE(list.v["targets"].isArray());
  bool hasWeb = false;
  for (const JsonMember m : list.v["targets"]) {
    if (m.value.stringEquals("sys.web")) hasWeb = true;
  }
  TEST_ASSERT_TRUE(hasWeb);

  Body man(sys.http().get("/api/manifest/sys.net"));
  TEST_ASSERT_TRUE(man.v["target"].stringEquals("sys.net"));
  bool join = false;
  for (const JsonMember m : man.v["commands"]) {
    if (!m.value["name"].stringEquals("join")) continue;
    join = true;
    TEST_ASSERT_TRUE(m.value["confirm"].asBool());
    TEST_ASSERT_TRUE(m.value["async"].asBool());
    TEST_ASSERT_TRUE(m.value["args"].at(1)["name"].stringEquals("psw"));
    TEST_ASSERT_TRUE(m.value["args"].at(1)["secret"].asBool());
  }
  TEST_ASSERT_TRUE(join);
  TEST_ASSERT_EQUAL_UINT16(404, sys.http().get("/api/manifest/sys.none").status);

  TEST_ASSERT_EQUAL_UINT16(404, sys.http().get("/api/none").status);
  TEST_ASSERT_EQUAL_UINT16(404, sys.http().get("/api/").status);
  TEST_ASSERT_EQUAL_UINT16(405, sys.http().post("/api/report", "{}").status);
}

void test_settings_without_secrets() {
  System sys(true);
  sys.kernel().config().web.adminPsw.assign("password-console");
  const std::string token = sys.login("password-console");
  TEST_ASSERT_FALSE(token.empty());

  HttpResult r = sys.http().get("/api/cfg/sys.net", token.c_str());
  Body net(r);
  TEST_ASSERT_TRUE(net.v["staSsid"].stringEquals(kHome));
  TEST_ASSERT_TRUE(net.v["staPswSet"].asBool());
  TEST_ASSERT_TRUE(net.v["apPswSet"].asBool());
  TEST_ASSERT_FALSE(net.v["prevPswSet"].asBool(true));
  TEST_ASSERT_FALSE(net.v["staPsw"].valid());
  TEST_ASSERT_EQUAL_INT64(180, net.v["rollbackSec"].asInt());

  Body web(sys.http().get("/api/cfg/sys.web", token.c_str()));
  TEST_ASSERT_TRUE(web.v["adminPswSet"].asBool());
  TEST_ASSERT_EQUAL_INT64(1440, web.v["sessionTtlMin"].asInt());

  Body system(sys.http().get("/api/cfg/sys.config", token.c_str()));
  TEST_ASSERT_TRUE(system.v["hostname"].stringEquals("loopmax-0001"));
  TEST_ASSERT_FALSE(system.v["key"].valid());

  // A target without settings of its own.
  Body none(sys.http().get("/api/cfg/sys.gpio", token.c_str()));
  TEST_ASSERT_TRUE(none.v.isObject());
  TEST_ASSERT_EQUAL_UINT(0, none.v.size());
  TEST_ASSERT_EQUAL_UINT16(404, sys.http().get("/api/cfg/sys.none", token.c_str()).status);

  const char* secrets[] = {kHomePassword, "password-console", "loopmaxap"};
  const char* paths[] = {"/api/cfg/sys.net", "/api/cfg/sys.web", "/api/doc/sys.net",
                         "/api/doc/sys.web", "/api/doc/sys.config", "/api/report", "/api/log"};
  for (const char* path : paths) {
    const HttpResult page = sys.http().get(path, token.c_str());
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(200, page.status, path);
    for (const char* secret : secrets) {
      TEST_ASSERT_NULL_MESSAGE(strstr(page.body.c_str(), secret), path);
    }
    TEST_ASSERT_NULL_MESSAGE(strstr(page.body.c_str(), token.c_str()), path);
  }
}

#if LMP_FEATURE_MODULES
// The module of this build is the demo.
void test_modules_from_the_console() {
  System sys(true);
  const std::string token = sys.login();
  TEST_ASSERT_FALSE(token.empty());

  // Before the login: what is needed to draw the tabs, and nothing else.
  Body state(sys.http().get("/api/state"));
  TEST_ASSERT_EQUAL_UINT(1, state.v["modules"].size());
  const JsonValue tab = state.v["modules"].at(0);
  TEST_ASSERT_TRUE(tab["name"].stringEquals("demo"));
  TEST_ASSERT_TRUE(tab["title"].stringEquals("Demo"));
  TEST_ASSERT_TRUE(tab["kind"].stringEquals("hardware"));
  TEST_ASSERT_TRUE(tab["version"].stringEquals("1.0.0"));
  TEST_ASSERT_FALSE(tab["state"].valid());
  TEST_ASSERT_EQUAL_UINT16(401, sys.http().get("/api/doc/mod.demo").status);
  TEST_ASSERT_EQUAL_UINT16(401, sys.http().get("/api/cfg/mod.demo").status);
  TEST_ASSERT_EQUAL_UINT16(401, sys.http().post("/api/cmd/mod.demo/set",
                                                "{\"channel\":0,\"value\":true}").status);

  // A command, its document.
  HttpResult r = sys.http().post("/api/cmd/mod.demo/set", "{\"channel\":1,\"value\":true}",
                                 token.c_str());
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  Body doc(sys.http().get("/api/doc/mod.demo", token.c_str()));
  TEST_ASSERT_EQUAL_INT64(1, doc.v["pins"].at(1)["level"].asInt());
  TEST_ASSERT_EQUAL_INT64(0, doc.v["pins"].at(0)["level"].asInt());

  // The settings: read, changed, refused.
  Body before(sys.http().get("/api/cfg/mod.demo", token.c_str()));
  TEST_ASSERT_TRUE(before.v["label"].stringEquals("Demo"));
  TEST_ASSERT_EQUAL_INT64(1000, before.v["pulseMs"].asInt());
  r = sys.http().post("/api/cmd/mod.demo/config.set", "{\"label\":\"Bench\",\"pulseMs\":250}",
                      token.c_str());
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  Body after(sys.http().get("/api/cfg/mod.demo", token.c_str()));
  TEST_ASSERT_TRUE(after.v["label"].stringEquals("Bench"));
  TEST_ASSERT_EQUAL_INT64(250, after.v["pulseMs"].asInt());
  r = sys.http().post("/api/cmd/mod.demo/config.set", "{\"pulseMs\":5}", token.c_str());
  TEST_ASSERT_EQUAL_UINT16(400, r.status);
  Body refused(r);
  TEST_ASSERT_TRUE(refused.v["field"].stringEquals("pulseMs"));
  TEST_ASSERT_EQUAL_UINT16(404, sys.http().get("/api/cfg/mod.nobody", token.c_str()).status);

  // The manifest tells the console how to draw the form of the settings.
  Body manifest(sys.http().get("/api/manifest/mod.demo", token.c_str()));
  TEST_ASSERT_TRUE(manifest.v["module"]["title"].stringEquals("Demo"));
  TEST_ASSERT_EQUAL_UINT(3, manifest.v["config"].size());
  TEST_ASSERT_TRUE(manifest.v["config"].at(0)["key"].stringEquals("label"));

  // Without the help texts, for the boards with little memory: the rest is the same.
  Body brief(sys.http().get("/api/manifest/mod.demo?help=0", token.c_str()));
  TEST_ASSERT_TRUE(brief.text.size() + 500 < manifest.text.size());
  TEST_ASSERT_EQUAL_UINT(manifest.v["commands"].size(), brief.v["commands"].size());
  TEST_ASSERT_FALSE(brief.v["commands"].at(0)["help"].valid());
  TEST_ASSERT_TRUE(brief.v["commands"].at(0)["name"].valid());
  TEST_ASSERT_TRUE(manifest.v["commands"].at(0)["help"].valid());
  TEST_ASSERT_EQUAL_UINT(3, brief.v["config"].size());

  // The demo is one module for the whole program: it is left as it was found.
  r = sys.http().post("/api/cmd/mod.demo/config.reset", "", token.c_str());
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  r = sys.http().post("/api/cmd/mod.demo/all", "{\"value\":false}", token.c_str());
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
}
#endif

void test_log() {
  System sys;
  Logger& log = sys.kernel().log();
  log.setLevel(LogLevel::Info);
  log.clear();
  const LogSource src = log.addSource("trial");
  const uint32_t first = log.lastSeq();
  for (int i = 1; i <= 6; ++i) log.write(LogLevel::Info, src, "line %d", i);
  log.write(LogLevel::Warn, kLogSys, "a warning");

  const std::string since = "/api/log?since=" + std::to_string(first);
  Body page(sys.http().get((since + "&max=4").c_str()));
  TEST_ASSERT_EQUAL_UINT(4, page.v["items"].size());
  TEST_ASSERT_TRUE(page.v["items"].at(0)["msg"].stringEquals("line 1"));
  TEST_ASSERT_TRUE(page.v["items"].at(0)["src"].stringEquals("trial"));
  TEST_ASSERT_EQUAL_INT64(log.lastSeq(), page.v["seqLast"].asInt());

  Body warnings(sys.http().get((since + "&level=warn").c_str()));
  TEST_ASSERT_EQUAL_UINT(1, warnings.v["items"].size());
  Body one(sys.http().get((since + "&src=trial&max=100").c_str()));
  TEST_ASSERT_EQUAL_UINT(6, one.v["items"].size());

  const HttpResult bad = sys.http().get("/api/log?level=loud");
  TEST_ASSERT_EQUAL_UINT16(400, bad.status);
  Body why(bad);
  TEST_ASSERT_TRUE(why.v["field"].stringEquals("level"));
}

void test_a_full_log_fits_in_one_answer() {
  System sys;
  Logger& log = sys.kernel().log();
  log.clear();
  const std::string longest(LMP_LOG_MSG_LEN + 40, '"');  // every character doubles in JSON
  for (int i = 0; i < LMP_LOG_RING; ++i) log.write(LogLevel::Info, kLogSys, "%s", longest.c_str());

  const HttpResult r = sys.http().get("/api/log?max=9999");
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  Body page(r);
  TEST_ASSERT_TRUE(page.v.valid());
  TEST_ASSERT_TRUE(page.v["items"].size() > 0);
  TEST_ASSERT_TRUE(r.body.size() <= LMP_HTTP_RESP_MAX);
  TEST_ASSERT_TRUE(page.v["items"].at(0)["cut"].asBool());
}

void test_an_answer_that_does_not_fit() {
  System sys;
  uint8_t id = 0;
  TEST_ASSERT_TRUE(sys.kernel().commands().addTarget("mod", "big", [](JsonWriter& w) {
    const std::string filler(200, 'x');
    w.beginArray("rows");
    for (int i = 0; i < LMP_HTTP_RESP_MAX / 100; ++i) w.str(filler.c_str());
    w.endArray();
  }, id) == Status::Ok);

  const HttpResult r = sys.http().get("/api/doc/mod.big");
  TEST_ASSERT_EQUAL_UINT16(507, r.status);
  Body b(r);
  TEST_ASSERT_TRUE(b.v.valid());
  TEST_ASSERT_TRUE(b.v["error"].stringEquals("too_large"));
  // The next answer is not touched by what was dropped.
  Body next(sys.http().get("/api/doc/sys.storage"));
  TEST_ASSERT_TRUE(next.v["fs"]["mounted"].asBool());
}

// ---------------------------------------------------------------- commands

void test_commands() {
  System sys;
  HttpResult r = sys.http().post("/api/cmd/sys.config/set",
                                 "{\"lang\":\"it\",\"deviceName\":\"Office Plugs\"}");
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  Body ok(r);
  TEST_ASSERT_TRUE(ok.v["ok"].asBool());
  TEST_ASSERT_TRUE(ok.v["status"].stringEquals("ok"));
  TEST_ASSERT_TRUE(ok.v["result"].isObject());
  TEST_ASSERT_EQUAL_STRING("it", sys.kernel().config().sys.lang.c_str());
  TEST_ASSERT_EQUAL_STRING("Office Plugs", sys.kernel().config().sys.deviceName.c_str());
  TEST_ASSERT_TRUE(logHas(sys.kernel(), "sys.config set from local: ok"));

  r = sys.http().post("/api/cmd/sys.config/set", "{\"hostname\":\"-no\"}");
  TEST_ASSERT_EQUAL_UINT16(400, r.status);
  Body bad(r);
  TEST_ASSERT_FALSE(bad.v["ok"].asBool(true));
  TEST_ASSERT_TRUE(bad.v["status"].stringEquals("invalid"));
  TEST_ASSERT_TRUE(bad.v["field"].stringEquals("hostname"));

  // A command that ends later.
  r = sys.http().post("/api/cmd/sys.net/scan", "");
  TEST_ASSERT_EQUAL_UINT16(202, r.status);
  Body later(r);
  TEST_ASSERT_TRUE(later.v["ok"].asBool());
  TEST_ASSERT_TRUE(later.v["status"].stringEquals("pending"));

  // With results.
  sys.tb.native().net.addNetwork(kHome, kHomePassword);
  sys.advance(1000);
  r = sys.http().post("/api/cmd/sys.net/join",
                      "{\"ssid\":\"Home\",\"psw\":\"home-password\"}");
  TEST_ASSERT_EQUAL_UINT16(202, r.status);
  Body join(r);
  TEST_ASSERT_TRUE(join.v["result"]["ssid"].stringEquals(kHome));
  TEST_ASSERT_EQUAL_INT64(180, join.v["result"]["timeoutSec"].asInt());
  r = sys.http().post("/api/cmd/sys.net/join",
                      "{\"ssid\":\"Home\",\"psw\":\"home-password\"}");
  TEST_ASSERT_EQUAL_UINT16(409, r.status);
}

void test_commands_as_a_form() {
  System sys;
  const char* form = "application/x-www-form-urlencoded";
  HttpResult r = sys.http().post("/api/cmd/sys.config/set", "lang=it&deviceName=Office+Plugs",
                                 nullptr, form);
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  TEST_ASSERT_EQUAL_STRING("Office Plugs", sys.kernel().config().sys.deviceName.c_str());

  // Numbers and switches too.
  r = sys.http().post("/api/cmd/sys.clock/set", "unix=1790000000&tz=UTC&posix=UTC0", nullptr,
                      form);
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  TEST_ASSERT_EQUAL_INT64(1790000000, sys.kernel().board().clock->unixTime());

  r = sys.http().post("/api/cmd/sys.config/set", "deviceName=a%22b", nullptr, form);
  TEST_ASSERT_EQUAL_UINT16(400, r.status);
}

void test_commands_that_are_not_well_formed() {
  System sys;
  struct Case {
    const char* target;
    const char* body;
    uint16_t status;
  };
  const Case cases[] = {
      {"/api/cmd/sys.config/set", "{\"lang\":", 400},
      {"/api/cmd/sys.config/set", "lang=it", 400},
      {"/api/cmd/sys.config/set", "[1,2]", 400},
      {"/api/cmd/sys.config/set", "{}", 400},
      {"/api/cmd/sys.none/set", "{}", 404},
      {"/api/cmd/sys.config/none", "{}", 404},
      {"/api/cmd/sys.config", "{}", 400},
      {"/api/cmd", "{}", 400},
      {"/api/cmd/sys.config/set/more", "{}", 404},
  };
  for (const Case& c : cases) {
    const HttpResult r = sys.http().post(c.target, c.body);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(c.status, r.status, c.target);
    Body b(r);
    TEST_ASSERT_TRUE_MESSAGE(b.v.valid(), c.body);
  }
  TEST_ASSERT_EQUAL_STRING("en", sys.kernel().config().sys.lang.c_str());
  // Commands change things: never with a GET.
  TEST_ASSERT_EQUAL_UINT16(405, sys.http().get("/api/cmd/sys.system/restart").status);
  TEST_ASSERT_FALSE(sys.kernel().restartPending());
}

void test_a_request_that_is_too_long() {
  System sys;
  const std::string body = "{\"deviceName\":\"" + std::string(LMP_HTTP_BODY_MAX, 'x') + "\"}";
  const HttpResult r = sys.http().post("/api/cmd/sys.config/set", body.c_str());
  TEST_ASSERT_EQUAL_UINT16(413, r.status);
  Body b(r);
  TEST_ASSERT_TRUE(b.v["error"].stringEquals("too_long"));
}

void test_restart_from_the_console() {
  System sys;
  const unsigned before = sys.tb.native().sys.restartCount();
  TEST_ASSERT_EQUAL_UINT16(200, sys.http().post("/api/cmd/sys.system/restart", "").status);
  // The answer leaves first.
  TEST_ASSERT_EQUAL_UINT(before, sys.tb.native().sys.restartCount());
  sys.advance(1500);
  TEST_ASSERT_EQUAL_UINT(before + 1, sys.tb.native().sys.restartCount());
}

// ---------------------------------------------------------------- password of the console

void test_the_password_is_changed() {
  System sys(true);
  const std::string token = sys.login();
  const int follower = sys.http().follow(token.c_str());
  TEST_ASSERT_TRUE(follower >= 0);

  HttpResult r = sys.http().post("/api/cmd/sys.web/setPassword",
                                 "{\"old\":\"wrong-one\",\"new\":\"new-password\"}",
                                 token.c_str());
  TEST_ASSERT_EQUAL_UINT16(403, r.status);
  Body denied(r);
  TEST_ASSERT_TRUE(denied.v["field"].stringEquals("old"));
  r = sys.http().post("/api/cmd/sys.web/setPassword", "{\"old\":\"loopmaxap\",\"new\":\"short\"}",
                      token.c_str());
  TEST_ASSERT_EQUAL_UINT16(400, r.status);
  TEST_ASSERT_EQUAL_UINT16(200, sys.http().get("/api/report", token.c_str()).status);

  r = sys.http().post("/api/cmd/sys.web/setPassword",
                      "{\"old\":\"loopmaxap\",\"new\":\"new-password\"}", token.c_str());
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  // Who was in has to come back.
  TEST_ASSERT_EQUAL_UINT16(401, sys.http().get("/api/report", token.c_str()).status);
  TEST_ASSERT_EQUAL_UINT(0, sys.kernel().board().http->sse("/api/events")->clients());
  TEST_ASSERT_TRUE(sys.login().empty());
  const std::string fresh = sys.login("new-password");
  TEST_ASSERT_FALSE(fresh.empty());

  Body doc(sys.http().get("/api/doc/sys.web", fresh.c_str()));
  TEST_ASSERT_TRUE(doc.v["auth"]["ownPassword"].asBool());
  TEST_ASSERT_FALSE(logHas(sys.kernel(), "new-password"));

  // Written with the settings.
  sys.advance(LMP_CONFIG_SAVE_DELAY_MS + 1200);
  TEST_ASSERT_FALSE(sys.kernel().config().dirty());

  // Without a password of its own the console goes back to the one of the access point.
  const std::string again = sys.login("new-password");
  r = sys.http().post("/api/cmd/sys.web/setPassword",
                      "{\"old\":\"new-password\",\"new\":\"\"}", again.c_str());
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  TEST_ASSERT_FALSE(sys.login().empty());
}

void test_every_session_is_closed_by_command() {
  System sys(true);
  const std::string one = sys.login();
  const std::string two = sys.login();
  sys.http().follow(one.c_str());
  TEST_ASSERT_EQUAL_UINT16(200, sys.http().post("/api/cmd/sys.web/logoutAll", "",
                                                one.c_str()).status);
  TEST_ASSERT_EQUAL_UINT16(401, sys.http().get("/api/report", one.c_str()).status);
  TEST_ASSERT_EQUAL_UINT16(401, sys.http().get("/api/report", two.c_str()).status);
  TEST_ASSERT_EQUAL_UINT(0, sys.kernel().board().http->sse("/api/events")->clients());
}

// ---------------------------------------------------------------- files of the console

void test_without_the_console_there_is_a_page() {
  System sys;
  const HttpResult r = sys.http().get("/");
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  TEST_ASSERT_EQUAL_STRING("text/html; charset=utf-8", r.contentType.c_str());
  TEST_ASSERT_NOT_NULL(strstr(r.body.c_str(), "LoopMaxPro"));
  TEST_ASSERT_NOT_NULL(strstr(r.body.c_str(), "not installed"));
  TEST_ASSERT_TRUE(r.file.empty());
  TEST_ASSERT_EQUAL_UINT16(200, sys.http().get("/index.html").status);
  TEST_ASSERT_EQUAL_UINT16(404, sys.http().get("/lmp-console.js").status);
}

void test_files_of_the_console() {
  System sys;
  sys.file("/web/index.html", "<html>console</html>");
  sys.file("/web/lmp-console.3f9a1c07.js.gz", "compressed-script");
  sys.file("/web/i18n/it.json", "{\"hello\":\"ciao\"}");
  sys.file("/web/i18n/it.json.gz", "compressed-json");
  sys.file("/cfg/secret.json", "{\"psw\":\"classified\"}");

  HttpResult r = sys.http().get("/");
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  TEST_ASSERT_EQUAL_STRING("<html>console</html>", r.body.c_str());
  TEST_ASSERT_EQUAL_STRING("/web/index.html", r.file.c_str());
  TEST_ASSERT_FALSE(r.gzip);
  // The page itself is always asked again: it names the files of the moment.
  TEST_ASSERT_EQUAL_UINT32(0, r.cacheSeconds);
  TEST_ASSERT_EQUAL_STRING("2", r.header("X-LMP-Api").c_str());

  r = sys.http().get("/lmp-console.3f9a1c07.js");
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  TEST_ASSERT_EQUAL_STRING("compressed-script", r.body.c_str());
  TEST_ASSERT_TRUE(r.gzip);
  TEST_ASSERT_EQUAL_STRING("gzip", r.header("Content-Encoding").c_str());
  TEST_ASSERT_EQUAL_STRING("text/javascript; charset=utf-8", r.contentType.c_str());
  TEST_ASSERT_EQUAL_UINT32(31536000, r.cacheSeconds);

  // The compressed copy comes first.
  r = sys.http().get("/i18n/it.json?v=3");
  TEST_ASSERT_EQUAL_STRING("compressed-json", r.body.c_str());
  TEST_ASSERT_EQUAL_STRING("application/json", r.contentType.c_str());
  TEST_ASSERT_EQUAL_UINT32(0, r.cacheSeconds);

  Body state(sys.http().get("/api/state"));
  TEST_ASSERT_TRUE(state.v["console"]["installed"].asBool());

  // Nothing outside the folder of the console.
  const char* outside[] = {"/../cfg/secret.json", "/%2e%2e/cfg/secret.json", "/i18n/../../cfg/secret.json",
                           "//cfg/secret.json", "/cfg/secret.json", "/..", "/i18n\\..\\x"};
  for (const char* path : outside) {
    r = sys.http().get(path);
    TEST_ASSERT_TRUE_MESSAGE(r.status == 400 || r.status == 404, path);
    TEST_ASSERT_NULL_MESSAGE(strstr(r.body.c_str(), "classified"), path);
  }
  TEST_ASSERT_EQUAL_UINT16(405, sys.http().post("/index.html", "x").status);
}

void test_files_need_no_login() {
  System sys(true);
  sys.file("/web/index.html", "<html>console</html>");
  TEST_ASSERT_EQUAL_UINT16(200, sys.http().get("/").status);
  TEST_ASSERT_EQUAL_UINT16(401, sys.http().get("/api/report").status);
}

// The console inside the program, as tools/pio/web.py writes it. Not real gzip: the
// service sends the bytes as they are.
const uint8_t kPage[] = {'<', 'p', 'r', 'o', 'g', 'r', 'a', 'm', '>'};
const uint8_t kScript[] = {'s', 'c', 'r', 'i', 'p', 't'};
const WebAsset kProgramConsole[] = {
    {"/index.html", kPage, sizeof(kPage)},
    {"/lmp.0123abcd.js", kScript, sizeof(kScript)},
};

void test_the_console_of_the_program() {
  webBundleReplace(kProgramConsole, 2);
  System sys;
  HttpResult r = sys.http().get("/");
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  TEST_ASSERT_EQUAL_STRING("<program>", r.body.c_str());
  TEST_ASSERT_TRUE(r.file.empty());
  TEST_ASSERT_EQUAL_STRING("gzip", r.header("Content-Encoding").c_str());
  TEST_ASSERT_EQUAL_STRING("no-cache", r.header("Cache-Control").c_str());
  TEST_ASSERT_EQUAL_STRING("text/html; charset=utf-8", r.contentType.c_str());

  r = sys.http().get("/lmp.0123abcd.js");
  TEST_ASSERT_EQUAL_STRING("script", r.body.c_str());
  TEST_ASSERT_EQUAL_STRING("public, max-age=31536000, immutable", r.header("Cache-Control").c_str());
  TEST_ASSERT_EQUAL_UINT16(404, sys.http().get("/lmp.99999999.js").status);

  Body state(sys.http().get("/api/state"));
  TEST_ASSERT_TRUE(state.v["console"]["installed"].asBool());
  TEST_ASSERT_TRUE(state.v["console"]["inProgram"].asBool());
  TEST_ASSERT_FALSE(state.v["console"]["uploaded"].asBool());
}

void test_the_console_of_an_older_firmware_stays_unused() {
  webBundleReplace(kProgramConsole, 2);
  System sys(false, 0, [](System& s) {
    s.file("/web/index.html.gz", "<old console>");
    s.file("/web/lmp.aaaaaaaa.js.gz", "old script");
    s.file("/cfg/m/zappy2.json", "{\"v\":1}");
    s.file("/kv/sys.bin", "settings");
  });
  // The program serves its own console...
  TEST_ASSERT_EQUAL_STRING("<program>", sys.http().get("/").body.c_str());
  TEST_ASSERT_EQUAL_STRING("script", sys.http().get("/lmp.0123abcd.js").body.c_str());
  // ...and the old one stays where it is: a board that goes back to the firmware of before,
  // by itself or with an update, finds it again.
  TEST_ASSERT_TRUE(sys.tb.native().fs.exists("/web/index.html.gz"));
  TEST_ASSERT_TRUE(sys.tb.native().fs.exists("/web/lmp.aaaaaaaa.js.gz"));
  TEST_ASSERT_TRUE(sys.tb.native().fs.exists("/cfg/m/zappy2.json"));
  TEST_ASSERT_TRUE(sys.tb.native().fs.exists("/kv/sys.bin"));
  Body state(sys.http().get("/api/state"));
  TEST_ASSERT_FALSE(state.v["console"]["uploaded"].asBool());
}

void test_files_uploaded_by_hand_come_first() {
  webBundleReplace(kProgramConsole, 2);
  // Uploaded before a restart: they stay, and they are served.
  System sys(false, 0, [](System& s) {
    s.file("/web/index.html.gz", "<my console>");
    s.file(kWebOverrideMark, "");
  });
  TEST_ASSERT_EQUAL_STRING("<my console>", sys.http().get("/").body.c_str());
  // What was not uploaded comes from the program.
  TEST_ASSERT_EQUAL_STRING("script", sys.http().get("/lmp.0123abcd.js").body.c_str());
  Body state(sys.http().get("/api/state"));
  TEST_ASSERT_TRUE(state.v["console"]["uploaded"].asBool());

  // Back to the console of the program.
  CmdContext ctx;
  ctx.source = CmdSource::Local;
  TEST_ASSERT_TRUE(sys.kernel().commands().execute("sys.web", "useProgramConsole", JsonValue(),
                                                   ctx) == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("<program>", sys.http().get("/").body.c_str());
  TEST_ASSERT_FALSE(sys.tb.native().fs.exists(kWebOverrideMark));
}

void test_an_upload_marks_the_files_as_wanted() {
  webBundleReplace(kProgramConsole, 2);
  System sys;
  const HttpResult r = sys.http().post("/api/upload/file?name=index.html.gz", "<uploaded>");
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  TEST_ASSERT_TRUE(sys.tb.native().fs.exists(kWebOverrideMark));
  TEST_ASSERT_EQUAL_STRING("<uploaded>", sys.http().get("/").body.c_str());
}

void test_captive_portal() {
  System sys;
  sys.file("/web/index.html", "<html>console</html>");
  HttpCall call;
  call.target = "/generate_204";
  call.headers.emplace_back("Host", "connectivitycheck.gstatic.com");
  HttpResult r = sys.http().request(call);
  TEST_ASSERT_EQUAL_UINT16(302, r.status);
  TEST_ASSERT_EQUAL_STRING("http://192.168.4.1/", r.location.c_str());

  // The loopback names: the simulator opened by the browser of the same PC.
  const char* ours[] = {"192.168.4.1", "192.168.4.1:80", "loopmax-0001.local", "LoopMax-0001",
                        "localhost:8088", "127.0.0.1"};
  for (const char* host : ours) {
    HttpCall own;
    own.target = "/";
    own.headers.emplace_back("Host", host);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(200, sys.http().request(own).status, host);
  }

  // The interface is never redirected: a program would not follow.
  HttpCall api;
  api.target = "/api/state";
  api.headers.emplace_back("Host", "example.com");
  TEST_ASSERT_EQUAL_UINT16(200, sys.http().request(api).status);
}

void test_no_captive_portal_on_a_network() {
  System sys(true);
  HttpCall call;
  call.target = "/generate_204";
  call.headers.emplace_back("Host", "connectivitycheck.gstatic.com");
  TEST_ASSERT_EQUAL_UINT16(404, sys.http().request(call).status);
}

// ---------------------------------------------------------------- events

void test_events_reach_who_follows() {
  System sys;
  const int client = sys.http().follow();
  TEST_ASSERT_TRUE(client >= 0);
  sys.advance(100);
  sys.http().taken(client);

  sys.http().post("/api/cmd/sys.config/set", "{\"lang\":\"it\"}");
  sys.advance(200);
  const std::string got = sys.http().taken(client);
  TEST_ASSERT_NOT_NULL(strstr(got.c_str(), "event: event\ndata: {\"id\":\"config\",\"a\":0"));
  // The line of the log that tells the command, with its number.
  TEST_ASSERT_NOT_NULL(strstr(got.c_str(), "event: log\ndata: {\"seq\":"));
  TEST_ASSERT_NOT_NULL(strstr(got.c_str(), "sys.config set from local: ok"));
  TEST_ASSERT_NOT_NULL(strstr(got.c_str(), "id: "));

  // The state of a service, with its name.
  sys.tb.native().net.addNetwork(kHome, kHomePassword);
  sys.http().post("/api/cmd/sys.clock/set", "{\"unix\":1790000000,\"tz\":\"UTC\",\"posix\":\"UTC0\"}");
  sys.advance(300);
  const std::string clock = sys.http().taken(client);
  TEST_ASSERT_NOT_NULL(strstr(clock.c_str(), "\"service\":\"clock\",\"state\":\"Ready\""));
  TEST_ASSERT_NOT_NULL(strstr(clock.c_str(), "\"id\":\"time\""));
}

void test_events_keep_the_connection_alive() {
  System sys;
  // A sign of life every fifteen seconds, counted from the arrival of the first client.
  const int client = sys.http().follow();
  sys.advance(1000);
  sys.http().taken(client);
  sys.advance(13000);
  TEST_ASSERT_NULL(strstr(sys.http().taken(client).c_str(), "event: ping"));
  sys.advance(2000);
  TEST_ASSERT_NOT_NULL(strstr(sys.http().taken(client).c_str(), "event: ping\ndata: {}"));
  sys.advance(12000);
  TEST_ASSERT_NULL(strstr(sys.http().taken(client).c_str(), "event: ping"));
  sys.advance(3000);
  TEST_ASSERT_NOT_NULL(strstr(sys.http().taken(client).c_str(), "event: ping"));
}

void test_who_arrives_later_does_not_get_the_past() {
  System sys;
  for (int i = 0; i < 20; ++i) sys.kernel().log().write(LogLevel::Info, kLogSys, "old-line %d", i);
  sys.advance(200);
  const int client = sys.http().follow();
  sys.advance(500);
  TEST_ASSERT_NULL(strstr(sys.http().taken(client).c_str(), "old-line"));
  sys.kernel().log().write(LogLevel::Info, kLogSys, "new-line");
  sys.advance(200);
  TEST_ASSERT_NOT_NULL(strstr(sys.http().taken(client).c_str(), "new-line"));
}

void test_a_burst_of_log_lines_arrives_in_order() {
  System sys;
  const int client = sys.http().follow();
  sys.advance(100);
  sys.http().taken(client);
  for (int i = 0; i < 30; ++i) sys.kernel().log().write(LogLevel::Info, kLogSys, "line %02d", i);
  sys.advance(1000);
  const std::string got = sys.http().taken(client);
  size_t at = 0;
  for (int i = 0; i < 30; ++i) {
    char line[16];
    snprintf(line, sizeof(line), "line %02d", i);
    const size_t found = got.find(line, at);
    TEST_ASSERT_TRUE_MESSAGE(found != std::string::npos, line);
    at = found;
  }
}

void test_followers_are_limited() {
  System sys;
  for (int i = 0; i < LMP_WEB_EVENT_CLIENTS; ++i) TEST_ASSERT_TRUE(sys.http().follow() >= 0);
  TEST_ASSERT_EQUAL_INT(-1, sys.http().follow());
  Body doc(sys.http().get("/api/doc/sys.web"));
  TEST_ASSERT_EQUAL_INT64(LMP_WEB_EVENT_CLIENTS, doc.v["events"]["clients"].asInt());
}

// ---------------------------------------------------------------- the service

void test_state_of_the_service() {
  System sys(true);
  const std::string token = sys.login();
  sys.http().get("/api/report");  // refused
  Body doc(sys.http().get("/api/doc/sys.web", token.c_str()));
  TEST_ASSERT_TRUE(doc.v["server"].stringEquals("native"));
  TEST_ASSERT_EQUAL_INT64(LMP_HTTP_PORT, doc.v["port"].asInt());
  TEST_ASSERT_TRUE(doc.v["running"].asBool());
  TEST_ASSERT_TRUE(doc.v["auth"]["required"].asBool());
  TEST_ASSERT_FALSE(doc.v["auth"]["ownPassword"].asBool(true));
  TEST_ASSERT_EQUAL_INT64(1, doc.v["auth"]["sessions"].asInt());
  TEST_ASSERT_EQUAL_INT64(1, doc.v["refused"].asInt());
  TEST_ASSERT_EQUAL_INT64(3, doc.v["requests"].asInt());
  TEST_ASSERT_EQUAL_INT64(LMP_HTTP_RESP_MAX, doc.v["answerMax"].asInt());
  TEST_ASSERT_TRUE(doc.v["urls"].at(0).stringEquals("http://loopmax-0001.local/"));
  TEST_ASSERT_TRUE(doc.v["urls"].at(1).stringEquals("http://192.168.1.50/"));
}

void test_the_server_stops_with_the_service() {
  System sys;
  TEST_ASSERT_EQUAL_UINT16(200, sys.http().get("/api/state").status);
  sys.kernel().shutdown();
  TEST_ASSERT_FALSE(sys.http().running());
  TEST_ASSERT_EQUAL_UINT16(0, sys.http().get("/api/state").status);
}

void test_the_web_follows_the_network() {
  TestBoard tb;
  tb.native().net.failNext(Status::Io, Status::Ok);
  LoopMax app(tb.board);
  Logger::setInstance(&app.kernel().log());
  TEST_ASSERT_TRUE(app.begin() == Status::Ok);
  run(app.kernel(), tb.clock, 1000);
  // No radio, no server: but the system is up.
  TEST_ASSERT_FALSE(app.web().running());
  TEST_ASSERT_FALSE(tb.native().http.running());
  TEST_ASSERT_TRUE(app.kernel().log().bootDoneMs() > 0);

  tb.native().net.failNext(Status::Ok, Status::Ok);
  run(app.kernel(), tb.clock, 3000);
  TEST_ASSERT_TRUE(app.web().state() == ServiceState::Ready);
  TEST_ASSERT_EQUAL_UINT16(200, tb.native().http.get("/api/state").status);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_secrets_are_compared_whole);
  RUN_TEST(test_form_values);
  RUN_TEST(test_the_console_of_the_program);
  RUN_TEST(test_the_console_of_an_older_firmware_stays_unused);
  RUN_TEST(test_files_uploaded_by_hand_come_first);
  RUN_TEST(test_an_upload_marks_the_files_as_wanted);
  RUN_TEST(test_form_becomes_arguments);
  RUN_TEST(test_content_types_and_fingerprints);
  RUN_TEST(test_sessions);
  RUN_TEST(test_the_oldest_session_gives_its_place);
  RUN_TEST(test_wrong_passwords_are_slowed_down);
  RUN_TEST(test_state_is_for_everybody);
  RUN_TEST(test_access_point_needs_no_login);
  RUN_TEST(test_who_changes_the_network_reads_the_new_address);
  RUN_TEST(test_a_network_being_tried_does_not_open_the_device);
  RUN_TEST(test_lan_needs_a_login);
  RUN_TEST(test_login);
  RUN_TEST(test_who_am_i_and_logout);
  RUN_TEST(test_a_session_ends);
  RUN_TEST(test_too_many_wrong_passwords);
  RUN_TEST(test_fallback_needs_no_login);
  RUN_TEST(test_report);
  RUN_TEST(test_documents_and_manifests);
  RUN_TEST(test_settings_without_secrets);
#if LMP_FEATURE_MODULES
  RUN_TEST(test_modules_from_the_console);
#endif
  RUN_TEST(test_log);
  RUN_TEST(test_a_full_log_fits_in_one_answer);
  RUN_TEST(test_an_answer_that_does_not_fit);
  RUN_TEST(test_commands);
  RUN_TEST(test_commands_as_a_form);
  RUN_TEST(test_commands_that_are_not_well_formed);
  RUN_TEST(test_a_request_that_is_too_long);
  RUN_TEST(test_restart_from_the_console);
  RUN_TEST(test_the_password_is_changed);
  RUN_TEST(test_every_session_is_closed_by_command);
  RUN_TEST(test_without_the_console_there_is_a_page);
  RUN_TEST(test_files_of_the_console);
  RUN_TEST(test_files_need_no_login);
  RUN_TEST(test_captive_portal);
  RUN_TEST(test_no_captive_portal_on_a_network);
  RUN_TEST(test_events_reach_who_follows);
  RUN_TEST(test_events_keep_the_connection_alive);
  RUN_TEST(test_who_arrives_later_does_not_get_the_past);
  RUN_TEST(test_a_burst_of_log_lines_arrives_in_order);
  RUN_TEST(test_followers_are_limited);
  RUN_TEST(test_state_of_the_service);
  RUN_TEST(test_the_server_stops_with_the_service);
  RUN_TEST(test_the_web_follows_the_network);
  return UNITY_END();
}
