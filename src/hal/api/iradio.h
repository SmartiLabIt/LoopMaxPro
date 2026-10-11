// LoopMaxPro HAL - radio chips wired to the board.
// The HAL only says which chip is on the board and where: the driver is portable code that
// talks to it through ISpi and IGpio.
#pragma once
#include "hal/api/types.h"

namespace lmp::hal {

enum class LoraChip : uint8_t { None, Sx1261, Sx1262, Sx1268 };

inline const char* loraChipName(LoraChip chip) {
  switch (chip) {
    case LoraChip::Sx1261: return "SX1261";
    case LoraChip::Sx1262: return "SX1262";
    case LoraChip::Sx1268: return "SX1268";
    case LoraChip::None:   break;
  }
  return "none";
}

struct LoraWiring {
  LoraChip chip = LoraChip::None;
  // What the antenna circuit of the board is made for: 433, 868 or 915.
  uint16_t bandMhz = 0;
  uint8_t cs = 0;
  uint8_t sck = 0;
  uint8_t mosi = 0;
  uint8_t miso = 0;
  uint8_t rst = 0;
  uint8_t busy = 0;
  uint8_t dio1 = 0;
  // Supply of the oscillator driven by the chip, in millivolts. 0 when the board has a crystal.
  uint16_t tcxoMillivolts = 0;
  // The chip drives the antenna switch with its DIO2 pin.
  bool dio2RfSwitch = false;
};

}  // namespace lmp::hal
