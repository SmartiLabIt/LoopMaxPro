#include "hal/native/native_hal.h"

#include "hal/common/lora_wiring.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <chrono>
#include <random>
#include <thread>

namespace lmp::hal::native {

// ---------------------------------------------------------------- system

System::System() {
  info_.chipModel = "PC";
  info_.sdkVersion = "native";
  info_.cores = static_cast<uint8_t>(std::thread::hardware_concurrency() > 255
                                         ? 255
                                         : std::thread::hardware_concurrency());
  if (info_.cores == 0) info_.cores = 1;
  info_.cpuMhz = 0;
  info_.flashSize = 16u * 1024 * 1024;
  info_.flashSpeedHz = 0;
}

void System::mac(Mac& out) const {
  static const uint8_t fixed[6] = {0x02, 0x4C, 0x4D, 0x50, 0x00, 0x01};
  memcpy(out.b, fixed, sizeof(fixed));
}

bool System::chipNoteRead(uint8_t* out, size_t len) const {
  if (len > kChipNoteMax) return false;
  memcpy(out, note_, len);
  return true;
}

Status System::chipNoteWrite(const uint8_t* data, size_t len) {
  if (len > kChipNoteMax) return Status::Invalid;
  for (uint8_t b : note_) {
    if (b) return Status::Denied;
  }
  memcpy(note_, data, len);
  ++noteWrites_;
  return Status::Ok;
}

void System::restart() {
  ++restarts_;
  if (exitOnRestart_) {
    fflush(stdout);
    exit(0);
  }
}

void System::lastWordsKeep(const char* line) {
  lines_.emplace_back(line ? line : "");
  if (lines_.size() > kLastWordsLines) lines_.erase(lines_.begin());
}

// ---------------------------------------------------------------- clock

namespace {
int64_t nowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
}  // namespace

Clock::Clock() : startNs_(nowNs()) {}

uint64_t Clock::uptimeMs() const { return static_cast<uint64_t>((nowNs() - startNs_) / 1000000); }
uint64_t Clock::micros() const { return static_cast<uint64_t>((nowNs() - startNs_) / 1000); }

int64_t Clock::unixTime() const {
  if (!unixSet_) return 0;
  return unixOffset_ + static_cast<int64_t>(uptimeMs() / 1000);
}

Status Clock::setUnixTime(int64_t seconds) {
  if (seconds <= 0) return Status::Invalid;
  unixOffset_ = seconds - static_cast<int64_t>(uptimeMs() / 1000);
  unixSet_ = true;
  return Status::Ok;
}

Status Clock::setTimezone(const char* posixTz) {
  if (!posixTz) return Status::Invalid;
#if defined(_WIN32)
  _putenv_s("TZ", posixTz);
  _tzset();
#else
  setenv("TZ", posixTz, 1);
  tzset();
#endif
  return Status::Ok;
}

bool Clock::localTime(int64_t unixSeconds, DateTime& out) const {
  const time_t t = static_cast<time_t>(unixSeconds);
  struct tm local;
  struct tm utc;
#if defined(_WIN32)
  if (localtime_s(&local, &t) != 0 || gmtime_s(&utc, &t) != 0) return false;
#else
  if (!localtime_r(&t, &local) || !gmtime_r(&t, &utc)) return false;
#endif
  out.year = static_cast<uint16_t>(local.tm_year + 1900);
  out.month = static_cast<uint8_t>(local.tm_mon + 1);
  out.day = static_cast<uint8_t>(local.tm_mday);
  out.hour = static_cast<uint8_t>(local.tm_hour);
  out.minute = static_cast<uint8_t>(local.tm_min);
  out.second = static_cast<uint8_t>(local.tm_sec);
  out.weekday = static_cast<uint8_t>(local.tm_wday);
  // Offset from the difference between the two broken down times.
  int32_t diff = (local.tm_hour - utc.tm_hour) * 3600 + (local.tm_min - utc.tm_min) * 60 +
                 (local.tm_sec - utc.tm_sec);
  const int dayDiff = local.tm_yday - utc.tm_yday;
  if (dayDiff == 1 || dayDiff < -1) diff += 86400;
  if (dayDiff == -1 || dayDiff > 1) diff -= 86400;
  out.utcOffset = diff;
  return true;
}

// ---------------------------------------------------------------- serial

Status Serial::begin(uint32_t) {
  ready_ = true;
  return Status::Ok;
}

size_t Serial::write(const uint8_t* data, size_t len) {
  if (!ready_ || !data) return 0;
  std::lock_guard<std::mutex> lock(mutex_);
  if (capture_) {
    output_.append(reinterpret_cast<const char*>(data), len);
  } else {
    fwrite(data, 1, len, stdout);
  }
  return len;
}

size_t Serial::read(uint8_t* out, size_t cap) {
  std::lock_guard<std::mutex> lock(mutex_);
  const size_t n = input_.size() < cap ? input_.size() : cap;
  if (n) {
    memcpy(out, input_.data(), n);
    input_.erase(0, n);
  }
  return n;
}

size_t Serial::available() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return input_.size();
}

