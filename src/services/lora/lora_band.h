// LoopMaxPro - rules on the use of the radio bands open to everyone.
// Each band is split in parts with their own limits: highest power and share of time a
// device may spend transmitting. The service refuses what the rules do not allow.
//
// The tables follow the European recommendation ERC 70-03 for short range devices.
// Check them against the rules in force before selling a product.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "base/span.h"

namespace lmp {

struct LoraSubBand {
  uint32_t fromHz;
  uint32_t toHz;
  uint16_t dutyPermille;   // time on air allowed in one hour, in thousandths
  int8_t maxPowerDbm;
};

constexpr size_t kLoraMaxSubBands = 8;

// Parts of a band, or an empty list when the band is not supported.
Span<const LoraSubBand> loraSubBands(uint16_t bandMhz);

// Index of the part that holds the whole signal, or -1 when none does.
int loraSubBandOf(uint16_t bandMhz, uint32_t frequencyHz, uint32_t bandwidthHz);

// Frequency used until the owner chooses one.
uint32_t loraDefaultFrequency(uint16_t bandMhz);

bool loraBandwidthAllowed(uint32_t bandwidthHz);

// Time on air still available in one part of the band. It fills up again with time,
// up to what the rules allow in one hour.
class AirtimeBudget {
 public:
  void start(uint16_t dutyPermille, uint32_t nowMs);
  bool allows(uint32_t airtimeUs, uint32_t nowMs) const { return leftUs(nowMs) >= airtimeUs; }
  void spend(uint32_t airtimeUs, uint32_t nowMs);
  uint32_t leftUs(uint32_t nowMs) const;
  uint32_t leftMs(uint32_t nowMs) const { return leftUs(nowMs) / 1000; }
  uint32_t capacityMs() const { return capacityUs_ / 1000; }

 private:
  uint32_t capacityUs_ = 0;
  uint32_t leftUs_ = 0;
  uint32_t lastMs_ = 0;
  uint16_t permille_ = 0;
};

}  // namespace lmp
