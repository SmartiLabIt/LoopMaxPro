// LoopMaxPro HAL - buses for modules that talk to external hardware.
#pragma once
#include "hal/api/types.h"

namespace lmp::hal {

class II2c {
 public:
  virtual ~II2c() = default;
  virtual Status begin(uint8_t sda, uint8_t scl, uint32_t frequencyHz) = 0;
  virtual void end() = 0;
  virtual bool probe(uint8_t address) = 0;
  virtual Status write(uint8_t address, const uint8_t* data, size_t len, bool stop = true) = 0;
  virtual Status read(uint8_t address, uint8_t* out, size_t len) = 0;
  // Write then read with a repeated start: the usual register access.
  virtual Status writeRead(uint8_t address, const uint8_t* tx, size_t txLen,
                           uint8_t* rx, size_t rxLen) = 0;
};

struct SpiConfig {
  uint32_t frequencyHz = 1000000;
  uint8_t mode = 0;        // 0..3
  bool msbFirst = true;
};

// Chip select handled by the caller: the transfer does not touch any pin.
constexpr uint8_t kSpiNoCs = 0xFF;
// A bus without the line that comes back from the chips: a screen only listens.
constexpr uint8_t kSpiNoPin = 0xFF;

class ISpi {
 public:
  virtual ~ISpi() = default;
  // miso: kSpiNoPin for a bus nobody answers on.
  virtual Status begin(uint8_t sck, uint8_t miso, uint8_t mosi) = 0;
  virtual void end() = 0;
  // Drives the chip select pin for the duration of the transfer. tx or rx may be null.
  virtual Status transfer(uint8_t cs, const SpiConfig& cfg, const uint8_t* tx, uint8_t* rx,
                          size_t len) = 0;
};

struct UartConfig {
  uint32_t baud = 9600;
  uint8_t dataBits = 8;
  char parity = 'N';       // 'N', 'E', 'O'
  uint8_t stopBits = 1;
  int16_t directionPin = -1;  // RS485 driver enable, -1 when unused
};

class IUart {
 public:
  virtual ~IUart() = default;
  virtual Status begin(uint8_t tx, uint8_t rx, const UartConfig& cfg) = 0;
  virtual void end() = 0;
  virtual size_t write(const uint8_t* data, size_t len) = 0;
  virtual size_t read(uint8_t* out, size_t cap) = 0;
  virtual size_t available() const = 0;
  virtual void flush() = 0;
};

class IOneWire {
 public:
  virtual ~IOneWire() = default;
  virtual Status begin(uint8_t pin) = 0;
  virtual bool reset() = 0;  // true when a device answers with a presence pulse
  virtual void writeByte(uint8_t value) = 0;
  virtual uint8_t readByte() = 0;
  // Enumerates devices. Returns false when there are no more.
  virtual void searchReset() = 0;
  virtual bool searchNext(uint8_t rom[8]) = 0;
};

}  // namespace lmp::hal