void Serial::flush() { fflush(stdout); }

std::string Serial::takeOutput() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string out;
  out.swap(output_);
  return out;
}

void Serial::feedInput(const char* text) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (text) input_.append(text);
}

// ---------------------------------------------------------------- rtos

namespace {

class StdMutex : public IMutex {
 public:
  void lock() override { m_.lock(); }
  void unlock() override { m_.unlock(); }

 private:
  std::mutex m_;
};

class StdSemaphore : public ISemaphore {
 public:
  void give() override {
    {
      std::lock_guard<std::mutex> lock(m_);
      ++count_;
    }
    cv_.notify_one();
  }
  bool take(uint32_t timeoutMs) override {
    std::unique_lock<std::mutex> lock(m_);
    if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return count_ > 0; })) {
      return false;
    }
    --count_;
    return true;
  }

 private:
  std::mutex m_;
  std::condition_variable cv_;
  unsigned count_ = 0;
};

class StdQueue : public IQueue {
 public:
  StdQueue(size_t itemSize, size_t length)
      : itemSize_(itemSize), length_(length), data_(itemSize * length) {}

  bool send(const void* item, uint32_t timeoutMs) override {
    std::unique_lock<std::mutex> lock(m_);
    if (!notFull_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                           [this] { return count_ < length_; })) {
      return false;
    }
    memcpy(&data_[((head_ + count_) % length_) * itemSize_], item, itemSize_);
    ++count_;
    lock.unlock();
    notEmpty_.notify_one();
    return true;
  }

  bool receive(void* item, uint32_t timeoutMs) override {
    std::unique_lock<std::mutex> lock(m_);
    if (!notEmpty_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                            [this] { return count_ > 0; })) {
      return false;
    }
    memcpy(item, &data_[head_ * itemSize_], itemSize_);
    head_ = (head_ + 1) % length_;
    --count_;
    lock.unlock();
    notFull_.notify_one();
    return true;
  }

  size_t waiting() const override {
    std::lock_guard<std::mutex> lock(m_);
    return count_;
  }

 private:
  const size_t itemSize_;
  const size_t length_;
  std::vector<uint8_t> data_;
  size_t head_ = 0;
  size_t count_ = 0;
  mutable std::mutex m_;
  std::condition_variable notEmpty_;
  std::condition_variable notFull_;
};

}  // namespace

IMutex* Rtos::createMutex() { return new StdMutex(); }
ISemaphore* Rtos::createSemaphore() { return new StdSemaphore(); }
IQueue* Rtos::createQueue(size_t itemSize, size_t length) {
  if (itemSize == 0 || length == 0) return nullptr;
  return new StdQueue(itemSize, length);
}

Status Rtos::createTask(const TaskConfig&, TaskFn fn, void* arg) {
  if (!fn) return Status::Invalid;
  std::thread(fn, arg).detach();
  return Status::Ok;
}

