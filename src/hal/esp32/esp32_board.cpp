// LoopMaxPro HAL - ESP32 family, both frameworks.
// System, clock, tasks, random and the table of the board.
#include <esp_chip_info.h>
#include <esp_efuse.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <esp_mac.h>
#include <esp_private/esp_clk.h>
#include <esp_random.h>
#include <esp_rom_sys.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <sdkconfig.h>
#include <soc/soc_caps.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#if CONFIG_IDF_TARGET_ESP32
#include <soc/efuse_reg.h>
#endif

#include "base/sha256.h"
#include "hal/api/board.h"
#include "hal/common/lora_wiring.h"
#include "hal/esp32/esp32_board.h"
#include "hal/esp32/esp32_eth.h"
#include "hal/esp32/esp32_http.h"
#include "hal/esp32/esp32_net.h"
#include "hal/esp32/esp32_online.h"
#include "hal/esp32/esp32_probe.h"
#include "hal/esp32/esp32_spi.h"
#include "hal/esp32/esp32_update.h"

#ifndef LMP_TLS_EXTERNAL_RAM
#if defined(ARDUINO) && ESP_IDF_VERSION_MAJOR < 5 && CONFIG_SPIRAM
#define LMP_TLS_EXTERNAL_RAM 1
#else
#define LMP_TLS_EXTERNAL_RAM 0
#endif
#endif
#if LMP_TLS_EXTERNAL_RAM
#include <mbedtls/platform.h>
#endif

#if LMP_ESP32_CONSOLE == 2
#include "hal/arduino/esp32/esp32_usb_console.h"
#endif

namespace lmp::hal {
namespace esp32 {

// ---------------------------------------------------------------- system

namespace {

// The name of the chip, with the package on the ESP32 as the Arduino core gives it: the
// same text in the builds of both frameworks.
const char* chipName(const esp_chip_info_t& chip, uint8_t revision) {
  switch (chip.model) {
    case CHIP_ESP32: {
#if CONFIG_IDF_TARGET_ESP32 && defined(EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ5)
      switch (esp_efuse_get_pkg_ver()) {
        case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ6:
          return revision >= 3 ? "ESP32-D0WDQ6-V3" : "ESP32-D0WDQ6";
        case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ5:
          return revision >= 3 ? "ESP32-D0WD-V3" : "ESP32-D0WD";
        case EFUSE_RD_CHIP_VER_PKG_ESP32D2WDQ5:   return "ESP32-D2WD";
        case EFUSE_RD_CHIP_VER_PKG_ESP32PICOD4:   return "ESP32-PICO-D4";
#if defined(EFUSE_RD_CHIP_VER_PKG_ESP32PICOV302)
        case EFUSE_RD_CHIP_VER_PKG_ESP32PICOV302: return "ESP32-PICO-V3-02";
#endif
#if defined(EFUSE_RD_CHIP_VER_PKG_ESP32D0WDR2V3)
        case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDR2V3: return "ESP32-D0WDR2-V3";
#endif
        default: break;
      }
#else
      (void)revision;
#endif
      return "ESP32";
    }
    case CHIP_ESP32S2: return "ESP32-S2";
    case CHIP_ESP32S3: return "ESP32-S3";
    case CHIP_ESP32C3: return "ESP32-C3";
#if ESP_IDF_VERSION_MAJOR >= 5
    case CHIP_ESP32C6: return "ESP32-C6";
    case CHIP_ESP32H2: return "ESP32-H2";
#endif
    default:           return CONFIG_IDF_TARGET;
  }
}

uint32_t flashSpeedHz() {
#if defined(CONFIG_ESPTOOLPY_FLASHFREQ_120M)
  return 120000000;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_80M)
  return 80000000;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_64M)
  return 64000000;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_48M)
  return 48000000;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_40M)
  return 40000000;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_32M)
  return 32000000;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_26M)
  return 26000000;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_20M)
  return 20000000;
#else
  return 0;
#endif
}

}  // namespace

// What must happen at every round of the system. Defined after the objects of the board.
void housekeeping();

