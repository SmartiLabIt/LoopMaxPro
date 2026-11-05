// LoopMaxPro - LoRa radio as the system sees it: one interface for every chip.
// A driver is portable code: it reaches the chip through the buses and the pins of the HAL.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "base/status.h"
#include "hal/api/board.h"

namespace lmp {

// Parameters of the radio link. Two devices hear each other only when they all match,
// power excluded.
struct LoraParams {
  uint32_t frequencyHz = 868100000;
  uint32_t bandwidthHz = 125000;
  uint8_t spreadingFactor = 7;   // 7..12
  uint8_t codingRate = 5;        // 5..8, meaning 4/5..4/8
  uint8_t syncWord = 0x12;
  int8_t powerDbm = 10;
  uint16_t preamble = 8;         // symbols
  bool crc = true;
};

enum class RadioEvent : uint8_t {
  None,
  TxDone,
  RxDone,     // a packet is waiting: call readPacket()
  RxError,    // a packet arrived damaged and was dropped
  Timeout
};

struct RxInfo {
  float rssiDbm = 0;
  float snrDb = 0;
};

class ILoraRadio {
 public:
  virtual ~ILoraRadio() = default;

  virtual const char* chipName() const = 0;
  // Limits of the chip. The rules of the band may ask for less.
  virtual int8_t minPowerDbm() const = 0;
  virtual int8_t maxPowerDbm() const = 0;

  // Resets the chip and applies the parameters. Io when the chip does not answer.
  virtual Status begin(const LoraParams& params) = 0;
  // New parameters on a chip already started. The radio is left in standby.
  virtual Status configure(const LoraParams& params) = 0;
  // Lowest consumption. begin() is needed afterwards.
  virtual void end() = 0;
  virtual Status standby() = 0;

  // Both return at once: the outcome arrives from poll().
  virtual Status startTransmit(const uint8_t* data, size_t len) = 0;
  // Listens until told otherwise: after a packet the radio keeps listening.
  virtual Status startReceive() = 0;

  // What happened since the last call.
  virtual RadioEvent poll() = 0;
  // After RxDone: the packet and how well it was heard.
  virtual Status readPacket(uint8_t* out, size_t cap, size_t& len, RxInfo& info) = 0;
  // Signal on the channel right now, in dBm. Meaningful while listening.
  virtual float channelRssi() = 0;
};

// The radio of this board, or null when it has none. Defined by the driver in the build.
ILoraRadio* loraRadio(const hal::Board& board);

}  // namespace lmp
