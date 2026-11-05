#include "hal/arduino/common/arduino_hal.h"

#if __has_include(<soc/soc_caps.h>)
#include <soc/soc_caps.h>
#endif

namespace lmp::hal::arduino {

// ---------------------------------------------------------------- serial

Status Serial0::begin(uint32_t baud) {
  ::Serial.begin(baud);
  ready_ = true;
  return Status::Ok;
}

size_t Serial0::write(const uint8_t* data, size_t len) {
  if (!ready_ || !data) return 0;
  LockGuard guard(mutex_);
  return ::Serial.write(data, len);
}

size_t Serial0::read(uint8_t* out, size_t cap) {
  if (!ready_ || !out) return 0;
  LockGuard guard(mutex_);
  size_t n = 0;
  while (n < cap && ::Serial.available() > 0) {
    const int c = ::Serial.read();
    if (c < 0) break;
    out[n++] = static_cast<uint8_t>(c);
  }
  return n;
}

size_t Serial0::available() const {
  if (!ready_) return 0;
  const int n = ::Serial.available();
  return n > 0 ? static_cast<size_t>(n) : 0;
}

void Serial0::flush() {
  if (ready_) ::Serial.flush();
}

// ---------------------------------------------------------------- gpio

namespace {

// One handler for each pin of the chip: memory is scarce on the small ones.
#if defined(SOC_GPIO_PIN_COUNT)
constexpr uint8_t kMaxPins = SOC_GPIO_PIN_COUNT;
#else
constexpr uint8_t kMaxPins = NUM_DIGITAL_PINS;
#endif
PinHandler g_handlers[kMaxPins];

void IRAM_ATTR dispatch(void* arg) {
  const uint8_t pin = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(arg));
  if (pin < kMaxPins && g_handlers[pin]) g_handlers[pin](pin);
}

}  // namespace

bool Gpio::isValid(uint8_t pin) const { return pinIsValid(pin); }
bool Gpio::canOutput(uint8_t pin) const { return pinCanOutput(pin); }

Status Gpio::setMode(uint8_t pin, PinMode mode) {
  if (!pinIsValid(pin)) return Status::Invalid;
  switch (mode) {
    case PinMode::Input:        ::pinMode(pin, INPUT); break;
    case PinMode::InputPullUp:  ::pinMode(pin, INPUT_PULLUP); break;
#ifdef INPUT_PULLDOWN
    case PinMode::InputPullDown: ::pinMode(pin, INPUT_PULLDOWN); break;
#endif
    case PinMode::Output:
      if (!pinCanOutput(pin)) return Status::Unsupported;
      ::pinMode(pin, OUTPUT);
      break;
#ifdef OUTPUT_OPEN_DRAIN
    case PinMode::OutputOpenDrain:
      if (!pinCanOutput(pin)) return Status::Unsupported;
      ::pinMode(pin, OUTPUT_OPEN_DRAIN);
      break;
#endif
#ifdef ANALOG
    case PinMode::Analog: ::pinMode(pin, ANALOG); break;
#endif
    default: return Status::Unsupported;
  }
  return Status::Ok;
}

Status Gpio::write(uint8_t pin, PinLevel level) {
  if (!pinCanOutput(pin)) return Status::Invalid;
  ::digitalWrite(pin, level == PinLevel::High ? HIGH : LOW);
  return Status::Ok;
}

Status Gpio::read(uint8_t pin, PinLevel& level) const {
  if (!pinIsValid(pin)) return Status::Invalid;
  level = ::digitalRead(pin) ? PinLevel::High : PinLevel::Low;
  return Status::Ok;
}

Status Gpio::attachInterrupt(uint8_t pin, Edge edge, PinHandler handler) {
  if (!pinIsValid(pin) || pin >= kMaxPins || !handler) return Status::Invalid;
  g_handlers[pin] = handler;
  const int mode = edge == Edge::Rising ? RISING : (edge == Edge::Falling ? FALLING : CHANGE);
  ::attachInterruptArg(pin, dispatch, reinterpret_cast<void*>(static_cast<uintptr_t>(pin)), mode);
  return Status::Ok;
}

Status Gpio::detachInterrupt(uint8_t pin) {
  if (!pinIsValid(pin) || pin >= kMaxPins) return Status::Invalid;
  ::detachInterrupt(pin);
  g_handlers[pin].reset();
  return Status::Ok;
}

}  // namespace lmp::hal::arduino