class System : public ISystem {
 public:
  System() {
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    // ESP-IDF 5 and later give the revision as major * 100 + minor: the major one is kept.
#if ESP_IDF_VERSION_MAJOR >= 5
    info_.revision = static_cast<uint8_t>(chip.revision / 100);
#else
    info_.revision = static_cast<uint8_t>(chip.revision);
#endif
    info_.chipModel = chipName(chip, info_.revision);
    info_.sdkVersion = esp_get_idf_version();
    info_.cores = chip.cores;
    info_.cpuMhz = static_cast<uint16_t>(esp_clk_cpu_freq() / 1000000);
    uint32_t flashBytes = 0;
    if (esp_flash_get_size(nullptr, &flashBytes) == ESP_OK) info_.flashSize = flashBytes;
    info_.flashSpeedHz = flashSpeedHz();
    // What the previous start left in the RTC memory, before anything writes there.
    lastWordsBegin();
    // For who develops: the map of the memory on the serial port (LMP_HEAP_MAP, off).
    heapMapBegin();
  }

  const SystemInfo& info() const override { return info_; }
  // The memory of the chip that malloc can give: internal, and of bytes. On the first ESP32 the
  // internal memory has also a zone for code alone (about 35 KB free on the N4), which nobody can
  // ask for data: counted here, the board said it had 40 KB free while a request of 4 KB failed
  // (found on a first ESP32 without PSRAM).
  static constexpr uint32_t kHeap = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  uint32_t heapFree() const override { return heap_caps_get_free_size(kHeap); }
  uint32_t heapMin() const override { return heap_caps_get_minimum_free_size(kHeap); }
  uint32_t heapTotal() const override { return heap_caps_get_total_size(kHeap); }
  uint32_t heapLargest() const override { return heap_caps_get_largest_free_block(kHeap); }
  uint32_t stackMin() const override { return uxTaskGetStackHighWaterMark(nullptr); }

  const char* resetDetail() const override {
    switch (esp_reset_reason()) {
      case ESP_RST_INT_WDT:  return "int";
      case ESP_RST_TASK_WDT: return "task";
      case ESP_RST_WDT:      return "other";
      default:               return "";
    }
  }

  ResetReason resetReason() const override {
    switch (esp_reset_reason()) {
      case ESP_RST_POWERON:  return ResetReason::PowerOn;
      case ESP_RST_EXT:      return ResetReason::External;
      case ESP_RST_SW:       return ResetReason::Software;
      case ESP_RST_PANIC:    return ResetReason::Panic;
      case ESP_RST_INT_WDT:
      case ESP_RST_TASK_WDT:
      case ESP_RST_WDT:      return ResetReason::Watchdog;
      case ESP_RST_BROWNOUT: return ResetReason::Brownout;
      case ESP_RST_DEEPSLEEP: return ResetReason::DeepSleep;
      default:               break;
    }
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    // A reset asked through the USB port of the chip, as the upload tool does.
    const soc_reset_reason_t rom = esp_rom_get_reset_reason(0);
    if (rom == RESET_REASON_CORE_USB_UART || rom == RESET_REASON_CORE_USB_JTAG) {
      return ResetReason::External;
    }
#endif
    return ResetReason::Unknown;
  }

  void mac(Mac& out) const override {
    // The six bytes written at the factory, the base of the addresses of the chip.
    uint8_t raw[8] = {0};
    if (esp_efuse_mac_get_default(raw) != ESP_OK) return;
    memcpy(out.b, raw, sizeof(out.b));
  }

#if !CONFIG_IDF_TARGET_ESP32
  // The user block of the eFuse, BLOCK3, from its first byte. Not on the first ESP32: there the
  // block can hold the calibration of the converters.
  bool chipNoteRead(uint8_t* out, size_t len) const override {
    if (len > kChipNoteMax) return false;
    return esp_efuse_read_block(EFUSE_BLK3, out, 0, len * 8) == ESP_OK;
  }

  Status chipNoteWrite(const uint8_t* data, size_t len) override {
    if (len > kChipNoteMax) return Status::Invalid;
    // The whole block must be empty: it takes one write only.
    uint8_t now[32];
    if (esp_efuse_read_block(EFUSE_BLK3, now, 0, sizeof(now) * 8) != ESP_OK) return Status::Io;
    for (uint8_t b : now) {
      if (b) return Status::Denied;
    }
    return esp_efuse_write_block(EFUSE_BLK3, data, 0, len * 8) == ESP_OK ? Status::Ok : Status::Io;
  }
#endif

  void watchdogFeed() override {
    stallFeed();
    taskYIELD();
    housekeeping();
  }
  void restart() override { esp_restart(); }

  void lastWordsKeep(const char* line) override { esp32::lastWordsKeep(line); }
  void lastWordsWhy(const char* why) override { esp32::lastWordsWhy(why); }
  const LastWords* lastWordsPrevious() const override { return esp32::lastWordsPrevious(); }
  void stallGuard(uint32_t seconds) override { esp32::stallGuard(seconds); }

