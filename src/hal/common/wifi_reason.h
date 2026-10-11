// LoopMaxPro HAL - what a disconnection code of the Espressif drivers means.
// ESP32 and ESP8266 use the same numbers.
#pragma once
#include "hal/api/inet.h"

namespace lmp::hal {

// Code given when the device left on request.
constexpr uint8_t kWifiReasonLeft = 8;

inline StaFail wifiFailOf(uint8_t reason) {
  switch (reason) {
    case 201:  // no access point found
    case 210:  // none found with compatible security
    case 211:  // none found above the authentication threshold
    case 212:  // none found above the signal threshold
      return StaFail::NotFound;
    case 2:    // authentication expired
    case 15:   // the four messages of the key exchange did not complete
    case 202:  // authentication failed
    case 204:  // key exchange timed out
      return StaFail::WrongPassword;
    default:
      return StaFail::Other;
  }
}

}  // namespace lmp::hal
