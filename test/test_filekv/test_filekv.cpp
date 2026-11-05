// Tests of the key/value store kept in files, used by the boards without one of their own.
#include <string.h>
#include <unity.h>

#include <string>

#include "../support/test_support.h"
#include "app/loopmax.h"
#include "hal/common/file_kv.h"
#include "kernel/config/config.h"

using namespace lmp;
using namespace lmp::test;

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

struct Store {
  TestBoard tb;
  hal::native::Fs& fs = tb.native().fs;
  hal::FileKv kv{fs};

  Store() { TEST_ASSERT_TRUE(kv.begin() == Status::Ok); }

  std::string text(const char* ns, const char* key) {
    char out[128];
    size_t len = 0;
    if (kv.get(ns, key, out, sizeof(out), len) != Status::Ok) return "<none>";
    return std::string(out, len);
  }
  Status put(const char* ns, const char* key, const char* value) {
    return kv.set(ns, key, value, strlen(value));
  }
  void writeRaw(const char* path, const std::string& bytes) {
    hal::IFile* f = fs.open(path, hal::OpenMode::Write);
    TEST_ASSERT_NOT_NULL(f);
    f->write(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    f->close();
  }
};

}  // namespace

void test_begin_mounts_the_filesystem() {
  TestBoard tb;
  TEST_ASSERT_FALSE(tb.native().fs.mounted());
  hal::FileKv kv(tb.native().fs);

  char out[8];
  size_t len = 0;
  TEST_ASSERT_TRUE(kv.get("sys", "lang", out, sizeof(out), len) == Status::Io);
  TEST_ASSERT_TRUE(kv.set("sys", "lang", "it", 2) == Status::Io);

  TEST_ASSERT_TRUE(kv.begin() == Status::Ok);
  TEST_ASSERT_TRUE(tb.native().fs.mounted());
  TEST_ASSERT_TRUE(tb.native().fs.exists("/kv"));
}

