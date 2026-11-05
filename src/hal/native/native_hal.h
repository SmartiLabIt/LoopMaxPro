// LoopMaxPro HAL - implementation for the PC.
// Used by the unit tests and to run the system without a board.
// This folder may use the standard library freely: it never runs on a microcontroller.
#pragma once
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

#include "hal/api/board.h"
#include "hal/native/native_http.h"
#include "hal/native/native_link.h"
#include "hal/native/native_net.h"
#include "hal/native/native_online.h"
#include "hal/native/native_probe.h"
#include "hal/native/native_update.h"

namespace lmp::hal::native {

class System : public ISystem {
 public:
  System();
  const SystemInfo& info() const override { return info_; }
  uint32_t heapFree() const override { return 6u * 1024 * 1024; }
  uint32_t heapMin() const override { return 5u * 1024 * 1024; }
  uint32_t heapTotal() const override { return 8u * 1024 * 1024; }
  uint32_t heapLargest() const override { return largest_ ? largest_ : heapFree(); }
  ResetReason resetReason() const override { return reason_; }
  // Test hook: why the board of the test started.
  void setResetReason(ResetReason reason) { reason_ = reason; }
  void mac(Mac& out) const override;
  // The eFuse of the PC: it stays when the tests wipe the flash, as on a chip.
  bool chipNoteRead(uint8_t* out, size_t len) const override;
  Status chipNoteWrite(const uint8_t* data, size_t len) override;
  // Test hook: a new chip.
  void chipNoteReset() {
    memset(note_, 0, sizeof(note_));
    noteWrites_ = 0;
  }
  unsigned chipNoteWrites() const { return noteWrites_; }
  void watchdogFeed() override {}
  void restart() override;

  // Test hook: counts the restarts instead of leaving the process.
  void setExitOnRestart(bool enabled) { exitOnRestart_ = enabled; }
  unsigned restartCount() const { return restarts_; }
  // Test hook: the largest free block, for memory cut in pieces. 0: as large as what is free.
  void setHeapLargest(uint32_t bytes) { largest_ = bytes; }
  uint8_t note_[kChipNoteMax] = {};
  unsigned noteWrites_ = 0;

  // The last words, as a board keeps them: the lines and the reason of this start, and what a
  // test says the previous start left.
  void lastWordsKeep(const char* line) override;
  void lastWordsWhy(const char* why) override { why_ = why ? why : ""; }
  const LastWords* lastWordsPrevious() const override { return hasPrevious_ ? &previous_ : nullptr; }
  void stallGuard(uint32_t seconds) override { stallS_ = seconds; }
  // Test hooks.
  const std::vector<std::string>& lastWordsLines() const { return lines_; }
  const std::string& lastWordsReason() const { return why_; }
  void setPrevious(const LastWords* words) {
    hasPrevious_ = words != nullptr;
    if (words) previous_ = *words;
  }
  uint32_t stallGuardSeconds() const { return stallS_; }
  const char* layout() const override { return layout_.c_str(); }
  const char* base() const override { return base_.c_str(); }
  // Test hook: what a board with a partition table would say.
  void setLayout(const char* layout, const char* base) {
    layout_ = layout ? layout : "";
    base_ = base ? base : "";
  }
  void resetLastWords() {
    lines_.clear();
    why_.clear();
    hasPrevious_ = false;
    reason_ = ResetReason::PowerOn;
    stallS_ = 0;
    layout_.clear();
    base_.clear();
  }

 private:
  SystemInfo info_;
  std::vector<std::string> lines_;
  std::string why_;
  LastWords previous_;
  bool hasPrevious_ = false;
  ResetReason reason_ = ResetReason::PowerOn;
  uint32_t stallS_ = 0;
  std::string layout_;
  std::string base_;
  bool exitOnRestart_ = true;
  unsigned restarts_ = 0;
  uint32_t largest_ = 0;
};

class Clock : public IClock {
 public:
  Clock();
  uint32_t millis() const override { return static_cast<uint32_t>(uptimeMs()); }
  uint64_t uptimeMs() const override;
  uint64_t micros() const override;
  int64_t unixTime() const override;
  Status setUnixTime(int64_t seconds) override;
  Status setTimezone(const char* posixTz) override;
  bool localTime(int64_t unixSeconds, DateTime& out) const override;

