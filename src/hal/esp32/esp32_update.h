// LoopMaxPro HAL - ESP32 family: firmware update and I2C bus.
// Written with the components of the chip maker, which are part of both the Arduino and the
// ESP-IDF builds: the two implementations of the HAL share this file.
#pragma once
#include <soc/soc_caps.h>

#include "hal/api/ibus.h"
#include "hal/api/istorage.h"
#include "lmp_config.h"

namespace lmp::hal::esp32 {

// I2C ports of the chip that a program can use. Some chips have one more, for the core
// that runs while the rest sleeps: it is not counted.
#if defined(SOC_HP_I2C_NUM)
constexpr size_t kI2cPorts = SOC_HP_I2C_NUM;
#else
constexpr size_t kI2cPorts = SOC_I2C_NUM;
#endif

#if LMP_FEATURE_OTA

// The firmware goes to the copy that is not running, the filesystem to its own partition.
class Ota : public IOta {
 public:
  // fs: unmounted before its partition is written. It can be null.
  explicit Ota(IFs* fs) : fs_(fs) {}

  Status begin(OtaTarget target, size_t totalSize) override;
  Status write(const uint8_t* data, size_t len) override;
  Status end() override;
  void abort() override;
  bool inProgress() const override { return inProgress_; }
  size_t written() const override { return written_; }

  bool pendingVerify() const override;
  Status markValid() override;
  bool canRollback() const override;
  Status rollback() override;

 private:
  IFs* fs_;
  const void* partition_ = nullptr;   // esp_partition_t
  uint32_t handle_ = 0;               // esp_ota_handle_t
  OtaTarget target_ = OtaTarget::App;
  size_t written_ = 0;
  size_t erased_ = 0;                 // filesystem: the partition is erased up to here
  bool inProgress_ = false;
};

#endif  // LMP_FEATURE_OTA

#if LMP_FEATURE_BUS

class I2c : public II2c {
 public:
  explicit I2c(uint8_t port) : port_(port) {}

  Status begin(uint8_t sda, uint8_t scl, uint32_t frequencyHz) override;
  void end() override;
  bool probe(uint8_t address) override;
  Status write(uint8_t address, const uint8_t* data, size_t len, bool stop) override;
  Status read(uint8_t address, uint8_t* out, size_t len) override;
  Status writeRead(uint8_t address, const uint8_t* tx, size_t txLen, uint8_t* rx,
                   size_t rxLen) override;

 private:
  static constexpr size_t kDevices = 8;

  // The newer driver wants a handle for each chip: the last ones used are kept.
  void* device(uint8_t address);

  uint8_t port_;
  uint32_t frequency_ = 100000;
  bool up_ = false;
  void* bus_ = nullptr;
  void* devices_[kDevices] = {};
  uint8_t addresses_[kDevices] = {};
  uint8_t next_ = 0;
};

#endif  // LMP_FEATURE_BUS

}  // namespace lmp::hal::esp32
