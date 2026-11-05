#include "services/lora/lora_service.h"

#if LMP_FEATURE_LORA

#include <stdio.h>
#include <string.h>

#include "drivers/lora/lora_airtime.h"
#include "kernel/config/kv_fields.h"

// Key names are limited to 15 characters by the NVS of the ESP32.
// The frequency has no default here: it depends on the band of the board.
LMP_FIELDS(lmp::LoraConfig,
  LMP_F(enabled,         "enabled",  Bool(true)),
  LMP_F(listen,          "listen",   Bool(true)),
  LMP_F(frequencyHz,     "freq",     U32(0, 1020000000, 0)),
  LMP_F(bandwidthHz,     "bw",       U32(7800, 500000, 125000)),
  LMP_F(spreadingFactor, "sf",       U8(7, 12, 7)),
  LMP_F(codingRate,      "cr",       U8(5, 8, 5)),
  LMP_F(syncWord,        "sync",     U8(0, 255, 0x12)),
  LMP_F(powerDbm,        "power",    I8(-17, 22, 10)),
  LMP_F(preamble,        "preamble", U16(6, 65535, 8)),
  LMP_F(crc,             "crc",      Bool(true)))

namespace lmp {
namespace {

constexpr const char* kOwner = "lora";
constexpr const char* kNamespace = "lora";
// Time given to a transmission beyond its time on air, in milliseconds.
constexpr uint32_t kSendMarginMs = 1500;
// Longest packet the command line can send: the line itself is short.
constexpr size_t kCommandPayload = 120;

constexpr ArgSpec kSetArgs[] = {
    Arg::u32("freq", 137000000, 1020000000).optional(),
    Arg::u32("bw", 7800, 500000).optional(),
    Arg::u8("sf", 7, 12).optional(),
    Arg::u8("cr", 5, 8).optional(),
    Arg::i32("power", -17, 22).optional(),
    Arg::u8("sync", 0, 255).optional(),
};

constexpr ArgSpec kOnArgs[] = {
    Arg::boolean("on"),
};

constexpr ArgSpec kSendArgs[] = {
    Arg::str("text", 1, kCommandPayload).optional(),
    Arg::str("hex", 2, 2 * kCommandPayload).optional(),
};

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Two digits for each byte, nothing else.
bool parseHex(const char* text, uint8_t* out, size_t cap, size_t& len) {
  len = 0;
  const size_t digits = strlen(text);
  if (digits == 0 || digits % 2 != 0 || digits / 2 > cap) return false;
  for (size_t i = 0; i < digits; i += 2) {
    const int high = hexValue(text[i]);
    const int low = hexValue(text[i + 1]);
    if (high < 0 || low < 0) return false;
    out[len++] = static_cast<uint8_t>(high * 16 + low);
  }
  return true;
}

// out must hold two characters for each byte and the terminator.
void toHex(const uint8_t* data, size_t len, char* out) {
  static const char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < len; ++i) {
    out[2 * i] = digits[data[i] >> 4];
    out[2 * i + 1] = digits[data[i] & 0x0F];
  }
  out[2 * len] = '\0';
}

bool printable(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (data[i] < 0x20 || data[i] > 0x7E) return false;
  }
  return len > 0;
}

}  // namespace

const char* loraStateName(LoraState state) {
  switch (state) {
    case LoraState::Idle:      return "idle";
    case LoraState::Listening: return "listening";
    case LoraState::Sending:   return "sending";
    case LoraState::Off:       break;
  }
  return "off";
}

const ServiceInfo& LoraService::info() const {
  static const ServiceInfo kInfo{ServiceId::Lora, "lora", "\xF0\x9F\x93\xA1", Mode::Any,
                                 RestartPolicy::Retry, 10,
                                 DepList(ServiceId::Storage, ServiceId::Gpio, ServiceId::Log)};
  return kInfo;
}

LoraParams LoraService::paramsOf(const LoraConfig& c) {
  LoraParams p;
  p.frequencyHz = c.frequencyHz;
  p.bandwidthHz = c.bandwidthHz;
  p.spreadingFactor = c.spreadingFactor;
  p.codingRate = c.codingRate;
  p.syncWord = c.syncWord;
  p.powerDbm = c.powerDbm;
  p.preamble = c.preamble;
  p.crc = c.crc;
  return p;
}

