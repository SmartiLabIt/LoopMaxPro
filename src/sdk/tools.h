// LoopMaxPro SDK - small tools for the logic of a module: something to do later, a button,
// a value read again and again.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "base/status.h"
#include "hal/api/types.h"

namespace lmp {

class IGpio;

namespace sdk {

// Something to do a while after it was asked. Asked again, the wait starts again: many
// changes in a row become one action.
class Debounced {
 public:
  void arm(uint32_t nowMs, uint32_t delayMs) {
    armed_ = true;
    atMs_ = nowMs + delayMs;
  }
  void cancel() { armed_ = false; }
  bool armed() const { return armed_; }
  // True once, when the time has come.
  bool due(uint32_t nowMs) {
    if (!armed_ || static_cast<int32_t>(nowMs - atMs_) < 0) return false;
    armed_ = false;
    return true;
  }

 private:
  uint32_t atMs_ = 0;
  bool armed_ = false;
};

enum class ButtonEvent : uint8_t {
  None,
  Pressed,     // held for longer than the noise of the contacts
  Released,    // let go before the long press
  LongPress,   // still held after the long time: told once
  LongReleased // let go after a long press
};

// A button on a pin, read at every round: no interrupt, nothing waits.
class Button {
 public:
  static constexpr uint32_t kNoiseMs = 40;

  // activeLow: the button takes the pin to ground. owner must stay valid.
  Status begin(IGpio& gpio, uint8_t pin, bool activeLow, const char* owner,
               uint32_t longPressMs = 2000);
  void end();

  ButtonEvent poll(uint32_t nowMs);
  bool held() const { return held_; }
  // Milliseconds the button has been held for. 0 when it is not.
  uint32_t heldMs(uint32_t nowMs) const { return held_ ? nowMs - sinceMs_ : 0; }

 private:
  IGpio* gpio_ = nullptr;
  const char* owner_ = "";
  uint32_t longPressMs_ = 2000;
  uint32_t sinceMs_ = 0;      // when the level seen now began
  uint8_t pin_ = 0;
  bool activeLow_ = true;
  bool raw_ = false;          // what the pin says, noise included
  bool held_ = false;         // what the button is, noise removed
  bool longTold_ = false;
};

// A value read again and again: the readings are averaged, and a change is told only when
// it is large enough to matter.
class Poller {
 public:
  // window: readings in the average, from 1 to 16. threshold: smallest change worth telling.
  void begin(uint32_t everyMs, uint8_t window, float threshold);

  // True when it is time for a new reading.
  bool due(uint32_t nowMs);
  // Takes a reading. True when the average moved more than the threshold from the last
  // value told.
  bool add(float sample);
  // A reading that failed. After three in a row the value is no longer good.
  void fail();

  float value() const { return average_; }
  bool valid() const { return count_ > 0 && failures_ < 3; }
  uint8_t failures() const { return failures_; }

 private:
  static constexpr uint8_t kWindow = 16;

  float samples_[kWindow] = {};
  float average_ = 0;
  float told_ = 0;
  float threshold_ = 0;
  uint32_t everyMs_ = 1000;
  uint32_t lastMs_ = 0;
  uint8_t window_ = 1;
  uint8_t count_ = 0;
  uint8_t next_ = 0;
  uint8_t failures_ = 0;
  bool started_ = false;
  bool toldOnce_ = false;
};

}  // namespace sdk
}  // namespace lmp
