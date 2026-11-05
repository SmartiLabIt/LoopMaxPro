#include "drivers/lora/lora_airtime.h"

namespace lmp {

uint32_t loraSymbolUs(const LoraParams& p) {
  if (p.bandwidthHz == 0 || p.spreadingFactor > 12) return 0;
  return static_cast<uint32_t>((static_cast<uint64_t>(1) << p.spreadingFactor) * 1000000u /
                               p.bandwidthHz);
}

bool loraLowDataRate(const LoraParams& p) { return loraSymbolUs(p) > 16000; }

uint32_t loraAirtimeUs(const LoraParams& p, size_t len) {
  const uint32_t symbolUs = loraSymbolUs(p);
  if (symbolUs == 0 || p.spreadingFactor < 7 || p.codingRate < 5 || p.codingRate > 8) return 0;

  const int32_t sf = p.spreadingFactor;
  const int32_t bitsPerSymbol = 4 * (sf - (loraLowDataRate(p) ? 2 : 0));
  // Payload, CRC and the 20 bits of the explicit header, less what fits in the first symbols.
  const int32_t bits = 8 * static_cast<int32_t>(len) - 4 * sf + 28 + (p.crc ? 16 : 0);
  int32_t groups = 0;
  if (bits > 0) groups = (bits + bitsPerSymbol - 1) / bitsPerSymbol;
  const uint32_t payloadSymbols = 8 + static_cast<uint32_t>(groups) * p.codingRate;

  // The preamble lasts its symbols plus 4.25: counted in quarters of a symbol.
  const uint64_t quarters =
      4ull * p.preamble + 17 + 4ull * payloadSymbols;
  return static_cast<uint32_t>(quarters * symbolUs / 4);
}

}  // namespace lmp