LoraParams LoraService::paramsNow(const LoraConfig& c) const {
  LoraParams p = paramsOf(c);
  if (syncNow_) p.syncWord = syncNow_;
  return p;
}

Status LoraService::useSyncWord(uint8_t sync) {
  if (!radio_ || state_ == LoraState::Off) return Status::Denied;
  if (state_ == LoraState::Sending) return Status::Busy;
  const uint8_t next = sync == config_.syncWord ? 0 : sync;
  if (next == syncNow_) return Status::Ok;
  syncNow_ = next;
  const Status s = radio_->configure(paramsNow(config_));
  state_ = LoraState::Idle;
  resume();
  LMP_LOGI(src_, "sync word 0x%02X", static_cast<unsigned>(syncNow_ ? syncNow_ : config_.syncWord));
  return s;
}

// ---------------------------------------------------------------- life cycle

Status LoraService::start(Kernel& kernel) {
  kernel_ = &kernel;
  src_ = kernel.log().addSource(info().name, info().icon);
  state_ = LoraState::Off;

  const hal::Board& board = kernel.board();
  wiring_ = board.lora;
  radio_ = injected_ ? injected_ : loraRadio(board);
  if (!wiring_ || !radio_) {
    fail("no radio on this board");
    return Status::Unsupported;
  }
  if (loraSubBands(wiring_->bandMhz).empty()) {
    fail("band not supported");
    return Status::Unsupported;
  }

  gpio_ = kernel.api<IGpio>();
  if (!gpio_) return Status::Unsupported;
  const Status claimed = claimPins();
  if (claimed != Status::Ok) {
    fail("radio pins not available");
    return claimed;
  }

  loadSettings();
  if (!budgetsStarted_) {
    // Nothing is known of what was sent before the start: the full share is assumed.
    size_t i = 0;
    for (const LoraSubBand& part : loraSubBands(wiring_->bandMhz)) {
      if (i < kLoraMaxSubBands) budgets_[i++].start(part.dutyPermille, kernel.now());
    }
    budgetsStarted_ = true;
  }

  if (!config_.enabled) {
    LMP_LOGI(src_, "%s present, radio off", radio_->chipName());
    return Status::Ok;
  }
  const Status up = bringUp();
  if (up != Status::Ok) {
    releasePins();
    fail("radio does not answer");
    return up;
  }
  return Status::Ok;
}

void LoraService::stop() {
  shutDown();
  releasePins();
}

Status LoraService::bringUp() {
  const Status s = radio_->begin(paramsNow(config_));
  if (s != Status::Ok) {
    state_ = LoraState::Off;
    return s;
  }
  state_ = LoraState::Idle;
  resume();
  LMP_LOGI(src_, "%s on %lu.%03lu MHz, bw %lu kHz, sf %u, cr 4/%u, %d dBm", radio_->chipName(),
           static_cast<unsigned long>(config_.frequencyHz / 1000000),
           static_cast<unsigned long>(config_.frequencyHz % 1000000 / 1000),
           static_cast<unsigned long>(config_.bandwidthHz / 1000),
           static_cast<unsigned>(config_.spreadingFactor),
           static_cast<unsigned>(config_.codingRate), static_cast<int>(config_.powerDbm));
  return Status::Ok;
}

void LoraService::shutDown() {
  if (radio_ && state_ != LoraState::Off) radio_->end();
  state_ = LoraState::Off;
}

void LoraService::resume() {
  if (state_ == LoraState::Off) return;
  if (config_.listen) {
    if (radio_->startReceive() == Status::Ok) {
      state_ = LoraState::Listening;
      return;
    }
    LMP_LOGW(src_, "the radio cannot listen");
  } else {
    radio_->standby();
  }
  state_ = LoraState::Idle;
}

Status LoraService::claimPins() {
  if (pinsClaimed_) return Status::Ok;
  struct Pin {
    uint8_t pin;
    hal::PinMode mode;
    const char* label;
  };
  const Pin pins[] = {
      {wiring_->cs, hal::PinMode::Output, "Radio select"},
      {wiring_->sck, hal::PinMode::Output, "Radio clock"},
      {wiring_->mosi, hal::PinMode::Output, "Radio data out"},
      {wiring_->miso, hal::PinMode::Input, "Radio data in"},
      {wiring_->rst, hal::PinMode::Output, "Radio reset"},
      {wiring_->busy, hal::PinMode::Input, "Radio busy"},
      {wiring_->dio1, hal::PinMode::Input, "Radio interrupt"},
  };
  for (const Pin& p : pins) {
    const Status s = gpio_->claim(p.pin, p.mode, kOwner, p.label);
    if (s != Status::Ok) {
      for (const Pin& taken : pins) {
        if (&taken == &p) break;
        gpio_->release(taken.pin, kOwner);
      }
      return s;
    }
  }
  pinsClaimed_ = true;
  return Status::Ok;
}

