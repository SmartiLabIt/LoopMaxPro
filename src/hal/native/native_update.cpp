#include "hal/native/native_update.h"

namespace lmp::hal::native {

// ---------------------------------------------------------------- firmware update

void Ota::reset() {
  image_.clear();
  installed_.clear();
  target_ = installedTarget_ = OtaTarget::App;
  declared_ = 0;
  failAfter_ = static_cast<size_t>(-1);
  installs_ = aborts_ = confirms_ = rollbacks_ = 0;
  beginResult_ = endResult_ = Status::Ok;
  inProgress_ = false;
  pendingVerify_ = false;
  canRollback_ = false;
}

Status Ota::begin(OtaTarget target, size_t totalSize) {
  if (inProgress_) return Status::Busy;
  if (beginResult_ != Status::Ok) return beginResult_;
  image_.clear();
  target_ = target;
  declared_ = totalSize;
  inProgress_ = true;
  return Status::Ok;
}

Status Ota::write(const uint8_t* data, size_t len) {
  if (!inProgress_) return Status::Denied;
  if (!data && len) return Status::Invalid;
  if (image_.size() + len > failAfter_) return Status::Io;
  if (declared_ && image_.size() + len > declared_) return Status::NoMem;
  image_.append(reinterpret_cast<const char*>(data), len);
  return Status::Ok;
}

Status Ota::end() {
  if (!inProgress_) return Status::Denied;
  inProgress_ = false;
  if (endResult_ != Status::Ok) {
    image_.clear();
    return endResult_;
  }
  if (declared_ && image_.size() != declared_) {
    image_.clear();
    return Status::Invalid;
  }
  installed_ = image_;
  installedTarget_ = target_;
  image_.clear();
  ++installs_;
  if (target_ == OtaTarget::App) canRollback_ = true;
  return Status::Ok;
}

void Ota::abort() {
  if (!inProgress_) return;
  inProgress_ = false;
  image_.clear();
  ++aborts_;
}

Status Ota::markValid() {
  if (!pendingVerify_) return Status::Ok;
  pendingVerify_ = false;
  ++confirms_;
  return Status::Ok;
}

Status Ota::rollback() {
  if (!canRollback_) return Status::Denied;
  ++rollbacks_;
  return Status::Ok;
}

// ---------------------------------------------------------------- bus

void I2c::reset() {
  devices_.clear();
  sda_ = scl_ = 0;
  frequency_ = 0;
  probes_ = 0;
  up_ = false;
  beginResult_ = Status::Ok;
}

Status I2c::begin(uint8_t sda, uint8_t scl, uint32_t frequencyHz) {
  if (beginResult_ != Status::Ok) return beginResult_;
  if (sda == scl || frequencyHz == 0) return Status::Invalid;
  sda_ = sda;
  scl_ = scl;
  frequency_ = frequencyHz;
  up_ = true;
  return Status::Ok;
}

void I2c::addDevice(uint8_t address) { devices_[address]; }
void I2c::removeDevice(uint8_t address) { devices_.erase(address); }

void I2c::setRegister(uint8_t address, uint8_t reg, uint8_t value) {
  devices_[address].reg[reg] = value;
}

uint8_t I2c::getRegister(uint8_t address, uint8_t reg) const {
  const auto found = devices_.find(address);
  return found == devices_.end() ? 0 : found->second.reg[reg];
}

bool I2c::probe(uint8_t address) {
  ++probes_;
  return up_ && devices_.count(address) != 0;
}

Status I2c::write(uint8_t address, const uint8_t* data, size_t len, bool) {
  if (!up_) return Status::Denied;
  const auto found = devices_.find(address);
  if (found == devices_.end()) return Status::NotFound;
  if (len == 0) return Status::Ok;
  if (!data) return Status::Invalid;
  Device& d = found->second;
  d.pointer = data[0];
  for (size_t i = 1; i < len; ++i) d.reg[d.pointer++] = data[i];
  return Status::Ok;
}

Status I2c::read(uint8_t address, uint8_t* out, size_t len) {
  if (!up_) return Status::Denied;
  const auto found = devices_.find(address);
  if (found == devices_.end()) return Status::NotFound;
  if (!out && len) return Status::Invalid;
  Device& d = found->second;
  for (size_t i = 0; i < len; ++i) out[i] = d.reg[d.pointer++];
  return Status::Ok;
}

Status I2c::writeRead(uint8_t address, const uint8_t* tx, size_t txLen, uint8_t* rx,
                      size_t rxLen) {
  LMP_TRY(write(address, tx, txLen, false));
  return read(address, rx, rxLen);
}

}  // namespace lmp::hal::native