 private:
  int64_t startNs_;
  int64_t unixOffset_ = 0;   // seconds added to the uptime to get the wall clock
  bool unixSet_ = false;
};

class Serial : public ISerial {
 public:
  Status begin(uint32_t baud) override;
  bool ready() const override { return ready_; }
  size_t write(const uint8_t* data, size_t len) override;
  size_t read(uint8_t* out, size_t cap) override;
  size_t available() const override;
  void flush() override;

  // Test hooks: captured output and injected input.
  void setCapture(bool enabled) { capture_ = enabled; }
  std::string takeOutput();
  void feedInput(const char* text);

 private:
  mutable std::mutex mutex_;
  std::string output_;
  std::string input_;
  bool ready_ = false;
  bool capture_ = false;
};

class Rtos : public IRtos {
 public:
  bool preemptive() const override { return true; }
  IMutex* createMutex() override;
  ISemaphore* createSemaphore() override;
  IQueue* createQueue(size_t itemSize, size_t length) override;
  Status createTask(const TaskConfig& cfg, TaskFn fn, void* arg) override;
  uintptr_t currentTaskId() const override;
  void delayMs(uint32_t ms) override;
  void yield() override;
};

class Random : public IRandom {
 public:
  void bytes(uint8_t* out, size_t len) override;
};

class Gpio : public IGpio {
 public:
  static constexpr uint8_t kPins = 64;

  bool isValid(uint8_t pin) const override { return pin < kPins; }
  bool canOutput(uint8_t pin) const override { return pin < kPins; }
  Status setMode(uint8_t pin, PinMode mode) override;
  Status write(uint8_t pin, PinLevel level) override;
  Status read(uint8_t pin, PinLevel& level) const override;
  Status attachInterrupt(uint8_t pin, Edge edge, PinHandler handler) override;
  Status detachInterrupt(uint8_t pin) override;

  // Test hook: drives an input pin and fires its interrupt handler.
  void setInput(uint8_t pin, PinLevel level);
  PinMode mode(uint8_t pin) const { return pin < kPins ? modes_[pin] : PinMode::Input; }

 private:
  PinMode modes_[kPins] = {};
  PinLevel levels_[kPins] = {};
  Edge edges_[kPins] = {};
  PinHandler handlers_[kPins];
};

// PWM of the PC: it keeps what it is told, for the tests and the simulator.
class Pwm : public IPwm {
 public:
  static constexpr uint8_t kPins = 64;

  bool supports(uint8_t pin) const override { return pin < kPins; }
  Status setup(uint8_t pin, uint32_t frequencyHz, uint8_t resolutionBits) override;
  Status write(uint8_t pin, uint32_t duty) override;
  Status release(uint8_t pin) override;

  // Test hooks.
  void reset();
  uint32_t duty(uint8_t pin) const { return pin < kPins ? duty_[pin] : 0; }
  uint8_t bits(uint8_t pin) const { return pin < kPins ? bits_[pin] : 0; }
  uint32_t writes() const { return writes_; }

 private:
  uint32_t duty_[kPins] = {};
  uint8_t bits_[kPins] = {};   // 0: not set up
  uint32_t writes_ = 0;
};

// Addressable LEDs of the PC: one line, the last frame kept for the tests.
class Pixels : public IPixels {
 public:
  bool supports(uint8_t pin) const override { return pin < 64; }
  Status begin(uint8_t pin, uint16_t count) override;
  Status show(uint8_t pin, const uint8_t* grb, size_t len) override;
  Status end(uint8_t pin) override;

  // Test hooks.
  void reset();
  uint8_t pin() const { return pin_; }
  uint16_t count() const { return count_; }
  // Red, green, blue of a pixel of the last frame.
  void rgbOf(uint16_t i, uint8_t& r, uint8_t& g, uint8_t& b) const;
  uint32_t frames() const { return frames_; }
  // What a spike on the wire or a dip of the supply does: the pixels show something else,
  // until somebody sends them a frame again.
  void spoil() { memset(frame_, 0x5A, sizeof(frame_)); }
  // The line does not take the next frames: so many of them.
  void failNext(unsigned frames) { failing_ = frames; }

