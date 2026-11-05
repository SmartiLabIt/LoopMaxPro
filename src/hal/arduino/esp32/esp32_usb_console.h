// LoopMaxPro HAL - console on the USB port of the chip, through the Arduino core: the full
// USB device of the ESP32-S2, the serial and debug unit of S3 and C3.
// The core keeps a few bytes and, with a host that is connected but does not read, its write
// waits. Here the text goes to a buffer and reaches the port a little at
// every round of the system: a write never waits. With nobody connected the last lines stay
// in the buffer and leave when a program opens the port.
#pragma once
#include <Arduino.h>

#include "hal/api/board.h"
#include "lmp_config.h"

// The console is the USB port of the chip, whichever unit the core drives.
#if ARDUINO_USB_CDC_ON_BOOT
#define LMP_CONSOLE_USB_DEVICE 1
#else
#define LMP_CONSOLE_USB_DEVICE 0
#endif

#if LMP_CONSOLE_USB_DEVICE

namespace lmp::hal::esp32 {

class UsbConsole : public ISerial {
 public:
  void setMutex(IMutex* mutex) { mutex_ = mutex; }

  Status begin(uint32_t baud) override;
  bool ready() const override { return ready_; }
  // Never waits. When the buffer is full the oldest text makes room for the new.
  size_t write(const uint8_t* data, size_t len) override;
  size_t read(uint8_t* out, size_t cap) override;
  size_t available() const override;
  // Waits for the buffer to empty, for a short time: used before a restart.
  void flush() override;

  // Moves to the port what it has room for. Called at every round of the system.
  void pump();

 private:
  void pumpLocked();

  IMutex* mutex_ = nullptr;
  // Taken at begin(), from the external RAM when there is one.
  uint8_t* buffer_ = nullptr;
  size_t capacity_ = 0;
  size_t head_ = 0;
  size_t used_ = 0;
  bool ready_ = false;
};

}  // namespace lmp::hal::esp32

#endif  // LMP_CONSOLE_USB_DEVICE
