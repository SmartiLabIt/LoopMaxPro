#include "sdk/devices.h"

#include <string.h>

#if LMP_FEATURE_HTTPCLIENT
#include "services/httpclient/httpclient_service.h"
#endif

namespace lmp::sdk {

// ---------------------------------------------------------------- chip on a bus

Status I2cDevice::attach(hal::II2c* bus) {
  if (!bus) return Status::Unsupported;
  bus_ = bus;
  failures_ = 0;
  if (!bus->probe(address_)) {
    failures_ = 3;
    return Status::NotFound;
  }
  return Status::Ok;
}

Status I2cDevice::note(Status s) {
  if (s == Status::Ok) {
    failures_ = 0;
  } else {
    ++errors_;
    if (failures_ < 255) ++failures_;
  }
  return s;
}

Status I2cDevice::read(uint8_t reg, uint8_t* out, size_t len) {
  if (!bus_) return Status::Denied;
  return note(bus_->writeRead(address_, &reg, 1, out, len));
}

Status I2cDevice::write(uint8_t reg, const uint8_t* data, size_t len) {
  if (!bus_) return Status::Denied;
  uint8_t frame[33];
  if (len + 1 > sizeof(frame)) return Status::NoMem;
  if (!data && len) return Status::Invalid;
  frame[0] = reg;
  if (len) memcpy(frame + 1, data, len);
  return note(bus_->write(address_, frame, len + 1, true));
}

Status I2cDevice::read8(uint8_t reg, uint8_t& out) { return read(reg, &out, 1); }

Status I2cDevice::read16(uint8_t reg, uint16_t& out) {
  uint8_t raw[2] = {0, 0};
  LMP_TRY(read(reg, raw, sizeof(raw)));
  out = static_cast<uint16_t>((raw[0] << 8) | raw[1]);
  return Status::Ok;
}

Status I2cDevice::write8(uint8_t reg, uint8_t value) { return write(reg, &value, 1); }

Status I2cDevice::command(uint8_t value) {
  if (!bus_) return Status::Denied;
  return note(bus_->write(address_, &value, 1, true));
}

// ---------------------------------------------------------------- service on the network

#if LMP_FEATURE_HTTPCLIENT

Status OnlineSource::begin(IHttpClientService* http, const char* url, uint32_t everySec,
                           char* buffer, size_t capacity) {
  if (!http) return Status::Unsupported;
  if (!url || !*url || !buffer || capacity < 32 || everySec == 0) return Status::Invalid;
  if (!url_.assign(url)) return Status::Invalid;   // longer than what is kept
  http_ = http;
  // Half of the room holds the last good answer, half the one that is arriving: what the
  // module shows is never an answer by halves.
  capacity_ = capacity / 2;
  buffer_ = buffer;
  incoming_ = buffer + capacity_;
  buffer_[0] = incoming_[0] = '\0';
  length_ = 0;
  everyMs_ = everySec * 1000u;
  asked_ = waiting_ = forced_ = good_ = news_ = false;
  failures_ = 0;
  status_ = 0;
  return Status::Ok;
}

bool OnlineSource::tick(uint32_t nowMs) {
  nowMs_ = nowMs;
  if (!http_) return false;

  const bool time = !asked_ || static_cast<uint32_t>(nowMs - askedMs_) >= everyMs_;
  if (!waiting_ && (time || forced_)) {
    HttpFetch what;
    what.url = url_.c_str();
    what.out = incoming_;
    what.cap = capacity_;
    // Refused now, because the device is not on a network or the queue is full: asked again
    // at the next round, without counting it as a failure of the service.
    if (http_->fetch(what, [this](const HttpAnswer& a) { onAnswer(a); }) == Status::Ok) {
      waiting_ = true;
      asked_ = true;
      forced_ = false;
      askedMs_ = nowMs;
    }
  }
  const bool news = news_;
  news_ = false;
  return news;
}

void OnlineSource::onAnswer(const HttpAnswer& answer) {
  waiting_ = false;
  status_ = answer.status;
  if (!answer.good() || answer.truncated) {
    ++failures_;
    return;   // the answer of before stays
  }
  memcpy(buffer_, incoming_, answer.length + 1);
  length_ = answer.length;
  gotMs_ = nowMs_;
  good_ = true;
  news_ = true;
}

#endif  // LMP_FEATURE_HTTPCLIENT

}  // namespace lmp::sdk
