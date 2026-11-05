// LoopMaxPro - time a LoRa packet stays on air.
// Plain arithmetic, the same on every chip: the rules on the use of the band are applied
// with these numbers, not with what a library reports.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "drivers/lora/lora_radio.h"

namespace lmp {

// Duration of one symbol, in microseconds.
uint32_t loraSymbolUs(const LoraParams& params);

// True when the symbols are so long that the chips switch to the low data rate mode.
bool loraLowDataRate(const LoraParams& params);

// Time on air of a packet with a payload of len bytes and explicit header, in microseconds.
// Valid for spreading factors from 7 to 12.
uint32_t loraAirtimeUs(const LoraParams& params, size_t len);

}  // namespace lmp
