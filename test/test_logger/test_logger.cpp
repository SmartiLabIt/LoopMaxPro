// Tests of the logger and of the serial sink.
#include <string.h>
#include <unity.h>

#include <string>

#include "hal/native/native_hal.h"
#include "kernel/log/logger.h"
#include "kernel/log/serial_sink.h"

using namespace lmp;

namespace {

// Clock driven by the test.
class FakeClock : public hal::IClock {
 public:
  uint32_t now = 0;
  uint32_t millis() const override { return now; }
  uint64_t uptimeMs() const override { return now; }
  uint64_t micros() const override { return static_cast<uint64_t>(now) * 1000; }
  int64_t unixTime() const override { return 0; }
  Status setUnixTime(int64_t) override { return Status::Ok; }
  Status setTimezone(const char*) override { return Status::Ok; }
  bool localTime(int64_t, hal::DateTime&) const override { return false; }
};

class CollectSink : public ILogSink {
 public:
  int count = 0;
  LogEntry last;
  std::string lastSource;
  void onLog(const LogEntry& entry, const char* sourceName) override {
    ++count;
    last = entry;
    lastSource = sourceName;
  }
};

FakeClock g_clock;

}  // namespace

void setUp() { g_clock.now = 0; }
void tearDown() { Logger::setInstance(nullptr); }

void test_write_and_read() {
  Logger log(g_clock, nullptr);
  g_clock.now = 120;
  log.write(LogLevel::Info, kLogSys, "hello %d", 1);
  g_clock.now = 250;
  log.write(LogLevel::Warn, kLogSys, "second");

  TEST_ASSERT_EQUAL_UINT(2, log.count());
  TEST_ASSERT_EQUAL_UINT32(2, log.lastSeq());

  LogEntry out[4];
  TEST_ASSERT_EQUAL_UINT(2, log.read(0, out, 4));
  TEST_ASSERT_EQUAL_UINT32(1, out[0].seq);
  TEST_ASSERT_EQUAL_UINT32(120, out[0].ms);
  TEST_ASSERT_EQUAL_STRING("hello 1", out[0].msg);
  TEST_ASSERT_TRUE(out[0].level == LogLevel::Info);
  TEST_ASSERT_EQUAL_STRING("second", out[1].msg);
  TEST_ASSERT_EQUAL_UINT32(250, out[1].ms);

  // Incremental read: only what came after the given sequence number.
  TEST_ASSERT_EQUAL_UINT(1, log.read(1, out, 4));
  TEST_ASSERT_EQUAL_STRING("second", out[0].msg);
  TEST_ASSERT_EQUAL_UINT(0, log.read(2, out, 4));
  TEST_ASSERT_EQUAL_UINT(1, log.read(0, out, 1));
}

void test_level_filter() {
  Logger log(g_clock, nullptr);
  log.setLevel(LogLevel::Warn);
  log.write(LogLevel::Info, kLogSys, "dropped");
  log.write(LogLevel::Debug, kLogSys, "dropped");
  log.write(LogLevel::Warn, kLogSys, "kept");
  log.write(LogLevel::Error, kLogSys, "kept");
  TEST_ASSERT_EQUAL_UINT(2, log.count());

  log.setLevel(LogLevel::Off);
  log.write(LogLevel::Error, kLogSys, "dropped");
  TEST_ASSERT_EQUAL_UINT(2, log.count());
}

void test_level_by_source() {
  Logger log(g_clock, nullptr);
  log.setLevel(LogLevel::Info);
  const LogSource net = log.addSource("net", "N");
  const LogSource web = log.addSource("web", "W");
  TEST_ASSERT_NOT_EQUAL(net, web);
  TEST_ASSERT_EQUAL_UINT8(net, log.addSource("net"));  // same name, same id
  TEST_ASSERT_EQUAL_STRING("net", log.sourceName(net));
  TEST_ASSERT_EQUAL_STRING("N", log.sourceIcon(net));
  TEST_ASSERT_EQUAL_UINT(3, log.sourceCount());

  log.setLevel(net, LogLevel::Debug);
  log.write(LogLevel::Debug, net, "kept");
  log.write(LogLevel::Debug, web, "dropped");
  TEST_ASSERT_EQUAL_UINT(1, log.count());
  TEST_ASSERT_TRUE(log.enabled(net, LogLevel::Debug));
  TEST_ASSERT_FALSE(log.enabled(web, LogLevel::Debug));

  LogSource found = 0;
  TEST_ASSERT_TRUE(log.findSource("web", found));
  TEST_ASSERT_EQUAL_UINT8(web, found);
  TEST_ASSERT_FALSE(log.findSource("none", found));
}