 private:
  uint8_t pin_ = 0xFF;
  uint16_t count_ = 0;
  uint8_t frame_[3 * 64] = {};
  uint32_t frames_ = 0;
  unsigned failing_ = 0;
};

// SPI bus of the PC: nothing is wired to it. What is sent is kept for the tests, with the
// level of a pin at each transfer, so that a test tells commands from data.
class Spi : public ISpi {
 public:
  Status begin(uint8_t sck, uint8_t miso, uint8_t mosi) override;
  void end() override { open_ = false; }
  Status transfer(uint8_t cs, const SpiConfig& cfg, const uint8_t* tx, uint8_t* rx,
                  size_t len) override;

  // Test hooks.
  void reset();
  bool open() const { return open_; }
  uint8_t sck() const { return sck_; }
  uint8_t mosi() const { return mosi_; }
  // The pin read at each transfer: the data/command line of a screen. 0xFF: none.
  void watch(uint8_t pin) { watched_ = pin; }
  // Every byte sent, in order, and for each one whether the watched pin was high.
  const std::string& sent() const { return sent_; }
  const std::string& high() const { return high_; }
  size_t transfers() const { return transfers_; }

 private:
  std::string sent_;
  std::string high_;
  size_t transfers_ = 0;
  uint8_t sck_ = 0xFF, mosi_ = 0xFF, watched_ = 0xFF;
  bool open_ = false;
};

// Files under a folder of the PC. Paths are the same used on the device: "/cfg/x.json".
class Fs : public IFs {
 public:
  explicit Fs(const char* rootDir);

  Status mount(bool formatOnFail) override;
  void unmount() override { mounted_ = false; }
  bool mounted() const override { return mounted_; }
  Status info(FsInfo& out) const override;
  IFile* open(const char* path, OpenMode mode) override;
  bool exists(const char* path) const override;
  Status remove(const char* path) override;
  Status rename(const char* from, const char* to) override;
  Status mkdir(const char* path) override;
  Status list(const char* dir, DirVisitor visitor) override;

  // Test hook: deletes everything.
  void wipe();
  const std::string& root() const { return root_; }

 private:
  std::string resolve(const char* path) const;

  std::string root_;
  bool mounted_ = false;
};

// One small file for each key, under a folder of the PC.
class Kv : public IKv {
 public:
  explicit Kv(const char* rootDir);

  Status begin() override;
  Status get(const char* ns, const char* key, void* out, size_t cap, size_t& len) override;
  Status set(const char* ns, const char* key, const void* data, size_t len) override;
  Status remove(const char* ns, const char* key) override;
  Status eraseNamespace(const char* ns) override;
  Status commit() override { return Status::Ok; }

  void wipe();

 private:
  std::string file(const char* ns, const char* key) const;

  std::string root_;
};

// Folder of the files and of the settings. The tests wipe theirs at every start: they have
// one of their own, so the simulator keeps its settings and its console.
#ifdef PIO_UNIT_TESTING
#define LMP_NATIVE_ROOT ".lmp_native/test"
#else
#define LMP_NATIVE_ROOT ".lmp_native"
#endif

// Concrete objects of the PC board, for the tests that need the hooks.
struct Objects {
  System sys;
  Clock clock;
  Serial serial;
  Rtos rtos;
  Random rng;
  Gpio gpio;
  Pwm pwm;
  Pixels pixels;
  Spi spi;
  Fs fs{LMP_NATIVE_ROOT "/fs"};
  Kv kv{LMP_NATIVE_ROOT "/kv"};
  Net net;
  HttpServer http{fs};
  Ntp ntp;
  HttpClient httpClient;
  Mqtt mqtt;
  Mqtt cloudMqtt;
  Ota ota;
  I2c i2c[LMP_HAL_I2C_MAX];
  Probe probe;
  // A cable: the simulator has it with LMP_SIM_CABLE=1 in its environment; the tests plug it
  // into the board they make.
  Link cable;
};

Objects& objects();

}  // namespace lmp::hal::native
