// LoopMaxPro SDK - what a module talks to: a chip on a bus, a service on the network.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "base/fixed_string.h"
#include "base/status.h"
#include "hal/api/ibus.h"
#include "lmp_config.h"

namespace lmp {

class IHttpClientService;
struct HttpAnswer;

namespace sdk {

// A chip on an I2C bus, read and written by registers. Errors are counted: a chip that
// stops answering is known as gone, and found again when it answers.
class I2cDevice {
 public:
  explicit I2cDevice(uint8_t address) : address_(address) {}

  // NotFound when nobody answers at the address.
  Status attach(hal::II2c* bus);
  bool attached() const { return bus_ != nullptr; }
  // False after three transfers in a row without an answer.
  bool present() const { return bus_ && failures_ < 3; }
  uint8_t address() const { return address_; }
  uint32_t errors() const { return errors_; }

  Status read(uint8_t reg, uint8_t* out, size_t len);
  Status write(uint8_t reg, const uint8_t* data, size_t len);
  Status read8(uint8_t reg, uint8_t& out);
  // The first byte is the high one, as most chips send it.
  Status read16(uint8_t reg, uint16_t& out);
  Status write8(uint8_t reg, uint8_t value);
  // A command without a register: some chips want just that.
  Status command(uint8_t value);

 private:
  Status note(Status s);

  hal::II2c* bus_ = nullptr;
  uint32_t errors_ = 0;
  uint8_t address_;
  uint8_t failures_ = 0;
};

#if LMP_FEATURE_HTTPCLIENT

// A service on the network asked again and again: the last answer is kept, so the module
// always has something to show, and knows how old it is.
class OnlineSource {
 public:
  // The buffer holds the answer and must live as long as the source. url is copied.
  Status begin(IHttpClientService* http, const char* url, uint32_t everySec, char* buffer,
               size_t capacity);
  // Application task, at every round. True once when a new answer has arrived.
  bool tick(uint32_t nowMs);
  // Asks now, without waiting for its time.
  void refresh() { forced_ = true; }

  // The last good answer. Empty before the first one.
  const char* body() const { return buffer_ && length_ ? buffer_ : ""; }
  size_t length() const { return length_; }
  bool fresh() const { return good_; }
  // Seconds since the last good answer.
  uint32_t ageSec(uint32_t nowMs) const { return good_ ? (nowMs - gotMs_) / 1000 : 0; }
  int lastStatus() const { return status_; }
  uint32_t failures() const { return failures_; }

 private:
  void onAnswer(const HttpAnswer& answer);

  IHttpClientService* http_ = nullptr;
  FixedString<160> url_;
  char* buffer_ = nullptr;
  size_t capacity_ = 0;
  size_t length_ = 0;
  uint32_t everyMs_ = 0;
  uint32_t askedMs_ = 0;
  uint32_t gotMs_ = 0;
  uint32_t nowMs_ = 0;
  uint32_t failures_ = 0;
  int status_ = 0;
  bool asked_ = false;       // at least once
  bool waiting_ = false;     // a request is travelling
  bool forced_ = false;
  bool good_ = false;
  bool news_ = false;
  // The request writes here: the answer of before stays readable until the new one is whole.
  char* incoming_ = nullptr;
};

#endif  // LMP_FEATURE_HTTPCLIENT

}  // namespace sdk
}  // namespace lmp
