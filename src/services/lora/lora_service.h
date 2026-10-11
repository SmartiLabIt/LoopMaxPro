// LoopMaxPro - LoRa service: packets between devices, radio to radio, without a network.
// Present in the builds of the boards that have a radio. It owns the radio: modules and other
// services send and receive through ILora and never touch the chip.
#pragma once
#include "lmp_config.h"

#if LMP_FEATURE_LORA

#include "base/delegate.h"
#include "drivers/lora/lora_radio.h"
#include "kernel/kernel.h"
#include "services/gpio/gpio_service.h"
#include "services/lora/lora_band.h"

namespace lmp {

static_assert(LMP_LORA_MAX_PAYLOAD >= 1 && LMP_LORA_MAX_PAYLOAD <= 255,
              "LMP_LORA_MAX_PAYLOAD: the radio chips carry from 1 to 255 bytes");

// Settings kept in storage, namespace "lora".
struct LoraConfig {
  bool enabled;
  bool listen;               // hear the others whenever the radio is not sending
  uint32_t frequencyHz;
  uint32_t bandwidthHz;
  uint8_t spreadingFactor;
  uint8_t codingRate;        // 5..8, meaning 4/5..4/8
  uint8_t syncWord;
  int8_t powerDbm;
  uint16_t preamble;
  bool crc;
};

struct LoraPacket {
  uint32_t seq = 0;
  uint32_t ms = 0;           // uptime at the arrival
  float rssiDbm = 0;
  float snrDb = 0;
  uint8_t len = 0;
  uint8_t data[LMP_LORA_MAX_PAYLOAD] = {};
};

struct LoraStats {
  uint32_t sent = 0;
  uint32_t received = 0;
  uint32_t damaged = 0;      // packets heard with errors and dropped
  uint32_t failed = 0;       // transmissions that did not end
  uint32_t refused = 0;      // transmissions the rules of the band did not allow
  uint32_t airtimeMs = 0;    // time spent transmitting since the start
};

enum class LoraState : uint8_t { Off, Idle, Listening, Sending };
const char* loraStateName(LoraState state);

using LoraHandler = Delegate<void(const LoraPacket&)>;

class ILora {
 public:
  static constexpr ServiceId kId = ServiceId::Lora;
  virtual ~ILora() = default;

  // True when the radio is on and answers.
  virtual bool available() const = 0;
  // Starts a transmission and returns at once: the end arrives with the event LoraSent.
  // Busy while another packet is on air, Denied when the radio is off or the rules of the
  // band do not allow more time on air.
  virtual Status send(const uint8_t* data, size_t len) = 0;
  // The same with a longer preamble, in symbols: for a receiver that sleeps and sniffs the
  // channel at intervals (a node on battery). The preamble of the settings comes back after it.
  virtual Status send(const uint8_t* data, size_t len, uint16_t preambleSymbols) = 0;
  // A sync word for now, not saved: a module that talks to its own network. 0: back to the one
  // of the settings. Busy while a packet is on air.
  virtual Status useSyncWord(uint8_t sync) = 0;
  // The handler is called on the application task for every packet received.
  virtual bool subscribe(LoraHandler handler) = 0;

  virtual uint32_t airtimeMs(size_t len) const = 0;
  virtual uint32_t airtimeLeftMs() const = 0;
  virtual const LoraConfig& settings() const = 0;
  virtual const LoraStats& stats() const = 0;
};

class LoraService : public Service, public ILora {
 public:
  // A null radio means the one of the board.
  explicit LoraService(ILoraRadio* radio = nullptr) : injected_(radio) {}

  const ServiceInfo& info() const override;
  Status start(Kernel& kernel) override;
  void stop() override;
  void tick(uint32_t nowMs) override;
  void onEvent(const Event& event) override;
  void describe(JsonWriter& w) const override;
  void commands(CommandSet& set) override;
  void* api() override { return static_cast<ILora*>(this); }

  bool available() const override { return state_ != LoraState::Off; }
  Status send(const uint8_t* data, size_t len) override { return transmit(data, len, nullptr); }
  Status send(const uint8_t* data, size_t len, uint16_t preambleSymbols) override {
    return transmit(data, len, nullptr, preambleSymbols);
  }
  Status useSyncWord(uint8_t sync) override;
  bool subscribe(LoraHandler handler) override;
  uint32_t airtimeMs(size_t len) const override;
  uint32_t airtimeLeftMs() const override;
  const LoraConfig& settings() const override { return config_; }
  const LoraStats& stats() const override { return stats_; }

  LoraState radioState() const { return state_; }
  // Packets kept, from the oldest. index below inboxCount().
  size_t inboxCount() const { return inboxCount_; }
  const LoraPacket& inboxAt(size_t index) const;

 private:
  static LoraParams paramsOf(const LoraConfig& config);
  // The parameters the radio has now: the settings, with the sync word of the moment.
  LoraParams paramsNow(const LoraConfig& config) const;
  // Checks a set of values against the chip and the rules of the band.
  Status check(const LoraConfig& config, const char*& field, int& subBand) const;
  Status applySettings(const LoraConfig& next, int subBand);
  Status bringUp();
  void shutDown();
  // After a transmission or a change: back to listening, when asked to.
  void resume();
  // why receives the name of what stopped the transmission, when it is known.
  Status transmit(const uint8_t* data, size_t len, const char** why, uint16_t preamble = 0);
  void finishSend(Status outcome);
  void restorePreamble();
  void receive(uint32_t nowMs);
  void loadSettings();
  Status saveSettings();
  Status claimPins();
  void releasePins();

  Status commandSet(const Args& a, CmdContext& ctx);
  Status commandSend(const Args& a, CmdContext& ctx);

  ILoraRadio* injected_;
  ILoraRadio* radio_ = nullptr;
  Kernel* kernel_ = nullptr;
  IGpio* gpio_ = nullptr;
  const hal::LoraWiring* wiring_ = nullptr;
  LogSource src_ = kLogSys;

  LoraConfig config_ = {};
  LoraStats stats_;
  LoraState state_ = LoraState::Off;
  int subBand_ = -1;
  AirtimeBudget budgets_[kLoraMaxSubBands];
  bool budgetsStarted_ = false;
  bool pinsClaimed_ = false;

  // transmission in progress
  uint32_t sendSeq_ = 0;
  uint32_t sendStartMs_ = 0;
  uint32_t sendLimitMs_ = 0;
  bool longPreamble_ = false;   // the packet on air has its own preamble: the settings come back after
  uint8_t syncNow_ = 0;         // the sync word of the moment, 0 for the one of the settings

  // received packets, oldest overwritten first
  LoraPacket inbox_[LMP_LORA_INBOX];
  size_t inboxHead_ = 0;
  size_t inboxCount_ = 0;
  uint32_t receiveSeq_ = 0;
  float lastRssiDbm_ = 0;
  float lastSnrDb_ = 0;

  LoraHandler listeners_[LMP_LORA_LISTENERS];
  size_t listenerCount_ = 0;
};

}  // namespace lmp

#endif  // LMP_FEATURE_LORA
