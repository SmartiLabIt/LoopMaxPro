// LoopMaxPro HAL - digital pins, PWM, ADC.
#pragma once
#include "hal/api/types.h"

namespace lmp::hal {

using PinHandler = Delegate<void(uint8_t pin)>;

class IGpio {
 public:
  virtual ~IGpio() = default;

  virtual bool isValid(uint8_t pin) const = 0;
  virtual bool canOutput(uint8_t pin) const = 0;
  virtual Status setMode(uint8_t pin, PinMode mode) = 0;
  virtual Status write(uint8_t pin, PinLevel level) = 0;
  virtual Status read(uint8_t pin, PinLevel& level) const = 0;
  // The handler runs in interrupt context: keep it short.
  virtual Status attachInterrupt(uint8_t pin, Edge edge, PinHandler handler) = 0;
  virtual Status detachInterrupt(uint8_t pin) = 0;
};

class IPwm {
 public:
  virtual ~IPwm() = default;
  virtual bool supports(uint8_t pin) const = 0;
  virtual Status setup(uint8_t pin, uint32_t frequencyHz, uint8_t resolutionBits) = 0;
  virtual Status write(uint8_t pin, uint32_t duty) = 0;
  virtual Status release(uint8_t pin) = 0;
};

// Addressable LEDs, WS2812 and alike ("NeoPixel"): a line of pixels on one pin.
class IPixels {
 public:
  virtual ~IPixels() = default;
  virtual bool supports(uint8_t pin) const = 0;
  // A line of count pixels on the pin. NoMem when the board has no line left.
  virtual Status begin(uint8_t pin, uint16_t count) = 0;
  // grb: 3 bytes a pixel, green, red, blue, in the order of the chips. Returns once sent.
  virtual Status show(uint8_t pin, const uint8_t* grb, size_t len) = 0;
  virtual Status end(uint8_t pin) = 0;
};

class IAdc {
 public:
  virtual ~IAdc() = default;
  virtual bool supports(uint8_t pin) const = 0;
  virtual uint8_t resolutionBits() const = 0;
  virtual Status readRaw(uint8_t pin, uint32_t& value) = 0;
  virtual Status readMillivolts(uint8_t pin, uint32_t& millivolts) = 0;
};

}  // namespace lmp::hal
