#include "services/lora/lora_band.h"

namespace lmp {
namespace {

// 863-870 MHz in Europe.
constexpr LoraSubBand kBand868[] = {
    {863000000, 865000000, 1, 14},
    {865000000, 868000000, 10, 14},
    {868000000, 868600000, 10, 14},
    {868700000, 869200000, 1, 14},
    {869400000, 869650000, 100, 22},   // the rules allow 27 dBm: the chips stop at 22
    {869700000, 870000000, 10, 14},
};

// 433 MHz in Europe.
constexpr LoraSubBand kBand433[] = {
    {433050000, 434790000, 100, 10},
};

constexpr uint32_t kBandwidths[] = {62500, 125000, 250000, 500000};

constexpr uint32_t kHourMs = 3600000;

}  // namespace

Span<const LoraSubBand> loraSubBands(uint16_t bandMhz) {
  switch (bandMhz) {
    case 868: return Span<const LoraSubBand>(kBand868, sizeof(kBand868) / sizeof(kBand868[0]));
    case 433: return Span<const LoraSubBand>(kBand433, sizeof(kBand433) / sizeof(kBand433[0]));
    default:  break;
  }
  return Span<const LoraSubBand>();
}

int loraSubBandOf(uint16_t bandMhz, uint32_t frequencyHz, uint32_t bandwidthHz) {
  const uint32_t half = bandwidthHz / 2;
  if (frequencyHz < half) return -1;
  int index = 0;
  for (const LoraSubBand& part : loraSubBands(bandMhz)) {
    if (frequencyHz - half >= part.fromHz && frequencyHz + half <= part.toHz) return index;
    ++index;
  }
  return -1;
}

uint32_t loraDefaultFrequency(uint16_t bandMhz) {
  switch (bandMhz) {
    case 868: return 868100000;
    case 433: return 433175000;
    default:  break;
  }
  return 0;
}

bool loraBandwidthAllowed(uint32_t bandwidthHz) {
  for (uint32_t allowed : kBandwidths) {
    if (allowed == bandwidthHz) return true;
  }
  return false;
}

// ---------------------------------------------------------------- budget

void AirtimeBudget::start(uint16_t dutyPermille, uint32_t nowMs) {
  permille_ = dutyPermille > 1000 ? 1000 : dutyPermille;
  // One hour times the share: the multiplication by 1000 turns milliseconds in microseconds.
  capacityUs_ = kHourMs * permille_;
  leftUs_ = capacityUs_;
  lastMs_ = nowMs;
}

uint32_t AirtimeBudget::leftUs(uint32_t nowMs) const {
  const uint32_t elapsedMs = nowMs - lastMs_;
  const uint64_t left = static_cast<uint64_t>(leftUs_) + static_cast<uint64_t>(elapsedMs) * permille_;
  return left > capacityUs_ ? capacityUs_ : static_cast<uint32_t>(left);
}

void AirtimeBudget::spend(uint32_t airtimeUs, uint32_t nowMs) {
  const uint32_t left = leftUs(nowMs);
  leftUs_ = airtimeUs > left ? 0 : left - airtimeUs;
  lastMs_ = nowMs;
}

}  // namespace lmp