  // The partition table as it is in the flash, the same bytes as partitions.bin of the build:
  // who makes a package of the firmware can write the same fingerprint beside it.
  const char* layout() const override {
    if (layout_[0]) return layout_;
    static constexpr uint32_t kTableSize = 0xC00;
#ifdef CONFIG_PARTITION_TABLE_OFFSET
    static constexpr uint32_t kTableAt = CONFIG_PARTITION_TABLE_OFFSET;
#else
    static constexpr uint32_t kTableAt = 0x8000;
#endif
    Sha256 hash;
    uint8_t piece[256];
    for (uint32_t at = 0; at < kTableSize; at += sizeof(piece)) {
      if (esp_flash_read(nullptr, piece, kTableAt + at, sizeof(piece)) != ESP_OK) return "";
      hash.update(piece, sizeof(piece));
    }
    uint8_t digest[Sha256::kSize];
    hash.finish(digest);
    char hex[Sha256::kSize * 2 + 1];
    sha256Hex(digest, hex);
    memcpy(layout_, hex, 16);
    layout_[16] = '\0';
    return layout_;
  }

  const char* base() const override {
#define LMP_BASE_TEXT(major) "idf" #major
#define LMP_BASE_OF(major) LMP_BASE_TEXT(major)
    return LMP_BASE_OF(ESP_IDF_VERSION_MAJOR);
#undef LMP_BASE_OF
#undef LMP_BASE_TEXT
  }

 private:
  SystemInfo info_;
  mutable char layout_[17] = "";
};

// ---------------------------------------------------------------- clock

