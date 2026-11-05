// LoopMaxPro HAL - ESP32 family: console, pins, storage, PWM, ADC.
// One implementation for both frameworks: it uses the ESP-IDF drivers, which the Arduino core
// also carries (ESP-IDF 4.4 in the core 2.0.x, 5 and 6 in the ESP-IDF builds). What changed
// between the versions is chosen with ESP_IDF_VERSION.
#pragma once
#include <esp_idf_version.h>
#include <sdkconfig.h>
#include <soc/soc_caps.h>

#if ESP_IDF_VERSION_MAJOR >= 5
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_oneshot.h>
#else
#include <driver/adc.h>
#include <esp_adc_cal.h>
#endif

#include "hal/api/board.h"
#include "lmp_config.h"

// Where the console is. The board says it: with the Arduino core through the flags of the
// USB console, with ESP-IDF through the console of sdkconfig.
//   0  the first UART, wired to the USB bridge of the board
//   1  the serial and debug unit of the chip (USB-JTAG), with ESP-IDF: C3, S3, C6
//   2  the USB port of the chip through the Arduino core: the USB device of the S2, the
//      serial and debug unit of S3 and C3. The driver of that unit in ESP-IDF 4.4, the one
//      inside the core 2.0.x, sends nothing until someone else writes to the unit.
#if defined(ARDUINO) && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
#define LMP_ESP32_CONSOLE 2
#elif SOC_USB_SERIAL_JTAG_SUPPORTED && defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
#define LMP_ESP32_CONSOLE 1
#else
#define LMP_ESP32_CONSOLE 0
#endif

namespace lmp::hal::esp32 {

// Console on the first UART, the one wired to the USB bridge of the board.
class Serial0 : public ISerial {
 public:
  void setMutex(IMutex* mutex) { mutex_ = mutex; }

  Status begin(uint32_t baud) override;
  bool ready() const override { return ready_; }
  size_t write(const uint8_t* data, size_t len) override;
  size_t read(uint8_t* out, size_t cap) override;
  size_t available() const override;
  void flush() override;

 private:
  IMutex* mutex_ = nullptr;
  bool ready_ = false;
};

#if LMP_ESP32_CONSOLE == 1
// Console on the serial and debug unit of the chip. With the cable in and nobody reading, a
// write waits at most LMP_SERIAL_TX_TIMEOUT_MS: what does not fit is dropped.
class JtagConsole : public ISerial {
 public:
  void setMutex(IMutex* mutex) { mutex_ = mutex; }

  Status begin(uint32_t baud) override;
  bool ready() const override { return ready_; }
  size_t write(const uint8_t* data, size_t len) override;
  size_t read(uint8_t* out, size_t cap) override;
  size_t available() const override;
  void flush() override {}

 private:
  // The driver cannot tell how much arrived without taking it: what was taken to count
  // waits here.
  size_t fill() const;

  IMutex* mutex_ = nullptr;
  bool ready_ = false;
  mutable uint8_t held_[64] = {};
  mutable size_t heldCount_ = 0;
};
#endif

class Gpio : public IGpio {
 public:
  bool isValid(uint8_t pin) const override;
  bool canOutput(uint8_t pin) const override;
  Status setMode(uint8_t pin, PinMode mode) override;
  Status write(uint8_t pin, PinLevel level) override;
  Status read(uint8_t pin, PinLevel& level) const override;
  Status attachInterrupt(uint8_t pin, Edge edge, PinHandler handler) override;
  Status detachInterrupt(uint8_t pin) override;

 private:
  bool isrReady_ = false;
};

// LittleFS on the partition named "spiffs". The same format the Arduino core uses: the files
// of a board stay when it moves from a framework to the other.
class Fs : public IFs {
 public:
  Status mount(bool formatOnFail) override;
  void unmount() override;
  bool mounted() const override { return mounted_; }
  Status info(FsInfo& out) const override;
  bool replacesWhole() const override { return true; }   // LittleFS writes on copies
  IFile* open(const char* path, OpenMode mode) override;
  bool exists(const char* path) const override;
  Status remove(const char* path) override;
  Status rename(const char* from, const char* to) override;
  Status mkdir(const char* path) override;
  Status list(const char* dir, DirVisitor visitor) override;

