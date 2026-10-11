// LoopMaxPro HAL for the PC - firmware update and buses, in memory only.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "hal/api/ibus.h"
#include "hal/api/istorage.h"

namespace lmp::hal::native {

// Keeps the image it receives, so a test can look at it. Nothing is installed.
class Ota : public IOta {
 public:
  Status begin(OtaTarget target, size_t totalSize) override;
  Status write(const uint8_t* data, size_t len) override;
  Status end() override;
  void abort() override;
  bool inProgress() const override { return inProgress_; }
  size_t written() const override { return image_.size(); }

  bool pendingVerify() const override { return pendingVerify_; }
  Status markValid() override;
  bool canRollback() const override { return canRollback_; }
  Status rollback() override;

  // ---- hooks for the tests
  void reset();
  void failBegin(Status result) { beginResult_ = result; }
  void failEnd(Status result) { endResult_ = result; }
  // The flash gives an error after this many bytes.
  void failWriteAfter(size_t bytes) { failAfter_ = bytes; }
  // The firmware running is a new one, not confirmed yet.
  void setPendingVerify(bool pending) { pendingVerify_ = pending; }
  void setCanRollback(bool can) { canRollback_ = can; }

  // Last image that arrived whole and was accepted.
  const std::string& installed() const { return installed_; }
  OtaTarget installedTarget() const { return installedTarget_; }
  unsigned installs() const { return installs_; }
  unsigned aborts() const { return aborts_; }
  unsigned confirms() const { return confirms_; }
  unsigned rollbacks() const { return rollbacks_; }
  size_t declared() const { return declared_; }

 private:
  std::string image_;
  std::string installed_;
  OtaTarget target_ = OtaTarget::App;
  OtaTarget installedTarget_ = OtaTarget::App;
  size_t declared_ = 0;
  size_t failAfter_ = static_cast<size_t>(-1);
  unsigned installs_ = 0;
  unsigned aborts_ = 0;
  unsigned confirms_ = 0;
  unsigned rollbacks_ = 0;
  Status beginResult_ = Status::Ok;
  Status endResult_ = Status::Ok;
  bool inProgress_ = false;
  bool pendingVerify_ = false;
  bool canRollback_ = false;
};

// A bus with the devices a test puts on it. A device is a table of registers: the first
// byte written chooses the register, what follows is written there or read from there.
class I2c : public II2c {
 public:
  Status begin(uint8_t sda, uint8_t scl, uint32_t frequencyHz) override;
  void end() override { up_ = false; }
  bool probe(uint8_t address) override;
  Status write(uint8_t address, const uint8_t* data, size_t len, bool stop) override;
  Status read(uint8_t address, uint8_t* out, size_t len) override;
  Status writeRead(uint8_t address, const uint8_t* tx, size_t txLen, uint8_t* rx,
                   size_t rxLen) override;

  // ---- hooks for the tests
  void reset();
  void addDevice(uint8_t address);
  void removeDevice(uint8_t address);
  void setRegister(uint8_t address, uint8_t reg, uint8_t value);
  uint8_t getRegister(uint8_t address, uint8_t reg) const;
  void failNext(Status begin) { beginResult_ = begin; }

  bool up() const { return up_; }
  uint8_t sda() const { return sda_; }
  uint8_t scl() const { return scl_; }
  uint32_t frequency() const { return frequency_; }
  unsigned probes() const { return probes_; }

 private:
  struct Device {
    uint8_t reg[256] = {};
    uint8_t pointer = 0;
  };

  std::map<uint8_t, Device> devices_;
  uint8_t sda_ = 0;
  uint8_t scl_ = 0;
  uint32_t frequency_ = 0;
  unsigned probes_ = 0;
  bool up_ = false;
  Status beginResult_ = Status::Ok;
};

}  // namespace lmp::hal::native