class Clock : public IClock {
 public:
  uint32_t millis() const override { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
  uint64_t uptimeMs() const override { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }
  uint64_t micros() const override { return static_cast<uint64_t>(esp_timer_get_time()); }

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

// ---------------------------------------------------------------- rtos

class Mutex : public IMutex {
 public:
  Mutex() : handle_(xSemaphoreCreateMutex()) {}
  void lock() override {
    if (handle_) xSemaphoreTake(handle_, portMAX_DELAY);
  }
  void unlock() override {
    if (handle_) xSemaphoreGive(handle_);
  }

 private:
  SemaphoreHandle_t handle_;
};

class Semaphore : public ISemaphore {
 public:
  Semaphore() : handle_(xSemaphoreCreateBinary()) {}
  void give() override {
    if (handle_) xSemaphoreGive(handle_);
  }
  bool take(uint32_t timeoutMs) override {
    return handle_ && xSemaphoreTake(handle_, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
  }

 private:
  SemaphoreHandle_t handle_;
};

class Queue : public IQueue {
 public:
  Queue(size_t itemSize, size_t length)
      : handle_(xQueueCreate(static_cast<UBaseType_t>(length),
                             static_cast<UBaseType_t>(itemSize))) {}
  bool valid() const { return handle_ != nullptr; }
  bool send(const void* item, uint32_t timeoutMs) override {
    return handle_ && xQueueSend(handle_, item, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
  }
  bool receive(void* item, uint32_t timeoutMs) override {
    return handle_ && xQueueReceive(handle_, item, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
  }
  size_t waiting() const override {
    return handle_ ? static_cast<size_t>(uxQueueMessagesWaiting(handle_)) : 0;
  }

 private:
  QueueHandle_t handle_;
};

class Rtos : public IRtos {
 public:
  bool preemptive() const override { return true; }
  IMutex* createMutex() override { return new Mutex(); }
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

  Status createTask(const TaskConfig& cfg, TaskFn fn, void* arg) override {
    if (!fn) return Status::Invalid;
#if SOC_CPU_CORES_NUM > 1
    const BaseType_t core = cfg.core < 0 ? tskNO_AFFINITY : static_cast<BaseType_t>(cfg.core);
#else
    const BaseType_t core = cfg.core < 0 ? tskNO_AFFINITY : 0;
#endif
    const BaseType_t ok = xTaskCreatePinnedToCore(fn, cfg.name, cfg.stackBytes, arg, cfg.priority,
                                                  nullptr, core);
    return ok == pdPASS ? Status::Ok : Status::NoMem;
  }

  uintptr_t currentTaskId() const override {
    return reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
  }
  void delayMs(uint32_t ms) override { vTaskDelay(pdMS_TO_TICKS(ms)); }
  void yield() override { taskYIELD(); }
};

// ---------------------------------------------------------------- random

class Random : public IRandom {
 public:
  void bytes(uint8_t* out, size_t len) override { esp_fill_random(out, len); }
};

// ---------------------------------------------------------------- objects

struct Objects {
  System sys;
  Clock clock;
  Rtos rtos;
  Random rng;
#if LMP_ESP32_CONSOLE == 2
  UsbConsole serial;
#elif LMP_ESP32_CONSOLE == 1
  JtagConsole serial;
#else
  Serial0 serial;
#endif
  Gpio gpio;
  Fs fs;
  Kv kv;
  Pwm pwm;
  Pixels pixels;
  Adc adc;
  SpiBus spi{gpio};
#if LMP_FEATURE_NET
  Net net;
  HttpServer http;
  Ntp ntp;
#endif
#if LMP_FEATURE_HTTPCLIENT
  HttpClient httpClient;
#endif
#if LMP_FEATURE_MQTT
  Mqtt mqtt;
#endif
#if LMP_FEATURE_MQTT && LMP_FEATURE_CLOUD
  Mqtt cloudMqtt;
#endif
#if LMP_FEATURE_OTA
  Ota ota{&fs};
#endif
#if LMP_FEATURE_BUS
  I2c i2c0{0};
  I2c i2c1{1};
#endif
#if LMP_FEATURE_PROBE
  NetProbe probe;
#endif
#if LMP_FEATURE_NET && ((LMP_NET_LINKS) & LMP_NET_LINK_ETH)
  Eth eth;
#endif
};

Objects& objects() {
  static Objects instance;
  return instance;
}

void housekeeping() {
#if LMP_ESP32_CONSOLE == 2
  // The USB port takes the text a little at a time, at every round.
  objects().serial.pump();
#endif
}

#if LMP_TLS_EXTERNAL_RAM
// The Arduino core 2.0.x builds mbedTLS with its memory taken only from the internal RAM:
// two encrypted connections, about 16 + 16 KB each plus certificates and keys, do not fit in
// it on the S2. With a PSRAM all of it goes there, as CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC of
// ESP-IDF would do; the internal RAM only when the PSRAM is full.
void* tlsCalloc(size_t count, size_t size) {
  void* p = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void tlsFree(void* p) { heap_caps_free(p); }
#endif

}  // namespace esp32

void boot() {
  esp32::Objects& o = esp32::objects();
  o.serial.setMutex(o.rtos.createMutex());
#if LMP_TLS_EXTERNAL_RAM
  // Blocks taken before keep working: they are freed by the same heap.
  if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) {
    mbedtls_platform_set_calloc_free(esp32::tlsCalloc, esp32::tlsFree);
  }
#endif
}

const Board& board() {
  static Board b = [] {
    esp32::Objects& o = esp32::objects();
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
    x.pixels = &o.pixels;
    x.adc = &o.adc;
    x.spi = &o.spi;
    x.caps.add(Cap::Rtos);
    x.caps.add(Cap::Rng);
    x.caps.add(Cap::Fs);
    x.caps.add(Cap::Kv);
    x.caps.add(Cap::Pwm);
    x.caps.add(Cap::Adc);
    x.caps.add(Cap::Spi);
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
    x.caps.add(Cap::Tls);
#endif
#if LMP_FEATURE_MQTT
    x.mqtt = &o.mqtt;
    x.caps.add(Cap::Mqtt);
#endif
#if LMP_FEATURE_MQTT && LMP_FEATURE_CLOUD
    x.cloudMqtt = &o.cloudMqtt;
#endif
#if LMP_FEATURE_OTA
    x.ota = &o.ota;
    x.caps.add(Cap::Ota);
#endif
#if LMP_FEATURE_PROBE
    x.probe = &o.probe;
#endif
#if LMP_FEATURE_NET && ((LMP_NET_LINKS) & LMP_NET_LINK_ETH)
    x.cable = &o.eth;
    x.caps.add(Cap::Ethernet);
#endif
#if LMP_FEATURE_BUS
    x.i2c[0] = &o.i2c0;
    if (esp32::kI2cPorts > 1 && LMP_HAL_I2C_MAX > 1) x.i2c[1] = &o.i2c1;
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

#if defined(ARDUINO)
// The Arduino core confirms a new firmware by itself at every start, unless told to wait:
// the update service does it, after the firmware has run for a while.
extern "C" bool verifyRollbackLater() { return LMP_FEATURE_OTA != 0; }
#endif
