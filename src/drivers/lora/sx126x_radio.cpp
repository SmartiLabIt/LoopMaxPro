// LoopMaxPro - driver of the SX126x radios, built on RadioLib.
// RadioLib reaches the chip through the HAL of LoopMaxPro: the same driver serves every
// framework and every board.
#include "lmp_config.h"

#if LMP_FEATURE_LORA && !defined(LMP_FW_NATIVE)

#include <RadioLib.h>

#include "drivers/lora/lora_radio.h"

namespace lmp {
namespace {

// Values RadioLib passes back for modes, levels and edges.
constexpr uint32_t kInput = 0;
constexpr uint32_t kOutput = 1;
constexpr uint32_t kLow = 0;
constexpr uint32_t kHigh = 1;
constexpr uint32_t kRising = 0;
constexpr uint32_t kFalling = 1;

constexpr uint32_t kSpiHz = 2000000;
// Highest current the power amplifier may draw, in milliamperes: the limit of the chip.
constexpr float kCurrentLimitMa = 140.0f;

// Set by the interrupt of the DIO1 pin, read by poll().
volatile bool g_dio1 = false;

void onDio1() { g_dio1 = true; }

class RadioHal : public RadioLibHal {
 public:
  RadioHal(const hal::Board& board, const hal::LoraWiring& wiring)
      : RadioLibHal(kInput, kOutput, kLow, kHigh, kRising, kFalling),
        board_(board),
        wiring_(wiring) {
    spi_.frequencyHz = kSpiHz;
    spi_.mode = 0;
    spi_.msbFirst = true;
  }

  void init() override { spiBegin(); }
  void term() override { spiEnd(); }

  void pinMode(uint32_t pin, uint32_t mode) override {
    if (pin == RADIOLIB_NC) return;
    board_.gpio->setMode(static_cast<uint8_t>(pin),
                         mode == kOutput ? hal::PinMode::Output : hal::PinMode::Input);
  }

  void digitalWrite(uint32_t pin, uint32_t value) override {
    if (pin == RADIOLIB_NC) return;
    board_.gpio->write(static_cast<uint8_t>(pin),
                       value == kLow ? hal::PinLevel::Low : hal::PinLevel::High);
  }

  uint32_t digitalRead(uint32_t pin) override {
    if (pin == RADIOLIB_NC) return kLow;
    hal::PinLevel level = hal::PinLevel::Low;
    board_.gpio->read(static_cast<uint8_t>(pin), level);
    return level == hal::PinLevel::High ? kHigh : kLow;
  }

  void attachInterrupt(uint32_t pin, void (*callback)(void), uint32_t mode) override {
    if (pin == RADIOLIB_NC || !callback) return;
    board_.gpio->attachInterrupt(static_cast<uint8_t>(pin),
                                 mode == kRising ? hal::Edge::Rising : hal::Edge::Falling,
                                 [callback](uint8_t) { callback(); });
  }

  void detachInterrupt(uint32_t pin) override {
    if (pin == RADIOLIB_NC) return;
    board_.gpio->detachInterrupt(static_cast<uint8_t>(pin));
  }

  void delay(RadioLibTime_t ms) override {
    if (board_.rtos) {
      board_.rtos->delayMs(static_cast<uint32_t>(ms));
    } else {
      delayMicroseconds(ms * 1000);
    }
  }

  void delayMicroseconds(RadioLibTime_t us) override {
    const uint64_t end = board_.clock->micros() + us;
    while (board_.clock->micros() < end) {
    }
  }

  RadioLibTime_t millis() override { return board_.clock->millis(); }
  RadioLibTime_t micros() override { return static_cast<RadioLibTime_t>(board_.clock->micros()); }

  // Not used by these radios.
  long pulseIn(uint32_t, uint32_t, RadioLibTime_t) override { return 0; }

  void spiBegin() override { board_.spi->begin(wiring_.sck, wiring_.miso, wiring_.mosi); }
  void spiBeginTransaction() override {}
  void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override {
    // The chip select is driven by RadioLib around the transfer.
    board_.spi->transfer(hal::kSpiNoCs, spi_, out, in, len);
  }
  void spiEndTransaction() override {}
  void spiEnd() override { board_.spi->end(); }

