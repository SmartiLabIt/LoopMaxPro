// Shared helpers for the tests that run on the PC.
#pragma once
#include <string>

#include "drivers/lora/fake_radio.h"
#include "hal/native/native_hal.h"
#include "kernel/kernel.h"

namespace lmp::test {

// Clock driven by the test.
class FakeClock : public hal::IClock {
 public:
  uint32_t now = 0;
  int64_t unix = 0;

  uint32_t millis() const override { return now; }
  uint64_t uptimeMs() const override { return now; }
  uint64_t micros() const override { return static_cast<uint64_t>(now) * 1000; }
  int64_t unixTime() const override { return unix; }
  Status setUnixTime(int64_t seconds) override {
    unix = seconds;
    return Status::Ok;
  }
  Status setTimezone(const char*) override { return Status::Ok; }
  bool localTime(int64_t seconds, hal::DateTime& out) const override {
    return hal::native::objects().clock.localTime(seconds, out);
  }
};

// Board of the PC with a clock the test can move and a serial port it can read.
struct TestBoard {
  FakeClock clock;
  hal::Board board;

  TestBoard() {
    hal::native::Objects& o = hal::native::objects();
    o.fs.unmount();
    o.fs.wipe();
    o.kv.wipe();
    o.sys.setExitOnRestart(false);
    o.sys.chipNoteReset();
    o.sys.resetLastWords();
    o.serial.setCapture(true);
    o.serial.begin(115200);
    o.serial.takeOutput();
    o.net.reset();
    o.http.reset();
    o.ntp.reset();
    o.httpClient.reset();
    o.mqtt.reset();
    o.cloudMqtt.reset();
    o.ota.reset();
    o.probe.reset();
    o.pwm.reset();
    o.pixels.reset();
    o.spi.reset();
    for (hal::native::I2c& bus : o.i2c) bus.reset();
#if LMP_FEATURE_LORA
    fakeLoraRadio().reset();
#endif
    // The long operations run inside the round of the system: every step of a test happens
    // when the test lets the time pass.
    Worker::setInlineDefault(true);
    board = hal::board();
    board.clock = &clock;
    // The answers of the time server set the clock the test moves.
    o.ntp.attach(&clock);
  }

  hal::native::Objects& native() { return hal::native::objects(); }
  std::string output() { return hal::native::objects().serial.takeOutput(); }
};

// Runs the kernel for a span of simulated time.
inline void run(Kernel& kernel, FakeClock& clock, uint32_t ms, uint32_t step = 10) {
  for (uint32_t elapsed = 0; elapsed < ms; elapsed += step) {
    clock.now += step;
    kernel.tick();
  }
}

// Text of a writer backed by a fixed buffer.
template <size_t N>
struct Json {
  FixedSink<N> sink;
  JsonWriter w{sink};
  const char* text() const { return sink.c_str(); }
  JsonValue parse() const { return JsonValue::parse(sink.view()); }
};

}  // namespace lmp::test