void LoraService::releasePins() {
  if (!pinsClaimed_ || !gpio_ || !wiring_) return;
  const uint8_t pins[] = {wiring_->cs,  wiring_->sck,  wiring_->mosi, wiring_->miso,
                          wiring_->rst, wiring_->busy, wiring_->dio1};
  for (uint8_t pin : pins) gpio_->release(pin, kOwner);
  pinsClaimed_ = false;
}

// ---------------------------------------------------------------- settings

void LoraService::loadSettings() {
  setDefaults(config_);
  hal::IKv* kv = kernel_->board().kv;
  if (kv) kvLoadFields(*kv, kNamespace, &config_, fieldsOf<LoraConfig>());
  if (config_.frequencyHz == 0) config_.frequencyHz = loraDefaultFrequency(wiring_->bandMhz);

  const char* field = nullptr;
  if (check(config_, field, subBand_) != Status::Ok) {
    LMP_LOGW(src_, "stored settings not valid (%s): back to the defaults", field ? field : "?");
    setDefaults(config_);
    config_.frequencyHz = loraDefaultFrequency(wiring_->bandMhz);
    check(config_, field, subBand_);
  }
}

Status LoraService::saveSettings() {
  hal::IKv* kv = kernel_ ? kernel_->board().kv : nullptr;
  if (!kv) return Status::Ok;
  LMP_TRY(kvSaveFields(*kv, kNamespace, &config_, fieldsOf<LoraConfig>()));
  return kv->commit();
}

Status LoraService::check(const LoraConfig& c, const char*& field, int& subBand) const {
  if (!loraBandwidthAllowed(c.bandwidthHz)) {
    field = "bw";
    return Status::Invalid;
  }
  if (c.spreadingFactor < 7 || c.spreadingFactor > 12) {
    field = "sf";
    return Status::Invalid;
  }
  if (c.codingRate < 5 || c.codingRate > 8) {
    field = "cr";
    return Status::Invalid;
  }
  const int index = loraSubBandOf(wiring_->bandMhz, c.frequencyHz, c.bandwidthHz);
  if (index < 0 || static_cast<size_t>(index) >= kLoraMaxSubBands) {
    field = "freq";
    return Status::Invalid;
  }
  const LoraSubBand& part = loraSubBands(wiring_->bandMhz)[static_cast<size_t>(index)];
  const int8_t highest =
      part.maxPowerDbm < radio_->maxPowerDbm() ? part.maxPowerDbm : radio_->maxPowerDbm();
  if (c.powerDbm < radio_->minPowerDbm() || c.powerDbm > highest) {
    field = "power";
    return Status::Invalid;
  }
  subBand = index;
  return Status::Ok;
}

Status LoraService::applySettings(const LoraConfig& next, int subBand) {
  if (state_ == LoraState::Sending) return Status::Busy;
  if (state_ != LoraState::Off) {
    const Status s = radio_->configure(paramsNow(next));
    if (s != Status::Ok) {
      // The chip refused: it goes back to what it had.
      radio_->configure(paramsNow(config_));
      state_ = LoraState::Idle;
      resume();
      return s;
    }
  }
  config_ = next;
  subBand_ = subBand;
  if (state_ != LoraState::Off) {
    state_ = LoraState::Idle;
    resume();
  }
  LMP_LOGI(src_, "%lu.%03lu MHz, bw %lu kHz, sf %u, cr 4/%u, %d dBm",
           static_cast<unsigned long>(config_.frequencyHz / 1000000),
           static_cast<unsigned long>(config_.frequencyHz % 1000000 / 1000),
           static_cast<unsigned long>(config_.bandwidthHz / 1000),
           static_cast<unsigned>(config_.spreadingFactor),
           static_cast<unsigned>(config_.codingRate), static_cast<int>(config_.powerDbm));
  return saveSettings();
}

// ---------------------------------------------------------------- packets

