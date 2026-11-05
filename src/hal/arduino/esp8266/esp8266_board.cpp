// LoopMaxPro HAL - ESP8266 with the Arduino framework.
// System, clock, console, cooperative tasks, random, table of the board.
// Wi-Fi and the HTTP server are in esp8266_net.cpp.
#include <Arduino.h>
#include <sys/time.h>
#include <time.h>

extern "C" {
#include <user_interface.h>
}

#include "hal/api/board.h"
#include "hal/arduino/common/arduino_hal.h"
#include "hal/arduino/esp8266/esp8266_hal.h"
#include "hal/common/file_kv.h"
#include "hal/common/lora_wiring.h"
#include "lmp_config.h"

namespace lmp::hal {
namespace esp8266 {

void housekeeping();

// ---------------------------------------------------------------- system

class System : public ISystem {
 public:
  System() {
    info_.chipModel = "ESP8266";
    info_.sdkVersion = ESP.getSdkVersion();
    info_.cores = 1;
    info_.revision = 0;
    info_.cpuMhz = ESP.getCpuFreqMHz();
    // What the chip says, not what the firmware was built for.
    info_.flashSize = ESP.getFlashChipRealSize();
    info_.flashSpeedHz = ESP.getFlashChipSpeed();
    heapTotal_ = heapMin_ = ESP.getFreeHeap();
  }

  const SystemInfo& info() const override { return info_; }
  uint32_t heapFree() const override { return sample(); }
  uint32_t heapMin() const override {
    sample();
    return heapMin_;
  }
  // The chip does not say how large the heap is: this is what was free at the start.
  uint32_t heapTotal() const override { return heapTotal_; }
  uint32_t stackMin() const override { return ESP.getFreeContStack(); }
  uint32_t heapLargest() const override { return ESP.getMaxFreeBlockSize(); }

  ResetReason resetReason() const override {
    const rst_info* info = ESP.getResetInfoPtr();
    if (!info) return ResetReason::Unknown;
    switch (info->reason) {
      case REASON_DEFAULT_RST:      return ResetReason::PowerOn;
      case REASON_EXT_SYS_RST:      return ResetReason::External;
      case REASON_SOFT_RESTART:     return ResetReason::Software;
      case REASON_EXCEPTION_RST:    return ResetReason::Panic;
      case REASON_WDT_RST:
      case REASON_SOFT_WDT_RST:     return ResetReason::Watchdog;
      case REASON_DEEP_SLEEP_AWAKE: return ResetReason::DeepSleep;
      default:                      break;
    }
    return ResetReason::Unknown;
  }

  void mac(Mac& out) const override { wifi_get_macaddr(STATION_IF, out.b); }

  // Called at every round of the system: the place for what must happen often.
  void watchdogFeed() override {
    ESP.wdtFeed();
    sample();
    housekeeping();
  }
  void restart() override { ESP.restart(); }

 private:
  uint32_t sample() const {
    const uint32_t free = ESP.getFreeHeap();
    if (free < heapMin_) heapMin_ = free;
    return free;
  }

  SystemInfo info_;
  uint32_t heapTotal_ = 0;
  mutable uint32_t heapMin_ = 0;
};

// ---------------------------------------------------------------- clock

class Clock : public IClock {
 public:
  uint32_t millis() const override { return ::millis(); }
  uint64_t uptimeMs() const override { return micros64() / 1000; }
  uint64_t micros() const override { return micros64(); }

  int64_t unixTime() const override {
    const time_t now = time(nullptr);
    // Before the first synchronization the system clock counts from 1970.
    return now > kMinValid ? static_cast<int64_t>(now) : 0;
  }

  Status setUnixTime(int64_t seconds) override {
    if (seconds <= kMinValid) return Status::Invalid;
    struct timeval tv;
    tv.tv_sec = static_cast<time_t>(seconds);
    tv.tv_usec = 0;
    return settimeofday(&tv, nullptr) == 0 ? Status::Ok : Status::Error;
  }

  Status setTimezone(const char* posixTz) override {
    if (!posixTz) return Status::Invalid;
    setenv("TZ", posixTz, 1);
    tzset();
    return Status::Ok;
  }

  bool localTime(int64_t unixSeconds, DateTime& out) const override {
    const time_t t = static_cast<time_t>(unixSeconds);
    struct tm local;
    struct tm utc;
    if (!localtime_r(&t, &local) || !gmtime_r(&t, &utc)) return false;
    out.year = static_cast<uint16_t>(local.tm_year + 1900);
    out.month = static_cast<uint8_t>(local.tm_mon + 1);
    out.day = static_cast<uint8_t>(local.tm_mday);
    out.hour = static_cast<uint8_t>(local.tm_hour);
    out.minute = static_cast<uint8_t>(local.tm_min);
    out.second = static_cast<uint8_t>(local.tm_sec);
    out.weekday = static_cast<uint8_t>(local.tm_wday);
    int32_t diff = (local.tm_hour - utc.tm_hour) * 3600 + (local.tm_min - utc.tm_min) * 60 +
                   (local.tm_sec - utc.tm_sec);
    const int dayDiff = local.tm_yday - utc.tm_yday;
    if (dayDiff == 1 || dayDiff < -1) diff += 86400;
    if (dayDiff == -1 || dayDiff > 1) diff -= 86400;
    out.utcOffset = diff;
    return true;
  }