void test_source_table_full() {
  Logger log(g_clock, nullptr);
  static char names[LMP_LOG_MAX_SOURCES + 4][8];
  LogSource last = 0;
  for (int i = 0; i < LMP_LOG_MAX_SOURCES + 4; ++i) {
    snprintf(names[i], sizeof(names[i]), "s%d", i);
    last = log.addSource(names[i]);
  }
  TEST_ASSERT_EQUAL_UINT(LMP_LOG_MAX_SOURCES, log.sourceCount());
  TEST_ASSERT_EQUAL_UINT8(kLogSys, last);  // no room: falls back to the system source
}

void test_ring_overwrites_and_counts() {
  Logger log(g_clock, nullptr);
  const int total = LMP_LOG_RING + 10;
  for (int i = 1; i <= total; ++i) log.write(LogLevel::Info, kLogSys, "n%d", i);

  TEST_ASSERT_EQUAL_UINT(LMP_LOG_RING, log.count());
  TEST_ASSERT_EQUAL_UINT32(10, log.dropped());
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(total), log.lastSeq());

  static LogEntry out[LMP_LOG_RING];
  TEST_ASSERT_EQUAL_UINT(LMP_LOG_RING, log.read(0, out, LMP_LOG_RING));
  TEST_ASSERT_EQUAL_UINT32(11, out[0].seq);
  TEST_ASSERT_EQUAL_STRING("n11", out[0].msg);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(total), out[LMP_LOG_RING - 1].seq);
}

void test_clear_keeps_sequence_growing() {
  Logger log(g_clock, nullptr);
  log.write(LogLevel::Info, kLogSys, "a");
  log.write(LogLevel::Info, kLogSys, "b");
  log.clear();
  TEST_ASSERT_EQUAL_UINT(0, log.count());
  log.write(LogLevel::Info, kLogSys, "c");

  LogEntry out[2];
  TEST_ASSERT_EQUAL_UINT(1, log.read(0, out, 2));
  TEST_ASSERT_EQUAL_UINT32(3, out[0].seq);
}

void test_long_message_is_truncated() {
  Logger log(g_clock, nullptr);
  std::string text(LMP_LOG_MSG_LEN * 2, 'x');
  log.write(LogLevel::Info, kLogSys, "%s", text.c_str());

  LogEntry out[1];
  TEST_ASSERT_EQUAL_UINT(1, log.read(0, out, 1));
  TEST_ASSERT_TRUE(out[0].truncated);
  TEST_ASSERT_EQUAL_UINT(LMP_LOG_MSG_LEN - 1, strlen(out[0].msg));

  log.write(LogLevel::Info, kLogSys, "short");
  TEST_ASSERT_EQUAL_UINT(1, log.read(1, out, 1));
  TEST_ASSERT_FALSE(out[0].truncated);
}

void test_truncation_does_not_split_utf8() {
  Logger log(g_clock, nullptr);
  // Fill so that a two byte character falls across the end of the buffer.
  std::string text(LMP_LOG_MSG_LEN - 2, 'a');
  text += "\xC3\xA8\xC3\xA8";
  log.write(LogLevel::Info, kLogSys, "%s", text.c_str());

  LogEntry out[1];
  log.read(0, out, 1);
  const size_t len = strlen(out[0].msg);
  TEST_ASSERT_TRUE(len > 0);
  const unsigned char lastByte = static_cast<unsigned char>(out[0].msg[len - 1]);
  TEST_ASSERT_TRUE(lastByte < 0x80);  // ends with a whole ASCII character
}

void test_sinks() {
  Logger log(g_clock, nullptr);
  const LogSource net = log.addSource("net");
  CollectSink a;
  CollectSink b;
  TEST_ASSERT_TRUE(log.addSink(&a));
  TEST_ASSERT_TRUE(log.addSink(&b));
  TEST_ASSERT_TRUE(log.addSink(&a));  // already there
  TEST_ASSERT_FALSE(log.addSink(nullptr));

  log.write(LogLevel::Warn, net, "link lost");
  TEST_ASSERT_EQUAL_INT(1, a.count);
  TEST_ASSERT_EQUAL_INT(1, b.count);
  TEST_ASSERT_EQUAL_STRING("link lost", a.last.msg);
  TEST_ASSERT_EQUAL_STRING("net", a.lastSource.c_str());

  log.removeSink(&a);
  log.write(LogLevel::Warn, net, "again");
  TEST_ASSERT_EQUAL_INT(1, a.count);
  TEST_ASSERT_EQUAL_INT(2, b.count);

  // Filtered entries do not reach the sinks.
  log.write(LogLevel::Trace, net, "quiet");
  TEST_ASSERT_EQUAL_INT(2, b.count);
}

void test_sink_table_full() {
  Logger log(g_clock, nullptr);
  CollectSink sinks[LMP_LOG_MAX_SINKS + 1];
  for (int i = 0; i < LMP_LOG_MAX_SINKS; ++i) TEST_ASSERT_TRUE(log.addSink(&sinks[i]));
  TEST_ASSERT_FALSE(log.addSink(&sinks[LMP_LOG_MAX_SINKS]));
}

