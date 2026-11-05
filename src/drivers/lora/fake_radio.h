// LoopMaxPro - simulated LoRa radio for the PC: tests and the system run without a board.
// The tests decide what arrives and look at what was sent.
#pragma once
#include "lmp_config.h"

#if LMP_FEATURE_LORA && defined(LMP_FW_NATIVE)

#include <string>
#include <vector>

#include "drivers/lora/lora_radio.h"

namespace lmp {

class FakeLoraRadio : public ILoraRadio {
 public:
  const char* chipName() const override { return "SX1262"; }
  int8_t minPowerDbm() const override { return -9; }
  int8_t maxPowerDbm() const override { return 22; }

  Status begin(const LoraParams& params) override;
  Status configure(const LoraParams& params) override;
  void end() override;
  Status standby() override;
  Status startTransmit(const uint8_t* data, size_t len) override;
  Status startReceive() override;
  RadioEvent poll() override;
  Status readPacket(uint8_t* out, size_t cap, size_t& len, RxInfo& info) override;
  float channelRssi() override { return -110.0f; }

  // ---- test hooks
  // Back to a radio just powered: nothing sent, nothing waiting, no fault.
  void reset();
  // A chip that does not answer.
  void setPresent(bool present) { present_ = present; }
  // A transmission that never ends.
  void setTxStuck(bool stuck) { txStuck_ = stuck; }
  // A packet from another device. Heard only while the radio is listening.
  void inject(const std::string& payload, float rssiDbm = -80.0f, float snrDb = 9.5f);
  void injectDamaged();

  const std::vector<std::string>& sent() const { return sent_; }
  // The preamble and the sync word each packet left with.
  const std::vector<uint16_t>& sentPreambles() const { return sentPreambles_; }
  const std::vector<uint8_t>& sentSyncs() const { return sentSyncs_; }
  const LoraParams& params() const { return params_; }
  bool started() const { return started_; }
  bool listening() const { return mode_ == Mode::Receiving; }
  bool transmitting() const { return mode_ == Mode::Transmitting; }
  unsigned begins() const { return begins_; }

 private:
  enum class Mode : uint8_t { Standby, Receiving, Transmitting };

  struct Incoming {
    std::string payload;
    RxInfo info;
    bool damaged = false;
  };

  LoraParams params_;
  Mode mode_ = Mode::Standby;
  bool present_ = true;
  bool started_ = false;
  bool txStuck_ = false;
  bool txPending_ = false;
  unsigned begins_ = 0;
  std::vector<std::string> sent_;
  std::vector<uint16_t> sentPreambles_;
  std::vector<uint8_t> sentSyncs_;
  std::vector<Incoming> incoming_;
  Incoming current_;
  bool currentValid_ = false;
};

// The one simulated radio of the PC build.
FakeLoraRadio& fakeLoraRadio();

}  // namespace lmp

#endif  // LMP_FEATURE_LORA && LMP_FW_NATIVE