void test_values_come_back() {
  Store s;
  TEST_ASSERT_EQUAL_STRING("<none>", s.text("sys", "lang").c_str());
  TEST_ASSERT_TRUE(s.put("sys", "lang", "it") == Status::Ok);
  TEST_ASSERT_TRUE(s.put("sys", "hostname", "office-plugs") == Status::Ok);
  const uint16_t number = 0xBEEF;
  TEST_ASSERT_TRUE(s.kv.set("sys", "schema", &number, sizeof(number)) == Status::Ok);

  TEST_ASSERT_EQUAL_STRING("it", s.text("sys", "lang").c_str());
  TEST_ASSERT_EQUAL_STRING("office-plugs", s.text("sys", "hostname").c_str());
  uint16_t back = 0;
  size_t len = 0;
  TEST_ASSERT_TRUE(s.kv.get("sys", "schema", &back, sizeof(back), len) == Status::Ok);
  TEST_ASSERT_EQUAL_UINT(2, len);
  TEST_ASSERT_EQUAL_HEX16(0xBEEF, back);

  // A value that changes size takes the place of the old one.
  TEST_ASSERT_TRUE(s.put("sys", "hostname", "p") == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("p", s.text("sys", "hostname").c_str());
  TEST_ASSERT_EQUAL_STRING("it", s.text("sys", "lang").c_str());
  TEST_ASSERT_EQUAL_HEX16(0xBEEF, back);
}

void test_small_buffer_reports_the_size() {
  Store s;
  s.put("net", "apSsid", "LoopMax-A6F4");
  char small[4];
  size_t len = 0;
  TEST_ASSERT_TRUE(s.kv.get("net", "apSsid", small, sizeof(small), len) == Status::NoMem);
  TEST_ASSERT_EQUAL_UINT(12, len);
}

void test_values_are_written_by_commit() {
  TestBoard tb;
  {
    hal::FileKv kv(tb.native().fs);
    kv.begin();
    kv.set("sys", "lang", "it", 2);
    // Nothing on the filesystem yet.
    TEST_ASSERT_FALSE(tb.native().fs.exists("/kv/sys.bin"));
    TEST_ASSERT_TRUE(kv.commit() == Status::Ok);
    TEST_ASSERT_TRUE(tb.native().fs.exists("/kv/sys.bin"));
    TEST_ASSERT_FALSE(tb.native().fs.exists("/kv/sys.tmp"));
  }
  // As after a power cycle.
  hal::FileKv kv(tb.native().fs);
  kv.begin();
  char out[8];
  size_t len = 0;
  TEST_ASSERT_TRUE(kv.get("sys", "lang", out, sizeof(out), len) == Status::Ok);
  TEST_ASSERT_EQUAL_UINT(2, len);
  TEST_ASSERT_EQUAL_MEMORY("it", out, 2);
}

void test_changes_without_commit_are_lost_at_power_off() {
  TestBoard tb;
  {
    hal::FileKv kv(tb.native().fs);
    kv.begin();
    kv.set("sys", "lang", "it", 2);
    kv.commit();
    kv.set("sys", "lang", "de", 2);
  }
  hal::FileKv kv(tb.native().fs);
  kv.begin();
  char out[8];
  size_t len = 0;
  kv.get("sys", "lang", out, sizeof(out), len);
  TEST_ASSERT_EQUAL_MEMORY("it", out, 2);
}

void test_another_namespace_writes_the_pending_changes() {
  Store s;
  s.put("sys", "lang", "it");
  s.put("net", "apSsid", "LoopMax-A6F4");    // the changes of "sys" are written here
  TEST_ASSERT_TRUE(s.fs.exists("/kv/sys.bin"));
  TEST_ASSERT_FALSE(s.fs.exists("/kv/net.bin"));
  TEST_ASSERT_EQUAL_STRING("it", s.text("sys", "lang").c_str());
  TEST_ASSERT_TRUE(s.fs.exists("/kv/net.bin"));
  TEST_ASSERT_EQUAL_STRING("LoopMax-A6F4", s.text("net", "apSsid").c_str());
}

void test_remove_and_erase() {
  Store s;
  s.put("sys", "lang", "it");
  s.put("sys", "hostname", "plugs");
  s.put("net", "apSsid", "LoopMax");
  s.kv.commit();

  TEST_ASSERT_TRUE(s.kv.remove("sys", "lang") == Status::Ok);
  TEST_ASSERT_TRUE(s.kv.remove("sys", "lang") == Status::NotFound);
  TEST_ASSERT_EQUAL_STRING("<none>", s.text("sys", "lang").c_str());
  TEST_ASSERT_EQUAL_STRING("plugs", s.text("sys", "hostname").c_str());

  TEST_ASSERT_TRUE(s.kv.eraseNamespace("sys") == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("<none>", s.text("sys", "hostname").c_str());
  TEST_ASSERT_FALSE(s.fs.exists("/kv/sys.bin"));
  // The other namespace is untouched, and erasing what is not there is fine.
  TEST_ASSERT_EQUAL_STRING("LoopMax", s.text("net", "apSsid").c_str());
  TEST_ASSERT_TRUE(s.kv.eraseNamespace("cloud") == Status::Ok);
}

void test_names_are_checked() {
  Store s;
  char out[8];
  size_t len = 0;
  TEST_ASSERT_TRUE(s.put("", "lang", "it") == Status::Invalid);
  TEST_ASSERT_TRUE(s.put("sys", "", "it") == Status::Invalid);
  TEST_ASSERT_TRUE(s.put("../etc", "lang", "it") == Status::Invalid);
  TEST_ASSERT_TRUE(s.put("sys", "a/b", "it") == Status::Invalid);
  TEST_ASSERT_TRUE(s.put("sys", "sixteen_letters_", "it") == Status::Invalid);
  TEST_ASSERT_TRUE(s.put("sys", "fifteen_letters", "it") == Status::Ok);
  TEST_ASSERT_TRUE(s.kv.set("sys", "lang", nullptr, 2) == Status::Invalid);
  TEST_ASSERT_TRUE(s.kv.set("sys", "lang", "it", 0) == Status::Invalid);
  TEST_ASSERT_TRUE(s.kv.get("sys", "lang", nullptr, 8, len) == Status::Invalid);
  TEST_ASSERT_TRUE(s.kv.get(nullptr, "lang", out, 8, len) == Status::Invalid);
}

void test_a_full_namespace_refuses_more() {
  Store s;
  const std::string value(100, 'x');
  unsigned stored = 0;
  for (unsigned i = 0; i < 20; ++i) {
    const std::string key = "k" + std::to_string(i);
    const Status st = s.kv.set("big", key.c_str(), value.data(), value.size());
    if (st != Status::Ok) {
      TEST_ASSERT_TRUE(st == Status::NoMem);
      break;
    }
    ++stored;
  }
  // 1024 bytes hold nine records of 100 bytes with their names.
  TEST_ASSERT_EQUAL_UINT(9, stored);
  TEST_ASSERT_TRUE(s.kv.commit() == Status::Ok);
  // What was stored is whole, and a value can still be replaced.
  TEST_ASSERT_EQUAL_STRING(value.c_str(), s.text("big", "k8").c_str());
  TEST_ASSERT_TRUE(s.put("big", "k0", "short") == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("short", s.text("big", "k0").c_str());
}

void test_a_damaged_file_counts_as_empty() {
  Store s;
  s.writeRaw("/kv/sys.bin", "this is not a store");
  TEST_ASSERT_EQUAL_STRING("<none>", s.text("sys", "lang").c_str());

  // A record that says to be longer than the file.
  std::string cut("LKV1", 4);
  cut += '\x04';
  cut += "lang";
  cut += '\x40';
  cut += '\x00';
  cut += "it";
  s.writeRaw("/kv/net.bin", cut);
  TEST_ASSERT_EQUAL_STRING("<none>", s.text("net", "lang").c_str());

  // The device works again as soon as something is stored.
  TEST_ASSERT_TRUE(s.put("net", "lang", "it") == Status::Ok);
  TEST_ASSERT_TRUE(s.kv.commit() == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("it", s.text("net", "lang").c_str());
}

void test_an_interrupted_write_is_recovered() {
  TestBoard tb;
  {
    hal::FileKv kv(tb.native().fs);
    kv.begin();
    kv.set("sys", "lang", "it", 2);
    kv.commit();
  }
  // The power went away after the old file was removed and before the new one took its place.
  TEST_ASSERT_TRUE(tb.native().fs.rename("/kv/sys.bin", "/kv/sys.tmp") == Status::Ok);

  hal::FileKv kv(tb.native().fs);
  kv.begin();
  char out[8];
  size_t len = 0;
  TEST_ASSERT_TRUE(kv.get("sys", "lang", out, sizeof(out), len) == Status::Ok);
  TEST_ASSERT_EQUAL_MEMORY("it", out, 2);
  TEST_ASSERT_TRUE(tb.native().fs.exists("/kv/sys.bin"));
}

namespace {

// The filesystem of the PC, seen as one that keeps the old content until the new is complete.
class WholeFs : public hal::IFs {
 public:
  explicit WholeFs(hal::IFs& real) : real_(real) {}
  bool replacesWhole() const override { return true; }

  Status mount(bool format) override { return real_.mount(format); }
  void unmount() override { real_.unmount(); }
  bool mounted() const override { return real_.mounted(); }
  Status info(hal::FsInfo& out) const override { return real_.info(out); }
  hal::IFile* open(const char* path, hal::OpenMode mode) override {
    if (mode != hal::OpenMode::Read) ++writes;
    return real_.open(path, mode);
  }
  bool exists(const char* path) const override { return real_.exists(path); }
  Status remove(const char* path) override { return real_.remove(path); }
  Status rename(const char* from, const char* to) override {
    ++renames;
    return real_.rename(from, to);
  }
  Status mkdir(const char* path) override { return real_.mkdir(path); }
  Status list(const char* dir, hal::DirVisitor visitor) override {
    return real_.list(dir, visitor);
  }

  unsigned writes = 0;
  unsigned renames = 0;

 private:
  hal::IFs& real_;
};

}  // namespace

void test_one_write_where_the_filesystem_allows() {
  TestBoard tb;
  WholeFs fs(tb.native().fs);
  {
    hal::FileKv kv(fs);
    TEST_ASSERT_TRUE(kv.begin() == Status::Ok);
    kv.set("sys", "lang", "it", 2);
    kv.set("sys", "hostname", "plugs", 5);
    TEST_ASSERT_TRUE(kv.commit() == Status::Ok);
    TEST_ASSERT_EQUAL_UINT(1, fs.writes);
    TEST_ASSERT_EQUAL_UINT(0, fs.renames);
    TEST_ASSERT_TRUE(fs.exists("/kv/sys.bin"));
    TEST_ASSERT_FALSE(fs.exists("/kv/sys.tmp"));

    // Nothing pending, nothing written.
    TEST_ASSERT_TRUE(kv.commit() == Status::Ok);
    TEST_ASSERT_EQUAL_UINT(1, fs.writes);

    kv.set("sys", "lang", "de", 2);
    TEST_ASSERT_TRUE(kv.commit() == Status::Ok);
    TEST_ASSERT_EQUAL_UINT(2, fs.writes);
  }
  hal::FileKv kv(fs);
  kv.begin();
  char out[8];
  size_t len = 0;
  TEST_ASSERT_TRUE(kv.get("sys", "lang", out, sizeof(out), len) == Status::Ok);
  TEST_ASSERT_EQUAL_MEMORY("de", out, 2);
  TEST_ASSERT_TRUE(kv.get("sys", "hostname", out, sizeof(out), len) == Status::Ok);
  TEST_ASSERT_EQUAL_MEMORY("plugs", out, 5);
}

void test_the_system_settings_live_on_it() {
  TestBoard tb;
  hal::native::Objects& o = tb.native();
  std::string key;
  {
    hal::FileKv kv(o.fs);
    kv.begin();
    Config c(&kv, &o.rng, &o.sys);
    TEST_ASSERT_TRUE(c.load() == Status::Ok);
    TEST_ASSERT_TRUE(c.firstBoot());
    key = c.sys.key.c_str();
    TEST_ASSERT_EQUAL_UINT(kDeviceKeyLength, key.size());

    c.sys.lang.assign("it");
    c.sys.deviceName.assign("Office Plugs");
    c.net.staSsid.assign("home");
    c.net.staPsw.assign("a-password-as-long-as-is-needed");
    c.clock.tz.assign("Europe/Rome");
    c.touch(Config::kSys);
    c.touch(Config::kNet);
    c.touch(Config::kClock);
    TEST_ASSERT_TRUE(c.save() == Status::Ok);
  }
  {
    hal::FileKv kv(o.fs);
    kv.begin();
    Config c(&kv, &o.rng, &o.sys);
    TEST_ASSERT_TRUE(c.load() == Status::Ok);
    TEST_ASSERT_FALSE(c.firstBoot());
    TEST_ASSERT_EQUAL_STRING(key.c_str(), c.sys.key.c_str());
    TEST_ASSERT_EQUAL_STRING("it", c.sys.lang.c_str());
    TEST_ASSERT_EQUAL_STRING("Office Plugs", c.sys.deviceName.c_str());
    TEST_ASSERT_EQUAL_STRING("home", c.net.staSsid.c_str());
    TEST_ASSERT_EQUAL_STRING("a-password-as-long-as-is-needed", c.net.staPsw.c_str());
    TEST_ASSERT_EQUAL_STRING("Europe/Rome", c.clock.tz.c_str());

    TEST_ASSERT_TRUE(c.factoryReset() == Status::Ok);
  }
  hal::FileKv kv(o.fs);
  kv.begin();
  Config c(&kv, &o.rng, &o.sys);
  TEST_ASSERT_TRUE(c.load() == Status::Ok);
  TEST_ASSERT_EQUAL_STRING(key.c_str(), c.sys.key.c_str());
  TEST_ASSERT_EQUAL_STRING("en", c.sys.lang.c_str());
  TEST_ASSERT_EQUAL_STRING("", c.net.staSsid.c_str());
}

void test_it_works_after_the_filesystem_was_unmounted() {
  Store s;
  s.put("sys", "lang", "it");
  s.kv.commit();
  s.fs.unmount();

  // What happens at shutdown: the services are stopped, then the settings are written.
  TEST_ASSERT_EQUAL_STRING("it", s.text("sys", "lang").c_str());
  TEST_ASSERT_TRUE(s.put("sys", "lang", "de") == Status::Ok);
  s.fs.unmount();
  TEST_ASSERT_TRUE(s.kv.commit() == Status::Ok);
  s.fs.unmount();
  TEST_ASSERT_TRUE(s.kv.eraseNamespace("net") == Status::Ok);
  TEST_ASSERT_EQUAL_STRING("de", s.text("sys", "lang").c_str());
}

void test_factory_reset_with_the_whole_system() {
  TestBoard tb;
  hal::Board board = tb.board;
  std::string key;
  {
    hal::FileKv kv(tb.native().fs);
    board.kv = &kv;
    LoopMax app(board);
    Logger::setInstance(&app.kernel().log());
    TEST_ASSERT_TRUE(app.begin() == Status::Ok);
    run(app.kernel(), tb.clock, 200);
    key = app.kernel().config().sys.key.c_str();

    CmdContext ctx;
    TEST_ASSERT_TRUE(app.kernel().commands().execute(
        "sys.config", "set", JsonValue::parse("{\"lang\":\"it\",\"deviceName\":\"Plugs\"}"),
        ctx) == Status::Ok);
    run(app.kernel(), tb.clock, LMP_CONFIG_SAVE_DELAY_MS + 1200);
    TEST_ASSERT_FALSE(app.kernel().config().dirty());

    TEST_ASSERT_TRUE(app.kernel().commands().execute("sys.system", "factoryReset",
                                                     JsonValue::parse("{}"), ctx) == Status::Ok);
    // The system stops its services, the filesystem with them, and then resets the settings.
    run(app.kernel(), tb.clock, 1500);
    TEST_ASSERT_EQUAL_UINT(1, tb.native().sys.restartCount());
    Logger::setInstance(nullptr);
  }

  // After the restart.
  tb.native().fs.unmount();
  hal::FileKv kv(tb.native().fs);
  board.kv = &kv;
  LoopMax app(board);
  Logger::setInstance(&app.kernel().log());
  TEST_ASSERT_TRUE(app.begin() == Status::Ok);
  run(app.kernel(), tb.clock, 200);
  const Config& c = app.kernel().config();
  TEST_ASSERT_FALSE(c.firstBoot());
  TEST_ASSERT_EQUAL_STRING(key.c_str(), c.sys.key.c_str());
  TEST_ASSERT_EQUAL_STRING("en", c.sys.lang.c_str());
  TEST_ASSERT_EQUAL_STRING("", c.sys.deviceName.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_begin_mounts_the_filesystem);
  RUN_TEST(test_values_come_back);
  RUN_TEST(test_small_buffer_reports_the_size);
  RUN_TEST(test_values_are_written_by_commit);
  RUN_TEST(test_changes_without_commit_are_lost_at_power_off);
  RUN_TEST(test_another_namespace_writes_the_pending_changes);
  RUN_TEST(test_remove_and_erase);
  RUN_TEST(test_names_are_checked);
  RUN_TEST(test_a_full_namespace_refuses_more);
  RUN_TEST(test_a_damaged_file_counts_as_empty);
  RUN_TEST(test_an_interrupted_write_is_recovered);
  RUN_TEST(test_one_write_where_the_filesystem_allows);
  RUN_TEST(test_the_system_settings_live_on_it);
  RUN_TEST(test_it_works_after_the_filesystem_was_unmounted);
  RUN_TEST(test_factory_reset_with_the_whole_system);
  return UNITY_END();
}
