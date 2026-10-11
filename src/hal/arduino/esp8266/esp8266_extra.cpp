// LoopMaxPro HAL - ESP8266 with the Arduino framework: time from the network and I2C.
#include "lmp_config.h"

#include <Arduino.h>

#include "hal/arduino/esp8266/esp8266_hal.h"

#if LMP_FEATURE_NET
#include <coredecls.h>
#include <sntp.h>
#endif
#if LMP_FEATURE_BUS
#include <Wire.h>
#endif

namespace lmp::hal::esp8266 {

// ---------------------------------------------------------------- time from the network

#if LMP_FEATURE_NET

namespace {
uint32_t g_answers = 0;
}  // namespace

uint32_t Ntp::answers() const { return g_answers; }

Status Ntp::start(const char* server) {
  if (!server || !*server) return Status::Invalid;
  if (!hooked_) {
    // The core calls back every time the clock is set: only the answers of a server count.
    settimeofday_cb([](bool fromServer) {
      if (fromServer) ++g_answers;
    });
    hooked_ = true;
  }
  snprintf(server_, sizeof(server_), "%s", server);
  sntp_stop();
  sntp_setservername(0, server_);
  sntp_init();
  running_ = true;
  return Status::Ok;
}

void Ntp::stop() {
  if (!running_) return;
  sntp_stop();
  running_ = false;
}

#endif  // LMP_FEATURE_NET

// ---------------------------------------------------------------- I2C

#if LMP_FEATURE_BUS

namespace {

// Longest transfer of the library of the core.
constexpr size_t kLongest = 128;

Status statusOf(uint8_t code) {
  switch (code) {
    case 0:  return Status::Ok;
    case 1:  return Status::NoMem;      // longer than the buffer of the library
    case 2:                             // nobody at that address
    case 3:  return Status::NotFound;   // the chip stopped answering
    default: break;
  }
  return Status::Io;
}

}  // namespace

Status I2c::begin(uint8_t sda, uint8_t scl, uint32_t frequencyHz) {
  if (sda == scl || frequencyHz == 0) return Status::Invalid;
  Wire.begin(sda, scl);
  Wire.setClock(frequencyHz);
  up_ = true;
  return Status::Ok;
}

bool I2c::probe(uint8_t address) {
  if (!up_) return false;
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

Status I2c::write(uint8_t address, const uint8_t* data, size_t len, bool stop) {
  if (!up_) return Status::Denied;
  if (len > kLongest) return Status::NoMem;
  if (!data && len) return Status::Invalid;
  Wire.beginTransmission(address);
  if (len) Wire.write(data, len);
  return statusOf(Wire.endTransmission(stop));
}

Status I2c::read(uint8_t address, uint8_t* out, size_t len) {
  if (!up_) return Status::Denied;
  if (!out || len == 0) return Status::Invalid;
  if (len > kLongest) return Status::NoMem;
  const size_t got = Wire.requestFrom(address, static_cast<uint8_t>(len));
  for (size_t i = 0; i < got && i < len; ++i) out[i] = static_cast<uint8_t>(Wire.read());
  return got == len ? Status::Ok : Status::NotFound;
}

Status I2c::writeRead(uint8_t address, const uint8_t* tx, size_t txLen, uint8_t* rx,
                      size_t rxLen) {
  LMP_TRY(write(address, tx, txLen, false));
  return read(address, rx, rxLen);
}

#endif  // LMP_FEATURE_BUS

}  // namespace lmp::hal::esp8266