bool LoraService::subscribe(LoraHandler handler) {
  if (!handler || listenerCount_ >= LMP_LORA_LISTENERS) return false;
  listeners_[listenerCount_++] = handler;
  return true;
}

uint32_t LoraService::airtimeMs(size_t len) const {
  return (loraAirtimeUs(paramsNow(config_), len) + 999) / 1000;
}

uint32_t LoraService::airtimeLeftMs() const {
  if (subBand_ < 0 || !kernel_) return 0;
  return budgets_[subBand_].leftMs(kernel_->now());
}

const LoraPacket& LoraService::inboxAt(size_t index) const {
  const size_t oldest = (inboxHead_ + LMP_LORA_INBOX - inboxCount_) % LMP_LORA_INBOX;
  return inbox_[(oldest + index) % LMP_LORA_INBOX];
}

Status LoraService::transmit(const uint8_t* data, size_t len, const char** why,
                             uint16_t preamble) {
  if (!radio_ || !kernel_ || state_ == LoraState::Off) {
    if (why) *why = "enabled";
    return Status::Denied;
  }
  if (!data || len == 0 || len > LMP_LORA_MAX_PAYLOAD) return Status::Invalid;
  if (state_ == LoraState::Sending) return Status::Busy;

  const uint32_t nowMs = kernel_->now();
  LoraParams p = paramsNow(config_);
  const bool longer = preamble > p.preamble;
  if (longer) p.preamble = preamble;
  const uint32_t airtimeUs = loraAirtimeUs(p, len);
  AirtimeBudget& budget = budgets_[subBand_];
  if (!budget.allows(airtimeUs, nowMs)) {
    ++stats_.refused;
    if (why) *why = "duty";
    LMP_LOGW(src_, "packet of %u bytes refused: %lu ms of time on air left, %lu needed",
             static_cast<unsigned>(len), static_cast<unsigned long>(budget.leftMs(nowMs)),
             static_cast<unsigned long>((airtimeUs + 999) / 1000));
    return Status::Denied;
  }

  // A preamble of its own: the chip takes it for this packet, the settings come back after.
  Status s = longer ? radio_->configure(p) : Status::Ok;
  longPreamble_ = longer;
  if (s == Status::Ok) s = radio_->startTransmit(data, len);
  if (s != Status::Ok) {
    ++stats_.failed;
    restorePreamble();
    state_ = LoraState::Idle;
    resume();
    return s;
  }
  budget.spend(airtimeUs, nowMs);
  stats_.airtimeMs += (airtimeUs + 500) / 1000;
  state_ = LoraState::Sending;
  sendSeq_ += 1;
  sendStartMs_ = nowMs;
  sendLimitMs_ = airtimeUs / 1000 + kSendMarginMs;
  LMP_LOGD(src_, "tx %lu: %u bytes, %lu ms on air", static_cast<unsigned long>(sendSeq_),
           static_cast<unsigned>(len), static_cast<unsigned long>((airtimeUs + 999) / 1000));
  return Status::Ok;
}

void LoraService::restorePreamble() {
  if (!longPreamble_) return;
  longPreamble_ = false;
  radio_->configure(paramsNow(config_));
}

void LoraService::finishSend(Status outcome) {
  restorePreamble();
  if (outcome == Status::Ok) {
    ++stats_.sent;
  } else {
    ++stats_.failed;
    LMP_LOGW(src_, "tx %lu did not end: %s", static_cast<unsigned long>(sendSeq_),
             statusName(outcome));
  }
  state_ = LoraState::Idle;
  resume();
  kernel_->events().publish(
      Event(EventId::LoraSent, 0, static_cast<uint8_t>(outcome), sendSeq_));
}