uintptr_t Rtos::currentTaskId() const {
  return static_cast<uintptr_t>(std::hash<std::thread::id>()(std::this_thread::get_id()));
}

void Rtos::delayMs(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
void Rtos::yield() { std::this_thread::yield(); }

// ---------------------------------------------------------------- random

void Random::bytes(uint8_t* out, size_t len) {
  static std::random_device device;
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  for (size_t i = 0; i < len; ++i) out[i] = static_cast<uint8_t>(device() & 0xFF);
}

// ---------------------------------------------------------------- gpio

Status Gpio::setMode(uint8_t pin, PinMode mode) {
  if (pin >= kPins) return Status::Invalid;
  modes_[pin] = mode;
  if (mode == PinMode::InputPullUp) levels_[pin] = PinLevel::High;
  if (mode == PinMode::InputPullDown) levels_[pin] = PinLevel::Low;
  return Status::Ok;
}

Status Gpio::write(uint8_t pin, PinLevel level) {
  if (pin >= kPins) return Status::Invalid;
  if (modes_[pin] != PinMode::Output && modes_[pin] != PinMode::OutputOpenDrain) {
    return Status::Denied;
  }
  levels_[pin] = level;
  return Status::Ok;
}

Status Gpio::read(uint8_t pin, PinLevel& level) const {
  if (pin >= kPins) return Status::Invalid;
  level = levels_[pin];
  return Status::Ok;
}

Status Gpio::attachInterrupt(uint8_t pin, Edge edge, PinHandler handler) {
  if (pin >= kPins) return Status::Invalid;
  edges_[pin] = edge;
  handlers_[pin] = handler;
  return Status::Ok;
}

Status Gpio::detachInterrupt(uint8_t pin) {
  if (pin >= kPins) return Status::Invalid;
  handlers_[pin].reset();
  return Status::Ok;
}

void Gpio::setInput(uint8_t pin, PinLevel level) {
  if (pin >= kPins) return;
  const PinLevel before = levels_[pin];
  levels_[pin] = level;
  if (before == level || !handlers_[pin]) return;
  const bool rising = level == PinLevel::High;
  const Edge edge = edges_[pin];
  if (edge == Edge::Change || (edge == Edge::Rising && rising) ||
      (edge == Edge::Falling && !rising)) {
    handlers_[pin](pin);
  }
}

// ---------------------------------------------------------------- pwm

Status Pwm::setup(uint8_t pin, uint32_t frequencyHz, uint8_t resolutionBits) {
  if (pin >= kPins || frequencyHz == 0 || resolutionBits == 0 || resolutionBits > 16) {
    return Status::Invalid;
  }
  bits_[pin] = resolutionBits;
  duty_[pin] = 0;
  return Status::Ok;
}

Status Pwm::write(uint8_t pin, uint32_t duty) {
  if (pin >= kPins || !bits_[pin]) return Status::Invalid;
  if (duty >= (1u << bits_[pin])) duty = (1u << bits_[pin]) - 1;
  duty_[pin] = duty;
  ++writes_;
  return Status::Ok;
}

Status Pwm::release(uint8_t pin) {
  if (pin >= kPins) return Status::Invalid;
  bits_[pin] = 0;
  duty_[pin] = 0;
  return Status::Ok;
}

void Pwm::reset() {
  for (uint8_t i = 0; i < kPins; ++i) bits_[i] = 0, duty_[i] = 0;
  writes_ = 0;
}

// ---------------------------------------------------------------- pixels

Status Pixels::begin(uint8_t pin, uint16_t count) {
  if (pin >= 64 || count == 0 || count > 64) return Status::Invalid;
  if (pin_ != 0xFF && pin_ != pin) return Status::NoMem;
  pin_ = pin;
  count_ = count;
  memset(frame_, 0, sizeof(frame_));
  return Status::Ok;
}

Status Pixels::show(uint8_t pin, const uint8_t* grb, size_t len) {
  if (pin != pin_ || !grb) return Status::Invalid;
  if (failing_) {
    --failing_;
    return Status::Io;
  }
  if (len > count_ * 3u) len = count_ * 3u;
  memcpy(frame_, grb, len);
  ++frames_;
  return Status::Ok;
}

Status Pixels::end(uint8_t pin) {
  if (pin != pin_) return Status::NotFound;
  reset();
  return Status::Ok;
}

void Pixels::reset() {
  pin_ = 0xFF;
  count_ = 0;
  frames_ = 0;
  failing_ = 0;
  memset(frame_, 0, sizeof(frame_));
}

void Pixels::rgbOf(uint16_t i, uint8_t& r, uint8_t& g, uint8_t& b) const {
  if (i >= count_) {
    r = g = b = 0;
    return;
  }
  g = frame_[i * 3];
  r = frame_[i * 3 + 1];
  b = frame_[i * 3 + 2];
}

// ---------------------------------------------------------------- spi

Status Spi::begin(uint8_t sck, uint8_t miso, uint8_t mosi) {
  (void)miso;
  sck_ = sck;
  mosi_ = mosi;
  open_ = true;
  return Status::Ok;
}

Status Spi::transfer(uint8_t cs, const SpiConfig& cfg, const uint8_t* tx, uint8_t* rx,
                     size_t len) {
  (void)cs;
  (void)cfg;
  if (!open_) return Status::Denied;
  if (len == 0 || (!tx && !rx)) return Status::Invalid;
  PinLevel level = PinLevel::Low;
  if (watched_ != 0xFF) objects().gpio.read(watched_, level);
  if (tx) {
    sent_.append(reinterpret_cast<const char*>(tx), len);
    high_.append(len, level == PinLevel::High ? '1' : '0');
  }
  if (rx) memset(rx, 0, len);
  ++transfers_;
  return Status::Ok;
}

void Spi::reset() {
  sent_.clear();
  high_.clear();
  transfers_ = 0;
  sck_ = mosi_ = watched_ = 0xFF;
  open_ = false;
}

// ---------------------------------------------------------------- board

Objects& objects() {
  static Objects instance;
  return instance;
}

}  // namespace lmp::hal::native

