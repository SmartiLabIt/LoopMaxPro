// LoopMaxPro - buses for the hardware outside the board.
// A bus is shared: several chips sit on the same two wires, and several parts of the
// system can use it. The first that opens it chooses pins and speed; who comes later joins
// it, and is refused when it asks for other pins.
#pragma once
#include "lmp_config.h"

#if LMP_FEATURE_BUS

#include "hal/api/ibus.h"
#include "kernel/kernel.h"

namespace lmp {

class IGpio;

// First and last address a chip can have on an I2C bus.
constexpr uint8_t kI2cFirst = 0x08;
constexpr uint8_t kI2cLast = 0x77;

class IBus {
 public:
  static constexpr ServiceId kId = ServiceId::Bus;
  virtual ~IBus() = default;

  virtual size_t i2cCount() const = 0;
  // Opens a bus, or joins it when it is open on the same pins. owner must stay valid.
  // Busy when the bus is open on other pins, or a pin belongs to someone else.
  virtual Status openI2c(uint8_t bus, uint8_t sda, uint8_t scl, uint32_t hz,
                         const char* owner) = 0;
  // The bus closes when the last one that opened it leaves.
  virtual Status closeI2c(uint8_t bus, const char* owner) = 0;
  // The bus, or null when it is not open.
  virtual hal::II2c* i2c(uint8_t bus) = 0;

  // The SPI bus of the board, shared as the I2C ones: who opens it first chooses the pins,
  // who comes later joins it, and is refused on other pins. miso: -1 for a bus nobody answers
  // on (a screen). Each chip has its own select pin, which its owner claims and gives to
  // hal::ISpi::transfer. Unsupported where the board has no SPI.
  virtual Status openSpi(uint8_t sck, int16_t miso, uint8_t mosi, const char* owner) = 0;
  virtual Status closeSpi(const char* owner) = 0;
  virtual hal::ISpi* spi() = 0;
};

class BusService : public Service, public IBus {
 public:
  static constexpr size_t kUsers = 4;

  const ServiceInfo& info() const override;
  Status start(Kernel& kernel) override;
  void stop() override;
  void tick(uint32_t nowMs) override;
  void describe(JsonWriter& w) const override;
  void commands(CommandSet& set) override;
  void* api() override { return static_cast<IBus*>(this); }

  size_t i2cCount() const override { return count_; }
  Status openI2c(uint8_t bus, uint8_t sda, uint8_t scl, uint32_t hz, const char* owner) override;
  Status closeI2c(uint8_t bus, const char* owner) override;
  hal::II2c* i2c(uint8_t bus) override;
  Status openSpi(uint8_t sck, int16_t miso, uint8_t mosi, const char* owner) override;
  Status closeSpi(const char* owner) override;
  hal::ISpi* spi() override { return spiOpen_ ? spi_ : nullptr; }

 private:
  struct Bus {
    hal::II2c* hal = nullptr;
    const char* users[kUsers] = {};
    uint8_t userCount = 0;
    uint8_t sda = 0;
    uint8_t scl = 0;
    uint32_t hz = 0;
    bool open = false;
    const char* pinOwner = "";   // name the pins are reserved with
  };
  enum class Scan : uint8_t { Idle, Running, Done };

  void endScan(Status outcome);

  Kernel* kernel_ = nullptr;
  IGpio* pins_ = nullptr;
  LogSource src_ = kLogSys;
  Bus buses_[LMP_HAL_I2C_MAX];
  size_t count_ = 0;

  void releaseSpiPins();

  hal::ISpi* spi_ = nullptr;
  const char* spiUsers_[kUsers] = {};
  uint8_t spiUserCount_ = 0;
  uint8_t spiSck_ = 0, spiMosi_ = 0;
  int16_t spiMiso_ = -1;
  bool spiOpen_ = false;

  // One scan at a time: a few addresses for each round of the system.
  Scan scan_ = Scan::Idle;
  uint8_t scanBus_ = 0;
  uint8_t scanNext_ = 0;
  uint32_t scanRef_ = 0;
  uint8_t found_[16];
  uint8_t foundCount_ = 0;
  bool foundMore_ = false;       // more chips than the list holds
};

}  // namespace lmp

#endif  // LMP_FEATURE_BUS
