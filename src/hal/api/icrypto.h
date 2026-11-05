// LoopMaxPro HAL - random numbers and hashing.
#pragma once
#include "hal/api/types.h"

namespace lmp::hal {

class IRandom {
 public:
  virtual ~IRandom() = default;
  // Fills the buffer with random bytes from the best source the board has.
  virtual void bytes(uint8_t* out, size_t len) = 0;

  uint32_t u32() {
    uint32_t v = 0;
    bytes(reinterpret_cast<uint8_t*>(&v), sizeof(v));
    return v;
  }
};

class IHash {
 public:
  virtual ~IHash() = default;
  virtual Status sha256Begin() = 0;
  virtual Status sha256Update(const uint8_t* data, size_t len) = 0;
  virtual Status sha256Finish(uint8_t out[32]) = 0;
};

}  // namespace lmp::hal