void LoraService::receive(uint32_t nowMs) {
  LoraPacket& slot = inbox_[inboxHead_];
  size_t len = 0;
  RxInfo heard;
  const Status s = radio_->readPacket(slot.data, sizeof(slot.data), len, heard);
  if (s != Status::Ok || len == 0) {
    ++stats_.damaged;
    return;
  }
  slot.seq = ++receiveSeq_;
  slot.ms = nowMs;
  slot.len = static_cast<uint8_t>(len);
  slot.rssiDbm = heard.rssiDbm;
  slot.snrDb = heard.snrDb;
  inboxHead_ = (inboxHead_ + 1) % LMP_LORA_INBOX;
  if (inboxCount_ < LMP_LORA_INBOX) ++inboxCount_;

  ++stats_.received;
  lastRssiDbm_ = heard.rssiDbm;
  lastSnrDb_ = heard.snrDb;
  LMP_LOGD(src_, "rx %lu: %u bytes, rssi %d dBm, snr %d dB",
           static_cast<unsigned long>(slot.seq), static_cast<unsigned>(len),
           static_cast<int>(heard.rssiDbm), static_cast<int>(heard.snrDb));

  kernel_->events().publish(Event(EventId::LoraReceived, slot.len, 0, slot.seq));
  for (size_t i = 0; i < listenerCount_; ++i) listeners_[i](slot);
}

void LoraService::tick(uint32_t nowMs) {
  if (state_ == LoraState::Off || !radio_) return;

  switch (radio_->poll()) {
    case RadioEvent::TxDone:
      if (state_ == LoraState::Sending) finishSend(Status::Ok);
      break;
    case RadioEvent::Timeout:
      if (state_ == LoraState::Sending) {
        finishSend(Status::Timeout);
      } else {
        resume();
      }
      break;
    case RadioEvent::RxDone:
      receive(nowMs);
      break;
    case RadioEvent::RxError:
      ++stats_.damaged;
      break;
    case RadioEvent::None:
      break;
  }

  // The start may be later than the time of this round, when the request came during the
  // round itself: the difference is read with its sign.
  if (state_ == LoraState::Sending &&
      static_cast<int32_t>(nowMs - sendStartMs_) > static_cast<int32_t>(sendLimitMs_)) {
    radio_->standby();
    finishSend(Status::Timeout);
  }
}

void LoraService::onEvent(const Event& event) {
  // A factory reset takes the settings of the radio with it.
  if (event.id == EventId::ResetRequested && event.a == 1 && kernel_) {
    hal::IKv* kv = kernel_->board().kv;
    if (kv) kv->eraseNamespace(kNamespace);
  }
}

// ---------------------------------------------------------------- documents and commands

void LoraService::describe(JsonWriter& w) const {
  w.str("chip", radio_ ? radio_->chipName() : "none");
  w.u32("band", wiring_ ? wiring_->bandMhz : 0);
  w.str("state", loraStateName(state_));
  w.boolean("enabled", config_.enabled);
  w.boolean("listen", config_.listen);
  w.u32("freq", config_.frequencyHz);
  w.u32("bw", config_.bandwidthHz);
  w.u32("sf", config_.spreadingFactor);
  w.u32("cr", config_.codingRate);
  w.u32("sync", config_.syncWord);
  // The sync word a module asked for now, not saved.
  if (syncNow_) w.u32("syncNow", syncNow_);
  w.i32("power", config_.powerDbm);
  w.u32("preamble", config_.preamble);
  w.boolean("crc", config_.crc);

  if (wiring_ && radio_ && subBand_ >= 0) {
    const LoraSubBand& part = loraSubBands(wiring_->bandMhz)[static_cast<size_t>(subBand_)];
    w.beginObject("limits");
    w.u32("fromHz", part.fromHz);
    w.u32("toHz", part.toHz);
    w.f64("dutyPercent", part.dutyPermille / 10.0, 1);
    w.i32("maxPower", part.maxPowerDbm < radio_->maxPowerDbm() ? part.maxPowerDbm
                                                                : radio_->maxPowerDbm());
    w.u32("airtimeHourMs", budgets_[subBand_].capacityMs());
    w.u32("airtimeLeftMs", airtimeLeftMs());
    w.endObject();
  }

  w.beginObject("stats");
  w.u32("sent", stats_.sent);
  w.u32("received", stats_.received);
  w.u32("damaged", stats_.damaged);
  w.u32("failed", stats_.failed);
  w.u32("refused", stats_.refused);
  w.u32("airtimeMs", stats_.airtimeMs);
  w.endObject();

  // What the radio hears right now: the noise of the channel, or somebody sending.
  if (state_ == LoraState::Listening) w.f64("channelRssi", radio_->channelRssi(), 1);

  if (stats_.received) {
    w.beginObject("last");
    w.f64("rssi", lastRssiDbm_, 1);
    w.f64("snr", lastSnrDb_, 1);
    w.endObject();
  }

  w.beginArray("inbox");
  for (size_t i = 0; i < inboxCount_; ++i) {
    const LoraPacket& p = inboxAt(i);
    char hex[2 * LMP_LORA_MAX_PAYLOAD + 1];
    toHex(p.data, p.len, hex);
    w.beginObject();
    w.u32("seq", p.seq);
    w.u32("ms", p.ms);
    w.u32("bytes", p.len);
    w.f64("rssi", p.rssiDbm, 1);
    w.f64("snr", p.snrDb, 1);
    w.str("hex", hex);
    if (printable(p.data, p.len)) {
      w.str("text", StringView(reinterpret_cast<const char*>(p.data), p.len));
    }
    w.endObject();
  }
  w.endArray();
}