void test_macros_use_the_instance() {
  Logger log(g_clock, nullptr);
  LMP_LOGI(kLogSys, "no instance yet: ignored");
  TEST_ASSERT_EQUAL_UINT(0, log.count());

  Logger::setInstance(&log);
  LMP_LOGI(kLogSys, "value %d", 5);
  LMP_LOGE(kLogSys, "failure");
  LMP_LOGT(kLogSys, "compiled out at the default level");
  TEST_ASSERT_EQUAL_UINT(2, log.count());

  LogEntry out[2];
  log.read(0, out, 2);
  TEST_ASSERT_EQUAL_STRING("value 5", out[0].msg);
  TEST_ASSERT_TRUE(out[1].level == LogLevel::Error);
}

void test_boot_marker() {
  Logger log(g_clock, nullptr);
  TEST_ASSERT_EQUAL_UINT32(0, log.bootDoneMs());
  g_clock.now = 640;
  log.markBootDone();
  g_clock.now = 900;
  log.markBootDone();  // only the first call counts
  TEST_ASSERT_EQUAL_UINT32(640, log.bootDoneMs());
}

void test_level_names() {
  LogLevel level = LogLevel::Off;
  TEST_ASSERT_TRUE(logLevelParse("debug", level));
  TEST_ASSERT_TRUE(level == LogLevel::Debug);
  TEST_ASSERT_TRUE(logLevelParse("2", level));
  TEST_ASSERT_TRUE(level == LogLevel::Warn);
  TEST_ASSERT_TRUE(logLevelParse("off", level));
  TEST_ASSERT_TRUE(level == LogLevel::Off);
  TEST_ASSERT_FALSE(logLevelParse("loud", level));
  TEST_ASSERT_FALSE(logLevelParse("9", level));
  TEST_ASSERT_FALSE(logLevelParse("", level));
  TEST_ASSERT_FALSE(logLevelParse(nullptr, level));
  TEST_ASSERT_EQUAL_STRING("warn", logLevelName(LogLevel::Warn));
  TEST_ASSERT_EQUAL_CHAR('E', logLevelLetter(LogLevel::Error));
}

void test_serial_sink_format() {
  hal::native::Serial serial;
  serial.setCapture(true);
  serial.begin(115200);

  Logger log(g_clock, nullptr);
  const LogSource net = log.addSource("net");
  SerialLogSink sink(serial);
  log.addSink(&sink);

  g_clock.now = 1234;
  log.write(LogLevel::Warn, net, "Wifi lost");
  TEST_ASSERT_EQUAL_STRING("[    1234] W net      Wifi lost\r\n", serial.takeOutput().c_str());

  sink.setLevel(LogLevel::Error);
  log.write(LogLevel::Warn, net, "hidden on serial");
  TEST_ASSERT_EQUAL_STRING("", serial.takeOutput().c_str());
  TEST_ASSERT_EQUAL_UINT(2, log.count());  // still stored in the ring
}

void test_serial_sink_waits_for_the_port() {
  hal::native::Serial serial;
  serial.setCapture(true);
  Logger log(g_clock, nullptr);
  SerialLogSink sink(serial);
  log.addSink(&sink);
  log.write(LogLevel::Info, kLogSys, "port closed");
  TEST_ASSERT_EQUAL_STRING("", serial.takeOutput().c_str());
}

void test_with_a_real_mutex() {
  hal::IMutex* mutex = hal::native::objects().rtos.createMutex();
  Logger log(g_clock, mutex);
  CollectSink sink;
  log.addSink(&sink);
  log.write(LogLevel::Info, kLogSys, "locked");
  // A sink may log again: the lock is not held while sinks run.
  TEST_ASSERT_EQUAL_INT(1, sink.count);
  TEST_ASSERT_EQUAL_UINT(1, log.count());
  delete mutex;
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_write_and_read);
  RUN_TEST(test_level_filter);
  RUN_TEST(test_level_by_source);
  RUN_TEST(test_source_table_full);
  RUN_TEST(test_ring_overwrites_and_counts);
  RUN_TEST(test_clear_keeps_sequence_growing);
  RUN_TEST(test_long_message_is_truncated);
  RUN_TEST(test_truncation_does_not_split_utf8);
  RUN_TEST(test_sinks);
  RUN_TEST(test_sink_table_full);
  RUN_TEST(test_macros_use_the_instance);
  RUN_TEST(test_boot_marker);
  RUN_TEST(test_level_names);
  RUN_TEST(test_serial_sink_format);
  RUN_TEST(test_serial_sink_waits_for_the_port);
  RUN_TEST(test_with_a_real_mutex);
  return UNITY_END();
}
