// LoopMaxPro HAL - ESP32 family: SPI bus.
// Written with the drivers of the chip maker, which are part of both the Arduino and the
// ESP-IDF builds: the two implementations of the HAL share this file.
#pragma once
#include <driver/spi_master.h>

#include "hal/api/ibus.h"
#include "hal/api/igpio.h"

namespace lmp::hal::esp32 {

class SpiBus : public ISpi {
 public:
  explicit SpiBus(IGpio& gpio) : gpio_(gpio) {}

  Status begin(uint8_t sck, uint8_t miso, uint8_t mosi) override;
  void end() override;
  // The chip select pin must already be an output.
  Status transfer(uint8_t cs, const SpiConfig& cfg, const uint8_t* tx, uint8_t* rx,
                  size_t len) override;

 private:
  // One entry for each combination of speed and mode in use on the bus.
  static constexpr size_t kProfiles = 3;
  struct Profile {
    spi_device_handle_t handle = nullptr;
    uint32_t frequencyHz = 0;
    uint8_t mode = 0;
    bool msbFirst = true;
  };

  spi_device_handle_t profileFor(const SpiConfig& cfg);

  IGpio& gpio_;
  Profile profiles_[kProfiles];
  bool ready_ = false;
};

}  // namespace lmp::hal::esp32