Status LoraService::commandSet(const Args& a, CmdContext& ctx) {
  if (a.count() == 0) return Status::Invalid;
  LoraConfig next = config_;
  if (a.has("freq")) next.frequencyHz = a.u32("freq");
  if (a.has("bw")) next.bandwidthHz = a.u32("bw");
  if (a.has("sf")) next.spreadingFactor = a.u8("sf");
  if (a.has("cr")) next.codingRate = a.u8("cr");
  if (a.has("power")) next.powerDbm = static_cast<int8_t>(a.i32("power"));
  if (a.has("sync")) next.syncWord = a.u8("sync");

  const char* field = nullptr;
  int subBand = -1;
  const Status valid = check(next, field, subBand);
  if (valid != Status::Ok) {
    ctx.errorField = field;
    return valid;
  }
  return applySettings(next, subBand);
}

Status LoraService::commandSend(const Args& a, CmdContext& ctx) {
  if (a.has("text") == a.has("hex")) {
    ctx.errorField = a.has("text") ? "hex" : "text";
    return Status::Invalid;
  }
  uint8_t payload[kCommandPayload];
  size_t len = 0;
  if (a.has("text")) {
    const char* text = a.str("text");
    len = strlen(text);
    if (len > sizeof(payload)) len = sizeof(payload);
    memcpy(payload, text, len);
  } else if (!parseHex(a.str("hex"), payload, sizeof(payload), len)) {
    ctx.errorField = "hex";
    return Status::Invalid;
  }

  const char* why = nullptr;
  const Status s = transmit(payload, len, &why);
  if (s != Status::Ok) {
    if (why) ctx.errorField = why;
    return s;
  }
  if (ctx.result) {
    ctx.result->u32("seq", sendSeq_);
    ctx.result->u32("bytes", static_cast<uint32_t>(len));
    ctx.result->u32("airtimeMs", airtimeMs(len));
    ctx.result->u32("airtimeLeftMs", airtimeLeftMs());
  }
  return Status::Ok;
}

void LoraService::commands(CommandSet& set) {
  set.add("set",
          LMP_HELP("Changes frequency, bandwidth, spreading factor, coding rate, power, sync word"),
          kSetArgs,
          [this](const Args& a, CmdContext& ctx) -> Status { return commandSet(a, ctx); });

  set.add("send", LMP_HELP("Sends a packet: text, or hex with two digits for each byte"), kSendArgs,
          [this](const Args& a, CmdContext& ctx) -> Status { return commandSend(a, ctx); });

  set.add("listen", LMP_HELP("Hears the other devices when the radio is not sending"), kOnArgs,
          [this](const Args& a, CmdContext&) -> Status {
            if (state_ == LoraState::Sending) return Status::Busy;
            config_.listen = a.boolean("on");
            if (state_ != LoraState::Off) {
              state_ = LoraState::Idle;
              resume();
            }
            return saveSettings();
          });

  set.add("enable", LMP_HELP("Turns the radio on or off"), kOnArgs,
          [this](const Args& a, CmdContext&) -> Status {
            const bool on = a.boolean("on");
            if (on && state_ == LoraState::Off) {
              const Status up = bringUp();
              if (up != Status::Ok) return up;
            } else if (!on) {
              shutDown();
              LMP_LOGI(src_, "radio off");
            }
            config_.enabled = on;
            return saveSettings();
          });

  set.add("clear", LMP_HELP("Empties the received packets and the counters"),
          [this](const Args&, CmdContext&) -> Status {
            stats_ = LoraStats();
            inboxHead_ = inboxCount_ = 0;
            return Status::Ok;
          });
}

}  // namespace lmp

#endif  // LMP_FEATURE_LORA