  void yield() override {
    if (board_.rtos) board_.rtos->yield();
  }

 private:
  const hal::Board& board_;
  const hal::LoraWiring& wiring_;
  hal::SpiConfig spi_;
};

// The SX1261 has the small power amplifier, the other two the large one.
#if LMP_LORA_CHIP == 1261
using Chip = SX1261;
constexpr int8_t kMaxPower = 14;
constexpr int8_t kMinPower = -17;
#elif LMP_LORA_CHIP == 1268
using Chip = SX1268;
constexpr int8_t kMaxPower = 22;
constexpr int8_t kMinPower = -9;
#else
using Chip = SX1262;
constexpr int8_t kMaxPower = 22;
constexpr int8_t kMinPower = -9;
#endif

class Sx126xRadio : public ILoraRadio {
 public:
  Sx126xRadio(const hal::Board& board, const hal::LoraWiring& wiring)
      : wiring_(wiring),
        hal_(board, wiring),
        module_(&hal_, wiring.cs, wiring.dio1, wiring.rst, wiring.busy),
        chip_(&module_) {}

  const char* chipName() const override { return hal::loraChipName(wiring_.chip); }
  int8_t minPowerDbm() const override { return kMinPower; }
  int8_t maxPowerDbm() const override { return kMaxPower; }

  Status begin(const LoraParams& p) override {
    started_ = false;
    mode_ = Mode::Standby;
    g_dio1 = false;
    const int16_t state =
        chip_.begin(megahertz(p), kilohertz(p), p.spreadingFactor, p.codingRate, p.syncWord,
                    p.powerDbm, p.preamble, wiring_.tcxoMillivolts / 1000.0f, false);
    if (state != RADIOLIB_ERR_NONE) return translate(state);
    if (chip_.setDio2AsRfSwitch(wiring_.dio2RfSwitch) != RADIOLIB_ERR_NONE) return Status::Io;
    if (chip_.setCurrentLimit(kCurrentLimitMa) != RADIOLIB_ERR_NONE) return Status::Io;
    if (chip_.setCRC(p.crc ? 2 : 0) != RADIOLIB_ERR_NONE) return Status::Io;
    chip_.setDio1Action(onDio1);
    started_ = true;
    return Status::Ok;
  }

  Status configure(const LoraParams& p) override {
    if (!started_) return Status::Denied;
    LMP_TRY(translate(chip_.standby()));
    mode_ = Mode::Standby;
    g_dio1 = false;
    LMP_TRY(translate(chip_.setFrequency(megahertz(p))));
    LMP_TRY(translate(chip_.setBandwidth(kilohertz(p))));
    LMP_TRY(translate(chip_.setSpreadingFactor(p.spreadingFactor)));
    LMP_TRY(translate(chip_.setCodingRate(p.codingRate)));
    LMP_TRY(translate(chip_.setSyncWord(p.syncWord)));
    LMP_TRY(translate(chip_.setOutputPower(p.powerDbm)));
    LMP_TRY(translate(chip_.setPreambleLength(p.preamble)));
    return translate(chip_.setCRC(p.crc ? 2 : 0));
  }

  void end() override {
    if (!started_) return;
    chip_.clearDio1Action();
    chip_.sleep();
    started_ = false;
    mode_ = Mode::Standby;
  }

  Status standby() override {
    if (!started_) return Status::Denied;
    mode_ = Mode::Standby;
    g_dio1 = false;
    return translate(chip_.standby());
  }

  Status startTransmit(const uint8_t* data, size_t len) override {
    if (!started_) return Status::Denied;
    if (!data || len == 0 || len > RADIOLIB_SX126X_MAX_PACKET_LENGTH) return Status::Invalid;
    g_dio1 = false;
    const int16_t state = chip_.startTransmit(data, len);
    if (state != RADIOLIB_ERR_NONE) {
      mode_ = Mode::Standby;
      return translate(state);
    }
    mode_ = Mode::Transmitting;
    return Status::Ok;
  }

