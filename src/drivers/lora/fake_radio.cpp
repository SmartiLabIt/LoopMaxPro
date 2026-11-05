#include "drivers/lora/fake_radio.h"

#if LMP_FEATURE_LORA && defined(LMP_FW_NATIVE)

#include <string.h>

namespace lmp {

Status FakeLoraRadio::begin(const LoraParams& params) {
  ++begins_;
  started_ = false;
  if (!present_) return Status::Io;
  params_ = params;
  mode_ = Mode::Standby;
  txPending_ = false;
  currentValid_ = false;
  started_ = true;
  return Status::Ok;
}

Status FakeLoraRadio::configure(const LoraParams& params) {
  if (!started_) return Status::Denied;
  params_ = params;
  mode_ = Mode::Standby;
  txPending_ = false;
  return Status::Ok;
}

void FakeLoraRadio::end() {
  started_ = false;
  mode_ = Mode::Standby;
  txPending_ = false;
}

Status FakeLoraRadio::standby() {
  if (!started_) return Status::Denied;
  mode_ = Mode::Standby;
  txPending_ = false;
  return Status::Ok;
}

Status FakeLoraRadio::startTransmit(const uint8_t* data, size_t len) {
  if (!started_) return Status::Denied;
  if (!data || len == 0 || len > 255) return Status::Invalid;
  sent_.emplace_back(reinterpret_cast<const char*>(data), len);
  sentPreambles_.push_back(params_.preamble);
  sentSyncs_.push_back(params_.syncWord);
  mode_ = Mode::Transmitting;
  txPending_ = true;
  return Status::Ok;
}

Status FakeLoraRadio::startReceive() {
  if (!started_) return Status::Denied;
  mode_ = Mode::Receiving;
  return Status::Ok;
}

RadioEvent FakeLoraRadio::poll() {
  if (!started_) return RadioEvent::None;

  if (mode_ == Mode::Transmitting) {
    if (txStuck_ || !txPending_) return RadioEvent::None;
    txPending_ = false;
    mode_ = Mode::Standby;
    return RadioEvent::TxDone;
  }

  if (mode_ == Mode::Receiving && !incoming_.empty()) {
    current_ = incoming_.front();
    incoming_.erase(incoming_.begin());
    if (current_.damaged) {
      currentValid_ = false;
      return RadioEvent::RxError;
    }
    currentValid_ = true;
    return RadioEvent::RxDone;
  }
  return RadioEvent::None;
}

Status FakeLoraRadio::readPacket(uint8_t* out, size_t cap, size_t& len, RxInfo& info) {
  len = 0;
  if (!started_ || !out) return Status::Invalid;
  if (!currentValid_) return Status::NotFound;
  currentValid_ = false;
  const size_t size = current_.payload.size();
  const size_t n = size < cap ? size : cap;
  memcpy(out, current_.payload.data(), n);
  len = n;
  info = current_.info;
  return size > cap ? Status::NoMem : Status::Ok;
}

void FakeLoraRadio::reset() {
  params_ = LoraParams();
  mode_ = Mode::Standby;
  present_ = true;
  started_ = false;
  txStuck_ = false;
  txPending_ = false;
  begins_ = 0;
  sent_.clear();
  sentPreambles_.clear();
  sentSyncs_.clear();
  incoming_.clear();
  currentValid_ = false;
}

void FakeLoraRadio::inject(const std::string& payload, float rssiDbm, float snrDb) {
  // A radio that is not listening hears nothing.
  if (mode_ != Mode::Receiving) return;
  Incoming in;
  in.payload = payload;
  in.info.rssiDbm = rssiDbm;
  in.info.snrDb = snrDb;
  incoming_.push_back(in);
}

void FakeLoraRadio::injectDamaged() {
  if (mode_ != Mode::Receiving) return;
  Incoming in;
  in.damaged = true;
  incoming_.push_back(in);
}

FakeLoraRadio& fakeLoraRadio() {
  static FakeLoraRadio radio;
  return radio;
}

ILoraRadio* loraRadio(const hal::Board& board) {
  if (!board.lora || board.lora->chip == hal::LoraChip::None) return nullptr;
  return &fakeLoraRadio();
}

}  // namespace lmp

#endif  // LMP_FEATURE_LORA && LMP_FW_NATIVE
