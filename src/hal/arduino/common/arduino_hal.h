// LoopMaxPro HAL - parts that work on any Arduino core.
#pragma once
#include <Arduino.h>

#include "hal/api/board.h"

namespace lmp::hal::arduino {

class Serial0 : public ISerial {
 public:
  explicit Serial0(IMutex* mutex = nullptr) : mutex_(mutex) {}
  void setMutex(IMutex* mutex) { mutex_ = mutex; }

  Status begin(uint32_t baud) override;
  bool ready() const override { return ready_; }
  size_t write(const uint8_t* data, size_t len) override;
  size_t read(uint8_t* out, size_t cap) override;
  size_t available() const override;
  void flush() override;

 private:
  IMutex* mutex_;
  bool ready_ = false;
};

class Gpio : public IGpio {
 public:
  bool isValid(uint8_t pin) const override;
  bool canOutput(uint8_t pin) const override;
  Status setMode(uint8_t pin, PinMode mode) override;
  Status write(uint8_t pin, PinLevel level) override;
  Status read(uint8_t pin, PinLevel& level) const override;
  Status attachInterrupt(uint8_t pin, Edge edge, PinHandler handler) override;
  Status detachInterrupt(uint8_t pin) override;
};

// Defined by each MCU family: which pins exist and which can drive an output.
bool pinIsValid(uint8_t pin);
bool pinCanOutput(uint8_t pin);

}  // namespace lmp::hal::arduino