namespace lmp::hal {

void boot() { native::objects(); }

const Board& board() {
  static Board b = [] {
    native::Objects& o = native::objects();
    Board x;
    x.sys = &o.sys;
    x.clock = &o.clock;
    x.serial = &o.serial;
    x.rtos = &o.rtos;
    x.rng = &o.rng;
    x.gpio = &o.gpio;
    x.pwm = &o.pwm;
    x.pixels = &o.pixels;
    x.spi = &o.spi;
    x.fs = &o.fs;
    x.kv = &o.kv;
    x.net = &o.net;
    x.http = &o.http;
    x.ntp = &o.ntp;
    x.httpClient = &o.httpClient;
    x.mqtt = &o.mqtt;
    x.cloudMqtt = &o.cloudMqtt;
    x.ota = &o.ota;
    for (size_t i = 0; i < LMP_HAL_I2C_MAX; ++i) x.i2c[i] = &o.i2c[i];
#if LMP_FEATURE_PROBE
    x.probe = &o.probe;
#endif
    o.ntp.attach(&o.clock);
    x.caps.add(Cap::Wifi);
    x.caps.add(Cap::Pwm);
    x.caps.add(Cap::HttpServer);
    x.caps.add(Cap::HttpClient);
    x.caps.add(Cap::Tls);
    x.caps.add(Cap::Mqtt);
    x.caps.add(Cap::Ota);
    x.caps.add(Cap::I2c);
    x.caps.add(Cap::Rtos);
    x.caps.add(Cap::Rng);
    x.caps.add(Cap::Fs);
    x.caps.add(Cap::Kv);
#if LMP_FEATURE_LORA
    // The radio is simulated by the driver of the PC build.
    x.lora = &loraWiring();
    x.caps.add(Cap::Lora);
#endif
    return x;
  }();
  return b;
}

}  // namespace lmp::hal
