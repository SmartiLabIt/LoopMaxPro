// Tests of the system settings: identity, storage, reset, validation.
#include <string.h>
#include <unity.h>

#include <string>

#include "../support/test_support.h"

using namespace lmp;
using namespace lmp::test;

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

// Key/value store that counts the writes.
class CountingKv : public hal::IKv {
 public:
  explicit CountingKv(hal::IKv& inner) : inner_(inner) {}
  Status begin() override { return inner_.begin(); }
  Status get(const char* ns, const char* key, void* out, size_t cap, size_t& len) override {
    return inner_.get(ns, key, out, cap, len);
  }
  Status set(const char* ns, const char* key, const void* data, size_t len) override {
    ++writes;
    if (failWrites) return Status::Io;
    return inner_.set(ns, key, data, len);
  }
  Status remove(const char* ns, const char* key) override { return inner_.remove(ns, key); }
  Status eraseNamespace(const char* ns) override { return inner_.eraseNamespace(ns); }
  Status commit() override { return inner_.commit(); }

  int writes = 0;
  bool failWrites = false;

 private:
  hal::IKv& inner_;
};

struct Fixture {
  TestBoard tb;
  CountingKv kv{tb.native().kv};

  Fixture() { kv.begin(); }
  Config make() { return Config(&kv, tb.board.rng, tb.board.sys); }
};

}  // namespace

