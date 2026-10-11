// LoopMaxPro HAL - wiring of the LoRa radio, taken from the build flags of the board.
// Every implementation of the HAL uses it to fill the table of the board.
#pragma once
#include "hal/api/iradio.h"
#include "lmp_config.h"

#if LMP_FEATURE_LORA

#if !defined(LMP_LORA_CHIP) || !defined(LMP_LORA_BAND) || !defined(LMP_LORA_PIN_CS) ||   \
    !defined(LMP_LORA_PIN_SCK) || !defined(LMP_LORA_PIN_MOSI) ||                          \
    !defined(LMP_LORA_PIN_MISO) || !defined(LMP_LORA_PIN_RST) ||                          \
    !defined(LMP_LORA_PIN_BUSY) || !defined(LMP_LORA_PIN_DIO1)
#error "LMP_FEATURE_LORA needs LMP_LORA_CHIP, LMP_LORA_BAND and the LMP_LORA_PIN_ flags"
#endif
#ifndef LMP_LORA_TCXO_MV
#define LMP_LORA_TCXO_MV 0
#endif
#ifndef LMP_LORA_DIO2_RF_SWITCH
#define LMP_LORA_DIO2_RF_SWITCH 0
#endif

namespace lmp::hal {

inline const LoraWiring& loraWiring() {
  static const LoraWiring wiring = [] {
    LoraWiring w;
#if LMP_LORA_CHIP == 1261
    w.chip = LoraChip::Sx1261;
#elif LMP_LORA_CHIP == 1262
    w.chip = LoraChip::Sx1262;
#elif LMP_LORA_CHIP == 1268
    w.chip = LoraChip::Sx1268;
#else
#error "LMP_LORA_CHIP: supported values are 1261, 1262 and 1268"
#endif
    w.bandMhz = LMP_LORA_BAND;
    w.cs = LMP_LORA_PIN_CS;
    w.sck = LMP_LORA_PIN_SCK;
    w.mosi = LMP_LORA_PIN_MOSI;
    w.miso = LMP_LORA_PIN_MISO;
    w.rst = LMP_LORA_PIN_RST;
    w.busy = LMP_LORA_PIN_BUSY;
    w.dio1 = LMP_LORA_PIN_DIO1;
    w.tcxoMillivolts = LMP_LORA_TCXO_MV;
    w.dio2RfSwitch = LMP_LORA_DIO2_RF_SWITCH != 0;
    return w;
  }();
  return wiring;
}

}  // namespace lmp::hal

#endif  // LMP_FEATURE_LORA