 private:
  bool mounted_ = false;
};

// Non volatile storage of the ESP32: one blob for each key.
class Kv : public IKv {
 public:
  Status begin() override;
  Status get(const char* ns, const char* key, void* out, size_t cap, size_t& len) override;
  Status set(const char* ns, const char* key, const void* data, size_t len) override;
  Status remove(const char* ns, const char* key) override;
  Status eraseNamespace(const char* ns) override;
  Status commit() override { return Status::Ok; }  // every write is committed at once
 private:
  bool ready_ = false;
};

// A timer gives frequency and resolution, a channel gives the duty of one pin.
// Pins with the same frequency and resolution share a timer.
class Pwm : public IPwm {
 public:
  Pwm();
  bool supports(uint8_t pin) const override;
  Status setup(uint8_t pin, uint32_t frequencyHz, uint8_t resolutionBits) override;
  Status write(uint8_t pin, uint32_t duty) override;
  Status release(uint8_t pin) override;

 private:
  static constexpr uint8_t kChannels = SOC_LEDC_CHANNEL_NUM < 8 ? SOC_LEDC_CHANNEL_NUM : 8;
  static constexpr uint8_t kTimers = 4;   // every chip of the family has four
  static constexpr uint8_t kFree = 0xFF;

  struct Timer {
    uint32_t frequencyHz = 0;
    uint8_t resolutionBits = 0;
    uint8_t users = 0;
  };
  struct Channel {
    uint8_t pin = kFree;
    uint8_t timer = 0;
  };

  int channelOf(uint8_t pin) const;
  int timerFor(uint32_t frequencyHz, uint8_t resolutionBits) const;

  Timer timers_[kTimers];
  Channel channels_[kChannels];
};

// The last words and the guard of the main task (esp32_guard.cpp).
void lastWordsBegin();
void lastWordsKeep(const char* line);
void lastWordsWhy(const char* why);
const LastWords* lastWordsPrevious();
void stallGuard(uint32_t seconds);
void stallFeed();

// Addressable LEDs on the RMT of the chip: two lines at most, the two transmit channels every
// chip of the family has. Each line keeps its own RMT channel while in use.
class Pixels : public IPixels {
 public:
  static constexpr uint8_t kLines = 2;

  bool supports(uint8_t pin) const override;
  Status begin(uint8_t pin, uint16_t count) override;
  Status show(uint8_t pin, const uint8_t* grb, size_t len) override;
  Status end(uint8_t pin) override;

 private:
  struct Line {
    uint8_t pin = 0xFF;
    uint16_t count = 0;
    void* channel = nullptr;   // RMT channel (ESP-IDF 5 and later)
    void* encoder = nullptr;   // bytes encoder (ESP-IDF 5 and later)
    void* items = nullptr;     // RMT items of a frame (ESP-IDF 4.4)
  };
  Line* find(uint8_t pin);

  Line lines_[kLines];
};

// First converter of the chip: the second one, where present, is taken by the radio.
class Adc : public IAdc {
 public:
  bool supports(uint8_t pin) const override;
  uint8_t resolutionBits() const override;
  Status readRaw(uint8_t pin, uint32_t& value) override;
  Status readMillivolts(uint8_t pin, uint32_t& millivolts) override;

 private:
#if ESP_IDF_VERSION_MAJOR >= 5
  static constexpr uint8_t kChannels = SOC_ADC_MAX_CHANNEL_NUM;
  struct Channel {
    bool ready = false;
    adc_cali_handle_t calibration = nullptr;
  };
  Status prepare(uint8_t pin, adc_channel_t& channel);
  adc_oneshot_unit_handle_t unit_ = nullptr;
#else
  static constexpr uint8_t kChannels = ADC1_CHANNEL_MAX;
  struct Channel {
    bool ready = false;
    esp_adc_cal_characteristics_t calibration = {};
  };
  Status prepare(uint8_t pin, int& channel);
  bool widthSet_ = false;
#endif
  Channel channels_[kChannels];
};

}  // namespace lmp::hal::esp32