void test_first_boot_creates_the_identity() {
  Fixture f;
  Config c = f.make();
  TEST_ASSERT_FALSE(c.loaded());
  TEST_ASSERT_TRUE(c.load() == Status::Ok);
  TEST_ASSERT_TRUE(c.loaded());
  TEST_ASSERT_TRUE(c.firstBoot());

  TEST_ASSERT_EQUAL_UINT(kDeviceKeyLength, c.sys.key.size());
  for (size_t i = 0; i < c.sys.key.size(); ++i) {
    const char ch = c.sys.key.c_str()[i];
    const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
    TEST_ASSERT_TRUE(ok);
  }
  TEST_ASSERT_EQUAL_STRING("LoopMax-0001", c.net.apSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("loopmax-0001", c.sys.hostname.c_str());
  TEST_ASSERT_EQUAL_STRING("loopmaxap", c.net.apPsw.c_str());
  TEST_ASSERT_EQUAL_STRING("en", c.sys.lang.c_str());
  TEST_ASSERT_TRUE(c.mode() == Mode::Ap);
  TEST_ASSERT_EQUAL_UINT16(kConfigSchema, c.sys.schema);
  TEST_ASSERT_FALSE(c.dirty());  // saved at once
}

void test_keys_are_different_on_every_device() {
  Fixture f;
  Config a = f.make();
  a.load();
  const std::string first = a.sys.key.c_str();
  f.tb.native().kv.wipe();
  Config b = f.make();
  b.load();
  TEST_ASSERT_FALSE(first == b.sys.key.c_str());
}

void test_values_survive_a_restart() {
  Fixture f;
  std::string key;
  {
    Config c = f.make();
    c.load();
    key = c.sys.key.c_str();
    c.sys.lang.assign("it");
    c.sys.deviceName.assign("Office Plugs");
    c.sys.mode = 2;
    c.net.staSsid.assign("Home");
    c.net.staPsw.assign("secret password");
    c.net.captive = false;
    c.web.sessionTtlMin = 60;
    c.clock.tz.assign("Europe/Rome");
    c.cloud.heartbeatMin = 0;
    c.touch(Config::kSys);
    c.touch(Config::kNet);
    c.touch(Config::kWeb);
    c.touch(Config::kClock);
    c.touch(Config::kCloud);
    TEST_ASSERT_TRUE(c.save() == Status::Ok);
  }
  Config c = f.make();
  TEST_ASSERT_TRUE(c.load() == Status::Ok);
  TEST_ASSERT_FALSE(c.firstBoot());
  TEST_ASSERT_EQUAL_STRING(key.c_str(), c.sys.key.c_str());
  TEST_ASSERT_EQUAL_STRING("it", c.sys.lang.c_str());
  TEST_ASSERT_EQUAL_STRING("Office Plugs", c.sys.deviceName.c_str());
  TEST_ASSERT_TRUE(c.mode() == Mode::Lan);
  TEST_ASSERT_EQUAL_STRING("Home", c.net.staSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("secret password", c.net.staPsw.c_str());
  TEST_ASSERT_FALSE(c.net.captive);
  TEST_ASSERT_EQUAL_UINT16(60, c.web.sessionTtlMin);
  TEST_ASSERT_EQUAL_STRING("Europe/Rome", c.clock.tz.c_str());
  TEST_ASSERT_EQUAL_UINT16(0, c.cloud.heartbeatMin);
  TEST_ASSERT_EQUAL_STRING("LoopMax-0001", c.net.apSsid.c_str());
}

void test_only_changed_values_are_written() {
  Fixture f;
  Config c = f.make();
  c.load();
  f.kv.writes = 0;

  c.touch(Config::kSys);
  c.touch(Config::kNet);
  TEST_ASSERT_TRUE(c.save() == Status::Ok);
  TEST_ASSERT_EQUAL_INT(0, f.kv.writes);  // nothing changed: flash is not touched

  c.sys.lang.assign("es");
  c.touch(Config::kSys);
  TEST_ASSERT_TRUE(c.save() == Status::Ok);
  TEST_ASSERT_EQUAL_INT(1, f.kv.writes);
}

void test_save_is_delayed_after_a_change() {
  Fixture f;
  Config c = f.make();
  c.load();
  f.kv.writes = 0;

  c.sys.lang.assign("it");
  c.touch(Config::kSys);
  c.tick(1000);
  c.tick(1000 + LMP_CONFIG_SAVE_DELAY_MS - 1);
  TEST_ASSERT_TRUE(c.dirty());
  TEST_ASSERT_EQUAL_INT(0, f.kv.writes);

  // A new change starts the wait again: a burst of changes gives one write.
  c.sys.deviceName.assign("x");
  c.touch(Config::kSys);
  c.tick(4000);
  c.tick(4000 + LMP_CONFIG_SAVE_DELAY_MS - 1);
  TEST_ASSERT_EQUAL_INT(0, f.kv.writes);
  c.tick(4000 + LMP_CONFIG_SAVE_DELAY_MS);
  TEST_ASSERT_FALSE(c.dirty());
  TEST_ASSERT_EQUAL_INT(2, f.kv.writes);
}

void test_failed_write_is_reported_and_retried() {
  Fixture f;
  Config c = f.make();
  c.load();
  c.sys.lang.assign("it");
  c.touch(Config::kSys);
  f.kv.failWrites = true;
  TEST_ASSERT_TRUE(c.save() == Status::Io);
  TEST_ASSERT_TRUE(c.dirty());
  f.kv.failWrites = false;
  TEST_ASSERT_TRUE(c.save() == Status::Ok);
  TEST_ASSERT_FALSE(c.dirty());
}

void test_factory_reset_keeps_the_key() {
  Fixture f;
  Config c = f.make();
  c.load();
  const std::string key = c.sys.key.c_str();
  c.sys.lang.assign("it");
  c.sys.mode = 2;
  c.net.apSsid.assign("Mine");
  c.net.apPsw.assign("changed123");
  c.net.staSsid.assign("Home");
  c.net.staPsw.assign("secret123");
  c.web.adminPsw.assign("admin12345");
  c.touch(Config::kSys);
  c.touch(Config::kNet);
  c.touch(Config::kWeb);
  c.save();

  TEST_ASSERT_TRUE(c.factoryReset() == Status::Ok);
  TEST_ASSERT_EQUAL_STRING(key.c_str(), c.sys.key.c_str());
  TEST_ASSERT_EQUAL_STRING("en", c.sys.lang.c_str());
  TEST_ASSERT_TRUE(c.mode() == Mode::Ap);
  TEST_ASSERT_EQUAL_STRING("LoopMax-0001", c.net.apSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("loopmaxap", c.net.apPsw.c_str());
  TEST_ASSERT_EQUAL_STRING("", c.net.staSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("", c.net.staPsw.c_str());
  TEST_ASSERT_EQUAL_STRING("", c.web.adminPsw.c_str());

  // The reset must hold after a restart.
  Config again = f.make();
  again.load();
  TEST_ASSERT_FALSE(again.firstBoot());
  TEST_ASSERT_EQUAL_STRING(key.c_str(), again.sys.key.c_str());
  TEST_ASSERT_EQUAL_STRING("", again.net.staSsid.c_str());
  TEST_ASSERT_TRUE(again.mode() == Mode::Ap);
}

void test_missing_values_do_not_discard_the_others() {
  Fixture f;
  std::string key;
  {
    Config c = f.make();
    c.load();
    key = c.sys.key.c_str();
    c.sys.lang.assign("it");
    c.touch(Config::kSys);
    c.save();
  }
  // As if an older firmware had never written these values.
  f.tb.native().kv.remove("sys", "hostname");
  f.tb.native().kv.remove("sys", "schema");
  f.tb.native().kv.remove("net", "rollbackSec");

  Config c = f.make();
  TEST_ASSERT_TRUE(c.load() == Status::Ok);
  TEST_ASSERT_EQUAL_STRING(key.c_str(), c.sys.key.c_str());   // identity is kept
  TEST_ASSERT_EQUAL_STRING("it", c.sys.lang.c_str());
  TEST_ASSERT_EQUAL_STRING("loopmax-0001", c.sys.hostname.c_str());
  TEST_ASSERT_EQUAL_UINT16(180, c.net.rollbackSec);
  TEST_ASSERT_EQUAL_UINT16(kConfigSchema, c.sys.schema);
}

void test_damaged_values_are_ignored() {
  Fixture f;
  {
    Config c = f.make();
    c.load();
  }
  const uint8_t wrongSize[3] = {1, 2, 3};
  f.tb.native().kv.set("net", "rollbackSec", wrongSize, sizeof(wrongSize));
  const char noTerminator[4] = {'a', 'b', 'c', 'd'};
  f.tb.native().kv.set("sys", "lang", noTerminator, sizeof(noTerminator));
  const uint8_t badMode = 9;
  f.tb.native().kv.set("sys", "mode", &badMode, 1);

  Config c = f.make();
  TEST_ASSERT_TRUE(c.load() == Status::Ok);
  TEST_ASSERT_EQUAL_UINT16(180, c.net.rollbackSec);
  TEST_ASSERT_EQUAL_STRING("en", c.sys.lang.c_str());
  TEST_ASSERT_TRUE(c.mode() == Mode::Ap);
}

void test_without_storage_the_settings_live_in_memory() {
  TestBoard tb;
  Config c(nullptr, tb.board.rng, tb.board.sys);
  TEST_ASSERT_TRUE(c.load() == Status::Ok);
  TEST_ASSERT_EQUAL_UINT(kDeviceKeyLength, c.sys.key.size());
  c.sys.lang.assign("it");
  c.touch(Config::kSys);
  TEST_ASSERT_TRUE(c.save() == Status::Ok);
  TEST_ASSERT_TRUE(c.factoryReset() == Status::Ok);
}

void test_describe_never_shows_secrets() {
  Fixture f;
  Config c = f.make();
  c.load();
  c.net.staSsid.assign("Home");
  c.net.staPsw.assign("very-secret");
  c.web.adminPsw.assign("admin-secret");
  const std::string key = c.sys.key.c_str();

  for (uint8_t i = 0; i < Config::kSectionCount; ++i) {
    Json<1024> out;
    out.w.beginObject();
    c.describe(out.w, static_cast<Config::Section>(i));
    out.w.endObject();
    TEST_ASSERT_TRUE(out.w.complete());
    TEST_ASSERT_NULL(strstr(out.text(), "very-secret"));
    TEST_ASSERT_NULL(strstr(out.text(), "admin-secret"));
    TEST_ASSERT_NULL(strstr(out.text(), "loopmaxap"));
    TEST_ASSERT_NULL(strstr(out.text(), key.c_str()));
  }

  Json<1024> net;
  net.w.beginObject();
  c.describe(net.w, Config::kNet);
  net.w.endObject();
  const JsonValue v = net.parse();
  TEST_ASSERT_TRUE(v["staSsid"].stringEquals("Home"));
  TEST_ASSERT_TRUE(v["staPswSet"].asBool());
  TEST_ASSERT_TRUE(v["apPswSet"].asBool());
  TEST_ASSERT_FALSE(v["prevPswSet"].asBool(true));
  TEST_ASSERT_FALSE(v["staPsw"].valid());
}

void test_apply_validates() {
  Fixture f;
  Config c = f.make();
  c.load();
  FieldError err;

  TEST_ASSERT_TRUE(c.apply(Config::kNet, JsonValue::parse(
      "{\"apSsid\":\"Workshop\",\"apPsw\":\"new1234567\",\"captive\":false}"), &err) == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("Workshop", c.net.apSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("new1234567", c.net.apPsw.c_str());
  TEST_ASSERT_TRUE(c.dirty());

  // Password too short: nothing of the request is applied.
  TEST_ASSERT_TRUE(c.apply(Config::kNet, JsonValue::parse(
      "{\"apSsid\":\"Other\",\"apPsw\":\"short\"}"), &err) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("apPsw", err.key);
  TEST_ASSERT_EQUAL_STRING("Workshop", c.net.apSsid.c_str());

  TEST_ASSERT_TRUE(c.apply(Config::kSys, JsonValue::parse("{\"mode\":3}"), &err) == Status::Invalid);
  TEST_ASSERT_TRUE(c.apply(Config::kSys, JsonValue::parse("{\"lang\":\"ita\"}"), &err) ==
                   Status::Invalid);
  TEST_ASSERT_TRUE(c.apply(Config::kNet, JsonValue::parse("{\"rollbackSec\":5}"), &err) ==
                   Status::Invalid);

  // Read only values cannot be changed from outside.
  const std::string key = c.sys.key.c_str();
  TEST_ASSERT_TRUE(c.apply(Config::kSys, JsonValue::parse("{\"key\":\"hacked\",\"schema\":9}"),
                           &err) == Status::Ok);
  TEST_ASSERT_EQUAL_STRING(key.c_str(), c.sys.key.c_str());
  TEST_ASSERT_EQUAL_UINT16(kConfigSchema, c.sys.schema);
}

void test_sections_and_key_names() {
  Fixture f;
  Config c = f.make();
  Config::Section s = Config::kSys;
  TEST_ASSERT_TRUE(Config::findSection("net", s));
  TEST_ASSERT_TRUE(s == Config::kNet);
  TEST_ASSERT_TRUE(Config::findSection("cloud", s));
  TEST_ASSERT_FALSE(Config::findSection("nope", s));
  TEST_ASSERT_FALSE(Config::findSection(nullptr, s));

  // The storage of the ESP32 accepts names up to 15 characters.
  for (uint8_t i = 0; i < Config::kSectionCount; ++i) {
    const Config::Section section = static_cast<Config::Section>(i);
    TEST_ASSERT_TRUE(strlen(Config::sectionName(section)) <= 15);
    for (const FieldDesc& field : c.fields(section)) {
      TEST_ASSERT_TRUE_MESSAGE(strlen(field.key) <= 15, field.key);
    }
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_boot_creates_the_identity);
  RUN_TEST(test_keys_are_different_on_every_device);
  RUN_TEST(test_values_survive_a_restart);
  RUN_TEST(test_only_changed_values_are_written);
  RUN_TEST(test_save_is_delayed_after_a_change);
  RUN_TEST(test_failed_write_is_reported_and_retried);
  RUN_TEST(test_factory_reset_keeps_the_key);
  RUN_TEST(test_missing_values_do_not_discard_the_others);
  RUN_TEST(test_damaged_values_are_ignored);
  RUN_TEST(test_without_storage_the_settings_live_in_memory);
  RUN_TEST(test_describe_never_shows_secrets);
  RUN_TEST(test_apply_validates);
  RUN_TEST(test_sections_and_key_names);
  return UNITY_END();
}