  Status startReceive() override {
    if (!started_) return Status::Denied;
    g_dio1 = false;
    const int16_t state = chip_.startReceive();
    if (state != RADIOLIB_ERR_NONE) {
      mode_ = Mode::Standby;
      return translate(state);
    }
    mode_ = Mode::Receiving;
    return Status::Ok;
  }

  RadioEvent poll() override {
    if (!started_ || !g_dio1) return RadioEvent::None;
    g_dio1 = false;
    const uint32_t irq = chip_.getIrqFlags();

    if (mode_ == Mode::Transmitting) {
      const bool done = (irq & RADIOLIB_SX126X_IRQ_TX_DONE) != 0;
      chip_.finishTransmit();
      mode_ = Mode::Standby;
      return done ? RadioEvent::TxDone : RadioEvent::Timeout;
    }

    if (mode_ == Mode::Receiving) {
      if (irq & RADIOLIB_SX126X_IRQ_RX_DONE) {
        const bool damaged =
            (irq & RADIOLIB_SX126X_IRQ_CRC_ERR) ||
            ((irq & RADIOLIB_SX126X_IRQ_HEADER_ERR) && !(irq & RADIOLIB_SX126X_IRQ_HEADER_VALID));
        if (!damaged) return RadioEvent::RxDone;
        chip_.clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
        return RadioEvent::RxError;
      }
      chip_.clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
      if (irq & RADIOLIB_SX126X_IRQ_TIMEOUT) return RadioEvent::Timeout;
    }
    return RadioEvent::None;
  }

  Status readPacket(uint8_t* out, size_t cap, size_t& len, RxInfo& info) override {
    len = 0;
    if (!started_ || !out) return Status::Invalid;
    const size_t size = chip_.getPacketLength();
    if (size == 0) {
      chip_.clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
      return Status::NotFound;
    }
    const size_t n = size < cap ? size : cap;
    const int16_t state = chip_.readData(out, n);
    if (state != RADIOLIB_ERR_NONE) return translate(state);
    len = n;
    info.rssiDbm = chip_.getRSSI();
    info.snrDb = chip_.getSNR();
    return size > cap ? Status::NoMem : Status::Ok;
  }

  float channelRssi() override { return started_ ? chip_.getRSSI(false) : 0.0f; }

 private:
  enum class Mode : uint8_t { Standby, Receiving, Transmitting };

  static float megahertz(const LoraParams& p) {
    return static_cast<float>(p.frequencyHz / 1000000.0);
  }
  static float kilohertz(const LoraParams& p) {
    return static_cast<float>(p.bandwidthHz / 1000.0);
  }

  static Status translate(int16_t state) {
    switch (state) {
      case RADIOLIB_ERR_NONE:                return Status::Ok;
      case RADIOLIB_ERR_CHIP_NOT_FOUND:      return Status::Io;
      case RADIOLIB_ERR_SPI_CMD_TIMEOUT:
      case RADIOLIB_ERR_SPI_CMD_FAILED:
      case RADIOLIB_ERR_SPI_CMD_INVALID:     return Status::Io;
      case RADIOLIB_ERR_TX_TIMEOUT:
      case RADIOLIB_ERR_RX_TIMEOUT:          return Status::Timeout;
      case RADIOLIB_ERR_CRC_MISMATCH:        return Status::Io;
      case RADIOLIB_ERR_PACKET_TOO_LONG:     return Status::Invalid;
      default:                               break;
    }
    // Every other code of the library is a parameter it refused.
    return Status::Invalid;
  }

  const hal::LoraWiring& wiring_;
  RadioHal hal_;
  Module module_;
  Chip chip_;
  Mode mode_ = Mode::Standby;
  bool started_ = false;
};

}  // namespace

ILoraRadio* loraRadio(const hal::Board& board) {
  if (!board.lora || board.lora->chip == hal::LoraChip::None) return nullptr;
  if (!board.spi || !board.gpio || !board.clock) return nullptr;
  static Sx126xRadio radio(board, *board.lora);
  return &radio;
}

}  // namespace lmp

#endif  // LMP_FEATURE_LORA && !LMP_FW_NATIVE