 private:
  static constexpr time_t kMinValid = 1600000000;  // September 2020
};

// ---------------------------------------------------------------- one context

// Nothing else runs while the system does: there is nothing to lock.
class Mutex : public IMutex {
 public:
  void lock() override {}
  void unlock() override {}
};

// Only an interrupt can give while somebody waits.
class Semaphore : public ISemaphore {
 public:
  void give() override {
    const uint32_t saved = xt_rsil(15);
    ++count_;
    xt_wsr_ps(saved);
  }

  bool take(uint32_t timeoutMs) override {
    const uint32_t start = ::millis();
    for (;;) {
      const uint32_t saved = xt_rsil(15);
      const bool got = count_ > 0;
      if (got) --count_;
      xt_wsr_ps(saved);
      if (got) return true;
      if (::millis() - start >= timeoutMs) return false;
      delay(1);
    }
  }

 private:
  volatile uint32_t count_ = 0;
};

class Queue : public IQueue {
 public:
  Queue(size_t itemSize, size_t length)
      : itemSize_(itemSize), length_(length), data_(new uint8_t[itemSize * length]) {}
  bool valid() const { return data_ != nullptr; }

  bool send(const void* item, uint32_t) override {
    const uint32_t saved = xt_rsil(15);
    const bool room = count_ < length_;
    if (room) {
      memcpy(&data_[((head_ + count_) % length_) * itemSize_], item, itemSize_);
      ++count_;
    }
    xt_wsr_ps(saved);
    return room;
  }

  bool receive(void* item, uint32_t timeoutMs) override {
    const uint32_t start = ::millis();
    for (;;) {
      const uint32_t saved = xt_rsil(15);
      const bool got = count_ > 0;
      if (got) {
        memcpy(item, &data_[head_ * itemSize_], itemSize_);
        head_ = (head_ + 1) % length_;
        --count_;
      }
      xt_wsr_ps(saved);
      if (got) return true;
      if (::millis() - start >= timeoutMs) return false;
      delay(1);
    }
  }

  size_t waiting() const override { return count_; }

 private:
  const size_t itemSize_;
  const size_t length_;
  uint8_t* data_;
  size_t head_ = 0;
  volatile size_t count_ = 0;
};

class Rtos : public IRtos {
 public:
  bool preemptive() const override { return false; }
  IMutex* createMutex() override { return &mutex_; }
  ISemaphore* createSemaphore() override { return new Semaphore(); }
  IQueue* createQueue(size_t itemSize, size_t length) override {
    if (itemSize == 0 || length == 0) return nullptr;
    Queue* q = new Queue(itemSize, length);
    if (!q->valid()) {
      delete q;
      return nullptr;
    }
    return q;
  }
  // The system runs inside loop().
  Status createTask(const TaskConfig&, TaskFn, void*) override { return Status::Unsupported; }

  uintptr_t currentTaskId() const override { return 1; }
  void delayMs(uint32_t ms) override {
    housekeeping();
    delay(ms);
  }
  void yield() override { ::yield(); }

 private:
  Mutex mutex_;   // one for everybody: it does nothing
};

// ---------------------------------------------------------------- random

class Random : public IRandom {
 public:
  // From the generator of the chip, fed by the noise of the radio.
  void bytes(uint8_t* out, size_t len) override { ESP.random(out, len); }
};

// ---------------------------------------------------------------- objects

struct Objects {
  System sys;
  Clock clock;
  Rtos rtos;
  Random rng;
  Serial0 serial;
  arduino::Gpio gpio;
  Fs fs;
  FileKv kv{fs};
  Pwm pwm;
  Adc adc;
#if LMP_FEATURE_NET
  Net net;
  HttpServer http;
  Ntp ntp;
#endif
#if LMP_FEATURE_HTTPCLIENT
  HttpClient httpClient;
#endif
#if LMP_FEATURE_BUS
  I2c i2c;
#endif
};

Objects& objects() {
  static Objects instance;
  return instance;
}

void housekeeping() { objects().serial.pump(); }

}  // namespace esp8266

// Pins of the chip. 6 to 11 belong to the flash.
namespace arduino {

bool pinIsValid(uint8_t pin) { return pin <= 5 || (pin >= 12 && pin <= 16); }
bool pinCanOutput(uint8_t pin) { return pinIsValid(pin); }

}  // namespace arduino

void boot() { esp8266::objects(); }

const Board& board() {
  static Board b = [] {
    esp8266::Objects& o = esp8266::objects();
    Board x;
    x.sys = &o.sys;
    x.clock = &o.clock;
    x.serial = &o.serial;
    x.rtos = &o.rtos;
    x.rng = &o.rng;
    x.gpio = &o.gpio;
    x.fs = &o.fs;
    x.kv = &o.kv;
    x.pwm = &o.pwm;
    x.adc = &o.adc;
    x.caps.add(Cap::Rng);
    x.caps.add(Cap::Fs);
    x.caps.add(Cap::Kv);
    x.caps.add(Cap::Pwm);
    x.caps.add(Cap::Adc);
#if LMP_FEATURE_NET
    x.net = &o.net;
    x.http = &o.http;
    x.ntp = &o.ntp;
    x.caps.add(Cap::Wifi);
    x.caps.add(Cap::HttpServer);
#endif
#if LMP_FEATURE_HTTPCLIENT
    x.httpClient = &o.httpClient;
    x.caps.add(Cap::HttpClient);
#endif
#if LMP_FEATURE_BUS
    x.i2c[0] = &o.i2c;
    x.caps.add(Cap::I2c);
#endif
#if LMP_FEATURE_LORA
    x.lora = &loraWiring();
    x.caps.add(Cap::Lora);
#endif
    return x;
  }();
  return b;
}

}  // namespace lmp::hal
