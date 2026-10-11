// LoopMaxPro HAL - common types. Pure C++: no framework, no dynamic containers.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "base/delegate.h"
#include "base/span.h"
#include "base/status.h"
#include "base/string_view.h"

namespace lmp::hal {

struct Ip4 {
  uint8_t b[4] = {0, 0, 0, 0};

  static constexpr Ip4 of(uint8_t a, uint8_t b1, uint8_t c, uint8_t d) {
    return Ip4{{a, b1, c, d}};
  }
  constexpr bool isSet() const { return (b[0] | b[1] | b[2] | b[3]) != 0; }
  constexpr bool operator==(const Ip4& o) const {
    return b[0] == o.b[0] && b[1] == o.b[1] && b[2] == o.b[2] && b[3] == o.b[3];
  }
  // Writes "a.b.c.d". The buffer must hold at least 16 characters.
  void toString(char* out, size_t cap) const {
    snprintf(out, cap, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
  }
  // Reads "a.b.c.d", and nothing more. False, and out untouched, for anything else.
  static bool parse(const char* text, Ip4& out) {
    if (!text) return false;
    Ip4 ip;
    for (int part = 0; part < 4; ++part) {
      if (*text < '0' || *text > '9') return false;
      unsigned value = 0;
      int digits = 0;
      while (*text >= '0' && *text <= '9') {
        value = value * 10 + static_cast<unsigned>(*text++ - '0');
        if (++digits > 3 || value > 255) return false;
      }
      ip.b[part] = static_cast<uint8_t>(value);
      if (part < 3 && *text++ != '.') return false;
    }
    if (*text) return false;
    out = ip;
    return true;
  }
};

struct Mac {
  uint8_t b[6] = {0, 0, 0, 0, 0, 0};

  // Writes "AA:BB:CC:DD:EE:FF". The buffer must hold at least 18 characters.
  void toString(char* out, size_t cap) const {
    snprintf(out, cap, "%02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
  }
};

enum class PinMode : uint8_t { Input, InputPullUp, InputPullDown, Output, OutputOpenDrain, Analog };
enum class PinLevel : uint8_t { Low = 0, High = 1 };
enum class Edge : uint8_t { Rising, Falling, Change };

// Optional capabilities of a board.
enum class Cap : uint32_t {
  Rtos     = 1u << 0,
  Wifi     = 1u << 1,
  Ethernet = 1u << 2,
  Fs       = 1u << 3,
  Kv       = 1u << 4,
  Tls      = 1u << 5,
  Mqtt     = 1u << 6,
  Ota      = 1u << 7,
  Pwm      = 1u << 8,
  Rng      = 1u << 9,
  Sha256   = 1u << 10,
  CoreDump = 1u << 11,
  Adc      = 1u << 12,
  I2c      = 1u << 13,
  Spi      = 1u << 14,
  Uart     = 1u << 15,
  OneWire  = 1u << 16,
  HttpServer = 1u << 17,
  HttpClient = 1u << 18,
  Lora     = 1u << 19
};

struct HalCaps {
  uint32_t bits = 0;
  constexpr bool has(Cap c) const { return (bits & static_cast<uint32_t>(c)) != 0; }
  void add(Cap c) { bits |= static_cast<uint32_t>(c); }
};

}  // namespace lmp::hal
