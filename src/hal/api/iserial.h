// LoopMaxPro HAL - console serial port. Implementations must be safe across tasks.
#pragma once
#include <string.h>

#include "hal/api/types.h"

namespace lmp::hal {

class ISerial {
 public:
  virtual ~ISerial() = default;

  virtual Status begin(uint32_t baud) = 0;
  virtual bool ready() const = 0;
  virtual size_t write(const uint8_t* data, size_t len) = 0;
  virtual size_t read(uint8_t* out, size_t cap) = 0;
  virtual size_t available() const = 0;
  virtual void flush() = 0;

  size_t print(const char* text) {
    return text ? write(reinterpret_cast<const uint8_t*>(text), strlen(text)) : 0;
  }
  size_t println(const char* text) { return print(text) + print("\r\n"); }
};

}  // namespace lmp::hal
